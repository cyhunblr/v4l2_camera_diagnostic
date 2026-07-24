#include "v4l2diag/cli/cli_main.hpp"

#include "v4l2diag/hw/device_discovery.hpp"
#include "v4l2diag/core/diagnostic_runner.hpp"
#include "v4l2diag/core/profile_registry.hpp"
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
  std::cout
      << "v4l2-camera-diagnostic\n\n"
      << "Usage:\n"
      << "  v4l2-camera-diagnostic list-devices\n"
      << "  v4l2-camera-diagnostic tests list [--all]\n"
      << "  v4l2-camera-diagnostic profiles list [--config-dir DIR]\n"
      << "  v4l2-camera-diagnostic profiles add --id ID --name NAME --gpio FSYNC:CHIP:LINE:DESC [--config-dir DIR]\n"
      << "  v4l2-camera-diagnostic profiles remove --id ID [--config-dir DIR]\n"
      << "  v4l2-camera-diagnostic run [options]\n\n"
      << "Run options:\n"
      << "  --camera PATH              Camera path. May be repeated or comma-separated.\n"
      << "  --trigger-mode MODE        hardware, software, or free-run. Default: free-run.\n"
      << "  --profile ID               Profile applied to every selected camera.\n"
      << "  --trigger-channel ID       Trigger channel applied to every selected camera.\n"
      << "  --backend LIST             mmap, dmabuf, userptr. Default: mmap.\n"
      << "  --tests LIST               Test ids, categories, all, stable, or implemented. Default: implemented.\n"
      << "  --report LIST              json, markdown, md, html, pdf. Default: json,html.\n"
      << "  --output-dir DIR           Report output directory. Default: reports.\n"
      << "  --thresholds ID            Verdict threshold config id. Default: default.\n"
      << "  --run-mode MODE            sequential or parallel. Default: sequential.\n"
      << "  --include-long             Include long-running tests when selecting all/stable/categories.\n"
      << "  --include-experimental     Include experimental tests when selecting all/categories.\n";
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

std::vector<ReportFormat> parse_report_list(const std::vector<std::string> &values) {
  std::vector<ReportFormat> out;
  for (const auto &value : values) {
    ReportFormat format;
    if (!parse_report_format(value, &format)) {
      std::cerr << "Unknown report format: " << value << "\n";
      std::exit(2);
    }
    out.push_back(format);
  }
  if (out.empty()) {
    out.push_back(ReportFormat::Json);
    out.push_back(ReportFormat::Html);
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
    if (!show_all && test.experimental) {
      continue;
    }
    std::cout << test.id << " [" << test.category << "]";
    std::cout << " implemented=" << (test.implemented_in_core ? "yes" : "no");
    std::cout << " trigger=" << (test.uses_trigger ? "yes" : "no");
    std::cout << " dmabuf=" << (test.requires_dmabuf ? "yes" : "no");
    std::cout << " long=" << (test.long_running ? "yes" : "no");
    std::cout << " risky=" << (test.risky ? "yes" : "no") << "\n";
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
    }
    return 0;
  }

  if (sub == "add") {
    if (id.empty() || name.empty() || gpio.empty()) {
      std::cerr << "profiles add requires --id, --name, and at least one --gpio.\n";
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
  std::string trigger_channel_id;
  bool trigger_mode_set = false;
  std::vector<std::string> backend_values;
  std::vector<std::string> report_values;

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
    } else if (arg == "--trigger-channel") {
      trigger_channel_id = arg_value(&i, argc, argv);
    } else if (arg == "--backend") {
      append_csv(&backend_values, arg_value(&i, argc, argv));
    } else if (arg == "--tests") {
      append_csv(&config.test_selectors, arg_value(&i, argc, argv));
    } else if (arg == "--report") {
      append_csv(&report_values, arg_value(&i, argc, argv));
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
    } else if (arg == "--include-long") {
      config.include_long_tests = true;
    } else if (arg == "--include-experimental") {
      config.include_experimental_tests = true;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      return 2;
    }
  }

  config.memory_backends = parse_backend_list(backend_values);
  config.report_formats = parse_report_list(report_values);

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
  if (config.trigger_mode != TriggerMode::FreeRun) {
    if (profile_id.empty()) {
      std::cerr << "Hardware and software trigger modes require --profile.\n";
      return 2;
    }
    if (trigger_channel_id.empty()) {
      std::vector<std::string> compatible;
      for (const auto &channel : profile.trigger_channels) {
        const bool matches_mode =
            (config.trigger_mode == TriggerMode::Hardware && channel.type == TriggerChannel::Type::Hardware) ||
            (config.trigger_mode == TriggerMode::Software && channel.type == TriggerChannel::Type::Software);
        if (matches_mode) {
          compatible.push_back(channel.id);
        }
      }
      if (compatible.size() != 1) {
        std::cerr << "Choose a compatible channel with --trigger-channel.\n";
        return 2;
      }
      trigger_channel_id = compatible.front();
    }
  }
  for (const auto &path : camera_paths) {
    config.cameras.push_back({path, profile_id, trigger_channel_id});
  }

  DiagnosticRunner runner(&profiles);
  const RunResult result = runner.run(config);
  const auto artifacts = write_reports(result, config.report_formats, config.output_directory);

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
  return v4l2diag::run_cli(argc, argv);
}
