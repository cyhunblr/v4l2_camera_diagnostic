// Exercises the background-thread pulse-firing concurrency primitive used by
// GpioTrigger (single-slot mutex/condvar handoff between send_async() and a
// persistent worker thread). GpioTrigger itself owns real gpiod::chip/line
// handles that require physical hardware, so this test reimplements the
// identical worker/mutex/condvar structure with a no-op stand-in for
// line_.set_value() instead, to validate the concurrency logic in isolation.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <iostream>
#include <mutex>
#include <thread>

namespace {

class TestableGpioTrigger {
 public:
  TestableGpioTrigger() {
    worker_ = std::thread(&TestableGpioTrigger::worker_loop, this);
  }

  ~TestableGpioTrigger() {
    wait_pulse_done();
    {
      std::lock_guard<std::mutex> lk(mtx_);
      stop_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
  }

  struct timespec send_async(uint64_t pulse_ns) {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [this] { return !pulse_in_progress_; });

    line_value_ = 1;
    set_value_calls_++;
    struct timespec t_high;
    clock_gettime(CLOCK_MONOTONIC, &t_high);

    pending_pulse_ns_ = pulse_ns;
    pulse_pending_ = true;
    pulse_in_progress_ = true;
    cv_.notify_all();
    return t_high;
  }

  void wait_pulse_done() {
    std::unique_lock<std::mutex> lk(mtx_);
    cv_.wait(lk, [this] { return !pulse_in_progress_ && !pulse_pending_; });
  }

  int line_value() const {
    return line_value_;
  }
  int set_value_calls() const {
    return set_value_calls_;
  }

 private:
  void worker_loop() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (true) {
      cv_.wait(lk, [this] { return stop_ || pulse_pending_; });
      if (!pulse_pending_) {
        return;
      }
      uint64_t pulse_ns = pending_pulse_ns_;
      pulse_pending_ = false;
      lk.unlock();

      struct timespec req;
      req.tv_sec = static_cast<time_t>(pulse_ns / 1'000'000'000UL);
      req.tv_nsec = static_cast<long>(pulse_ns % 1'000'000'000UL);
      nanosleep(&req, nullptr);
      line_value_ = 0;
      set_value_calls_++;

      lk.lock();
      pulse_in_progress_ = false;
      cv_.notify_all();
    }
  }

  std::thread worker_;
  std::mutex mtx_;
  std::condition_variable cv_;
  bool stop_ = false;

  bool pulse_pending_ = false;
  bool pulse_in_progress_ = false;
  uint64_t pending_pulse_ns_ = 0;

  std::atomic<int> line_value_{0};
  std::atomic<int> set_value_calls_{0};
};

double elapsed_ms(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

}  // namespace

int main() {
  // 1. send_async() returns in near-zero wall time regardless of pulse_ns.
  {
    TestableGpioTrigger trigger;
    const auto t0 = std::chrono::steady_clock::now();
    trigger.send_async(13'000'000UL);  // 13ms pulse
    const auto t1 = std::chrono::steady_clock::now();
    if (elapsed_ms(t0, t1) >= 5.0) {
      std::cerr << "send_async() blocked for " << elapsed_ms(t0, t1) << "ms, expected near-instant return\n";
      return 1;
    }
    trigger.wait_pulse_done();
  }

  // 2. wait_pulse_done() blocks until at least pulse_ns has elapsed.
  {
    TestableGpioTrigger trigger;
    const auto t0 = std::chrono::steady_clock::now();
    trigger.send_async(20'000'000UL);  // 20ms pulse
    trigger.wait_pulse_done();
    const auto t1 = std::chrono::steady_clock::now();
    if (elapsed_ms(t0, t1) < 18.0) {
      std::cerr << "wait_pulse_done() returned too early after " << elapsed_ms(t0, t1) << "ms\n";
      return 1;
    }
    if (trigger.line_value() != 0) {
      std::cerr << "line was not LOW after wait_pulse_done()\n";
      return 1;
    }
  }

  // 3. Destructor joins cleanly with no pulse in flight.
  { TestableGpioTrigger trigger; }

  // 3b. Destructor joins cleanly with a pulse still in flight (waits for it
  // to finish rather than corrupting state or deadlocking).
  {
    const auto t0 = std::chrono::steady_clock::now();
    {
      TestableGpioTrigger trigger;
      trigger.send_async(15'000'000UL);  // 15ms pulse, destructor runs immediately after
    }
    const auto t1 = std::chrono::steady_clock::now();
    if (elapsed_ms(t0, t1) < 13.0) {
      std::cerr << "destructor did not wait for the in-flight pulse to finish\n";
      return 1;
    }
  }

  // 4. Overlapping send_async() calls serialize: a second call issued while
  // the first pulse is still in progress blocks until the first's LOW
  // transition completes.
  {
    TestableGpioTrigger trigger;
    trigger.send_async(15'000'000UL);
    const auto t0 = std::chrono::steady_clock::now();
    trigger.send_async(5'000'000UL);  // should block until the first pulse's LOW
    const auto t1 = std::chrono::steady_clock::now();
    if (elapsed_ms(t0, t1) < 10.0) {
      std::cerr << "overlapping send_async() did not serialize (returned after " << elapsed_ms(t0, t1) << "ms)\n";
      return 1;
    }
    trigger.wait_pulse_done();
    if (trigger.set_value_calls() != 4) {  // HIGH,LOW,HIGH,LOW
      std::cerr << "unexpected set_value call count: " << trigger.set_value_calls() << "\n";
      return 1;
    }
  }

  return 0;
}
