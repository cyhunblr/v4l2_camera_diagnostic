#include "v4l2diag/core/run_routing.hpp"

#include <string>

namespace v4l2diag {

bool channel_matches_mode(TriggerMode mode, bool channel_is_hardware) {
  switch (mode) {
    case TriggerMode::Hardware:
      return channel_is_hardware;
    case TriggerMode::Software:
      return !channel_is_hardware;
    case TriggerMode::FreeRun:
      // Free-run routes nothing, so there is no channel to be incompatible with.
      return true;
  }
  return false;
}

std::string describe_mode_mismatch(TriggerMode mode, bool channel_is_hardware, const std::string &role,
                                   const std::string &channel_id) {
  if (channel_matches_mode(mode, channel_is_hardware)) {
    return std::string();
  }
  return std::string("trigger channel \"") + channel_id + "\" (role " + role + ") is a " +
         (channel_is_hardware ? "hardware" : "software") + " channel, which cannot serve a " + to_string(mode) + " run";
}

std::string effective_trigger_profile_id(TriggerMode mode, const std::string &requested_profile_id) {
  // Free-run neither needs nor uses a Trigger Profile. Normalised here rather than
  // checked at each surface, so no caller can produce a run that says "free-run"
  // and also names a profile.
  return mode == TriggerMode::FreeRun ? std::string() : requested_profile_id;
}

}  // namespace v4l2diag
