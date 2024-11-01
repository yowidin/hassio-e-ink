/**
 * @file   common.hpp
 * @author Dennis Sitelew
 * @date   Oct. 25, 2024
 */

#pragma once

#include <cstdint>

namespace it8951::common {

enum class endianness : std::uint16_t {
   little = 0,
   big = 1,
};

enum class pixel_format : std::uint16_t {
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
enum class waveform_mode : std::uint16_t {
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

   //! The GL16 waveform is primarily used to update sparse content on a white background, such as a page of
   //! anti-aliased text, with reduced flash. The GL16 waveform has 16 unique gray levels.
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

enum class rotation : std::uint16_t {
   rotate0 = 0b00,
   rotate90 = 0b01,
   rotate180 = 0b10,
   rotate270 = 0b11,
};

namespace image {

struct area {
   std::uint16_t x;
   std::uint16_t y;
   std::uint16_t width;
   std::uint16_t height;
};

struct config {
   common::endianness endianness;
   common::pixel_format pixel_format;
   common::rotation rotation;
   common::waveform_mode mode;
};

} // namespace image

} // namespace it8951::common
