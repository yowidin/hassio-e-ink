/**
 * @file   it8951.hpp
 * @author Dennis Sitelew
 * @date   Jul. 25, 2024
 */

#pragma once

#include <zephyr/device.h>

namespace it8951 {

void read_device_info(const device *dev);

} // namespace it8951
