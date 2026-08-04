#include "v4l2diag/cli/cli_main.hpp"

#include "v4l2diag/hw/device_discovery.hpp"
#include "v4l2diag/core/diagnostic_runner.hpp"
#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/core/role_bindings.hpp"
#include "v4l2diag/core/run_routing.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/test_registry.hpp"
#include "v4l2diag/core/types.hpp"

#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using namespace v4l2diag;

void print_usage() {
  std::cout << "v4l2-camera-diagnostic\n\n"
            << "Usage:\n"
            << "  v4l2-camera-diagnostic list-devices\n"
            << "  v4l2-camera-diagnostic tests list [--all]\n"
            << "  v4l2-camera-diagnostic profiles list [--config-dir DIR]\n"
            << "  v4l2-camera-diagnostic profiles add --id ID --name NAME --gpio FSYNC:CHIP:LINE:DESC\n"
            << "                                        --bind-role ROLE:CHANNEL_ID [--config-dir DIR]\n"
            << "  v4l2-camera-diagnostic profiles remove --id ID [--config-dir DIR]\n"
            << "  v4l2-camera-diagnostic run [options]\n\n"
            << "Run options:\n"
            << "  --camera PATH              Camera path. May be repeated or comma-separated.\n"
            << "  --trigger-mode MODE        hardware, software, or free-run. Default: free-run.\n"
            << "  --profile ID               Run-level Trigger Profile. Required unless free-run.\n"
            << "  --backend LIST             mmap, dmabuf, userptr. Default: mmap.\n"
            << "  --tests LIST               Test ids, categories, tags, or all. Default: stable.\n"
            << "                             A test named by its exact id always runs, even if long-running or\n"
            << "                             experimental; the flags below only affect group selectors.\n"
            << "  --output-dir DIR           Report output directory. Default: reports.\n"
            << "  --thresholds ID            Verdict threshold config id. Default: default.\n"
            << "  --run-mode MODE            sequential or parallel. Default: sequential.\n\n"
            << "Profile options (profiles add):\n"
            << "  --gpio F:CHIP:LINE:DESC    Hardware trigger channel, id \"gpio-<F>\". May be repeated.\n"
            << "  --bind-role ROLE:CHANNEL   Bind a run role to a channel, e.g. master:gpio-0 or\n"
            << "                             slave-1:gpio-1. May be repeated. Required: no channel is\n"
            << "                             bound to a role automatically.\n";
}

std::string arg_value(int *i, int argc, char **argv) {
  if (*i + 1 >= argc) {
    return {};
  }
  ++(*i);
  return argv[*i];
}

void append_csv(std::vector<std::string> *out, const std::string &value) {
  for (const auto &item : split_csv(value)) {
    out->push_back(item);
  }
}

std::vector<MemoryBackend> parse_backend_list(const std::vector<std::string> &values) {
  std::vector<MemoryBackend> out;
  for (const auto &value : values) {
    MemoryBackend backend;
    if (!parse_memory_backend(value, &backend)) {
      std::cerr << "Unknown memory backend: " << value << "\n";
      std::exit(2);
    }
    out.push_back(backend);
  }
  if (out.empty()) {
    out.push_back(MemoryBackend::Mmap);
  }
  return out;
}

void print_device(const DeviceInfo &device, std::size_t index) {
  std::cout << "[" << index << "] " << device.path;
  if (!device.error.empty()) {
    std::cout << " error=\"" << device.error << "\"\n";
    return;
  }
  std::cout << " driver=\"" << device.driver << "\" card=\"" << device.card << "\" bus=\"" << device.bus_info << "\"\n";
  std::cout << "    capture=" << (device.supports_capture ? "yes" : "no")
            << " streaming=" << (device.supports_streaming ? "yes" : "no") << " formats=" << device.formats.size()
            << "\n";
  for (const auto &format : device.formats) {
    std::cout << "    - " << format.fourcc << " " << format.description << " (" << format.buffer_type << ")\n";
  }
}

std::vector<std::string> choose_cameras_interactively(const std::vector<DeviceInfo> &devices) {
  if (devices.empty()) {
    std::cerr << "No /dev/video* devices were found.\n";
    std::exit(1);
  }

  if (devices.size() == 1) {
    std::cout << "Using the only discovered camera: " << devices.front().path << "\n";
    return {devices.front().path};
  }

  if (!isatty(STDIN_FILENO)) {
    std::cerr << "Multiple cameras were discovered. Choose one or more with --camera PATH.\n";
    for (std::size_t i = 0; i < devices.size(); ++i) {
      print_device(devices[i], i);
    }
    std::exit(2);
  }

  std::cout << "Discovered cameras:\n";
  for (std::size_t i = 0; i < devices.size(); ++i) {
    print_device(devices[i], i);
  }
  std::cout << "Select camera indexes (comma-separated): ";
  std::string input;
  std::getline(std::cin, input);

  std::vector<std::string> selected;
  for (const auto &item : split_csv(input)) {
    const int idx = std::atoi(item.c_str());
    if (idx >= 0 && static_cast<std::size_t>(idx) < devices.size()) {
      selected.push_back(devices[static_cast<std::size_t>(idx)].path);
    }
  }
  if (selected.empty()) {
    std::cerr << "No valid camera was selected.\n";
    std::exit(2);
  }
  return selected;
}

GpioMapping parse_gpio_mapping(const std::string &value) {
  std::vector<std::string> parts;
  std::stringstream ss(value);
  std::string item;
  while (std::getline(ss, item, ':')) {
    parts.push_back(trim(item));
  }
  if (parts.size() < 4) {
    std::cerr << "--gpio must use FSYNC:CHIP:LINE:DESCRIPTION\n";
    std::exit(2);
  }
  GpioMapping mapping;
  mapping.fsync_index = std::atoi(parts[0].c_str());
  mapping.chip_id = std::atoi(parts[1].c_str());
  mapping.line_number = std::atoi(parts[2].c_str());
  mapping.description = parts[3];
  return mapping;
}

// ROLE:CHANNEL_ID, e.g. "master:gpio-0". Repeatable.
RoleBinding parse_role_binding(const std::string &value) {
  const std::size_t colon = value.find(':');
  if (colon == std::string::npos || colon == 0 || colon + 1 == value.size()) {
    std::cerr << "--bind-role must use ROLE:CHANNEL_ID, e.g. master:gpio-0\n";
    std::exit(2);
  }
  RoleBinding binding;
  binding.role = trim(value.substr(0, colon));
  binding.trigger_channel_id = trim(value.substr(colon + 1));
  if (!is_canonical_role(binding.role)) {
    std::cerr << "Not a role: \"" << binding.role << "\". Use master, slave-1, slave-2, ...\n";
    std::exit(2);
  }
  return binding;
}

int command_list_devices() {
  const auto devices = discover_video_devices();
  if (devices.empty()) {
    std::cout << "No /dev/video* devices found.\n";
    return 0;
  }
  for (std::size_t i = 0; i < devices.size(); ++i) {
    print_device(devices[i], i);
  }
  return 0;
}

int command_tests(int argc, char **argv) {
  bool show_all = false;
  for (int i = 2; i < argc; ++i) {
    if (std::string(argv[i]) == "--all") {
      show_all = true;
    }
  }

  const auto tests = built_in_tests();
  for (const auto &test : tests) {
    if (!show_all && std::find(test.tags.begin(), test.tags.end(), "stress") != test.tags.end()) {
      continue;
    }
    std::cout << test.id << " [" << test.category << "]";
    std::cout << " trigger=" << (test.uses_trigger ? "yes" : "no");
    std::cout << " dmabuf=" << (test.requires_dmabuf ? "yes" : "no");
    std::cout << " tags=[";
    for (size_t j = 0; j < test.tags.size(); ++j) {
      std::cout << test.tags[j] << (j + 1 < test.tags.size() ? "," : "");
    }
    std::cout << "]\n";
    std::cout << "  " << test.name << "\n";
    std::cout << "  " << test.description << "\n";
  }
  return 0;
}

int command_profiles(int argc, char **argv) {
  if (argc < 3) {
    print_usage();
    return 2;
  }
  const std::string sub = argv[2];
  std::string config_dir;
  std::string id;
  std::string name;
  std::string description;
  std::vector<GpioMapping> gpio;
  std::vector<RoleBinding> role_bindings;

  for (int i = 3; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--config-dir") {
      config_dir = arg_value(&i, argc, argv);
    } else if (arg == "--id") {
      id = arg_value(&i, argc, argv);
    } else if (arg == "--name") {
      name = arg_value(&i, argc, argv);
    } else if (arg == "--description") {
      description = arg_value(&i, argc, argv);
    } else if (arg == "--gpio") {
      gpio.push_back(parse_gpio_mapping(arg_value(&i, argc, argv)));
    } else if (arg == "--bind-role") {
      role_bindings.push_back(parse_role_binding(arg_value(&i, argc, argv)));
    }
  }

  ProfileRegistry registry(config_dir);

  if (sub == "list") {
    std::cout << "Profile config directory: " << registry.config_directory() << "\n";
    for (const auto &profile : registry.list_profiles()) {
      std::cout << profile.id << " - " << profile.name << "\n";
      std::cout << "  " << profile.description << "\n";
      for (const auto &channel : profile.trigger_channels) {
        std::cout << "  channel=" << channel.id
                  << " type=" << (channel.type == TriggerChannel::Type::Hardware ? "hardware" : "software") << "\n";
      }
      for (const auto &binding : profile.role_bindings) {
        std::cout << "  role=" << binding.role << " -> " << binding.trigger_channel_id << "\n";
      }
    }
    return 0;
  }

  if (sub == "add") {
    if (id.empty() || name.empty() || gpio.empty()) {
      std::cerr << "profiles add requires --id, --name, and at least one --gpio.\n";
      return 2;
    }
    // No automatic master binding, not even with exactly one --gpio. A profile
    // with no routing cannot start a triggered run, so it is refused at save time
    // rather than stored and failed later (plan 2.5.5).
    if (role_bindings.empty()) {
      std::cerr << "profiles add requires at least --bind-role master:CHANNEL_ID.\n";
      std::cerr << "Channel ids are gpio-<FSYNC>, one per --gpio; a single channel is not "
                   "bound to master automatically.\n";
      return 2;
    }
    DeviceProfile profile;
    profile.id = id;
    profile.name = name;
    profile.description = description;
    profile.defaults.trigger_mode = TriggerMode::Hardware;
    for (const auto &mapping : gpio) {
      TriggerChannel channel;
      channel.id = "gpio-" + std::to_string(mapping.fsync_index);
      channel.name = mapping.description;
      channel.type = TriggerChannel::Type::Hardware;
      channel.gpio = mapping;
      profile.trigger_channels.push_back(std::move(channel));
    }
    // Duplicate, non-canonical and unknown-channel bindings are all rejected by
    // validate_device_profile() below, so the CLI does not re-implement the rules.
    profile.role_bindings = role_bindings;
    std::string error;
    if (!registry.add_or_update_profile(profile, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "Profile saved: " << id << "\n";
    return 0;
  }

  if (sub == "remove") {
    if (id.empty()) {
      std::cerr << "profiles remove requires --id.\n";
      return 2;
    }
    std::string error;
    if (!registry.remove_profile(id, &error)) {
      std::cerr << error << "\n";
      return 1;
    }
    std::cout << "Profile removed or disabled: " << id << "\n";
    return 0;
  }

  print_usage();
  return 2;
}

int command_run(int argc, char **argv) {
  RunConfig config;
  std::vector<std::string> camera_paths;
  std::string profile_id;
  bool trigger_mode_set = false;
  std::vector<std::string> backend_values;

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--camera") {
      append_csv(&camera_paths, arg_value(&i, argc, argv));
    } else if (arg == "--trigger-mode") {
      if (!parse_trigger_mode(arg_value(&i, argc, argv), &config.trigger_mode)) {
        std::cerr << "Unknown trigger mode.\n";
        return 2;
      }
      trigger_mode_set = true;
    } else if (arg == "--profile") {
      profile_id = arg_value(&i, argc, argv);
    } else if (arg == "--backend") {
      append_csv(&backend_values, arg_value(&i, argc, argv));
    } else if (arg == "--tests") {
      append_csv(&config.test_selectors, arg_value(&i, argc, argv));
    } else if (arg == "--output-dir") {
      config.output_directory = arg_value(&i, argc, argv);
    } else if (arg == "--config-dir") {
      config.config_directory = arg_value(&i, argc, argv);
    } else if (arg == "--thresholds") {
      config.threshold_config_id = arg_value(&i, argc, argv);
    } else if (arg == "--run-mode") {
      if (!parse_run_mode(arg_value(&i, argc, argv), &config.run_mode)) {
        std::cerr << "Unknown run mode.\n";
        return 2;
      }
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      return 2;
    }
  }

  config.memory_backends = parse_backend_list(backend_values);

  if (camera_paths.empty()) {
    camera_paths = choose_cameras_interactively(discover_video_devices());
  }

  ProfileRegistry profiles(config.config_directory);
  DeviceProfile profile;
  if (!profile_id.empty() && !profiles.get_profile(profile_id, &profile)) {
    std::cerr << "Unknown profile: " << profile_id << "\n";
    return 2;
  }
  if (!trigger_mode_set && !profile_id.empty()) {
    config.trigger_mode = profile.defaults.trigger_mode;
  }
  // First --camera is the master (the full test suite runs against it); any
  // further ones are slaves that only participate in t25-multi-camera.
  config.master.path = camera_paths.front();
  for (size_t i = 1; i < camera_paths.size(); i++) {
    RunConfig::CameraConfig slave;
    slave.path = camera_paths[i];
    config.slaves.push_back(slave);
  }
  // Free-run carries no Trigger Profile; normalised centrally so the CLI cannot
  // produce a run that says "free-run" and names a profile.
  config.trigger_profile_id = effective_trigger_profile_id(config.trigger_mode, profile_id);

  if (config.trigger_mode != TriggerMode::FreeRun) {
    if (profile_id.empty()) {
      std::cerr << "Hardware and software trigger modes require --profile.\n";
      return 2;
    }
    // No automatic channel selection. This used to take the only compatible
    // channel silently, which is the guess plan 2.5.4 forbids: routing a trigger
    // to the wrong camera is worse than making the user state the binding.
    //
    // Deliberately the same seam the web server uses, so both refuse an
    // incomplete routing with identical wording.
    std::vector<std::string> channel_ids;
    for (const auto &channel : profile.trigger_channels) {
      channel_ids.push_back(channel.id);
    }
    const RoleResolution routing = resolve_role_bindings(profile.role_bindings, channel_ids, config.slaves.size());
    if (!routing.ok()) {
      std::cerr << "Cannot start the run: " << describe_role_resolution(routing) << "\n";
      std::cerr << "Bind the missing roles on the profile (see `profiles add --bind-role`).\n";
      return 2;
    }
    // Channel/mode compatibility, through the same helper the web server uses. A
    // hardware channel cannot serve a software run; this check existed only on the
    // web side, so the CLI would have opened the wrong kind of trigger.
    for (const auto &binding : routing.resolved) {
      const auto channel =
          std::find_if(profile.trigger_channels.begin(), profile.trigger_channels.end(),
                       [&](const TriggerChannel &item) { return item.id == binding.trigger_channel_id; });
      if (channel == profile.trigger_channels.end()) {
        std::cerr << "Cannot start the run: trigger channel \"" << binding.trigger_channel_id << "\" (role "
                  << binding.role << ") is not defined by the profile\n";
        return 2;
      }
      const std::string mismatch =
          describe_mode_mismatch(config.trigger_mode, channel->type == TriggerChannel::Type::Hardware, binding.role,
                                 binding.trigger_channel_id);
      if (!mismatch.empty()) {
        std::cerr << "Cannot start the run: " << mismatch << "\n";
        return 2;
      }
    }
  }

  DiagnosticRunner runner(&profiles);
  const RunResult result = runner.run(config);
  const auto artifacts = write_reports(result, config.output_directory);

  std::cout << "Diagnostic run complete.\n";
  for (const auto &camera : result.cameras) {
    std::map<TestStatus, int> counts;
    for (const auto &test : camera.tests) {
      counts[test.status]++;
    }
    std::cout << "Camera " << camera.camera_path << ": pass=" << counts[TestStatus::Pass]
              << " warn=" << counts[TestStatus::Warn] << " fail=" << counts[TestStatus::Fail]
              << " skipped=" << counts[TestStatus::Skipped] << "\n";
  }
  std::cout << "Artifacts:\n";
  for (const auto &artifact : artifacts) {
    std::cout << "  " << to_string(artifact.format) << ": " << artifact.path << "\n";
  }
  return 0;
}

}  // namespace

namespace v4l2diag {

int run_cli(int argc, char **argv) {
  if (argc < 2) {
    print_usage();
    return 0;
  }

  const std::string command = argv[1];
  if (command == "--help" || command == "-h" || command == "help") {
    print_usage();
    return 0;
  }
  if (command == "list-devices") {
    return command_list_devices();
  }
  if (command == "tests" && argc >= 3 && std::string(argv[2]) == "list") {
    return command_tests(argc, argv);
  }
  if (command == "profiles") {
    return command_profiles(argc, argv);
  }
  if (command == "run") {
    return command_run(argc, argv);
  }

  print_usage();
  return 2;
}

}  // namespace v4l2diag

int main(int argc, char **argv) {
  // A report that cannot be written is a failed run, and the user has to be told in
  // words rather than by std::terminate. Without this, a ReportWriteError -- an
  // unwritable output directory, a full disk -- escaped as an uncaught exception and
  // the process aborted with no usable message.
  try {
    return v4l2diag::run_cli(argc, argv);
  } catch (const v4l2diag::ReportWriteError &error) {
    std::cerr << "Report generation failed: " << error.what() << "\n";
    return 1;
  } catch (const std::exception &error) {
    std::cerr << "Diagnostic run failed: " << error.what() << "\n";
    return 1;
  }
}
