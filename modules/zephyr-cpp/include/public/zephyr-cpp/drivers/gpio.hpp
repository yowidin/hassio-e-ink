/**
 * @file   gpio.hpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#pragma once

#include <zephyr-cpp/expected.hpp>

#include <zephyr/drivers/gpio.h>

namespace zephyr::gpio {

void_t ready(const gpio_dt_spec &spec);

void_t configure(const gpio_dt_spec &spec, gpio_flags_t extra_flags);
void_t interrupt_configure(const gpio_dt_spec &spec, gpio_flags_t flags);
void_t add_callback(const gpio_dt_spec &spec, gpio_callback &cb);

void_t set(const gpio_dt_spec &spec, bool logic_high);
expected<bool> get(const gpio_dt_spec &spec);

} // namespace zephyr::gpio
