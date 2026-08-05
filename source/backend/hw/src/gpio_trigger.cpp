#include "v4l2diag/hw/gpio_trigger.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <ctime>
#include <mutex>
#include <stdexcept>
#include <string>

namespace v4l2diag {

GpioTrigger::~GpioTrigger() {
  // Let any in-flight pulse finish naturally (typically a few ms, at most
  // the configured pulse width) so the pin is always left in a clean LOW
  // state rather than being forced LOW mid-pulse.
  if (open_) {
    wait_pulse_done();
  }
  {
    std::lock_guard<std::mutex> lk(mtx_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }

  if (open_) {
    try {
      line_.set_value(0);
      line_.release();
    } catch (...) {
    }
  }
}

bool GpioTrigger::open(const GpioMapping &mapping, std::string *error) {
  try {
    chip_ = gpiod::chip("gpiochip" + std::to_string(mapping.chip_id));
    line_ = chip_.get_line(mapping.line_number);
    line_.request({"v4l2diag", gpiod::line_request::DIRECTION_OUTPUT, 0});
    open_ = true;
    worker_ = std::thread(&GpioTrigger::worker_loop, this);
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = e.what();
    }
    return false;
  }
}

struct timespec GpioTrigger::send_async(uint64_t pulse_ns) {
  std::unique_lock<std::mutex> lk(mtx_);
  // Overlap guard: a prior pulse's LOW transition hasn't completed yet.
  // This should never happen in practice (callers on the same physical
  // line are already serialized), but block rather than corrupt state or
  // race the GPIO line if it does due to a bug elsewhere.
  cv_.wait(lk, [this] { return !pulse_in_progress_; });

  line_.set_value(1);
  struct timespec t_high;
  clock_gettime(CLOCK_REALTIME, &t_high);

  pending_pulse_ns_ = pulse_ns;
  pulse_pending_ = true;
  pulse_in_progress_ = true;
  cv_.notify_all();
  return t_high;
}

void GpioTrigger::worker_loop() {
  std::unique_lock<std::mutex> lk(mtx_);
  while (true) {
    cv_.wait(lk, [this] { return stop_ || pulse_pending_; });
    if (!pulse_pending_) {
      // Woken only by stop_ with nothing queued.
      return;
    }

    uint64_t pulse_ns = pending_pulse_ns_;
    pulse_pending_ = false;
    lk.unlock();

    struct timespec req;
    req.tv_sec = static_cast<time_t>(pulse_ns / 1'000'000'000UL);
    req.tv_nsec = static_cast<long>(pulse_ns % 1'000'000'000UL);
    nanosleep(&req, nullptr);
    line_.set_value(0);

    lk.lock();
    pulse_in_progress_ = false;
    cv_.notify_all();
  }
}

void GpioTrigger::wait_pulse_done() {
  std::unique_lock<std::mutex> lk(mtx_);
  cv_.wait(lk, [this] { return !pulse_in_progress_ && !pulse_pending_; });
}

bool GpioTrigger::wait_pulse_done_for(int timeout_ms) {
  std::unique_lock<std::mutex> lk(mtx_);
  return cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                      [this] { return !pulse_in_progress_ && !pulse_pending_; });
}

}  // namespace v4l2diag
