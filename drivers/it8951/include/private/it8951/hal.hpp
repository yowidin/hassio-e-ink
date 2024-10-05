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

//! From E-paper-mode-declaration.pdf
//! The AF waveform look-up tables are defined in a 5-bit (32-level) pixel state representation where the 16 gray-tones
//! are assigned to the even pixel states (0, 2, 4, ... 30), where 0 is black and 30 is white.
//! Odd pixel states 29 and 31 (along with state 30) are used to denote gray-tone 16; states 29 and 31 are used to
//! invoke special transitions to gray-tone 16.
enum class waveform_mode_t : std::uint16_t {
   //! The initialization (INIT) mode is used to completely erase the display and leave it in the white state.
   //! It is useful for situations where the display information in memory is not a faithful representation of the
   //! optical state of the display, for example, after the device receives power after it has been fully powered down.
   //! This waveform switches the display several times and leaves it in the white state.
   //!
   //! Recommended usage: display initialization
   //! Pixel state transition: [0 1 2 3 ... 31] -> 30
   //! Ghosting: N/A
   //! Update time: 2000ms
   init = 0,

   //! The direct update (DU) is a very fast, non-flashy update.
   //! This mode supports transitions from any gray-tone to black or white only. It cannot be used to update to any
   //! gray-tone other than black or white. The fast update time for this mode makes it useful for response to touch
   //! sensor or pen input or menu selection indicators.
   //!
   //! Recommended usage: Monochrome menu, text input, and touch screen/pen input
   //! Pixel state transition: [0 2 4 .. 30 31] -> [0 30]
   //! Ghosting: Low
   //! Update time: 260ms
   direct_update = 1,

   //! The grayscale clearing (GC16) mode is used to update the full display and provide a high image quality.
   //! When GC16 is used with Full Display Update the entire display will update as the new image is written.
   //! If a Partial Update command is used the only pixels with changing gray-tone values will update.
   //! The GC16 mode has 16 unique gray levels.
   //!
   //! Recommended usage: High quality images
   //! Pixel state transition: [0 2 4 .. 30 31] -> [0 2 4 .. 30]
   //! Ghosting: Very Low
   //! Update time: 450ms
   grayscale_clearing = 2,

   //! he GL16 waveform is primarily used to update sparse content on a white background, such as a page of anti-aliased
   //! text, with reduced flash. The GL16 waveform has 16 unique gray levels.
   //!
   //! Recommended usage: Text with white background
   //! Pixel state transition: [0 2 4 .. 30 31] -> [0 2 4 .. 30]
   //! Ghosting: Medium
   //! Update time: 450ms
   grayscale_limited = 3,

   //! The GLR16 mode is used in conjunction with an image preprocessing algorithm to update sparse content on a white
   //! background with reduced flash and reduced image artifacts. The GLR16 mode supports 16 gray-tones. If only the
   //! even pixel states are used (0, 2, 4, ... 30), the mode will behave exactly as a traditional GL16 waveform mode.
   //! If a separately-supplied image preprocessing algorithm is used, the transitions invoked by the pixel states 29
   //! and 31 are used to improve display quality.
   //!
   //! Recommended usage: Text with white background
   //! Pixel state transition: [0 2 4 .. 30 31] -> [0 2 4 .. 30]
   //! Ghosting: Low
   //! Update time: 450ms
   grayscale_limited_reduced = 4,
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

namespace image {

void begin(const device &dev, std::uint16_t width, std::uint16_t height);
void update(const device &dev, std::span<const std::uint8_t> data);
void end(const device &dev, std::uint16_t width, std::uint16_t height, waveform_mode_t mode);

} // namespace image

namespace vcom {

std::uint16_t get(const device &dev);
void set(const device &dev, std::uint16_t value);

} // namespace vcom

namespace system {

void run(const device &dev);
void sleep(const device &dev);

void power(const device &dev, bool is_on);

} // namespace system

} // namespace hal

} // namespace it8951