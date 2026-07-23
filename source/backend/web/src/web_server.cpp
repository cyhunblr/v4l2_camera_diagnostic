#include "v4l2diag/web/web_server.hpp"

#include "v4l2diag/hw/device_discovery.hpp"
#include "v4l2diag/hw/gpio_trigger.hpp"
#include "v4l2diag/hw/v4l2_capture.hpp"
#include "v4l2diag/hw/v4l2_controls.hpp"
#include "v4l2diag/core/diagnostic_runner.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/test_registry.hpp"

#include <json/json.h>
#include <microhttpd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <arpa/inet.h>
#include <map>
#include <set>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <ctime>
#include <unistd.h>
#include <utility>
#include <vector>

namespace v4l2diag {

namespace {

struct RequestBuffer {
  std::string body;
};

std::string json_to_string(const Json::Value &value) {
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  return Json::writeString(builder, value);
}

Json::Value parse_json_body(const std::string &body) {
  if (body.empty()) {
    return Json::Value(Json::objectValue);
  }
  Json::CharReaderBuilder builder;
  std::string errors;
  Json::Value root;
  std::istringstream in(body);
  if (!Json::parseFromStream(builder, in, &root, &errors)) {
    Json::Value error(Json::objectValue);
    error["parse_error"] = errors;
    return error;
  }
  return root;
}

std::string query_value(const std::string &query, const std::string &key) {
  std::stringstream ss(query);
  std::string item;
  while (std::getline(ss, item, '&')) {
    const std::size_t eq = item.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    if (item.substr(0, eq) == key) {
      return item.substr(eq + 1);
    }
  }
  return {};
}

bool file_exists(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool directory_exists(const std::string &path) {
  struct stat st {};
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool ensure_directory(const std::string &path) {
  if (path.empty()) {
    return false;
  }
  std::string partial;
  for (char c : path) {
    partial.push_back(c);
    if (c == '/' && partial.size() > 1) {
      mkdir(partial.c_str(), 0755);
    }
  }
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

std::string read_file(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::string extension(const std::string &path) {
  const std::size_t pos = path.find_last_of('.');
  if (pos == std::string::npos) {
    return {};
  }
  return path.substr(pos + 1);
}

std::string content_type_for(const std::string &path) {
  const std::string ext = extension(path);
  if (ext == "html")
    return "text/html; charset=utf-8";
  if (ext == "css")
    return "text/css; charset=utf-8";
  if (ext == "js")
    return "application/javascript; charset=utf-8";
  if (ext == "json")
    return "application/json; charset=utf-8";
  if (ext == "svg")
    return "image/svg+xml";
  if (ext == "pdf")
    return "application/pdf";
  if (ext == "md")
    return "text/markdown; charset=utf-8";
  return "application/octet-stream";
}

bool parse_ipv4_bind_address(const std::string &address, in_addr *out) {
  const std::string normalized = address == "localhost" ? "127.0.0.1" : address;
  return inet_pton(AF_INET, normalized.c_str(), out) == 1;
}

bool safe_relative_path(const std::string &path) {
  return path.find("..") == std::string::npos && path.find('\\') == std::string::npos;
}

Json::Value backend_to_json(MemoryBackend backend) {
  Json::Value value;
  value = to_string(backend);
  return value;
}

Json::Value device_to_json(const DeviceInfo &device) {
  Json::Value out(Json::objectValue);
  out["path"] = device.path;
  out["driver"] = device.driver;
  out["card"] = device.card;
  out["bus_info"] = device.bus_info;
  out["readable"] = device.readable;
  out["supports_capture"] = device.supports_capture;
  out["supports_streaming"] = device.supports_streaming;
  out["error"] = device.error;
  out["formats"] = Json::Value(Json::arrayValue);
  for (const auto &format : device.formats) {
    Json::Value item(Json::objectValue);
    item["fourcc"] = format.fourcc;
    item["description"] = format.description;
    item["buffer_type"] = format.buffer_type;
    out["formats"].append(item);
  }
  return out;
}

Json::Value profile_to_json(const DeviceProfile &profile) {
  Json::Value out(Json::objectValue);
  out["schema_version"] = profile.schema_version;
  out["id"] = profile.id;
  out["name"] = profile.name;
  out["description"] = profile.description;
  out["enabled"] = profile.enabled;
  out["camera_match"]["driver"] = profile.camera_match.driver;
  out["camera_match"]["card"] = profile.camera_match.card;
  out["camera_match"]["bus_info"] = profile.camera_match.bus_info;
  out["defaults"]["trigger_mode"] = to_string(profile.defaults.trigger_mode);
  out["defaults"]["memory_backends"] = Json::Value(Json::arrayValue);
  out["defaults"]["test_selectors"] = Json::Value(Json::arrayValue);
  out["defaults"]["report_formats"] = Json::Value(Json::arrayValue);
  out["trigger_channels"] = Json::Value(Json::arrayValue);
  out["camera_bindings"] = Json::Value(Json::arrayValue);
  for (MemoryBackend backend : profile.defaults.memory_backends) {
    out["defaults"]["memory_backends"].append(to_string(backend));
  }
  for (const auto &selector : profile.defaults.test_selectors) {
    out["defaults"]["test_selectors"].append(selector);
  }
  for (ReportFormat format : profile.defaults.report_formats) {
    out["defaults"]["report_formats"].append(to_string(format));
  }

  for (const auto &channel : profile.trigger_channels) {
    Json::Value item(Json::objectValue);
    item["id"] = channel.id;
    item["name"] = channel.name;
    item["description"] = channel.description;
    item["type"] = channel.type == TriggerChannel::Type::Hardware ? "hardware" : "software";
    if (channel.type == TriggerChannel::Type::Hardware) {
      item["gpio"]["chip_id"] = channel.gpio.chip_id;
      item["gpio"]["line_number"] = channel.gpio.line_number;
      item["gpio"]["description"] = channel.gpio.description;
    } else {
      const auto kind = channel.control_device.kind;
      item["control_device"]["kind"] = kind == ControlDeviceSelector::Kind::CaptureDevice ? "capture"
                                       : kind == ControlDeviceSelector::Kind::SubDevice   ? "subdevice"
                                                                                          : "video";
      item["control_device"]["driver"] = channel.control_device.driver;
      item["control_device"]["card"] = channel.control_device.card;
      item["control_device"]["bus_info"] = channel.control_device.bus_info;
      item["control_device"]["sysfs_name"] = channel.control_device.sysfs_name;
      const auto append_writes = [&](const char *key, const std::vector<V4l2ControlWrite> &writes) {
        item[key] = Json::Value(Json::arrayValue);
        for (const auto &write : writes) {
          Json::Value control(Json::objectValue);
          control["id"] = Json::UInt(write.id);
          control["name"] = write.name;
          control["type"] = Json::UInt(write.type);
          control["value"] = Json::Int64(write.value);
          item[key].append(control);
        }
      };
      append_writes("setup", channel.setup_controls);
      append_writes("fire", channel.fire_controls);
      append_writes("teardown", channel.teardown_controls);
    }
    out["trigger_channels"].append(item);
  }
  for (const auto &binding : profile.camera_bindings) {
    Json::Value item(Json::objectValue);
    item["camera"]["driver"] = binding.camera.driver;
    item["camera"]["card"] = binding.camera.card;
    item["camera"]["bus_info"] = binding.camera.bus_info;
    item["trigger_channel_id"] = binding.trigger_channel_id;
    out["camera_bindings"].append(item);
  }
  return out;
}

CameraMatcher matcher_from_json(const Json::Value &root) {
  CameraMatcher matcher;
  matcher.driver = root.get("driver", "").asString();
  matcher.card = root.get("card", "").asString();
  matcher.bus_info = root.get("bus_info", "").asString();
  return matcher;
}

V4l2ControlWrite control_write_from_json(const Json::Value &root) {
  V4l2ControlWrite write;
  write.id = root.get("id", 0).asUInt();
  write.name = root.get("name", "").asString();
  write.type = root.get("type", 0).asUInt();
  write.value = root.get("value", 0).asInt64();
  return write;
}

DeviceProfile profile_from_json(const Json::Value &root) {
  DeviceProfile profile;
  profile.schema_version = 2;
  profile.id = root.get("id", "").asString();
  profile.name = root.get("name", "").asString();
  profile.description = root.get("description", "").asString();
  profile.enabled = root.get("enabled", true).asBool();
  profile.camera_match = matcher_from_json(root["camera_match"]);
  parse_trigger_mode(root["defaults"].get("trigger_mode", "free-run").asString(), &profile.defaults.trigger_mode);
  for (const auto &value : root["defaults"]["memory_backends"]) {
    MemoryBackend backend;
    if (parse_memory_backend(value.asString(), &backend)) {
      profile.defaults.memory_backends.push_back(backend);
    }
  }
  for (const auto &value : root["defaults"]["test_selectors"]) {
    profile.defaults.test_selectors.push_back(value.asString());
  }
  for (const auto &value : root["defaults"]["report_formats"]) {
    ReportFormat format;
    if (parse_report_format(value.asString(), &format)) {
      profile.defaults.report_formats.push_back(format);
    }
  }
  for (const auto &value : root["trigger_channels"]) {
    TriggerChannel channel;
    channel.id = value.get("id", "").asString();
    channel.name = value.get("name", "").asString();
    channel.description = value.get("description", "").asString();
    channel.type = value.get("type", "hardware").asString() == "software" ? TriggerChannel::Type::Software
                                                                          : TriggerChannel::Type::Hardware;
    if (channel.type == TriggerChannel::Type::Hardware) {
      channel.gpio.chip_id = value["gpio"].get("chip_id", 0).asInt();
      channel.gpio.line_number = value["gpio"].get("line_number", 0).asInt();
      channel.gpio.description = value["gpio"].get("description", "").asString();
    } else {
      const std::string kind = value["control_device"].get("kind", "capture").asString();
      channel.control_device.kind = kind == "subdevice" ? ControlDeviceSelector::Kind::SubDevice
                                    : kind == "video"   ? ControlDeviceSelector::Kind::VideoDevice
                                                        : ControlDeviceSelector::Kind::CaptureDevice;
      channel.control_device.driver = value["control_device"].get("driver", "").asString();
      channel.control_device.card = value["control_device"].get("card", "").asString();
      channel.control_device.bus_info = value["control_device"].get("bus_info", "").asString();
      channel.control_device.sysfs_name = value["control_device"].get("sysfs_name", "").asString();
      for (const auto &write : value["setup"])
        channel.setup_controls.push_back(control_write_from_json(write));
      for (const auto &write : value["fire"])
        channel.fire_controls.push_back(control_write_from_json(write));
      for (const auto &write : value["teardown"])
        channel.teardown_controls.push_back(control_write_from_json(write));
    }
    profile.trigger_channels.push_back(std::move(channel));
  }
  for (const auto &value : root["camera_bindings"]) {
    profile.camera_bindings.push_back(
        {matcher_from_json(value["camera"]), value.get("trigger_channel_id", "").asString()});
  }
  return profile;
}

Json::Value control_device_to_json(const ControlDeviceInfo &device) {
  Json::Value out(Json::objectValue);
  out["path"] = device.path;
  out["kind"] = device.kind;
  out["driver"] = device.driver;
  out["card"] = device.card;
  out["bus_info"] = device.bus_info;
  out["sysfs_name"] = device.sysfs_name;
  out["error"] = device.error;
  out["controls"] = Json::Value(Json::arrayValue);
  for (const auto &control : device.controls) {
    Json::Value item(Json::objectValue);
    item["id"] = Json::UInt(control.id);
    item["type"] = Json::UInt(control.type);
    item["name"] = control.name;
    item["minimum"] = Json::Int64(control.minimum);
    item["maximum"] = Json::Int64(control.maximum);
    item["step"] = Json::UInt64(control.step);
    item["default_value"] = Json::Int64(control.default_value);
    item["current_value"] = Json::Int64(control.current_value);
    item["flags"] = Json::UInt(control.flags);
    item["readable"] = control.readable;
    item["writable"] = control.writable;
    item["supported_for_trigger"] = control.supported_for_trigger;
    item["menu_items"] = Json::Value(Json::arrayValue);
    for (const auto &menu : control.menu_items) {
      Json::Value menu_item(Json::objectValue);
      menu_item["value"] = Json::Int64(menu.value);
      menu_item["name"] = menu.name;
      item["menu_items"].append(menu_item);
    }
    out["controls"].append(item);
  }
  return out;
}

Json::Value test_to_json(const TestDefinition &test) {
  Json::Value out(Json::objectValue);
  out["id"] = test.id;
  out["name"] = test.name;
  out["category"] = test.category;
  out["description"] = test.description;
  out["uses_trigger"] = test.uses_trigger;
  for (TriggerMode mode : {TriggerMode::Hardware, TriggerMode::Software, TriggerMode::FreeRun}) {
    if (supports_trigger_mode(test, mode)) {
      out["supported_trigger_modes"].append(to_string(mode));
    }
  }
  out["requires_dmabuf"] = test.requires_dmabuf;
  out["long_running"] = test.long_running;
  out["experimental"] = test.experimental;
  out["risky"] = test.risky;
  out["implemented_in_core"] = test.implemented_in_core;
  return out;
}

Json::Value metric_to_json(const MetricValue &metric) {
  Json::Value out(Json::objectValue);
  out["name"] = metric.name;
  out["unit"] = metric.unit;
  out["value"] = metric.value;
  out["description"] = metric.description;
  return out;
}

Json::Value result_to_json(const TestResult &test) {
  Json::Value out(Json::objectValue);
  out["id"] = test.id;
  out["name"] = test.name;
  out["category"] = test.category;
  out["memory_backend"] = test.memory_backend;
  out["status"] = to_string(test.status);
  out["summary"] = test.summary;
  out["duration_ms"] = test.duration_ms;
  for (const auto &metric : test.metrics) {
    out["metrics"].append(metric_to_json(metric));
  }
  for (const auto &detail : test.details) {
    out["details"].append(detail);
  }
  for (const auto &warning : test.warnings) {
    out["warnings"].append(warning);
  }
  return out;
}

Json::Value camera_result_to_json(const CameraRunResult &camera) {
  Json::Value out(Json::objectValue);
  out["camera_path"] = camera.camera_path;
  out["profile_id"] = camera.profile_id;
  out["trigger_channel_id"] = camera.trigger_channel_id;
  out["trigger_description"] = camera.trigger_description;
  out["trigger_mode"] = to_string(camera.trigger_mode);
  for (auto backend : camera.memory_backends) {
    out["memory_backends"].append(backend_to_json(backend));
  }
  for (const auto &test : camera.tests) {
    out["tests"].append(result_to_json(test));
  }
  return out;
}

Json::Value run_result_to_json(const RunResult &result) {
  Json::Value out(Json::objectValue);
  out["project_name"] = result.project_name;
  out["started_at_utc"] = result.started_at_utc;
  out["finished_at_utc"] = result.finished_at_utc;
  out["host_name"] = result.host_name;
  out["output_directory"] = result.output_directory;
  out["run_mode"] = to_string(result.run_mode);
  for (const auto &camera : result.cameras) {
    out["cameras"].append(camera_result_to_json(camera));
  }
  return out;
}

// Builds a compact historical-run summary (id, status, counts, report links) for the
// persistent runs index — deliberately excludes the full nested per-test detail that
// run_result_to_json includes, since the index is meant to stay small across many runs.
Json::Value run_summary_to_json(const std::string &id, const std::string &status, const RunConfig &config,
                                const RunResult &result, const std::vector<ReportArtifact> &artifacts,
                                long long duration_ms) {
  Json::Value out(Json::objectValue);
  out["id"] = id;
  out["status"] = status;
  out["trigger_mode"] = to_string(config.trigger_mode);
  for (const auto &camera : config.cameras) {
    out["camera_paths"].append(camera.path);
    Json::Value assignment(Json::objectValue);
    assignment["path"] = camera.path;
    assignment["profile_id"] = camera.profile_id;
    assignment["trigger_channel_id"] = camera.trigger_channel_id;
    out["cameras"].append(assignment);
  }
  out["started_at_utc"] = result.started_at_utc;
  out["finished_at_utc"] = result.finished_at_utc;
  out["duration_ms"] = static_cast<Json::Int64>(duration_ms);

  int pass_count = 0, fail_count = 0, warn_count = 0, skip_count = 0;
  for (const auto &camera : result.cameras) {
    for (const auto &test : camera.tests) {
      switch (test.status) {
        case TestStatus::Pass:
          pass_count++;
          break;
        case TestStatus::Fail:
        case TestStatus::Error:
          fail_count++;
          break;
        case TestStatus::Warn:
          warn_count++;
          break;
        case TestStatus::Skipped:
          skip_count++;
          break;
      }
    }
  }
  out["pass_count"] = pass_count;
  out["fail_count"] = fail_count;
  out["warn_count"] = warn_count;
  out["skip_count"] = skip_count;

  for (const auto &artifact : artifacts) {
    Json::Value item(Json::objectValue);
    item["format"] = to_string(artifact.format);
    const std::string filename = artifact.path.substr(artifact.path.find_last_of('/') + 1);
    item["url"] = "/reports/" + id + "/" + filename;
    out["reports"].append(item);
  }
  return out;
}

RunConfig run_config_from_json(const Json::Value &root, const WebServerOptions &options, const std::string &run_id) {
  RunConfig config;
  config.output_directory = options.report_root + "/web-run-" + run_id;
  config.config_directory = options.config_directory;

  if (root.isMember("trigger_mode")) {
    parse_trigger_mode(root["trigger_mode"].asString(), &config.trigger_mode);
  }
  if (root.isMember("run_mode")) {
    RunMode mode;
    if (parse_run_mode(root["run_mode"].asString(), &mode)) {
      config.run_mode = mode;
    }
  }
  config.include_long_tests = root.get("include_long_tests", false).asBool();
  config.include_experimental_tests = root.get("include_experimental_tests", false).asBool();
  config.threshold_config_id = root.get("threshold_config_id", "default").asString();

  for (const auto &item : root["cameras"]) {
    RunConfig::CameraConfig camera;
    camera.path = item.get("path", "").asString();
    camera.profile_id = item.get("profile_id", "").asString();
    camera.trigger_channel_id = item.get("trigger_channel_id", "").asString();
    config.cameras.push_back(std::move(camera));
  }

  for (const auto &item : root["memory_backends"]) {
    MemoryBackend backend;
    if (parse_memory_backend(item.asString(), &backend)) {
      config.memory_backends.push_back(backend);
    }
  }
  if (config.memory_backends.empty()) {
    config.memory_backends.push_back(MemoryBackend::Mmap);
  }

  for (const auto &item : root["test_selectors"]) {
    config.test_selectors.push_back(item.asString());
  }
  if (config.test_selectors.empty()) {
    config.test_selectors.push_back("implemented");
  }

  for (const auto &item : root["report_formats"]) {
    ReportFormat format;
    if (parse_report_format(item.asString(), &format)) {
      config.report_formats.push_back(format);
    }
  }
  if (config.report_formats.empty()) {
    config.report_formats.push_back(ReportFormat::Json);
    config.report_formats.push_back(ReportFormat::Html);
  }

  return config;
}

bool validate_run_config(const RunConfig &config, const WebServerOptions &options, std::string *error) {
  if (config.cameras.empty()) {
    *error = "at least one camera assignment is required";
    return false;
  }
  std::set<std::string> camera_paths;
  ProfileRegistry profiles(options.config_directory);
  for (const auto &camera : config.cameras) {
    if (camera.path.empty() || !camera_paths.insert(camera.path).second) {
      *error = camera.path.empty() ? "camera path is required" : "camera paths must be unique";
      return false;
    }
    if (config.trigger_mode == TriggerMode::FreeRun) {
      continue;
    }
    DeviceProfile profile;
    if (camera.profile_id.empty() || !profiles.get_profile(camera.profile_id, &profile)) {
      *error = "active trigger modes require a valid profile for every camera";
      return false;
    }
    const auto channel = std::find_if(profile.trigger_channels.begin(), profile.trigger_channels.end(),
                                      [&](const TriggerChannel &item) { return item.id == camera.trigger_channel_id; });
    if (channel == profile.trigger_channels.end()) {
      *error = "active trigger modes require a valid trigger channel for every camera";
      return false;
    }
    const bool compatible =
        (config.trigger_mode == TriggerMode::Hardware && channel->type == TriggerChannel::Type::Hardware) ||
        (config.trigger_mode == TriggerMode::Software && channel->type == TriggerChannel::Type::Software);
    if (!compatible) {
      *error = "trigger channel type does not match the selected trigger mode";
      return false;
    }
  }
  return true;
}

std::string executable_dir() {
  char buffer[4096];
  const ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len <= 0) {
    return ".";
  }
  buffer[len] = '\0';
  std::string path(buffer);
  const std::size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return ".";
  }
  return path.substr(0, slash);
}

}  // namespace

struct WebServer::RunState {
  std::string id;
  std::string status = "queued";
  RunConfig config;
  RunResult result;
  std::vector<ReportArtifact> artifacts;
  std::vector<RunLogLine> logs;
  std::size_t next_offset = 0;
  std::thread worker;
  std::atomic<bool> stop_requested{false};
  mutable std::mutex mutex;
};

WebServer::WebServer(WebServerOptions options) : options_(std::move(options)) {}

WebServer::~WebServer() {
  stop();
  std::vector<std::shared_ptr<RunState>> runs;
  {
    std::lock_guard<std::mutex> lock(runs_mutex_);
    runs = runs_;
  }
  for (auto &run : runs) {
    if (run->worker.joinable()) {
      run->worker.join();
    }
  }
}

bool WebServer::start(std::string *error) {
  if (running_) {
    return true;
  }

  if (options_.web_root.empty()) {
    options_.web_root = default_web_root();
  }
  if (options_.report_root.empty()) {
    options_.report_root = default_report_root();
  }

  load_run_history();

  for (unsigned short port = options_.port; port <= options_.max_port; ++port) {
    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_port = htons(port);
    if (!parse_ipv4_bind_address(options_.bind_address, &bind_addr.sin_addr)) {
      if (error) {
        *error = "invalid IPv4 bind address: " + options_.bind_address;
      }
      return false;
    }

    daemon_ =
        MHD_start_daemon(MHD_USE_INTERNAL_POLLING_THREAD | MHD_USE_ERROR_LOG, port, nullptr, nullptr,
                         &WebServer::handle_request_static, this, MHD_OPTION_SOCK_ADDR, &bind_addr, MHD_OPTION_END);
    if (daemon_) {
      active_port_ = port;
      running_ = true;
      return true;
    }
  }

  if (error) {
    *error = "no available port in requested range";
  }
  return false;
}

void WebServer::stop() {
  if (daemon_) {
    MHD_stop_daemon(daemon_);
    daemon_ = nullptr;
  }
  running_ = false;
}

bool WebServer::running() const {
  return running_;
}

std::string WebServer::url() const {
  return "http://" + options_.bind_address + ":" + std::to_string(active_port_);
}

std::string WebServer::handle_request(const std::string &method, const std::string &path, const std::string &query,
                                      const std::string &body, int *status_code, std::string *content_type) {
  if (path.rfind("/api/", 0) == 0) {
    return handle_api(method, path, query, body, status_code, content_type);
  }
  if (path.rfind("/reports/", 0) == 0) {
    return handle_report_file(path, status_code, content_type);
  }
  return handle_static(path, status_code, content_type);
}

std::string WebServer::handle_api(const std::string &method, const std::string &path, const std::string &query,
                                  const std::string &body, int *status_code, std::string *content_type) {
  *content_type = "application/json; charset=utf-8";
  *status_code = MHD_HTTP_OK;

  if (method == "GET" && path == "/api/health") {
    Json::Value out(Json::objectValue);
    out["status"] = "ok";
    out["version"] = "0.1.0";
    out["url"] = url();
    out["web_root"] = options_.web_root;
    return json_to_string(out);
  }

  if (method == "GET" && path == "/api/devices") {
    Json::Value out(Json::objectValue);
    out["devices"] = Json::Value(Json::arrayValue);
    for (const auto &device : discover_video_devices()) {
      out["devices"].append(device_to_json(device));
    }
    return json_to_string(out);
  }

  if (method == "GET" && path == "/api/profiles") {
    ProfileRegistry registry(options_.config_directory);
    Json::Value out(Json::objectValue);
    out["profiles"] = Json::Value(Json::arrayValue);
    for (const auto &profile : registry.list_profiles()) {
      out["profiles"].append(profile_to_json(profile));
    }
    return json_to_string(out);
  }

  if (method == "GET" && path == "/api/control-devices") {
    Json::Value out(Json::objectValue);
    out["devices"] = Json::Value(Json::arrayValue);
    for (const auto &device : discover_control_devices()) {
      out["devices"].append(control_device_to_json(device));
    }
    return json_to_string(out);
  }

  if (method == "GET" && path.rfind("/api/profiles/", 0) == 0 && path.find("/export") != std::string::npos) {
    const std::string tail = path.substr(std::string("/api/profiles/").size());
    const std::string id = tail.substr(0, tail.find('/'));
    ProfileRegistry registry(options_.config_directory);
    DeviceProfile profile;
    if (!registry.get_profile(id, &profile)) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "profile not found";
      return json_to_string(out);
    }
    *content_type = "application/json; charset=utf-8";
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, profile_to_json(profile));
  }

  if (method == "POST" && path == "/api/profiles/import") {
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error")) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = body_json["parse_error"];
      return json_to_string(out);
    }
    const DeviceProfile profile = profile_from_json(body_json);
    std::string error;
    if (!validate_device_profile(profile, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    ProfileRegistry registry(options_.config_directory);
    if (!registry.add_or_update_profile(profile, &error)) {
      *status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    *status_code = MHD_HTTP_CREATED;
    return json_to_string(profile_to_json(profile));
  }

  if (method == "POST" && (path == "/api/profiles" || path == "/api/profiles/validate")) {
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error")) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = body_json["parse_error"];
      return json_to_string(out);
    }
    const DeviceProfile profile = profile_from_json(body_json);
    std::string error;
    if (!validate_device_profile(profile, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    if (path == "/api/profiles/validate") {
      Json::Value out(Json::objectValue);
      out["valid"] = true;
      return json_to_string(out);
    }
    ProfileRegistry registry(options_.config_directory);
    DeviceProfile existing;
    if (registry.get_profile(profile.id, &existing)) {
      *status_code = MHD_HTTP_CONFLICT;
      Json::Value out(Json::objectValue);
      out["error"] = "profile already exists";
      return json_to_string(out);
    }
    if (!registry.add_or_update_profile(profile, &error)) {
      *status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    *status_code = MHD_HTTP_CREATED;
    return json_to_string(profile_to_json(profile));
  }

  if ((method == "PUT" || method == "DELETE") && path.rfind("/api/profiles/", 0) == 0) {
    const std::string id = path.substr(std::string("/api/profiles/").size());
    ProfileRegistry registry(options_.config_directory);
    std::string error;
    if (method == "DELETE") {
      if (!registry.remove_profile(id, &error)) {
        *status_code = MHD_HTTP_BAD_REQUEST;
        Json::Value out(Json::objectValue);
        out["error"] = error;
        return json_to_string(out);
      }
      *status_code = MHD_HTTP_NO_CONTENT;
      return "";
    }
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error")) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = body_json["parse_error"];
      return json_to_string(out);
    }
    DeviceProfile profile = profile_from_json(body_json);
    if (profile.id.empty()) {
      profile.id = id;
    }
    if (profile.id != id || !validate_device_profile(profile, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = profile.id != id ? "profile id cannot be changed" : error;
      return json_to_string(out);
    }
    DeviceProfile existing;
    if (!registry.get_profile(id, &existing)) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "profile not found";
      return json_to_string(out);
    }
    if (!registry.add_or_update_profile(profile, &error)) {
      *status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    return json_to_string(profile_to_json(profile));
  }

  // --- Threshold configuration endpoints ---

  if (method == "GET" && path == "/api/thresholds") {
    ThresholdRegistry registry(default_threshold_directory());
    Json::Value out(Json::objectValue);
    out["configs"] = Json::Value(Json::arrayValue);
    for (const auto &config : registry.list_configs()) {
      Json::Value item(Json::objectValue);
      item["id"] = config.id;
      item["name"] = config.name;
      item["description"] = config.description;
      Json::Value values(Json::objectValue);
      for (const auto &test : config.values) {
        Json::Value keys(Json::objectValue);
        for (const auto &kv : test.second) {
          keys[kv.first] = kv.second;
        }
        values[test.first] = keys;
      }
      item["values"] = values;
      out["configs"].append(item);
    }
    return json_to_string(out);
  }

  if (method == "GET" && path.rfind("/api/thresholds/", 0) == 0 && path.find("/export") != std::string::npos) {
    const std::string tail = path.substr(std::string("/api/thresholds/").size());
    const std::string id = tail.substr(0, tail.find('/'));
    ThresholdRegistry registry(default_threshold_directory());
    std::string json_text;
    if (!registry.export_config(id, &json_text)) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "threshold config not found";
      return json_to_string(out);
    }
    *content_type = "application/json; charset=utf-8";
    return json_text;
  }

  if (method == "GET" && path.rfind("/api/thresholds/", 0) == 0) {
    const std::string id = path.substr(std::string("/api/thresholds/").size());
    ThresholdRegistry registry(default_threshold_directory());
    ThresholdConfig config;
    if (!registry.get_config(id, &config)) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "threshold config not found";
      return json_to_string(out);
    }
    Json::Value out(Json::objectValue);
    out["id"] = config.id;
    out["name"] = config.name;
    out["description"] = config.description;
    Json::Value values(Json::objectValue);
    for (const auto &test : config.values) {
      Json::Value keys(Json::objectValue);
      for (const auto &kv : test.second) {
        keys[kv.first] = kv.second;
      }
      values[test.first] = keys;
    }
    out["values"] = values;
    return json_to_string(out);
  }

  if (method == "POST" && path == "/api/thresholds/import") {
    ThresholdRegistry registry(default_threshold_directory());
    std::string error;
    if (!registry.import_config(body, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    *status_code = MHD_HTTP_CREATED;
    Json::Value out(Json::objectValue);
    out["ok"] = true;
    return json_to_string(out);
  }

  if (method == "POST" && path == "/api/thresholds") {
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error")) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = body_json["parse_error"];
      return json_to_string(out);
    }
    ThresholdConfig config;
    config.id = body_json.get("id", "").asString();
    config.name = body_json.get("name", "").asString();
    config.description = body_json.get("description", "").asString();
    const Json::Value &vals = body_json["values"];
    if (vals.isObject()) {
      for (const auto &test_id : vals.getMemberNames()) {
        const Json::Value &keys = vals[test_id];
        if (!keys.isObject())
          continue;
        for (const auto &key : keys.getMemberNames()) {
          if (keys[key].isNumeric()) {
            config.values[test_id][key] = keys[key].asDouble();
          }
        }
      }
    }
    ThresholdRegistry registry(default_threshold_directory());
    std::string error;
    if (!registry.add_or_update_config(config, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    *status_code = MHD_HTTP_CREATED;
    Json::Value out(Json::objectValue);
    out["id"] = config.id;
    out["name"] = config.name;
    return json_to_string(out);
  }

  if (method == "PUT" && path.rfind("/api/thresholds/", 0) == 0) {
    const std::string id = path.substr(std::string("/api/thresholds/").size());
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error")) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = body_json["parse_error"];
      return json_to_string(out);
    }
    ThresholdConfig config;
    config.id = id;
    config.name = body_json.get("name", id).asString();
    config.description = body_json.get("description", "").asString();
    const Json::Value &vals = body_json["values"];
    if (vals.isObject()) {
      for (const auto &test_id : vals.getMemberNames()) {
        const Json::Value &keys = vals[test_id];
        if (!keys.isObject())
          continue;
        for (const auto &key : keys.getMemberNames()) {
          if (keys[key].isNumeric()) {
            config.values[test_id][key] = keys[key].asDouble();
          }
        }
      }
    }
    ThresholdRegistry registry(default_threshold_directory());
    std::string error;
    if (!registry.add_or_update_config(config, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    Json::Value out(Json::objectValue);
    out["id"] = config.id;
    out["name"] = config.name;
    return json_to_string(out);
  }

  if (method == "DELETE" && path.rfind("/api/thresholds/", 0) == 0) {
    const std::string id = path.substr(std::string("/api/thresholds/").size());
    ThresholdRegistry registry(default_threshold_directory());
    std::string error;
    if (!registry.remove_config(id, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    *status_code = MHD_HTTP_NO_CONTENT;
    return "";
  }

  if (method == "POST" && path == "/api/triggers/test") {
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error") || !body_json.get("confirmed", false).asBool()) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = "an explicit confirmation is required";
      return json_to_string(out);
    }
    const std::string camera_path = body_json.get("camera_path", "").asString();
    const std::string profile_id = body_json.get("profile_id", "").asString();
    const std::string channel_id = body_json.get("trigger_channel_id", "").asString();
    ProfileRegistry registry(options_.config_directory);
    DeviceProfile profile;
    if (!registry.get_profile(profile_id, &profile)) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "profile not found";
      return json_to_string(out);
    }
    const auto channel = std::find_if(profile.trigger_channels.begin(), profile.trigger_channels.end(),
                                      [&](const TriggerChannel &item) { return item.id == channel_id; });
    if (channel == profile.trigger_channels.end() || channel->type != TriggerChannel::Type::Software) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = "a software trigger channel is required";
      return json_to_string(out);
    }
    V4l2ControlTrigger trigger;
    std::string error;
    if (!trigger.open(camera_path, *channel, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    V4lSession session;
    if (!session.open(camera_path, &error) || !session.start(4, MemoryBackend::Mmap, &error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    const CaptureFrame frame = session.capture(trigger, 1000);
    if (!frame.success) {
      *status_code = MHD_HTTP_REQUEST_TIMEOUT;
      Json::Value out(Json::objectValue);
      out["error"] =
          trigger.last_error().empty() ? "no frame arrived after the software trigger" : trigger.last_error();
      return json_to_string(out);
    }
    Json::Value out(Json::objectValue);
    out["status"] = "frame-received";
    out["sequence"] = Json::UInt(frame.sequence);
    out["latency_ms"] = frame.latency_ms;
    return json_to_string(out);
  }

  if (method == "GET" && path == "/api/tests") {
    Json::Value out(Json::objectValue);
    for (const auto &test : built_in_tests()) {
      out["tests"].append(test_to_json(test));
    }
    return json_to_string(out);
  }

  if (method == "POST" && path == "/api/runs") {
    const std::string run_id = std::to_string(std::time(nullptr)) + "-" + std::to_string(std::rand() % 100000);
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error")) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = body_json["parse_error"];
      return json_to_string(out);
    }
    RunConfig config = run_config_from_json(body_json, options_, run_id);
    std::string validation_error;
    if (!validate_run_config(config, options_, &validation_error)) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = validation_error;
      return json_to_string(out);
    }
    auto run = create_run(config);
    Json::Value out(Json::objectValue);
    out["id"] = run->id;
    out["status"] = run->status;
    out["logs_url"] = "/api/runs/" + run->id + "/logs";
    return json_to_string(out);
  }

  if (method == "GET" && path == "/api/runs") {
    std::lock_guard<std::mutex> lock(history_mutex_);
    Json::Value out(Json::objectValue);
    out["runs"] = Json::Value(Json::arrayValue);
    for (const auto &entry : history_) {
      out["runs"].append(entry);
    }
    return json_to_string(out);
  }

  // POST /api/runs/{id}/stop — request cancellation of a running diagnostic.
  if (method == "POST" && path.rfind("/api/runs/", 0) == 0 && path.find("/stop") != std::string::npos) {
    const std::string tail = path.substr(std::string("/api/runs/").size());
    const std::string id = tail.substr(0, tail.find('/'));
    auto run = find_run(id);
    if (!run) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "run not found";
      return json_to_string(out);
    }
    run->stop_requested.store(true, std::memory_order_relaxed);
    {
      std::lock_guard<std::mutex> lock(run->mutex);
      if (run->status == "running" || run->status == "queued") {
        run->status = "stopped";
      }
    }
    append_log(run, "warn", "Run stop requested by user.");
    Json::Value out(Json::objectValue);
    out["id"] = run->id;
    out["status"] = "stopped";
    return json_to_string(out);
  }

  if (method == "GET" && path.rfind("/api/runs/", 0) == 0) {
    const std::string tail = path.substr(std::string("/api/runs/").size());
    const std::size_t slash = tail.find('/');
    const std::string id = slash == std::string::npos ? tail : tail.substr(0, slash);
    const std::string sub = slash == std::string::npos ? "" : tail.substr(slash + 1);
    auto run = find_run(id);
    if (!run) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "run not found";
      return json_to_string(out);
    }

    if (sub.empty()) {
      std::lock_guard<std::mutex> lock(run->mutex);
      Json::Value out(Json::objectValue);
      out["id"] = run->id;
      out["status"] = run->status;
      out["result"] = run_result_to_json(run->result);
      return json_to_string(out);
    }

    if (sub == "logs") {
      std::size_t after = 0;
      const std::string after_value = query_value(query, "after");
      if (!after_value.empty()) {
        after = static_cast<std::size_t>(std::strtoull(after_value.c_str(), nullptr, 10));
      }
      std::lock_guard<std::mutex> lock(run->mutex);
      Json::Value out(Json::objectValue);
      out["id"] = run->id;
      out["status"] = run->status;
      out["next_offset"] = static_cast<Json::UInt64>(run->next_offset);
      for (const auto &line : run->logs) {
        if (line.offset < after) {
          continue;
        }
        Json::Value item(Json::objectValue);
        item["offset"] = static_cast<Json::UInt64>(line.offset);
        item["timestamp_utc"] = line.timestamp_utc;
        item["severity"] = line.severity;
        item["log_type"] = line.log_type;
        item["camera"] = line.camera;
        item["test"] = line.test;
        item["message"] = line.message;
        out["lines"].append(item);
      }
      return json_to_string(out);
    }

    if (sub == "reports") {
      std::lock_guard<std::mutex> lock(run->mutex);
      Json::Value out(Json::objectValue);
      out["id"] = run->id;
      for (const auto &artifact : run->artifacts) {
        Json::Value item(Json::objectValue);
        item["format"] = to_string(artifact.format);
        const std::string filename = artifact.path.substr(artifact.path.find_last_of('/') + 1);
        item["url"] = "/reports/" + run->id + "/" + filename;
        item["path"] = artifact.path;
        out["reports"].append(item);
      }
      return json_to_string(out);
    }
  }

  *status_code = MHD_HTTP_NOT_FOUND;
  Json::Value out(Json::objectValue);
  out["error"] = "not found";
  return json_to_string(out);
}

std::string WebServer::handle_static(const std::string &path, int *status_code, std::string *content_type) const {
  std::string relative = path == "/" ? "/index.html" : path;
  if (!safe_relative_path(relative)) {
    *status_code = MHD_HTTP_BAD_REQUEST;
    *content_type = "text/plain; charset=utf-8";
    return "invalid path";
  }

  std::string file_path = options_.web_root + relative;
  if (!file_exists(file_path)) {
    file_path = options_.web_root + "/index.html";
  }
  if (!file_exists(file_path)) {
    *status_code = MHD_HTTP_NOT_FOUND;
    *content_type = "text/html; charset=utf-8";
    return "<h1>Web UI assets not found</h1><p>Build the frontend or pass --web-root.</p>";
  }

  *status_code = MHD_HTTP_OK;
  *content_type = content_type_for(file_path);
  return read_file(file_path);
}

std::string WebServer::handle_report_file(const std::string &path, int *status_code, std::string *content_type) const {
  const std::string prefix = "/reports/";
  std::string relative = path.substr(prefix.size());
  if (!safe_relative_path(relative)) {
    *status_code = MHD_HTTP_BAD_REQUEST;
    *content_type = "text/plain; charset=utf-8";
    return "invalid report path";
  }
  const std::size_t slash = relative.find('/');
  if (slash == std::string::npos) {
    *status_code = MHD_HTTP_NOT_FOUND;
    *content_type = "text/plain; charset=utf-8";
    return "report not found";
  }
  const std::string run_id = relative.substr(0, slash);
  const std::string filename = relative.substr(slash + 1);
  const std::string file_path = options_.report_root + "/web-run-" + run_id + "/" + filename;
  if (!file_exists(file_path)) {
    *status_code = MHD_HTTP_NOT_FOUND;
    *content_type = "text/plain; charset=utf-8";
    return "report not found";
  }
  *status_code = MHD_HTTP_OK;
  *content_type = content_type_for(file_path);
  return read_file(file_path);
}

namespace {
constexpr std::size_t kMaxHistoryEntries = 500;
}  // namespace

void WebServer::load_run_history() {
  const std::string index_path = options_.report_root + "/runs-index.json";
  if (!file_exists(index_path)) {
    return;
  }
  const Json::Value parsed = parse_json_body(read_file(index_path));
  if (!parsed.isArray()) {
    return;  // corrupt or unexpected index — start with empty history rather than block startup
  }
  std::lock_guard<std::mutex> lock(history_mutex_);
  history_.clear();
  for (const auto &entry : parsed) {
    history_.push_back(entry);
  }
}

void WebServer::persist_run_summary(const Json::Value &summary) {
  std::lock_guard<std::mutex> lock(history_mutex_);
  history_.insert(history_.begin(), summary);
  if (history_.size() > kMaxHistoryEntries) {
    history_.resize(kMaxHistoryEntries);
  }
  Json::Value array(Json::arrayValue);
  for (const auto &entry : history_) {
    array.append(entry);
  }
  ensure_directory(options_.report_root);
  std::ofstream out(options_.report_root + "/runs-index.json", std::ios::trunc);
  out << json_to_string(array);
}

std::shared_ptr<WebServer::RunState> WebServer::find_run(const std::string &id) const {
  std::lock_guard<std::mutex> lock(runs_mutex_);
  auto it = std::find_if(runs_.begin(), runs_.end(), [&](const auto &run) { return run->id == id; });
  if (it == runs_.end()) {
    return {};
  }
  return *it;
}

std::shared_ptr<WebServer::RunState> WebServer::create_run(const RunConfig &config) {
  auto run = std::make_shared<RunState>();
  const std::string marker = "web-run-";
  const std::size_t marker_pos = config.output_directory.find(marker);
  if (marker_pos != std::string::npos) {
    run->id = config.output_directory.substr(marker_pos + marker.size());
  }
  if (run->id.empty()) {
    run->id = std::to_string(std::time(nullptr)) + "-" + std::to_string(std::rand() % 100000);
  }
  run->config = config;
  {
    std::lock_guard<std::mutex> lock(runs_mutex_);
    runs_.push_back(run);
  }
  append_log(run, "info", "Diagnostic run queued.");
  run->worker = std::thread([this, run]() { execute_run(run); });
  return run;
}

void WebServer::execute_run(std::shared_ptr<RunState> run) {
  {
    std::lock_guard<std::mutex> lock(run->mutex);
    run->status = "running";
  }

  append_log(run, "info", "Diagnostic run started.");
  ensure_directory(run->config.output_directory);

  if (run->config.cameras.empty()) {
    append_log(run, "warn", "No cameras were selected. The run will complete without camera diagnostics.");
  } else {
    for (const auto &camera : run->config.cameras) {
      append_log(run, "info", "Selected camera: " + camera.path, camera.path);
    }
  }

  ProfileRegistry profiles(options_.config_directory);
  DiagnosticRunner runner(&profiles);

  // Stream each test result as it completes rather than waiting for the full run.
  run->config.progress_callback = [this, run](const std::string &camera_path, const TestResult &test) {
    const std::string severity = test.status == TestStatus::Error || test.status == TestStatus::Fail     ? "error"
                                 : test.status == TestStatus::Warn || test.status == TestStatus::Skipped ? "warn"
                                                                                                         : "info";
    append_log(run, severity, test.id + " [" + std::string(to_string(test.status)) + "] " + test.summary, camera_path,
               test.id, "summary");
  };

  // Fine-grained log callback for real-time progress within each test.
  run->config.log_callback = [this, run](const std::string &severity, const std::string &camera,
                                         const std::string &test, const std::string &message,
                                         const std::string &log_type) {
    append_log(run, severity, message, camera, test, log_type);
  };

  // Wire cancellation token so user can stop the run mid-flight.
  run->config.stop_token = &run->stop_requested;

  const auto start_time = std::chrono::steady_clock::now();
  RunResult result = runner.run(run->config);
  auto artifacts = write_reports(result, run->config.report_formats, run->config.output_directory);
  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();

  std::string final_status;
  {
    std::lock_guard<std::mutex> lock(run->mutex);
    run->result = result;
    run->artifacts = artifacts;
    run->status = "completed";
    final_status = run->status;
  }

  persist_run_summary(run_summary_to_json(run->id, final_status, run->config, result, artifacts, duration_ms));

  append_log(run, "info", "Diagnostic run completed.");
}

void WebServer::append_log(const std::shared_ptr<RunState> &run, const std::string &severity,
                           const std::string &message, const std::string &camera, const std::string &test,
                           const std::string &log_type) {
  std::lock_guard<std::mutex> lock(run->mutex);
  RunLogLine line;
  line.offset = run->next_offset++;
  line.timestamp_utc = utc_timestamp();
  line.severity = severity;
  line.log_type = log_type;
  line.camera = camera;
  line.test = test;
  line.message = message;
  run->logs.push_back(line);
}

std::string default_web_root() {
  const char *env = std::getenv("V4L2_DIAG_WEB_ROOT");
  if (env && *env) {
    return env;
  }

  const std::string exe_dir = executable_dir();
  const std::vector<std::string> candidates = {
      exe_dir + "/../share/v4l2-camera-diagnostic/web",
      exe_dir + "/../source/frontend/dist",
      "source/frontend/dist",
  };
  for (const auto &candidate : candidates) {
    if (directory_exists(candidate)) {
      return candidate;
    }
  }
  return "source/frontend/dist";
}

std::string default_report_root() {
  // Reports must land in a stable, absolute location regardless of the process's cwd at
  // launch (e.g. the desktop launcher entry sets no working directory) — otherwise each
  // launch can silently write to a different "reports" directory and prior runs/history
  // appear to vanish. Mirrors profile_registry.cpp's default_config_directory() pattern.
  const char *xdg = std::getenv("XDG_DATA_HOME");
  if (xdg && *xdg) {
    return std::string(xdg) + "/v4l2-camera-diagnostic/reports";
  }
  const char *home = std::getenv("HOME");
  if (home && *home) {
    return std::string(home) + "/.local/share/v4l2-camera-diagnostic/reports";
  }
  return "reports";
}

bool open_url_in_browser(const std::string &url) {
  const std::string command = "xdg-open '" + url + "' >/dev/null 2>&1 &";
  return std::system(command.c_str()) == 0;
}

MhdRequestResult WebServer::handle_request_static(void *cls, MHD_Connection *connection, const char *url,
                                                  const char *method, const char *, const char *upload_data,
                                                  size_t *upload_data_size, void **con_cls) {
  auto *server = static_cast<WebServer *>(cls);

  if (*con_cls == nullptr) {
    *con_cls = new RequestBuffer();
    return MHD_YES;
  }

  auto *buffer = static_cast<RequestBuffer *>(*con_cls);
  if (*upload_data_size != 0) {
    buffer->body.append(upload_data, *upload_data_size);
    *upload_data_size = 0;
    return MHD_YES;
  }

  std::string path(url ? url : "/");
  std::string query;
  const std::size_t question = path.find('?');
  if (question != std::string::npos) {
    query = path.substr(question + 1);
    path = path.substr(0, question);
  }

  const char *after_value = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "after");
  if (after_value && *after_value) {
    query = "after=" + std::string(after_value);
  }

  int status_code = MHD_HTTP_OK;
  std::string content_type;
  std::string response_body =
      server->handle_request(method ? method : "GET", path, query, buffer->body, &status_code, &content_type);

  auto *response = MHD_create_response_from_buffer(response_body.size(), const_cast<char *>(response_body.data()),
                                                   MHD_RESPMEM_MUST_COPY);
  MHD_add_response_header(response, "Content-Type", content_type.c_str());
  MHD_add_response_header(response, "Access-Control-Allow-Origin", "http://127.0.0.1");
  MHD_add_response_header(response, "Cache-Control", "no-store");
  const MhdRequestResult ret = MHD_queue_response(connection, status_code, response);
  MHD_destroy_response(response);
  delete buffer;
  *con_cls = nullptr;
  return ret;
}

}  // namespace v4l2diag
