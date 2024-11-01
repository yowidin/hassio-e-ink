/**
 * @file   expected.hpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#pragma once

#include <zephyr-cpp/error.hpp>

#include <tl/expected.hpp>

#include <system_error>

namespace zephyr {

using error_t = std::error_code;

template <typename T>
using expected = tl::expected<T, error_t>;

using void_t = expected<void>;

inline tl::unexpected<error_t> unexpected(int err) {
   return tl::unexpected(error::make(err));
}

} // namespace zephyr