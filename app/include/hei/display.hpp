/**
 * @file   display.hpp
 * @author Dennis Sitelew
 * @date   Oct. 03, 2024
 */
#pragma once

#include <it8951/display.hpp>

#include <cstdint>

#include <span>

namespace hei::display {

bool init();

it8951::display &get();

} // namespace hei::display
