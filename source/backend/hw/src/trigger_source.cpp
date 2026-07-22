#include "v4l2diag/hw/trigger_source.hpp"

#include <ctime>

namespace v4l2diag {

struct timespec FreeRunTrigger::send(uint64_t) {
  struct timespec now {};
  clock_gettime(CLOCK_REALTIME, &now);
  return now;
}

}  // namespace v4l2diag
