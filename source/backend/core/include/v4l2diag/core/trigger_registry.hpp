#pragma once

#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/hw/trigger_source.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace v4l2diag {

/*
 * Caches GpioTrigger instances keyed by physical "chip_id:line_number", so
 * that multiple cameras sharing the same physical GPIO line share one
 * GpioTrigger (and therefore its one background pulse-worker thread)
 * instead of each opening an independent handle to the same pin.
 */
class TriggerRegistry {
 public:
  // Returns the existing shared trigger for mapping's chip_id:line_number,
  // opening and caching a new GpioTrigger if none exists yet. Returns
  // nullptr and sets *error if opening a new trigger fails; failed opens
  // are not cached, so a later call may retry.
  std::shared_ptr<TriggerSource> get_or_create(const GpioMapping &mapping, std::string *error = nullptr);

 private:
  static std::string key(const GpioMapping &mapping);

  std::mutex mtx_;
  std::unordered_map<std::string, std::shared_ptr<TriggerSource>> triggers_;
};

}  // namespace v4l2diag
