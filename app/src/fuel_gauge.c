/**
 * @file   fuel_gauge.c
 * @author Dennis Sitelew
 * @date   Jul. 24, 2024
 */

#include <hei/fuel_gauge.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/fuel_gauge.h>
#include <zephyr/logging/log.h>

static const struct device *const fuel_gauge = DEVICE_DT_GET_ONE(maxim_max17048);

LOG_MODULE_REGISTER(fuel_gauge, CONFIG_APP_LOG_LEVEL);

bool hei_fuel_gauge_init(void) {
   if (!device_is_ready(fuel_gauge)) {
      LOG_ERR("Fuel gauge not ready: %s", fuel_gauge->name);
      return false;
   }

   return true;
}

void hei_fuel_gauge_print(void) {
   if (!device_is_ready(fuel_gauge)) {
      LOG_WRN("Fuel gauge not ready: %s", fuel_gauge->name);
      return;
   }

   fuel_gauge_prop_t props[] = {
      FUEL_GAUGE_RUNTIME_TO_EMPTY,
      FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE,
      FUEL_GAUGE_VOLTAGE,
   };

   union fuel_gauge_prop_val vals[ARRAY_SIZE(props)];

   int err = fuel_gauge_get_props(fuel_gauge, props, vals, ARRAY_SIZE(props));
   if (err < 0) {
      LOG_ERR("Fuel gauge read failed");
   } else {
      LOG_INF("Time to empty %" PRIu32, vals[0].runtime_to_empty);
      LOG_INF("Charge  %" PRIu8 "%%", vals[1].relative_state_of_charge);
      LOG_INF("Voltage %d", vals[2].voltage);
   }
}
