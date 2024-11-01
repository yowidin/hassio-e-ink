/**
 * @file   error.cpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#include <zephyr-cpp/error.hpp>

#include <cstdlib>

namespace zephyr::error {

std::error_code make(int error) {
   return std::error_code{std::abs(error), std::system_category()};
}

} // namespace zephyr::error