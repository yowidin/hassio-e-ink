/**
 * @file   hal.hpp
 * @author Dennis Sitelew
 * @date   Jul. 28, 2024
 */

#pragma once

#include <it8951/driver.hpp>
#include <it8951/util.hpp>

#include <cstdint>
#include <functional>
#include <optional>

#include <zephyr/kernel.h>

namespace it8951 {

//! When using SPI every data exchange with the IT8951 requires a preamble, which depends on the desired action
enum class preamble_t : std::uint16_t {
   write_command = 0x6000,
   write_data = 0x0000,
   read_data = 0x1000,
};

enum class command_t : std::uint16_t {
   run = 0x0001,
   sleep = 0x0003,

   register_read = 0x0010,
   register_write = 0x0011,

   memory_burst_read_trigger = 0x0012,
   memory_burst_read_start = 0x0013,
   memory_burst_write = 0x0014,
   memory_burst_end = 0x0015,

   load_image = 0x0020,
   load_image_area = 0x0021,
   load_image_end = 0x0022,

   get_device_info = 0x0302,
   display_area = 0x0034,
   epd_power = 0x0038,
   set_vcom = 0x0039,
   force_set_temperature = 0x0040,
};

enum class reg_t : std::uint16_t {
   // System Registers
   SYS_REG_BASE = 0x0000,
   i80cpcr = 0x0004, // Command Parameter Control

   // Memory Converter Registers
   MCSR_BASE = 0x0200,
   mcsr = MCSR_BASE + 0x0000,
   lisar = MCSR_BASE + 0x0008,
   lisar_low = lisar,
   lisar_high = lisar + sizeof(std::uint16_t),

   DISPLAY_BASE = 0x1000,
   lutafsr = DISPLAY_BASE + 0x0224, // LUT Status (all LUT engines)
};

enum class endianness_t : std::uint16_t {
   little = 0,
   big = 1,
};

enum class pixel_format_t : std::uint16_t {
   pf2bpp = 0b00,
   pf3bpp = 0b01,
   pf4bpp = 0b10,
   pf8bpp = 0b11,
};

enum class waveform_mode_t : std::uint16_t {
   mode0 = 0,
   mode1 = 1,
   mode2 = 2,
   mode3 = 3,
   mode4 = 4,
};

enum class rotation_t : std::uint16_t {
   rotate0 = 0b00,
   rotate90 = 0b01,
   rotate180 = 0b10,
   rotate270 = 0b11,
};

struct image_config {
   endianness_t endianness;
   pixel_format_t pixel_format;
   rotation_t rotation;
};

struct image_area {
   std::uint16_t x, y, width, height;
};

namespace hal {

void write_command(const device &dev, command_t cmd);
void write_command(const device &dev, command_t cmd, const_span_t args);

void write_data(const device &dev, const_span_t data);
void write_register(const device &dev, reg_t reg, std::uint16_t value);

void read_data(const device &dev, span_t data);
std::uint16_t read_register(const device &dev, reg_t reg);

void enable_packed_mode(const device &dev);

void fill_screen(const device &dev,
                 const std::function<std::uint8_t(std::uint16_t, std::uint16_t)> &generator,
                 waveform_mode_t mode);

namespace vcom {

std::uint16_t get(const device &dev);
void set(const device &dev, std::uint16_t value);

} // namespace vcom

void set_power(const device &dev, std::uint16_t value);

} // namespace hal

} // namespace it8951