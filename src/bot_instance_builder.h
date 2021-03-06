#pragma once
#include <memory>
#include "bot_instance.h"
#include "satorivideo/multiframe/bot.h"

namespace satori {
namespace video {
struct bot_instance_builder {
  explicit bot_instance_builder(multiframe_bot_descriptor descriptor);
  bot_instance_builder(const bot_instance_builder &b) = default;
  ~bot_instance_builder() = default;

  bot_instance_builder &set_execution_mode(execution_mode mode);
  bot_instance_builder &set_config(const nlohmann::json &config);
  bot_instance_builder &set_bot_id(std::string id);
  std::unique_ptr<bot_instance> build();

 private:
  multiframe_bot_descriptor _descriptor;
  execution_mode _mode;
  std::string _id;
  nlohmann::json _config;
};
}  // namespace video
}  // namespace satori
