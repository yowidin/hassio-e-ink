/**
 * @file   util.hpp
 * @author Dennis Sitelew
 * @date   Jul. 28, 2024
 */

#pragma once

#include <cstdint>

#include <bit>
#include <span>

#include <it8951/init.h>

namespace it8951 {

//! Span type definition (IT8951 has a word size of 2)
using span_t = std::span<std::uint16_t>;
using const_span_t = std::span<const std::uint16_t>;

namespace encoding {

constexpr std::uint16_t byte_swap(std::uint16_t value) {
   return static_cast<std::uint16_t>(((value & 0xFF00U) >> 8U) | ((value & 0x00FFU) << 8U));
}

constexpr std::uint16_t from_host(std::uint16_t value) {
   if constexpr (std::endian::native == std::endian::little) {
      return byte_swap(value);
   } else {
      return value;
   }
}

constexpr std::uint16_t to_host(std::uint16_t value) {
   if constexpr (std::endian::native == std::endian::little) {
      return byte_swap(value);
   } else {
      return value;
   }
}

} // namespace encoding

inline it8951_data_t &get_data(const device &dev) {
   return *static_cast<it8951_data_t *>(dev.data);
}

inline const it8951_config_t &get_config(const device &dev) {
   return *static_cast<const it8951_config_t *>(dev.config);
}

} // namespace it8951
