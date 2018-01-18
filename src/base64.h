#pragma once

#include <string>

namespace satori {
namespace video {
namespace base64 {

constexpr double overhead = 4. / 3.;

std::string decode(const std::string &val);
std::string encode(const std::string &val);

}  // namespace base64
}  // namespace video
}  // namespace satori
