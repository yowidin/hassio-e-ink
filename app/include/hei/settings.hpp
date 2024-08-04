/**
 * @file   settings.hpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>

namespace hei::settings {

using span_t = std::span<char, std::dynamic_extent>;
using const_span_t = std::span<const char, std::dynamic_extent>;
using optional_span_t = std::optional<span_t>;

namespace wifi {

optional_span_t ssid();
optional_span_t password();
std::optional<std::uint8_t> security();

namespace ap {

span_t ssid();
span_t password();

} // namespace ap

} // namespace wifi

namespace image_server {

optional_span_t address();
std::optional<std::uint16_t> port();
std::optional<std::chrono::seconds> refresh_interval();

} // namespace image_server

bool configured();

} // namespace hei::settings
