/**
 * @file   error.cpp
 * @author Dennis Sitelew
 * @date   Jul. 31, 2024
 */

#include <it8951/error.hpp>

#include <string>

namespace detail {

struct it8951_error_category : std::error_category {
   [[nodiscard]] const char *name() const noexcept override;
   [[nodiscard]] std::string message(int ev) const override;
};

const char *it8951_error_category::name() const noexcept {
   return "it8951-error";
}

std::string it8951_error_category::message(int ev) const {
   using namespace it8951;
   switch (static_cast<error>(ev)) {
      case error::success:
         return "not an error";

      case error::hal_error:
         return "HAL error";

      case error::kernel_error:
         return "kernel error";

      case error::transport_error:
         return "SPI transport error";

      case error::driver_error:
         return "display driver error";

      case error::display_error:
         return "display error";

      case error::timeout:
         return "internal timeout";

      case error::invalid_usage:
         return "invalid driver usage";

      default:
         return "(unrecognized error)";
   }
}

} // namespace detail

namespace it8951 {

const std::error_category &it8951_category() noexcept {
   static const detail::it8951_error_category category_const;
   return category_const;
}

} // namespace it8951