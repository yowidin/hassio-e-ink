/**
 * @file   shutdown.hpp
 * @author Dennis Sitelew
 * @date   Oct. 20, 2024
 */
#pragma once

#include <chrono>

namespace hei::shutdown {

void request(std::chrono::seconds duration);

} // namespace hei::shutdown
