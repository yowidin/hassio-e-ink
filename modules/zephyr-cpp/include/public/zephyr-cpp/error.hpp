/**
 * @file   error.hpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#pragma once

#include <system_error>

namespace zephyr::error {

//! Construct a system error code out of a Zephyr error value (usually negative)
std::error_code make(int error);

} // namespace zephyr::error
