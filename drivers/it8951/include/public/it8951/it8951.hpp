/**
 * @file   it8951.hpp
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>

#include <zephyr/device.h>

namespace it8951 {

enum class refresh {
   full,
   image,
};

void begin(const device &dev, std::uint16_t width, std::uint16_t height);
void update(const device &dev, std::span<const std::uint8_t> data);
void end(const device &dev, std::uint16_t width, std::uint16_t height, refresh type);

void clear(const device &dev);

} // namespace it8951
