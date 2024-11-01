/**
 * @file   common.hpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#pragma once

#include <zephyr-cpp/expected.hpp>

namespace hei {

template <typename T>
using expected = zephyr::expected<T>;

using void_t = zephyr::void_t;

} // namespace hei
