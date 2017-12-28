#include <iostream>

#include "avutils.h"
#include "cli_streams.h"
#include "mkv_options.h"
#include "streams/asio_streams.h"
#include "streams/threaded_worker.h"
#include "video_streams.h"
#include "vp9_encoder.h"

namespace satori {
namespace video {
namespace cli_streams {

namespace {

po::options_description rtm_options() {
  po::options_description online("Satori RTM connection options");
  online.add_options()("endpoint", po::value<std::string>(), "app endpoint");
  online.add_options()("appkey", po::value<std::string>(), "app key");
  online.add_options()("port", po::value<std::string>()->default_value("443"), "port");
  online.add_options()("channel", po::value<std::string>(), "channel");

  return online;
}

po::options_description file_input_options(bool enable_batch_mode) {
  po::options_description file_sources("Input file options");
  file_sources.add_options()("input-video-file", po::value<std::string>(),
                             "input mp4,mkv,webm");
  file_sources.add_options()("input-replay-file", po::value<std::string>(), "input txt");
  file_sources.add_options()("loop", "Is file looped");

  if (enable_batch_mode) {
    file_sources.add_options()(
        "batch",
        "turns on batch analysis mode, where analysis of a single video frame might take "
        "longer than frame duration (file source only).");
  }

  return file_sources;
}

po::options_description camera_input_options() {
  po::options_description camera_options("Camera options");
  camera_options.add_options()("input-camera", "Is camera used as a source");

  return camera_options;
}

po::options_description generic_input_options() {
  po::options_description options("Generic input options");
  options.add_options()("time-limit", po::value<long>(),
                        "(seconds) if specified, bot will exit after given time elapsed");
  options.add_options()(
      "frames-limit", po::value<long>(),
      "(number) if specified, bot will exit after processing given number of frames");
  options.add_options()("input-resolution",
                        po::value<std::string>()->default_value("320x240"),
                        "(<width>x<height>|original) resolution of input video stream");
  options.add_options()("keep-proportions", po::value<bool>()->default_value(true),
                        "(bool) tells if original video stream resolution's proportion "
                        "should remain unchanged");

  return options;
}

po::options_description generic_output_options() {
  po::options_description options("Generic output options");
  options.add_options()("output-resolution",
                        po::value<std::string>()->default_value("320x240"),
                        "(<width>x<height>|original) resolution of output video stream");
  options.add_options()("keep-proportions", po::value<bool>()->default_value(true),
                        "(bool) tells if output video stream resolution's proportion "
                        "should remain unchanged");

  return options;
}

po::options_description url_input_options() {
  po::options_description url_options("URL options");
  url_options.add_options()("input-url", po::value<std::string>(), "Input video URL");
  return url_options;
}

po::options_description file_output_options() {
  po::options_description output_file_options("Output file options");
  output_file_options.add_options()("output-video-file", po::value<std::string>(),
                                    "Output video file");
  output_file_options.add_options()(
      "reserved-index-space", po::value<int>()->default_value(0),
      "(bytes) Tells how much space to reserve at the beginning of "
      "file for cues (indexes) to improve seeking. "
      "Typically 50000 is enough for one hour of video. "
      "For Matroska, if not specified (e.g. set to zero), "
      "cues will be written at the end of the file.");

  return output_file_options;
}

bool check_rtm_args_provided(const po::variables_map &vm) {
  return vm.count("endpoint") > 0 || vm.count("appkey") > 0 || vm.count("channel") > 0;
}

bool check_file_input_args_provided(const po::variables_map &vm) {
  return vm.count("input-video-file") > 0 || vm.count("input-replay-file") > 0;
}

bool check_camera_input_args_provided(const po::variables_map &vm) {
  return vm.count("input-camera") > 0;
}

bool check_url_input_args_provided(const po::variables_map &vm) {
  return vm.count("input-url") > 0;
}

bool check_file_output_args_provided(const po::variables_map &vm) {
  return vm.count("output-video-file") > 0;
}

bool validate_rtm_args(const po::variables_map &vm) {
  if (vm.count("endpoint") == 0) {
    std::cerr << "Missing --endpoint argument\n";
    return false;
  }
  if (vm.count("appkey") == 0) {
    std::cerr << "Missing --appkey argument\n";
    return false;
  }
  if (vm.count("channel") == 0) {
    std::cerr << "Missing --channel argument\n";
    return false;
  }
  if (vm.count("port") == 0) {
    std::cerr << "Missing --port argument\n";
    return false;
  }

  return true;
}

bool validate_input_file_args(const po::variables_map &vm) {
  if (vm.count("input-video-file") > 0 && vm.count("input-replay-file") > 9) {
    std::cerr << "--input-video-file and --input-replay-file are mutually exclusive\n";
    return false;
  }

  return true;
}

po::options_description to_boost(const cli_options &opts) {
  po::options_description options;

  if (opts.enable_rtm_input) {
    options.add(rtm_options());
  }
  if (opts.enable_file_input) {
    options.add(file_input_options(opts.enable_file_batch_mode));
  }
  if (opts.enable_camera_input) {
    options.add(camera_input_options());
  }
  if (opts.enable_url_input) {
    options.add(url_input_options());
  }
  if (opts.enable_generic_input_options) {
    options.add(generic_input_options());
  }
  if (opts.enable_generic_output_options) {
    options.add(generic_output_options());
  }
  if (opts.enable_rtm_output) {
    options.add(rtm_options());
  }
  if (opts.enable_file_output) {
    options.add(file_output_options());
  }

  return options;
}
}  // namespace

constexpr size_t network_buffer_size = 1024;
constexpr size_t encoded_buffer_size = 10;

configuration::configuration(int argc, char *argv[], cli_options options,
                             const po::options_description &custom_options)
    : _cli_options(options) {
  po::options_description description = to_boost(options);
  description.add(custom_options);

  try {
    po::store(po::parse_command_line(argc, argv, description), _vm);
    po::notify(_vm);
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl << description << std::endl;
    exit(1);
  }

  if (argc == 1 || _vm.count("help") > 0) {
    std::cerr << description << std::endl;
    exit(1);
  }

  if (!validate()) {
    exit(1);
  }

  if (_vm.count("config") > 0 && _vm.count("config-file") > 0) {
    std::cerr << "--config and --config-file options are mutually exclusive" << std::endl;
    exit(1);
  }
}

bool configuration::validate() const {
  const bool has_input_rtm_args =
      _cli_options.enable_rtm_input && check_rtm_args_provided(_vm);
  const bool has_input_file_args =
      _cli_options.enable_file_input && check_file_input_args_provided(_vm);
  const bool has_input_camera_args =
      _cli_options.enable_camera_input && check_camera_input_args_provided(_vm);

  if (((int)has_input_rtm_args + (int)has_input_file_args + (int)has_input_camera_args)
      > 1) {
    std::cerr << "Only one video source should be specified\n";
    return false;
  }

  if (_cli_options.enable_rtm_input || _cli_options.enable_file_input
      || _cli_options.enable_camera_input || _cli_options.enable_url_input) {
    if (!has_input_rtm_args && !has_input_file_args && !has_input_camera_args
        && !_cli_options.enable_url_input) {
      std::cerr << "Video source should be specified\n";
      return false;
    }
  }

  if (has_input_rtm_args && !validate_rtm_args(_vm)) {
    return false;
  }
  if (has_input_file_args && !validate_input_file_args(_vm)) {
    return false;
  }

  const bool has_output_rtm_args =
      _cli_options.enable_rtm_output && check_rtm_args_provided(_vm);
  const bool has_output_file_args =
      _cli_options.enable_file_output && check_file_output_args_provided(_vm);

  if (has_output_rtm_args && has_output_file_args) {
    std::cerr << "Only one video output should be specified\n";
    return false;
  }

  if (_cli_options.enable_rtm_output || _cli_options.enable_file_output) {
    if (!has_output_rtm_args && !has_output_file_args) {
      std::cerr << "Video output should be specified\n";
      return false;
    }
  }

  if (has_input_rtm_args && has_output_rtm_args) {
    std::cerr << "RTM input and RTM output together are not supported currently\n";
    return false;
  }

  if (has_output_rtm_args && !validate_rtm_args(_vm)) {
    return false;
  }

  if (_cli_options.enable_generic_input_options) {
    std::string resolution = _vm["input-resolution"].as<std::string>();
    if (!avutils::parse_image_size(resolution)) {
      std::cerr << "Unable to parse input resolution: " << resolution << "\n";
      return false;
    }
  }

  if (_cli_options.enable_generic_output_options) {
    std::string resolution = _vm["output-resolution"].as<std::string>();
    if (!avutils::parse_image_size(resolution)) {
      std::cerr << "Unable to parse output resolution: " << resolution << "\n";
      return false;
    }
  }

  return true;
}

std::shared_ptr<rtm::client> configuration::rtm_client(
    boost::asio::io_service &io_service, std::thread::id io_thread_id,
    boost::asio::ssl::context &ssl_context,
    rtm::error_callbacks &rtm_error_callbacks) const {
  if (!_cli_options.enable_rtm_input && !_cli_options.enable_rtm_output) {
    return nullptr;
  }
  if (!check_rtm_args_provided(_vm)) {
    return nullptr;
  }

  const std::string endpoint = _vm["endpoint"].as<std::string>();
  const std::string port = _vm["port"].as<std::string>();
  const std::string appkey = _vm["appkey"].as<std::string>();

  return std::make_shared<rtm::thread_checking_client>(
      io_service, io_thread_id,
      std::make_unique<rtm::resilient_client>(
          io_service, io_thread_id,
          [endpoint, port, appkey, &io_service, &ssl_context,
           &rtm_error_callbacks](rtm::error_callbacks &callbacks) {
            return rtm::new_client(endpoint, port, appkey, io_service, ssl_context, 1,
                                   callbacks);
          },
          rtm_error_callbacks));
}

std::string configuration::rtm_channel() const {
  if (!_cli_options.enable_rtm_input && !_cli_options.enable_rtm_output) {
    return "";
  }
  if (!check_rtm_args_provided(_vm)) {
    return "";
  }

  return _vm["channel"].as<std::string>();
}

bool configuration::is_batch_mode() const {
  return _cli_options.enable_file_input && _cli_options.enable_file_batch_mode
         && _vm.count("batch") > 0;
}

streams::publisher<encoded_packet> configuration::encoded_publisher(
    boost::asio::io_service &io_service, const std::shared_ptr<rtm::client> &client,
    const std::string &channel) const {
  const bool has_input_rtm_args =
      _cli_options.enable_rtm_input && check_rtm_args_provided(_vm);
  const bool has_input_file_args =
      _cli_options.enable_file_input && check_file_input_args_provided(_vm);
  const bool has_input_camera_args =
      _cli_options.enable_camera_input && check_camera_input_args_provided(_vm);
  const bool has_input_url_args =
      _cli_options.enable_url_input && check_url_input_args_provided(_vm);

  if (has_input_rtm_args) {
    streams::publisher<network_packet> source =
        satori::video::rtm_source(client, channel);

    return std::move(source) >> report_network_frame_dynamics() >> decode_network_stream()
           >> streams::threaded_worker("decoder_worker") >> streams::flatten();
  }

  if (has_input_file_args) {
    const bool batch = _cli_options.enable_file_batch_mode && _vm.count("batch") > 0;

    streams::publisher<encoded_packet> source;
    if (_vm.count("input-video-file") > 0) {
      source = satori::video::file_source(io_service,
                                          _vm["input-video-file"].as<std::string>(),
                                          _vm.count("loop") > 0, batch);
    } else {
      source = satori::video::network_replay_source(
                   io_service, _vm["input-replay-file"].as<std::string>(), batch)
               >> report_network_frame_dynamics() >> decode_network_stream();
    }

    if (batch) {
      return std::move(source);
    }

    return std::move(source) >> streams::threaded_worker("input.encoded_buffer")
           >> streams::flatten();
  }

  if (has_input_camera_args) {
    CHECK(_cli_options.enable_generic_input_options
          || _cli_options.enable_generic_output_options);
    const uint8_t fps = 25;                // FIXME: hardcoded value
    const uint8_t vp9_lag_in_frames = 25;  // FIXME: hardcoded value

    const std::string resolution = _cli_options.enable_generic_input_options
                                       ? _vm["input-resolution"].as<std::string>()
                                       : _vm["output-resolution"].as<std::string>();
    return satori::video::camera_source(io_service, resolution, fps)
           >> encode_vp9(vp9_lag_in_frames);
  }

  if (has_input_url_args) {
    std::string url = _vm["input-url"].as<std::string>();
    return satori::video::url_source(url);
  }

  ABORT() << "should not happen";
}

streams::publisher<owned_image_packet> configuration::decoded_publisher(
    boost::asio::io_service &io_service, const std::shared_ptr<rtm::client> &client,
    const std::string &channel, image_pixel_format pixel_format) const {
  CHECK(_cli_options.enable_generic_input_options
        || _cli_options.enable_generic_output_options);

  const std::string resolution_str = _cli_options.enable_generic_input_options
                                         ? _vm["input-resolution"].as<std::string>()
                                         : _vm["output-resolution"].as<std::string>();
  boost::optional<image_size> resolution = avutils::parse_image_size(resolution_str);

  bool keep_proportions = _vm["keep-proportions"].as<bool>();

  streams::publisher<owned_image_packet> source =
      encoded_publisher(io_service, client, channel) >> report_encoded_frame_dynamics()
      >> decode_image_frames(resolution->width, resolution->height, pixel_format,
                             keep_proportions);

  if (_vm.count("time-limit") > 0) {
    source = std::move(source)
             >> streams::asio::timer_breaker<owned_image_packet>(
                    io_service, std::chrono::seconds(_vm["time-limit"].as<long>()));
  }

  if (_vm.count("frames-limit") > 0) {
    source = std::move(source) >> streams::take(_vm["frames-limit"].as<long>());
  }

  return source;
}

streams::subscriber<encoded_packet> &configuration::encoded_subscriber(
    const std::shared_ptr<rtm::client> &client, boost::asio::io_service &io_service,
    const std::string &channel) const {
  const bool has_output_rtm_args =
      _cli_options.enable_rtm_output && check_rtm_args_provided(_vm);
  const bool has_output_file_args =
      _cli_options.enable_file_output && check_file_output_args_provided(_vm);

  if (has_output_rtm_args) {
    return satori::video::rtm_sink(client, io_service, channel);
  }

  if (has_output_file_args) {
    mkv::format_options mkv_format_options;
    mkv_format_options.reserved_index_space = _vm["reserved-index-space"].as<int>();
    return satori::video::mkv_sink(_vm["output-video-file"].as<std::string>(),
                                   mkv_format_options);
  }

  ABORT() << "shouldn't happen";
}

}  // namespace cli_streams
}  // namespace video
}  // namespace satori
