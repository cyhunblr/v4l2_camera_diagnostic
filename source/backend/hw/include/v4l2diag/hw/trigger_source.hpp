#pragma once

#include "v4l2diag/core/types.hpp"

#include <cstdint>
#include <ctime>

namespace v4l2diag {

class TriggerSource {
 public:
  virtual ~TriggerSource() = default;
  virtual TriggerMode mode() const = 0;
  virtual struct timespec send(uint64_t pulse_ns = 13'000'000UL) = 0;
};

class FreeRunTrigger final : public TriggerSource {
 public:
  TriggerMode mode() const override {
    return TriggerMode::FreeRun;
  }
  struct timespec send(uint64_t pulse_ns = 13'000'000UL) override;
};

}  // namespace v4l2diag
