/**
 * @file   hal.hpp
 * @author Dennis Sitelew
 * @date   Jul. 28, 2024
 */

#pragma once

#include <it8951/common.hpp>
#include <it8951/util.hpp>

#include <cstdint>
#include <optional>
#include <system_error>

#include <zephyr/kernel.h>

namespace it8951::hal {

//! When using SPI every data exchange with the IT8951 requires a preamble, which depends on the desired action
enum class preamble : std::uint16_t {
   write_command = 0x6000,
   write_data = 0x0000,
   read_data = 0x1000,
};

enum class command : std::uint16_t {
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

enum class reg : std::uint16_t {
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

void write_command(const device &dev, command cmd, std::error_code &ec);
void write_command(const device &dev, command cmd);

void write_command(const device &dev, command cmd, const_span_t args, std::error_code &ec);
void write_command(const device &dev, command cmd, const_span_t args);

void write_data(const device &dev, const_span_t data, std::error_code &ec);
void write_data(const device &dev, const_span_t data);

// Write a maximum of CONFIG_EPD_BURST_WRITE_BUFFER_SIZE bytes
void burst_write_one_chunk(const device &dev, std::span<const std::uint8_t> data, std::error_code &ec);
void burst_write_one_chunk(const device &dev, std::span<const std::uint8_t> data);

void write_data_chunked_bursts(const device &dev, std::span<const std::uint8_t> data, std::error_code &ec);
void write_data_chunked_bursts(const device &dev, std::span<const std::uint8_t> data);

void write_register(const device &dev, reg reg, std::uint16_t value, std::error_code &ec);
void write_register(const device &dev, reg reg, std::uint16_t value);

void read_data(const device &dev, span_t data, std::error_code &ec);
void read_data(const device &dev, span_t data);

std::optional<std::uint16_t> read_register(const device &dev, reg reg, std::error_code &ec);
std::uint16_t read_register(const device &dev, reg reg);

void enable_packed_mode(const device &dev, std::error_code &ec);
void enable_packed_mode(const device &dev);

namespace vcom {

std::optional<std::uint16_t> get(const device &dev, std::error_code &ec);
std::uint16_t get(const device &dev);

void set(const device &dev, std::uint16_t value, std::error_code &ec);
void set(const device &dev, std::uint16_t value);

} // namespace vcom

namespace system {

void run(const device &dev, std::error_code &ec);
void run(const device &dev);

void sleep(const device &dev, std::error_code &ec);
void sleep(const device &dev);

void power(const device &dev, bool is_on, std::error_code &ec);
void power(const device &dev, bool is_on);

} // namespace system

namespace image {

void begin(const device &dev,
           const common::image::area &area,
           const common::image::config &config,
           std::error_code &ec);
void begin(const device &dev, const common::image::area &area, const common::image::config &config);

void end(const device &dev, const common::image::area &area, const common::waveform_mode mode, std::error_code &ec);
void end(const device &dev, const common::image::area &area, const common::waveform_mode mode);

} // namespace image

} // namespace it8951::hal