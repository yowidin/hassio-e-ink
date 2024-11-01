/**
 * @file   mutex.cpp
 * @author Dennis Sitelew
 * @date   Nov. 01, 2024
 */

#include <zephyr-cpp/error.hpp>
#include <zephyr-cpp/mutex.hpp>

#include <mutex>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zpp_mutex, CONFIG_ZEPHYR_CPP_LOG_LEVEL);

namespace zephyr {

mutex::mutex() {
   if (const auto err = k_mutex_init(&m_)) {
      LOG_ERR("Error making a mutex: %d", err);
   }

   initialized_ = true;
}

mutex::mutex(mutex &&o) noexcept {
   std::swap(m_, o.m_);
}

mutex &mutex::operator=(mutex &&o) noexcept {
   std::swap(m_, o.m_);
   return *this;
}

void_t mutex::lock() {
   if (!initialized_) {
      return unexpected(EINVAL);
   }

   if (const auto err = k_mutex_lock(&m_, K_FOREVER)) {
      LOG_ERR("Error locking a mutex: %d", err);
      return unexpected(err);
   }

   return {};
}

void_t mutex::unlock() {
   if (!initialized_) {
      return unexpected(EINVAL);
   }

   if (const auto err = k_mutex_unlock(&m_)) {
      LOG_ERR("Error unlocking a mutex: %d", err);
      return unexpected(err);
   }

   return {};
}

expected<bool> mutex::try_lock() {
   if (!initialized_) {
      return unexpected(EINVAL);
   }

   const auto err = k_mutex_lock(&m_, K_NO_WAIT);
   if (!err) {
      return true;
   }

   if (err == -EBUSY) {
      return false;
   }

   LOG_ERR("Error checking a mutex: %d", err);
   return unexpected(err);
}

} // namespace zephyr