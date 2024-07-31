/**
 * @file   error.hpp
 * @author Dennis Sitelew
 * @date   Jul. 31, 2024
 */

#pragma once

#include <system_error>

namespace it8951 {

enum class error {
   //! Not an error
   success = 0,
   hal_error,
   kernel_error,
   transport_error,
   driver_error,
   display_error,
   timeout,
   invalid_usage,
};

const std::error_category &it8951_category() noexcept;

inline std::error_code make_error_code(it8951::error ec) {
   return {static_cast<int>(ec), it8951_category()};
}

} // namespace it8951

namespace std {

template <>
struct is_error_code_enum<it8951::error> : true_type {};

} // namespace std