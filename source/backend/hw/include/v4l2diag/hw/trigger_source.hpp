#pragma once

#include "v4l2diag/core/types.hpp"

#include <cstdint>
#include <ctime>

namespace v4l2diag {

class TriggerSource {
 public:
  virtual ~TriggerSource() = default;
  virtual TriggerMode mode() const = 0;

  // Sets the line HIGH synchronously, timestamps the HIGH edge, hands the
  // "sleep pulse_ns then go LOW" work to a background mechanism, and
  // returns t_trigger immediately — does NOT wait for the pulse to finish.
  virtual struct timespec send_async(uint64_t pulse_ns = 13'000'000UL) = 0;

  // Blocks the calling thread until the most recently issued send_async()
  // pulse has completed its LOW transition. No-op for triggers with
  // nothing in flight (e.g. FreeRunTrigger).
  virtual void wait_pulse_done() = 0;

  // Back-compat blocking call: fire and wait for completion before
  // returning. Built from the two primitives above so every existing
  // caller that wants the old blocking semantics needs no changes.
  virtual struct timespec send(uint64_t pulse_ns = 13'000'000UL) {
    struct timespec t = send_async(pulse_ns);
    wait_pulse_done();
    return t;
  }
};

class FreeRunTrigger final : public TriggerSource {
 public:
  TriggerMode mode() const override {
    return TriggerMode::FreeRun;
  }
  struct timespec send_async(uint64_t pulse_ns = 13'000'000UL) override;
  void wait_pulse_done() override {}
};

}  // namespace v4l2diag
