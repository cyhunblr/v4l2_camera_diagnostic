#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/core/test_registry.hpp"
#include "v4l2diag/core/types.hpp"
#include "v4l2diag/hw/trigger_source.hpp"
#include "v4l2diag/hw/v4l2_controls.hpp"

#include <iostream>
#include <linux/videodev2.h>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeControlIo final : public v4l2diag::V4l2ControlIo {
 public:
  int open_device(const std::string &path, std::string *) override {
    opened_path = path;
    return 42;
  }

  void close_device(int) override {
    closed = true;
  }

  bool query_control(int, uint32_t id, v4l2diag::V4l2ControlInfo *control, std::string *error) override {
    const auto it = controls.find(id);
    if (it == controls.end()) {
      if (error) {
        *error = "fake control is unavailable";
      }
      return false;
    }
    *control = it->second;
    return true;
  }

  bool set_control(int, const v4l2diag::V4l2ControlInfo &control, int64_t value, std::string *) override {
    writes.emplace_back(control.id, value);
    return true;
  }

  std::map<uint32_t, v4l2diag::V4l2ControlInfo> controls;
  std::vector<std::pair<uint32_t, int64_t>> writes;
  std::string opened_path;
  bool closed = false;
};

v4l2diag::V4l2ControlInfo control(uint32_t id, uint32_t type, const std::string &name, int64_t minimum, int64_t maximum,
                                  uint64_t step = 1) {
  v4l2diag::V4l2ControlInfo value;
  value.id = id;
  value.type = type;
  value.name = name;
  value.minimum = minimum;
  value.maximum = maximum;
  value.step = step;
  value.writable = true;
  value.supported_for_trigger = true;
  return value;
}

}  // namespace

int main() {
  v4l2diag::TriggerMode mode;
  if (!v4l2diag::parse_trigger_mode("hardware", &mode) || mode != v4l2diag::TriggerMode::Hardware ||
      !v4l2diag::parse_trigger_mode("software", &mode) || mode != v4l2diag::TriggerMode::Software ||
      !v4l2diag::parse_trigger_mode("free-run", &mode) || mode != v4l2diag::TriggerMode::FreeRun ||
      v4l2diag::parse_trigger_mode("mixed", &mode)) {
    std::cerr << "trigger mode parsing failed\n";
    return 1;
  }

  v4l2diag::FreeRunTrigger free_run;
  if (free_run.mode() != v4l2diag::TriggerMode::FreeRun || free_run.send().tv_sec == 0) {
    std::cerr << "free-run trigger did not provide a capture timestamp\n";
    return 1;
  }

  v4l2diag::DeviceProfile profile;
  profile.id = "software-test";
  profile.name = "Software Test";
  v4l2diag::TriggerChannel channel;
  channel.id = "software-control";
  channel.type = v4l2diag::TriggerChannel::Type::Software;
  profile.trigger_channels.push_back(channel);
  std::string error;
  if (v4l2diag::validate_device_profile(profile, &error)) {
    std::cerr << "software channel without fire controls was accepted\n";
    return 1;
  }
  profile.trigger_channels.front().fire_controls.push_back({1, "Trigger", 4, 0});
  if (!v4l2diag::validate_device_profile(profile, &error)) {
    std::cerr << "valid software profile was rejected: " << error << "\n";
    return 1;
  }

  auto fake = std::make_shared<FakeControlIo>();
  fake->controls.emplace(10, control(10, V4L2_CTRL_TYPE_INTEGER, "Setup", 0, 20, 2));
  fake->controls.emplace(11, control(11, V4L2_CTRL_TYPE_BUTTON, "Fire", 0, 0));
  fake->controls.emplace(12, control(12, V4L2_CTRL_TYPE_BOOLEAN, "Teardown", 0, 1));
  v4l2diag::TriggerChannel recipe;
  recipe.id = "software-recipe";
  recipe.type = v4l2diag::TriggerChannel::Type::Software;
  recipe.setup_controls.push_back({10, "Setup", V4L2_CTRL_TYPE_INTEGER, 8});
  recipe.fire_controls.push_back({11, "Fire", V4L2_CTRL_TYPE_BUTTON, 0});
  recipe.teardown_controls.push_back({12, "Teardown", V4L2_CTRL_TYPE_BOOLEAN, 1});
  {
    v4l2diag::V4l2ControlTrigger trigger(fake);
    if (!trigger.open("/dev/video-test", recipe, &error) || fake->opened_path != "/dev/video-test" ||
        fake->writes != std::vector<std::pair<uint32_t, int64_t>>{{10, 8}}) {
      std::cerr << "software trigger setup phase failed: " << error << "\n";
      return 1;
    }
    trigger.send();
    if (!trigger.last_error().empty() || fake->writes != std::vector<std::pair<uint32_t, int64_t>>{{10, 8}, {11, 0}}) {
      std::cerr << "software trigger fire phase failed\n";
      return 1;
    }
  }
  if (!fake->closed || fake->writes != std::vector<std::pair<uint32_t, int64_t>>{{10, 8}, {11, 0}, {12, 1}}) {
    std::cerr << "software trigger teardown phase failed\n";
    return 1;
  }

  const auto ranged = control(20, V4L2_CTRL_TYPE_INTEGER, "Ranged", 0, 10, 2);
  if (v4l2diag::validate_v4l2_control_write(ranged, {20, "Ranged", V4L2_CTRL_TYPE_INTEGER, 11}, &error) ||
      error.find("range") == std::string::npos ||
      v4l2diag::validate_v4l2_control_write(ranged, {20, "Renamed", V4L2_CTRL_TYPE_INTEGER, 8}, &error) ||
      error.find("metadata") == std::string::npos) {
    std::cerr << "software trigger metadata/range validation failed\n";
    return 1;
  }

  v4l2diag::TestDefinition pulse_width;
  if (!v4l2diag::find_test_definition("t15-gpio-pulse-width", &pulse_width) ||
      !v4l2diag::supports_trigger_mode(pulse_width, v4l2diag::TriggerMode::Hardware) ||
      v4l2diag::supports_trigger_mode(pulse_width, v4l2diag::TriggerMode::Software) ||
      v4l2diag::supports_trigger_mode(pulse_width, v4l2diag::TriggerMode::FreeRun)) {
    std::cerr << "hardware-only test mode contract is incorrect\n";
    return 1;
  }
  return 0;
}
