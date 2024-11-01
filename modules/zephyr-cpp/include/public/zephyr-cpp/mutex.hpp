/**
 * @file   mutex.hpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#pragma once

#include <zephyr-cpp/expected.hpp>

#include <zephyr/kernel.h>

namespace zephyr {

class mutex {
public:
   mutex();

   mutex(const mutex &o) = delete;
   mutex(mutex &&o) noexcept;

public:
   mutex &operator=(const mutex &o) = delete;
   mutex &operator=(mutex &&o) noexcept;

public:
   void_t lock();
   void_t unlock();
   expected<bool> try_lock();

private:
   bool initialized_{false};
   k_mutex m_{};
};

} // namespace zephyr
