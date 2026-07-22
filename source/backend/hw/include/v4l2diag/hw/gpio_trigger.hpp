#pragma once

#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/hw/trigger_source.hpp"

#include <cstdint>
#include <ctime>
#include <string>

#include <gpiod.hpp>

namespace v4l2diag {

/*
 * RAII wrapper around a single libgpiodcxx output line used to generate
 * hardware trigger pulses for V4L2 cameras.
 *
 * The line is driven LOW on construction and is released on destruction.
 * send() emits a HIGH pulse of the requested width and returns the
 * timestamp of the rising (HIGH) edge measured with CLOCK_REALTIME.
 */
class GpioTrigger final : public TriggerSource {
 public:
  GpioTrigger() = default;
  ~GpioTrigger();

  GpioTrigger(const GpioTrigger &) = delete;
  GpioTrigger &operator=(const GpioTrigger &) = delete;
  GpioTrigger(GpioTrigger &&) = delete;

  bool open(const GpioMapping &mapping, std::string *error = nullptr);
  bool is_open() const {
    return open_;
  }

  // Send a trigger pulse: HIGH → sleep(pulse_ns) → LOW.
  // Returns the timestamp of the HIGH (rising) edge.
  // Default pulse width: 13 ms (minimum for TIER4 ISX021 cameras).
  TriggerMode mode() const override {
    return TriggerMode::Hardware;
  }
  struct timespec send(uint64_t pulse_ns = 13'000'000UL) override;

 private:
  gpiod::chip chip_;
  gpiod::line line_;
  bool open_ = false;
};

}  // namespace v4l2diag
