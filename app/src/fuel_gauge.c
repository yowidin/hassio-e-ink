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
   const hei_fuel_gauge_measurement_t mes = hei_fuel_gauge_get();
   if (mes.valid) {
      return;
   }

   LOG_INF("Time to empty %" PRIu32, mes.runtime_to_empty_minutes);
   LOG_INF("Time to full %" PRIu32, mes.runtime_to_full_minutes);
   LOG_INF("Charge  %" PRIu8 "%%", mes.relative_state_of_charge_percentage);
   LOG_INF("Voltage %d", mes.voltage_uv);
}

hei_fuel_gauge_measurement_t hei_fuel_gauge_get(void) {
   if (!device_is_ready(fuel_gauge)) {
      LOG_WRN("Fuel gauge not ready: %s", fuel_gauge->name);
      return (hei_fuel_gauge_measurement_t){.valid = false};
   }

   // This is everything, MAX17048 supports (at least it's driver doesn't support anything else at the moment).
   fuel_gauge_prop_t props[] = {
      FUEL_GAUGE_RUNTIME_TO_EMPTY,
      FUEL_GAUGE_RUNTIME_TO_FULL,
      FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE,
      FUEL_GAUGE_VOLTAGE,
   };

   union fuel_gauge_prop_val vals[ARRAY_SIZE(props)];

   int err = fuel_gauge_get_props(fuel_gauge, props, vals, ARRAY_SIZE(props));
   if (err < 0) {
      LOG_ERR("Fuel gauge read failed");
      return (hei_fuel_gauge_measurement_t){.valid = false};
   }

   const int voltage = vals[3].voltage;
   if (voltage < 0) {
      LOG_ERR("Unexpected negative voltage: %d", voltage);
      return (hei_fuel_gauge_measurement_t){.valid = false};
   }

   return (hei_fuel_gauge_measurement_t){
      .valid = true,
      .runtime_to_empty_minutes = vals[0].runtime_to_empty,
      .runtime_to_full_minutes = vals[1].runtime_to_full,
      .relative_state_of_charge_percentage = vals[2].relative_state_of_charge,
      .voltage_uv = (uint32_t)voltage,
   };
}
