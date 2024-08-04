/**
 * @file   wifi.hpp
 * @author Dennis Sitelew
 * @date   Aug. 02, 2024
 */

#pragma once

namespace hei::wifi {

/**
 * Ensure there is an active network configuration (either connected to a network, or an AP is configured to accept
 * clients).
 */
void setup();

} // namespace hei::wifi