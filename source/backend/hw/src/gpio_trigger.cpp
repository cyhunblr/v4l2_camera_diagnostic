#include "v4l2diag/hw/gpio_trigger.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>

namespace v4l2diag {

GpioTrigger::~GpioTrigger() {
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
    return true;
  } catch (const std::exception &e) {
    if (error) {
      *error = e.what();
    }
    return false;
  }
}

struct timespec GpioTrigger::send(uint64_t pulse_ns) {
  struct timespec t_high;
  line_.set_value(1);
  clock_gettime(CLOCK_REALTIME, &t_high);

  struct timespec req;
  req.tv_sec = static_cast<time_t>(pulse_ns / 1'000'000'000UL);
  req.tv_nsec = static_cast<long>(pulse_ns % 1'000'000'000UL);
  nanosleep(&req, nullptr);

  line_.set_value(0);
  return t_high;
}

}  // namespace v4l2diag
