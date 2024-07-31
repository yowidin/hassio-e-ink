/**
 * @file   it8951.hpp
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include <zephyr/device.h>

namespace it8951 {

void display_dummy_image(const device &dev);

} // namespace it8951
