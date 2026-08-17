#include "v4l2diag/core/trigger_registry.hpp"

#include "v4l2diag/hw/gpio_trigger.hpp"

#include <memory>
#include <string>

namespace v4l2diag {

std::string TriggerRegistry::key(const GpioMapping &mapping) {
  return std::to_string(mapping.chip_id) + ":" + std::to_string(mapping.line_number);
}

std::shared_ptr<TriggerSource> TriggerRegistry::get_or_create(const GpioMapping &mapping, std::string *error) {
  std::lock_guard<std::mutex> lk(mtx_);
  const std::string k = key(mapping);
  auto it = triggers_.find(k);
  if (it != triggers_.end()) {
    return it->second;
  }

  auto trigger = std::make_shared<GpioTrigger>();
  if (!trigger->open(mapping, error)) {
    return nullptr;
  }
  triggers_.emplace(k, trigger);
  return trigger;
}

}  // namespace v4l2diag
