#include "v4l2diag/core/profile_registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-profile-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : "/tmp/v4l2diag-profile-test";
}

bool exists(const std::string &path) {
  struct stat info {};
  return stat(path.c_str(), &info) == 0;
}

}  // namespace

int main() {
  const std::string dir = make_temp_dir();
  v4l2diag::ProfileRegistry empty_registry(dir);
  if (!empty_registry.list_profiles().empty()) {
    std::cerr << "a new registry must start empty\n";
    return 1;
  }

  v4l2diag::DeviceProfile profile;
  profile.id = "custom-rig";
  profile.name = "Custom Rig";
  profile.description = "Test profile.";
  profile.defaults.trigger_mode = v4l2diag::TriggerMode::Hardware;
  profile.defaults.memory_backends = {v4l2diag::MemoryBackend::Mmap};
  v4l2diag::TriggerChannel channel;
  channel.id = "primary-line";
  channel.name = "Primary line";
  channel.type = v4l2diag::TriggerChannel::Type::Hardware;
  channel.gpio = {0, 3, 42, "TEST"};
  profile.trigger_channels.push_back(channel);
  profile.camera_bindings.push_back({{"driver-test", "card-test", "bus-test"}, channel.id});

  std::string error;
  if (!empty_registry.add_or_update_profile(profile, &error)) {
    std::cerr << error << "\n";
    return 1;
  }
  if (!exists(dir + "/custom-rig.json")) {
    std::cerr << "versioned JSON profile was not written\n";
    return 1;
  }

  v4l2diag::ProfileRegistry reloaded(dir);
  v4l2diag::DeviceProfile loaded;
  if (!reloaded.get_profile(profile.id, &loaded) || loaded.schema_version != 2 || loaded.trigger_channels.size() != 1 ||
      loaded.trigger_channels.front().gpio.line_number != 42 || loaded.camera_bindings.size() != 1) {
    std::cerr << "JSON profile did not round-trip\n";
    return 1;
  }

  v4l2diag::DeviceProfile invalid = profile;
  invalid.id = "../escape";
  if (reloaded.add_or_update_profile(invalid, &error)) {
    std::cerr << "unsafe profile id was accepted\n";
    return 1;
  }

  if (!reloaded.remove_profile(profile.id, &error) || reloaded.get_profile(profile.id, &loaded)) {
    std::cerr << "profile removal failed\n";
    return 1;
  }
  return 0;
}
