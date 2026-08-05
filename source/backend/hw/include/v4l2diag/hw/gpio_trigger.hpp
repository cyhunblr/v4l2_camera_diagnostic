#pragma once

#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/hw/trigger_source.hpp"

#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>
#include <thread>

#include <gpiod.hpp>

namespace v4l2diag {

/*
 * RAII wrapper around a single libgpiodcxx output line used to generate
 * hardware trigger pulses for V4L2 cameras.
 *
 * The line is driven LOW on construction and is released on destruction.
 * send_async() drives the line HIGH and timestamps the rising edge
 * synchronously, then hands the "hold pulse_ns then go LOW" work off to a
 * persistent background thread (started in open()) so the caller can
 * start waiting for a frame immediately instead of blocking for the full
 * pulse width. wait_pulse_done()/send() are available for callers that
 * still want to block until the LOW transition has completed.
 */
class GpioTrigger final : public TriggerSource {
 public:
  GpioTrigger() = default;
  ~GpioTrigger() override;

  GpioTrigger(const GpioTrigger &) = delete;
  GpioTrigger &operator=(const GpioTrigger &) = delete;
  GpioTrigger(GpioTrigger &&) = delete;

  bool open(const GpioMapping &mapping, std::string *error = nullptr);
  bool is_open() const {
    return open_;
  }

  // Default pulse width: 13 ms (minimum for TIER4 ISX021 cameras).
  TriggerMode mode() const override {
    return TriggerMode::Hardware;
  }
  struct timespec send_async(uint64_t pulse_ns = 13'000'000UL) override;
  void wait_pulse_done() override;
  bool wait_pulse_done_for(int timeout_ms);

 private:
  void worker_loop();

  gpiod::chip chip_;
  gpiod::line line_;
  bool open_ = false;

  std::thread worker_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool stop_ = false;

  // Single in-flight pulse slot: only one pulse can physically be on the
  // wire at a time, so a full work queue is unnecessary.
  bool pulse_pending_ = false;      // LOW-job submitted, worker hasn't picked it up yet
  bool pulse_in_progress_ = false;  // true from send_async() until the worker sets LOW
  uint64_t pending_pulse_ns_ = 0;
};

}  // namespace v4l2diag
