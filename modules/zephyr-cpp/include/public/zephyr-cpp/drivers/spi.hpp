/**
 * @file   spi.hpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#pragma once

#include <zephyr-cpp/expected.hpp>

#include <zephyr/drivers/spi.h>

namespace zephyr::spi {

void_t ready(const spi_dt_spec &spec);
void_t write(const spi_dt_spec &spec, const spi_buf_set &buf_set);
void_t read(const spi_dt_spec &spec, const spi_buf_set &buf_set);

} // namespace zephyr::spi
