/**
 * @file   shutdown.hpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */

#pragma once

#include <chrono>

namespace power_ic::shutdown {

std::chrono::seconds get_sleep_duration();

} // namespace power_ic::power
