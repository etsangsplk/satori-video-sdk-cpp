#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>
#include <boost/accumulators/statistics/rolling_window.hpp>
#include <iostream>

#include "base64.h"
#include "logging.h"
#include "metrics.h"
#include "video_error.h"
#include "video_streams.h"

namespace satori {
namespace video {

namespace {

namespace accum = boost::accumulators;

auto &frame_chunks_mismatch = prometheus::BuildCounter()
                                  .Name("network_decoder_frame_chunks_mismatch")
                                  .Register(metrics_registry())
                                  .Add({});

auto &frame_id_deltas =
    prometheus::BuildHistogram()
        .Name("frame_id_deltas")
        .Register(metrics_registry())
        .Add({}, std::vector<double>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

auto &frame_timestamp_delta_millis =
    prometheus::BuildHistogram()
        .Name("frame_timestamp_delta_millis")
        .Register(metrics_registry())
        .Add({}, std::vector<double>{0,  1,   2,   3,   4,   5,   6,   7,   8,  9,
                                     10, 15,  20,  25,  30,  40,  50,  60,  70, 80,
                                     90, 100, 200, 300, 400, 500, 750, 1000});

auto &frame_timestamp_jitter =
    prometheus::BuildHistogram()
        .Name("frame_timestamp_jitter")
        .Register(metrics_registry())
        .Add({}, std::vector<double>{0,  1,   2,   3,   4,   5,   6,   7,  8,  9,
                                     10, 15,  20,  25,  30,  40,  50,  60, 70, 80,
                                     90, 100, 150, 200, 250, 300, 400, 500});

auto &frame_arrival_time_delta_millis =
    prometheus::BuildHistogram()
        .Name("frame_arrival_time_delta_millis")
        .Register(metrics_registry())
        .Add({}, std::vector<double>{0,  1,   2,   3,   4,   5,   6,   7,   8,  9,
                                     10, 15,  20,  25,  30,  40,  50,  60,  70, 80,
                                     90, 100, 200, 300, 400, 500, 750, 1000});

auto &frame_arrival_time_jitter =
    prometheus::BuildHistogram()
        .Name("frame_arrival_time_itter")
        .Register(metrics_registry())
        .Add({}, std::vector<double>{0,  1,   2,   3,   4,   5,   6,   7,  8,  9,
                                     10, 15,  20,  25,  30,  40,  50,  60, 70, 80,
                                     90, 100, 150, 200, 250, 300, 400, 500});

}  // namespace

streams::op<network_packet, encoded_packet> decode_network_stream() {
  struct packet_visitor : boost::static_visitor<streams::publisher<encoded_packet>> {
   public:
    streams::publisher<encoded_packet> operator()(const network_metadata &nm) {
      encoded_metadata em;
      em.codec_name = nm.codec_name;
      em.codec_data = decode64(nm.base64_data);
      return streams::publishers::of({encoded_packet{em}});
    }

    streams::publisher<encoded_packet> operator()(const network_frame &nf) {
      if (_chunk != nf.chunk) {
        LOG(ERROR) << "chunk mismatch f.id=" << nf.id << " expected " << _chunk
                   << ", got " << nf.chunk;
        frame_chunks_mismatch.Increment();
        reset();
        return streams::publishers::empty<encoded_packet>();
      }

      _aggregated_data.append(nf.base64_data);
      if (nf.chunk == 1) {
        _id = nf.id;
      }

      if (nf.chunk == nf.chunks) {
        encoded_frame frame;
        frame.data = decode64(_aggregated_data);
        frame.id = _id;
        frame.timestamp = nf.t;
        reset();
        return streams::publishers::of({encoded_packet{frame}});
      }

      _chunk++;
      return streams::publishers::empty<encoded_packet>();
    }

   private:
    void reset() {
      _chunk = 1;
      _aggregated_data.clear();
    }

    uint8_t _chunk{1};
    frame_id _id;
    std::string _aggregated_data;
  };

  return [](streams::publisher<network_packet> &&src) {
    packet_visitor visitor;
    return std::move(src) >> streams::flat_map([visitor = std::move(visitor)](
                                 const network_packet &data) mutable {
             return boost::apply_visitor(visitor, data);
           });
  };
}

streams::op<encoded_packet, encoded_packet> repeat_metadata() {
  return streams::repeat_if<encoded_packet>(6000, [](const encoded_packet &p) {
    return nullptr != boost::get<encoded_metadata>(&p);
  });
}

streams::op<encoded_packet, encoded_packet> report_frame_dynamics() {
  struct packet_visitor : boost::static_visitor<void> {
   public:
    void operator()(const encoded_metadata & /*m*/) {}

    void operator()(const encoded_frame &f) {
      const auto arrival_time = std::chrono::system_clock::now();

      if (_first_frame) {
        _first_frame = false;
      } else {
        const double timestamp_delta =
            std::abs(std::chrono::duration_cast<std::chrono::milliseconds>(
                         f.timestamp - _last_frame_timestamp)
                         .count());
        const double arrival_time_delta =
            std::abs(std::chrono::duration_cast<std::chrono::milliseconds>(
                         arrival_time - _last_frame_arrival_time)
                         .count());

        frame_id_deltas.Observe(std::abs(f.id.i1 - _last_frame_id.i1));
        frame_timestamp_delta_millis.Observe(timestamp_delta);
        frame_arrival_time_delta_millis.Observe(arrival_time_delta);

        _timestamp_variance_accum(timestamp_delta);
        _arrival_time_variance_accum(arrival_time_delta);

        frame_timestamp_jitter.Observe(standard_deviation(_timestamp_variance_accum));
        frame_arrival_time_jitter.Observe(
            standard_deviation(_arrival_time_variance_accum));
      }

      _last_frame_id = f.id;
      _last_frame_timestamp = f.timestamp;
      _last_frame_arrival_time = arrival_time;
    }

   private:
    // TODO: prometheus can probably calculate standard deviation on it's own.
    double standard_deviation(
        accum::accumulator_set<double, accum::stats<accum::tag::variance>> &accumulator) {
      auto n = static_cast<double>(accum::count(accumulator));
      return std::sqrt((accum::variance(accumulator) * n) / n - 1);
    }

    bool _first_frame{true};
    frame_id _last_frame_id;
    std::chrono::system_clock::time_point _last_frame_timestamp;
    std::chrono::system_clock::time_point _last_frame_arrival_time;

    // TODO: might be nicer to have sliding time windows
    accum::accumulator_set<double, accum::stats<accum::tag::variance>>
        _timestamp_variance_accum{accum::tag::rolling_window::window_size = 25};
    accum::accumulator_set<double, accum::stats<accum::tag::variance>>
        _arrival_time_variance_accum{accum::tag::rolling_window::window_size = 25};
  };

  return [](streams::publisher<encoded_packet> &&src) {
    packet_visitor visitor;
    return std::move(src)
           >> streams::map([visitor = std::move(visitor)](encoded_packet && p) mutable {
               boost::apply_visitor(visitor, p);
               return std::move(p);
             });
  };
}

}  // namespace video
}  // namespace satori
