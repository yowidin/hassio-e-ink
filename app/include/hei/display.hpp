/**
 * @file   display.hpp
 * @author Dennis Sitelew
 * @date   Oct. 03, 2024
 */
#pragma once

#include <cstdint>

#include <span>

namespace hei::display {

using span_t = std::span<const std::uint8_t, std::dynamic_extent>;

enum class refresh {
   full,
   image,
};

bool init();

void begin(std::uint16_t width, std::uint16_t height);

void update(span_t data);

void end(std::uint16_t width, std::uint16_t height, refresh type);

void clear();

} // namespace hei::display
