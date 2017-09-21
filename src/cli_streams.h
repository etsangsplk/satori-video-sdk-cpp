#pragma once

#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <memory>
#include <string>

#include "librtmvideo/data.h"
#include "rtmclient.h"
#include "streams.h"

namespace rtm {
namespace video {
namespace cli_streams {

namespace po = boost::program_options;

struct configuration {
 public:
  po::options_description to_boost() const;

  bool validate(const po::variables_map &vm) const;

  std::shared_ptr<rtm::client> rtm_client(const po::variables_map &vm,
                                          boost::asio::io_service &io_service,
                                          boost::asio::ssl::context &ssl_context,
                                          error_callbacks &rtm_error_callbacks) const;

  std::string rtm_channel(const po::variables_map &vm) const;

  bool is_batch_mode(const po::variables_map &vm) const;

  streams::publisher<encoded_packet> encoded_publisher(
      const po::variables_map &vm, boost::asio::io_service &io_service,
      const std::shared_ptr<rtm::client> client, const std::string &channel,
      bool network_buffer) const;

  streams::subscriber<encoded_packet> &encoded_subscriber(
      const po::variables_map &vm, const std::shared_ptr<rtm::client> client,
      const std::string &channel) const;

 public:
  bool enable_rtm_input{false};
  bool enable_file_input{false};
  bool enable_camera_input{false};
  bool enable_rtm_output{false};
  bool enable_file_output{false};
  bool enable_file_batch_mode{false};
};

}  // namespace cli_streams
}  // namespace video
}  // namespace rtm