/**
 * @file   display.cpp
 * @author Dennis Sitelew
 * @date   Oct. 03, 2024
 */

#include <hei/display.hpp>

#if CONFIG_EPD_IT8951
#include <it8951/it8951.hpp>
#endif

#include <zephyr/logging/log.h>

#include <stdexcept>

LOG_MODULE_REGISTER(display, CONFIG_APP_LOG_LEVEL);

namespace {
#if CONFIG_EPD_IT8951
const struct device *const display_driver = DEVICE_DT_GET_ONE(ite_it8951);
#endif
}

namespace hei::display {

bool init() {
#if CONFIG_EPD_IT8951
   if (!device_is_ready(display_driver)) {
      LOG_ERR("Display not ready: %s", display_driver->name);
      return false;
   }
#endif

   return true;
}

void begin(std::uint16_t width, std::uint16_t height) {
#if CONFIG_EPD_IT8951
   it8951::begin(*display_driver, width, height);
#endif
}

void update(span_t data) {
#if CONFIG_EPD_IT8951
   it8951::update(*display_driver, data);
#endif
}

void end(std::uint16_t width, std::uint16_t height, refresh type) {
#if CONFIG_EPD_IT8951
   it8951::refresh t;
   switch (type) {
      case refresh::image:
         t = it8951::refresh::image;
         break;

      case refresh::full:
      default:
         t = it8951::refresh::full;
         break;
   }

   it8951::end(*display_driver, width, height, t);
#endif
}

void clear() {
#if CONFIG_EPD_IT8951
   it8951::clear(*display_driver);
#endif
}

} // namespace hei::display
