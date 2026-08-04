#include "v4l2diag/web/web_server.hpp"

#include "v4l2diag/core/duration_format.hpp"
#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/config_migration.hpp"
#include "v4l2diag/core/run_config_json.hpp"
#include "v4l2diag/core/test_json.hpp"
#include "v4l2diag/core/run_result_json.hpp"
#include "v4l2diag/core/run_routing.hpp"
#include "v4l2diag/core/run_status.hpp"

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
#include <iostream>
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

// A ThresholdConfig carries two per-test maps: `values` (verdict cut-offs) and
// `params` (the knobs a test actually runs with). Both are persisted to disk and
// both are editable in the UI, so both have to survive the API round trip.
Json::Value test_key_map_to_json(const std::map<std::string, TestThresholds> &by_test) {
  Json::Value out(Json::objectValue);
  for (const auto &test : by_test) {
    Json::Value keys(Json::objectValue);
    for (const auto &kv : test.second) {
      keys[kv.first] = kv.second;
    }
    out[test.first] = keys;
  }
  return out;
}

void json_to_test_key_map(const Json::Value &node, std::map<std::string, TestThresholds> *out) {
  if (!node.isObject()) {
    return;
  }
  for (const auto &test_id : node.getMemberNames()) {
    const Json::Value &keys = node[test_id];
    if (!keys.isObject()) {
      continue;
    }
    for (const auto &key : keys.getMemberNames()) {
      if (keys[key].isNumeric()) {
        (*out)[test_id][key] = keys[key].asDouble();
      }
    }
  }
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
  // Kept deliberately. The product no longer writes PDFs, but report
  // directories archived by older builds still contain .pdf files and must
  // keep downloading with the right content type.
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
  // Absolute paths are rejected too: "/etc/passwd" contains no ".." but must not be
  // treated as a path relative to the report root.
  return path.find("..") == std::string::npos && path.find('\\') == std::string::npos &&
         (path.empty() || path.front() != '/');
}

// realpath() with an empty string for "does not exist / cannot be resolved". Used to
// compare a candidate against the report root AFTER symlinks are followed, so a link
// pointing outside cannot smuggle a file out.
std::string real_path_or_empty(const std::string &path) {
  char *resolved = realpath(path.c_str(), nullptr);
  if (resolved == nullptr) {
    return std::string();
  }
  std::string out(resolved);
  std::free(resolved);
  return out;
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
  out["defaults"]["trigger_mode"] = to_string(profile.defaults.trigger_mode);
  out["defaults"]["memory_backends"] = Json::Value(Json::arrayValue);
  out["defaults"]["test_selectors"] = Json::Value(Json::arrayValue);
  out["trigger_channels"] = Json::Value(Json::arrayValue);
  out["role_bindings"] = Json::Value(Json::arrayValue);
  for (MemoryBackend backend : profile.defaults.memory_backends) {
    out["defaults"]["memory_backends"].append(to_string(backend));
  }
  for (const auto &selector : profile.defaults.test_selectors) {
    out["defaults"]["test_selectors"].append(selector);
  }
  out["defaults"]["trigger_rate_hz"] = profile.defaults.trigger_rate_hz;
  out["defaults"]["pulse_width_ms"] = profile.defaults.pulse_width_ms;

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
  for (const auto &binding : profile.role_bindings) {
    Json::Value item(Json::objectValue);
    item["role"] = binding.role;
    item["trigger_channel_id"] = binding.trigger_channel_id;
    out["role_bindings"].append(item);
  }
  return out;
}

// One structured answer for every path that refuses a config, so the caller sees
// which fields are at fault instead of a generic profile-id error.
std::string migration_error_response(const MigrationReport &report, int *status_code) {
  *status_code = MHD_HTTP_BAD_REQUEST;
  Json::Value out(Json::objectValue);
  out["error"] = report.error.empty() ? "config cannot be used as submitted" : report.error;
  out["migration"] = migration_report_to_json(report);
  return json_to_string(out);
}

// Delegates to the one migration entry point, so a profile arriving over the API
// is interpreted exactly as one read from disk. This used to be a second
// hand-written parser and the two drifted.
//
// Strict on purpose: a body with no schema_version is Malformed, not silently
// stamped as current. The web UI sends the version, and stamping it would punch
// a hole through the versioning rule on exactly the path that accepts files from
// elsewhere.
bool profile_from_json(const Json::Value &root, DeviceProfile *profile, MigrationReport *report) {
  return migrate_profile_json(root, profile, report);
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

  // One run-level Trigger Profile, plus the routing it resolved to. Roles come
  // from the topology, so the camera entries only need their path.
  out["trigger_profile_id"] = config.trigger_profile_id;
  out["role_bindings"] = Json::Value(Json::arrayValue);
  for (const auto &binding : result.role_bindings) {
    Json::Value item(Json::objectValue);
    item["role"] = binding.role;
    item["trigger_channel_id"] = binding.trigger_channel_id;
    out["role_bindings"].append(item);
  }
  // Declared before use: an empty collection must still be an array, or the UI does
  // `undefined.map(...)` on it (plan 2.7). `reports` is the one that actually bit --
  // a run that failed before writing artifacts blanked the Dashboard.
  out["camera_paths"] = Json::Value(Json::arrayValue);
  out["slaves"] = Json::Value(Json::arrayValue);
  out["reports"] = Json::Value(Json::arrayValue);
  out["camera_paths"].append(config.master.path);
  {
    Json::Value assignment(Json::objectValue);
    assignment["path"] = config.master.path;
    assignment["role"] = kMasterRole();
    out["master"] = assignment;
  }
  for (std::size_t i = 0; i < config.slaves.size(); i++) {
    Json::Value assignment(Json::objectValue);
    assignment["path"] = config.slaves[i].path;
    assignment["role"] = slave_role(i);
    out["slaves"].append(assignment);
  }
  out["started_at_utc"] = result.started_at_utc;
  out["finished_at_utc"] = result.finished_at_utc;
  out["duration_ms"] = static_cast<Json::Int64>(duration_ms);
  // The naming inputs, recorded so the DMESG name can be regenerated after a restart
  // from the index alone (plan 3.4). Not interchangeable with the ids: a config file's
  // name and the id inside it are independent.
  out["trigger_profile_file"] = result.trigger_profile_file;
  out["threshold_config_file"] = result.threshold_config_file;

  int pass_count = 0, fail_count = 0, warn_count = 0, skip_count = 0;
  for (const auto &camera : result.cameras) {
    for (const auto &test : camera.tests) {
      switch (test.status) {
        case TestStatus::Pass:
          pass_count++;
          break;
        case TestStatus::Fail:
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
    // The filename as the server itself wrote it. Recorded so a later lookup resolves
    // the artifact from THIS entry rather than rebuilding a path out of a run id --
    // pasting a client-supplied id into a path is how a traversal gets in.
    item["filename"] = filename;
    out["reports"].append(item);
  }
  return out;
}

bool validate_run_config(const RunConfig &config, const WebServerOptions &options, std::string *error) {
  if (config.master.path.empty()) {
    *error = "a master camera is required";
    return false;
  }

  std::set<std::string> camera_paths;
  ProfileRegistry profiles(options.config_directory);

  auto validate_camera = [&](const RunConfig::CameraConfig &camera) {
    if (camera.path.empty() || !camera_paths.insert(camera.path).second) {
      *error = camera.path.empty() ? "camera path is required" : "camera paths must be unique";
      return false;
    }
    return true;
  };

  if (!validate_camera(config.master)) {
    return false;
  }
  for (const auto &slave : config.slaves) {
    if (!validate_camera(slave)) {
      return false;
    }
  }

  // Free-run does not route at all, so it needs no Trigger Profile.
  if (config.trigger_mode == TriggerMode::FreeRun) {
    return true;
  }
  DeviceProfile profile;
  if (config.trigger_profile_id.empty() || !profiles.get_profile(config.trigger_profile_id, &profile)) {
    *error = "active trigger modes require a valid run-level trigger profile";
    return false;
  }
  // The same seam the CLI goes through, so both refuse an incomplete routing with
  // the same words instead of maintaining two ideas of what is valid.
  std::vector<std::string> channel_ids;
  for (const auto &channel : profile.trigger_channels) {
    channel_ids.push_back(channel.id);
  }
  const RoleResolution routing = resolve_role_bindings(profile.role_bindings, channel_ids, config.slaves.size());
  if (!routing.ok()) {
    *error = describe_role_resolution(routing);
    return false;
  }
  // Every resolved channel has to match the selected mode. Same helper the CLI
  // uses, so both refuse with the same words -- this check used to live only here,
  // which meant the CLI did not make it at all.
  for (const auto &binding : routing.resolved) {
    const auto channel =
        std::find_if(profile.trigger_channels.begin(), profile.trigger_channels.end(),
                     [&](const TriggerChannel &item) { return item.id == binding.trigger_channel_id; });
    if (channel == profile.trigger_channels.end()) {
      *error = "trigger channel \"" + binding.trigger_channel_id + "\" (role " + binding.role +
               ") is not defined by the profile";
      return false;
    }
    const std::string mismatch = describe_mode_mismatch(
        config.trigger_mode, channel->type == TriggerChannel::Type::Hardware, binding.role, binding.trigger_channel_id);
    if (!mismatch.empty()) {
      *error = mismatch;
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
  // Set once the run's final status is resolved and persist_run_summary() has
  // returned, so the thread-entry guard cannot double-finalise a run
  // execute_run() already handled. Note that persist_run_summary() does not yet
  // check the ofstream, so this flag does not prove the history reached disk --
  // atomic write + real I/O verification stay open under plan 2.9.
  std::atomic<bool> finalized{false};
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

  if (method == "GET" && path == "/api/dmesg") {
    *content_type = "text/plain; charset=utf-8";

    // A download must name a real run, and it is checked BEFORE anything is executed
    // (plan 3.4). Rejecting only the Content-Disposition header would still run
    // journalctl and hand back the kernel log with a 200 -- the refusal has to be the
    // response, not a missing header on an otherwise successful one.
    //
    // The live view (no download=1) needs no run: it is the current boot's log, not a
    // particular run's artifact.
    if (query_value(query, "download") == "1") {
      const std::string run_id = query_value(query, "run");
      if (run_id.empty()) {
        *status_code = MHD_HTTP_BAD_REQUEST;
        return "A kernel-log download must name a run: /api/dmesg?download=1&run=<run-id>.\n";
      }
      // Exact match against a live run or a history record. A prefix, a suffix or a
      // traversal attempt selects nothing and is refused here, before any command runs.
      if (!find_run(run_id) && !find_history_entry(run_id).isObject()) {
        *status_code = MHD_HTTP_NOT_FOUND;
        return "No such run: a kernel-log download is only served for a run this server knows.\n";
      }
    }

    // journalctl rather than dmesg: reading it needs only membership in "adm"
    // (or systemd-journal), which the journal directories grant by ACL, so the
    // server stays unprivileged. dmesg would additionally need CAP_SYSLOG or
    // root wherever kernel.dmesg_restrict=1. -b limits output to the current
    // boot — without it journalctl -k spans every retained boot.
    //
    // Fixed command, no user input: the request contributes a run id, which is used to
    // look up a record and to name the download, and never reaches a shell.
    std::string output;
    if (read_kernel_log(&output)) {
      return output;
    }
    // Hand back what journalctl actually said. A generic "permission denied?"
    // leaves the reader guessing between a missing group, a disabled journal
    // and journalctl not being installed at all.
    *status_code = MHD_HTTP_INTERNAL_SERVER_ERROR;
    std::string message = "Cannot read the kernel log via 'journalctl -k -b'.\n";
    if (!output.empty()) {
      message += "\n" + output + "\n";
    }
    message += "This needs membership in the 'adm' group. Add it with:\n";
    message += "  sudo usermod -aG adm $(id -un)\n";
    message += "then log out and back in.\n";
    return message;
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
    // Migration state per stored config, including ones too new or too broken to
    // load. The backend contract (MigrationReport) and what the UI shows are
    // separate concerns: this is the wire model the UI will render.
    out["schema_version"] = kProfileSchemaVersion;
    out["configs"] = Json::Value(Json::arrayValue);
    for (const auto &stored : registry.stored_configs()) {
      Json::Value item = migration_report_to_json(stored.report);
      item["file"] = stored.file;
      item["profile_id"] = stored.profile_id;
      if (stored.report.has_draft()) {
        // Migrated values, for prefilling the migration form. Sent whenever the
        // file parsed -- a Current config that fails field validation needs the
        // form just as much as a Legacy one. Deliberately not in "profiles":
        // neither is runnable until the user saves it.
        item["draft_profile"] = profile_to_json(stored.draft);
      }
      out["configs"].append(item);
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
    DeviceProfile profile;
    MigrationReport report;
    if (!profile_from_json(body_json, &profile, &report)) {
      // Future or Malformed: structured, not a generic id error.
      return migration_error_response(report, status_code);
    }
    if (report.needs_user_input()) {
      // A Legacy or incomplete import is previewed, never written. The user
      // reviews the migration and saves it through POST /api/profiles.
      Json::Value out(Json::objectValue);
      out["imported"] = false;
      out["migration"] = migration_report_to_json(report);
      out["draft_profile"] = profile_to_json(profile);
      return json_to_string(out);
    }
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
    DeviceProfile profile;
    MigrationReport report;
    if (!profile_from_json(body_json, &profile, &report) || report.needs_user_input()) {
      return migration_error_response(report, status_code);
    }
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

  // Explicit migration commit, addressed by source config file rather than by
  // profile id.
  //
  // POST /api/profiles writes "<profile.id>.json", which is wrong for a
  // migration whose file name differs from its id, or whose id the user edited on
  // the form: it would leave the original file behind, still reported as needing
  // migration. This endpoint knows which file it is replacing.
  //
  // The {config-id} is the file name from GET /api/profiles' configs[]. It is
  // only ever compared against that list, so it cannot address anything outside
  // the config directory.
  if (method == "PUT" && path.rfind("/api/profiles/configs/", 0) == 0 &&
      path.size() > std::string("/api/profiles/configs/").size() &&
      path.substr(path.size() - std::string("/migrate").size()) == "/migrate") {
    const std::string tail = path.substr(std::string("/api/profiles/configs/").size());
    const std::string config_id = tail.substr(0, tail.size() - std::string("/migrate").size());
    const Json::Value body_json = parse_json_body(body);
    if (body_json.isMember("parse_error")) {
      *status_code = MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = body_json["parse_error"];
      return json_to_string(out);
    }
    ProfileRegistry registry(options_.config_directory);
    const auto configs = registry.stored_configs();
    const auto source = std::find_if(configs.begin(), configs.end(),
                                     [&](const ProfileRegistry::StoredConfig &c) { return c.file == config_id; });
    if (source == configs.end()) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "config not found";
      return json_to_string(out);
    }
    // Only a config that genuinely needs repairing may be committed here. Without
    // this, a valid current body aimed at a Future, Malformed or already-runnable
    // config would rewrite or delete that file. migrate_config() enforces the same
    // rule; this answers with the source's own migration report so the caller
    // learns why, rather than getting a bare 400.
    if (!source->report.has_draft() || !source->report.needs_user_input()) {
      MigrationReport refused = source->report;
      refused.error = source->report.usable_for_run()
                          ? "this config does not need migration; use POST or PUT /api/profiles"
                          : "this config cannot be migrated: it could not be read";
      *status_code = MHD_HTTP_CONFLICT;
      Json::Value out(Json::objectValue);
      out["error"] = refused.error;
      out["migration"] = migration_report_to_json(refused);
      return json_to_string(out);
    }
    // The filled-in draft has to stand on its own at the current schema: the same
    // strictness as any other save, so a migration cannot be committed while it
    // is still Legacy or incomplete.
    DeviceProfile profile;
    MigrationReport report;
    if (!profile_from_json(body_json, &profile, &report) || report.needs_user_input()) {
      return migration_error_response(report, status_code);
    }
    // Target collisions are the registry's call: it sees every stored file, while
    // get_profile() is blind to the Future/Legacy/Malformed ones a rename would
    // overwrite.
    std::string error;
    if (!registry.migrate_config(config_id, profile, &error)) {
      *status_code = error.find("already") != std::string::npos ? MHD_HTTP_CONFLICT : MHD_HTTP_BAD_REQUEST;
      Json::Value out(Json::objectValue);
      out["error"] = error;
      return json_to_string(out);
    }
    Json::Value out(Json::objectValue);
    out["migrated"] = true;
    out["source_file"] = config_id;
    out["profile"] = profile_to_json(profile);
    return json_to_string(out);
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
    DeviceProfile profile;
    MigrationReport put_report;
    if (!profile_from_json(body_json, &profile, &put_report) || put_report.needs_user_input()) {
      return migration_error_response(put_report, status_code);
    }
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
    for (const auto &stored : registry.list_configs()) {
      // The built-in "default" preset is read-only in the UI, so it is served
      // fully resolved: every configurable test appears, even one the stored file
      // has no entry for. Custom presets keep their stored overrides only, so
      // saving one cannot freeze today's defaults into it.
      const ThresholdConfig config = stored.id == "default" ? registry.resolve(stored.id) : stored;
      Json::Value item(Json::objectValue);
      item["id"] = config.id;
      item["name"] = config.name;
      item["description"] = config.description;
      item["values"] = test_key_map_to_json(config.values);
      item["params"] = test_key_map_to_json(config.params);
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
    if (config.id == "default") {
      config = registry.resolve(config.id);  // read-only preset: serve the complete set
    }
    Json::Value out(Json::objectValue);
    out["id"] = config.id;
    out["name"] = config.name;
    out["description"] = config.description;
    out["values"] = test_key_map_to_json(config.values);
    out["params"] = test_key_map_to_json(config.params);
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
    json_to_test_key_map(body_json["values"], &config.values);
    json_to_test_key_map(body_json["params"], &config.params);
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
    json_to_test_key_map(body_json["values"], &config.values);
    json_to_test_key_map(body_json["params"], &config.params);
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
    RunConfig config = run_config_from_json(body_json, options_.report_root, options_.config_directory, run_id);
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
    std::string observed;
    {
      std::lock_guard<std::mutex> lock(run->mutex);
      if (run->status == "running" || run->status == "queued") {
        run->status = "stopped";
      }
      observed = run->status;
    }
    append_log(run, "warn", "Run stop requested by user.");
    Json::Value out(Json::objectValue);
    out["id"] = run->id;
    // Report what the run actually is. A run that had already finished stays
    // "completed"/"error"; claiming "stopped" told the UI a lie it then cached.
    out["status"] = observed;
    return json_to_string(out);
  }

  if (method == "GET" && path.rfind("/api/runs/", 0) == 0) {
    const std::string tail = path.substr(std::string("/api/runs/").size());
    const std::size_t slash = tail.find('/');
    const std::string id = slash == std::string::npos ? tail : tail.substr(0, slash);
    const std::string sub = slash == std::string::npos ? "" : tail.substr(slash + 1);
    // The in-memory run is always the primary source: it is the live one, and its
    // result is more current than anything on disk.
    auto run = find_run(id);
    // Only if it is gone do we fall back to what the server itself recorded. The id is
    // never pasted into a path -- it can only select an index entry (see
    // find_history_entry) or select nothing.
    const Json::Value history_entry = run ? Json::Value() : find_history_entry(id);
    if (!run && !history_entry.isObject()) {
      *status_code = MHD_HTTP_NOT_FOUND;
      Json::Value out(Json::objectValue);
      out["error"] = "run not found";
      return json_to_string(out);
    }

    if (sub.empty()) {
      if (run) {
        std::lock_guard<std::mutex> lock(run->mutex);
        Json::Value out(Json::objectValue);
        out["id"] = run->id;
        out["status"] = run->status;
        out["result"] = run_result_to_json(run->result);
        return json_to_string(out);
      }
      // Restored from the canonical JSON artifact, through the same serializer the
      // live path uses, so the two documents are the same shape.
      RunResult restored;
      std::string load_error;
      if (!load_historical_result(history_entry, &restored, &load_error)) {
        // The run EXISTS -- 404 would be a lie -- but its structured result does not.
        // An explicit code beats an empty result the UI would render as "no tests".
        *status_code = MHD_HTTP_NOT_FOUND;
        Json::Value out(Json::objectValue);
        out["id"] = id;
        out["status"] = history_entry["status"];
        out["error"] = "structured_result_unavailable";
        out["reason"] = load_error;
        return json_to_string(out);
      }
      Json::Value out(Json::objectValue);
      out["id"] = id;
      out["status"] = history_entry["status"];
      out["result"] = run_result_to_json(restored);
      return json_to_string(out);
    }

    if (sub == "logs") {
      if (!run) {
        // Logs are held in memory only, so a restart loses them. The run itself is
        // known, so "run not found" would be wrong -- this says exactly what is
        // missing instead.
        *status_code = MHD_HTTP_NOT_FOUND;
        Json::Value out(Json::objectValue);
        out["id"] = id;
        out["status"] = history_entry["status"];
        out["error"] = "historical_logs_unavailable";
        out["reason"] = "run logs are not persisted across a server restart";
        return json_to_string(out);
      }
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
      if (!run) {
        // Served from the index metadata the server itself wrote: the entry already
        // carries a url and a format per artifact, so no path is rebuilt from the id.
        Json::Value out(Json::objectValue);
        out["id"] = id;
        out["reports"] = Json::Value(Json::arrayValue);
        for (const auto &report : history_entry["reports"]) {
          out["reports"].append(report);
        }
        return json_to_string(out);
      }
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
  std::string relative = path == "/" ? "index.html" : path.substr(1);
  if (!safe_relative_path(relative)) {
    *status_code = MHD_HTTP_BAD_REQUEST;
    *content_type = "text/plain; charset=utf-8";
    return "invalid path";
  }

  std::string file_path = options_.web_root + "/" + relative;
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
  // The id selects a RECORD -- live run or runs-index entry -- and the record says
  // which files it produced. Building a path straight out of the URL is what let an
  // unknown id, a prefix of a real one, or an undeclared file be served.
  const std::vector<std::string> declared = declared_artifact_filenames(run_id);
  std::string file_path;
  std::string resolve_error;
  if (!resolve_artifact_path(run_id, filename, declared, &file_path, &resolve_error)) {
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
  // Older builds wrote a bare array with no version; the current shape is an
  // object carrying schema_version plus "runs". Both are read; an index from a
  // newer build or a corrupt file yields empty history rather than blocking
  // startup, and clears history_index_writable_ so no later run writes the index
  // at all -- the file we could not read is left exactly as it is.
  const RunsIndexState state = classify_runs_index(parsed);
  const Json::Value *entries = nullptr;
  switch (state.state) {
    case ConfigVersionState::Legacy:
      entries = &parsed;  // bare array
      break;
    case ConfigVersionState::Current:
      entries = &parsed["runs"];
      break;
    case ConfigVersionState::Future:
    case ConfigVersionState::Malformed:
      // Unsupported. Refuse to touch it: overwriting would destroy history this
      // build cannot read but a newer one can.
      history_index_writable_.store(false, std::memory_order_relaxed);
      std::cerr << "v4l2diag: runs-index.json is " << to_string(state.state)
                << "; leaving it untouched and not recording new runs there\n";
      return;
  }
  if (entries == nullptr || !entries->isArray()) {
    // A current-version document whose "runs" is not an array is malformed too.
    history_index_writable_.store(false, std::memory_order_relaxed);
    std::cerr << "v4l2diag: runs-index.json has no usable \"runs\" array; leaving it untouched\n";
    return;
  }
  std::lock_guard<std::mutex> lock(history_mutex_);
  history_.clear();
  for (const auto &entry : *entries) {
    history_.push_back(entry);
  }
}

void WebServer::persist_run_summary(const Json::Value &summary) {
  std::lock_guard<std::mutex> lock(history_mutex_);
  // Upsert, not insert. Finalisation can be retried (execute_run() persists,
  // and the worker's recovery path may persist again for the same run), so
  // keying on the run id keeps the history free of duplicate entries.
  const std::string id = summary.get("id", "").asString();
  auto existing = history_.end();
  if (!id.empty()) {
    existing = std::find_if(history_.begin(), history_.end(),
                            [&](const Json::Value &entry) { return entry.get("id", "").asString() == id; });
  }
  if (existing != history_.end()) {
    *existing = summary;
  } else {
    history_.insert(history_.begin(), summary);
  }
  if (history_.size() > kMaxHistoryEntries) {
    history_.resize(kMaxHistoryEntries);
  }
  Json::Value array(Json::arrayValue);
  for (const auto &entry : history_) {
    array.append(entry);
  }
  if (!history_index_writable_.load(std::memory_order_relaxed)) {
    // The stored index is unsupported (see load_run_history). The run itself
    // still completed and its report files are on disk; only the index entry is
    // skipped, because writing would truncate a file we could not read.
    return;
  }
  // Versioned from now on, so a future reader can tell what shape this is.
  Json::Value document(Json::objectValue);
  document["schema_version"] = kRunsIndexSchemaVersion;
  document["runs"] = array;
  ensure_directory(options_.report_root);
  std::ofstream out(options_.report_root + "/runs-index.json", std::ios::trunc);
  out << json_to_string(document);
}

std::shared_ptr<WebServer::RunState> WebServer::find_run(const std::string &id) const {
  std::lock_guard<std::mutex> lock(runs_mutex_);
  auto it = std::find_if(runs_.begin(), runs_.end(), [&](const auto &run) { return run->id == id; });
  if (it == runs_.end()) {
    return {};
  }
  return *it;
}

Json::Value WebServer::find_history_entry(const std::string &id) const {
  if (id.empty()) {
    return Json::Value();
  }
  std::lock_guard<std::mutex> lock(history_mutex_);
  for (const auto &entry : history_) {
    // Exact match only. A prefix or suffix match would let a crafted id select an
    // entry that is not the one it names.
    if (entry.isObject() && entry["id"].isString() && entry["id"].asString() == id) {
      return entry;
    }
  }
  return Json::Value();
}

bool WebServer::read_kernel_log(std::string *output) const {
  if (options_.kernel_log_reader) {
    // Only a test injects this (see WebServerOptions::kernel_log_reader).
    return options_.kernel_log_reader(output);
  }
  // A fixed command string. Nothing from the request is interpolated, so there is no
  // shell injection surface, and no privileged path is introduced: journalctl reads the
  // journal through group membership alone.
  FILE *pipe = popen("journalctl -k -b --no-pager 2>&1", "r");
  if (pipe == nullptr) {
    return false;
  }
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    *output += buffer;
  }
  return pclose(pipe) == 0 && !output->empty();
}

std::string WebServer::dmesg_download_filename(const std::string &id) const {
  // The live run first, then the index -- the same order every other lookup uses.
  ReportNaming naming;
  if (auto run = find_run(id)) {
    std::lock_guard<std::mutex> lock(run->mutex);
    naming.started_at_utc = run->result.started_at_utc;
    naming.trigger_mode = run->config.trigger_mode;
    naming.trigger_profile_file = run->config.trigger_profile_file;
    naming.test_configuration_file = run->config.threshold_config_file;
    return dmesg_log_filename(naming);
  }

  const Json::Value entry = find_history_entry(id);
  if (!entry.isObject() || !entry["started_at_utc"].isString()) {
    // Not a run this server knows about. No name is invented for it: a caller that
    // cannot name a real run has nothing to download.
    return std::string();
  }
  naming.started_at_utc = entry["started_at_utc"].asString();
  if (entry["trigger_mode"].isString()) {
    // An unparseable mode keeps the free-run default rather than guessing; free-run
    // simply omits the profile part of the name.
    TriggerMode mode = TriggerMode::FreeRun;
    if (parse_trigger_mode(entry["trigger_mode"].asString(), &mode)) {
      naming.trigger_mode = mode;
    }
  }
  if (entry["trigger_profile_file"].isString()) {
    naming.trigger_profile_file = entry["trigger_profile_file"].asString();
  }
  if (entry["threshold_config_file"].isString()) {
    naming.test_configuration_file = entry["threshold_config_file"].asString();
  }
  return dmesg_log_filename(naming);
}

std::vector<std::string> WebServer::declared_artifact_filenames(const std::string &id) const {
  std::vector<std::string> names;
  // The live run is the primary source here too.
  if (auto run = find_run(id)) {
    std::lock_guard<std::mutex> lock(run->mutex);
    for (const auto &artifact : run->artifacts) {
      const std::size_t slash = artifact.path.find_last_of('/');
      names.push_back(slash == std::string::npos ? artifact.path : artifact.path.substr(slash + 1));
    }
    return names;
  }
  const Json::Value entry = find_history_entry(id);
  if (!entry.isObject()) {
    return names;  // unknown id: nothing is declared, so nothing can be served
  }
  for (const auto &report : entry["reports"]) {
    if (report.isObject() && report["filename"].isString()) {
      names.push_back(report["filename"].asString());
    }
  }
  return names;
}

bool WebServer::resolve_artifact_path(const std::string &run_id, const std::string &filename,
                                      const std::vector<std::string> &declared_filenames, std::string *resolved,
                                      std::string *error) const {
  // The record is the authority, not the directory listing. A file that happens to sit
  // next to the artifacts -- dropped there by anything -- is not an artifact.
  if (std::find(declared_filenames.begin(), declared_filenames.end(), filename) == declared_filenames.end()) {
    *error = "the run's record does not declare an artifact named \"" + filename + "\"";
    return false;
  }
  // A declared name must still be a plain filename; an index can be hand-edited.
  if (filename.empty() || filename.find('/') != std::string::npos || filename.find("..") != std::string::npos ||
      filename.front() == '.') {
    *error = "the declared artifact filename is unusable";
    return false;
  }

  const std::string run_dir = options_.report_root + "/web-run-" + run_id;
  const std::string candidate = run_dir + "/" + filename;
  // Bounded by the run's OWN directory, not just the report root. A symlink from this
  // run's directory into another run's would otherwise resolve fine and serve that
  // run's result under this id.
  const std::string resolved_dir = real_path_or_empty(run_dir);
  const std::string resolved_file = real_path_or_empty(candidate);
  if (resolved_dir.empty() || resolved_file.empty() ||
      resolved_file.compare(0, resolved_dir.size() + 1, resolved_dir + "/") != 0) {
    *error = "the artifact is missing or resolves outside the run's own directory";
    return false;
  }
  *resolved = resolved_file;
  return true;
}

bool WebServer::load_historical_result(const Json::Value &entry, RunResult *result, std::string *error) const {
  // The id comes from the entry, not from the request, so the two cannot disagree.
  const std::string id = entry["id"].asString();
  std::string filename;
  for (const auto &report : entry["reports"]) {
    if (!report.isObject() || !report["format"].isString()) {
      continue;
    }
    if (report["format"].asString() == to_string(ReportFormat::Json) && report["filename"].isString()) {
      filename = report["filename"].asString();
    }
  }
  if (filename.empty()) {
    *error = "the run's index entry names no JSON artifact";
    return false;
  }
  // The same resolver the HTTP download route uses, so both are bounded by the run's
  // OWN directory. A symlink from this run's directory into another run's would
  // otherwise pass a report-root check and serve that run's result under this id.
  std::string path;
  if (!resolve_artifact_path(id, filename, {filename}, &path, error)) {
    return false;
  }

  const std::string text = read_file(path);
  if (text.empty()) {
    *error = "the run's JSON report artifact could not be read";
    return false;
  }
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string parse_errors;
  std::istringstream in(text);
  if (!Json::parseFromStream(builder, in, &root, &parse_errors)) {
    *error = "the run's JSON report artifact is not valid JSON: " + parse_errors;
    return false;
  }
  // Malformed or from a newer build: refused with a reason rather than parsed as if it
  // were the current shape.
  return run_result_from_json(root, result, error);
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
  run->worker = std::thread([this, run]() {
    // Last line of defence. execute_run() guards the run itself, but setup
    // (ProfileRegistry, callbacks), status finalisation, append_log and
    // persist_run_summary are outside that block -- and this is a worker
    // thread, so anything escaping here reaches std::terminate and kills the
    // whole server along with every other run's history.
    const GuardedOutcome outcome =
        run_guarded_with_recovery([this, run]() { execute_run(run); },
                                  [this, run](const std::string &message) { finalize_failed_run(run, message); });
    if (outcome.recovery.failed) {
      // The recovery path threw as well. There is nothing left to record --
      // the history write is exactly what failed -- but the thread must still
      // return normally instead of terminating the process.
      std::cerr << "v4l2diag: run " << run->id << " could not be finalised: " << outcome.recovery.message << "\n";
    }
  });
  return run;
}

void WebServer::execute_run(std::shared_ptr<RunState> run) {
  {
    std::lock_guard<std::mutex> lock(run->mutex);
    run->status = "running";
  }

  append_log(run, "info", "Diagnostic run started.");
  ensure_directory(run->config.output_directory);

  append_log(run, "info", "Master camera: " + run->config.master.path, run->config.master.path);
  for (const auto &slave : run->config.slaves) {
    append_log(run, "info", "Slave camera (t25 only): " + slave.path, slave.path);
  }

  ProfileRegistry profiles(options_.config_directory);
  DiagnosticRunner runner(&profiles);

  // Stream each test result as it completes rather than waiting for the full run. This is
  // the single per-test completion line: the duration rides along here instead of on a
  // separate "completed" line, which used to be emitted before the verdict was known and
  // so stamped a green check on tests that had just failed.
  run->config.progress_callback = [this, run](const std::string &camera_path, const TestResult &test) {
    const std::string severity = test.status == TestStatus::Fail                                         ? "error"
                                 : test.status == TestStatus::Warn || test.status == TestStatus::Skipped ? "warn"
                                                                                                         : "info";
    // The shared formatter (plan 3.2): the live log, the HTML report and the web UI all
    // spell the same duration the same way.
    const std::string elapsed = format_duration_ms(test.duration_ms);
    // The trailing "(duration)" also guarantees there is always text after the status tag,
    // so a test that reports no summary can no longer defeat the client-side parser.
    std::string message = test.id + " [" + std::string(to_string(test.status)) + "] ";
    if (!test.summary.empty()) {
      message += test.summary + " ";
    }
    message += std::string("(") + elapsed + ")";
    append_log(run, severity, message, camera_path, test.id, "summary");
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

  // Nothing below may throw past this frame. This is a detached worker thread:
  // an escaping exception reaches std::terminate and takes the whole server
  // down with it, losing every other run's history in the process.
  RunResult result;
  std::vector<ReportArtifact> artifacts;
  const WorkerFailure failure = run_guarded([&]() {
    result = runner.run(run->config);
    // Naming inputs, before anything is written (plan 3.4 / 3.5). The runner produces a
    // result; the id and the source filenames belong to the REQUEST that produced it, and
    // the writer needs both to name the artifacts and to point Export DMESG at this run.
    result.run_id = run->id;
    result.trigger_profile_file = run->config.trigger_profile_file;
    result.threshold_config_file = run->config.threshold_config_file;
    artifacts = write_reports(result, run->config.output_directory);
  });
  const bool worker_failed = failure.failed;

  const auto duration_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_time).count();

  std::string final_status;
  bool truncated = false;
  {
    std::lock_guard<std::mutex> lock(run->mutex);
    run->result = result;
    run->artifacts = artifacts;
    // Resolve rather than overwrite: the stop endpoint may already have set
    // "stopped", and that must survive.
    const RunOutcome outcome =
        resolve_run_outcome(run->stop_requested.load(std::memory_order_relaxed), run->status, worker_failed);
    run->status = to_string(outcome);
    final_status = run->status;
    truncated = run_is_truncated(outcome);
  }
  // run->finalized is deliberately NOT set here. Everything below can throw --
  // append_log, building the summary, persist_run_summary -- and if it does the
  // worker's recovery path must still be allowed to record the failure. Setting
  // the flag early made that recovery a no-op and lost the history entry.

  if (worker_failed) {
    append_log(run, "error", "Diagnostic run failed: " + failure.message);
  }

  // A summary is handed to persist_run_summary() on the failure path too, so a
  // failed run does not vanish from the history.
  Json::Value summary = run_summary_to_json(run->id, final_status, run->config, result, artifacts, duration_ms);
  // A run cut short carries partial per-test counters; plan 2.8 excludes these
  // from Pass Rate and Average Duration.
  summary["truncated"] = truncated;
  if (worker_failed) {
    summary["error"] = failure.message;
  }
  persist_run_summary(summary);
  // Only now: status resolved and persist_run_summary() returned. That call
  // does not verify the write, so this marks "finalisation attempted and
  // returned", not "durably stored" (plan 2.9).
  run->finalized.store(true, std::memory_order_relaxed);

  if (worker_failed) {
    return;
  }
  append_log(run, "info", final_status == "stopped" ? "Diagnostic run stopped." : "Diagnostic run completed.");
}

// Records a run that failed outside execute_run()'s own guarded block. Safe to
// call after execute_run() already finished: the finalized flag makes it a no-op
// so a run is never written to the history twice.
void WebServer::finalize_failed_run(const std::shared_ptr<RunState> &run, const std::string &message) {
  // Only skip if the run was already fully recorded. Checking (rather than
  // exchanging) the flag matters: execute_run() sets it only after
  // persist_run_summary() returns, so a throw before that point still reaches
  // this path.
  if (run->finalized.load(std::memory_order_relaxed)) {
    return;
  }
  std::string final_status;
  RunResult result;
  std::vector<ReportArtifact> artifacts;
  {
    std::lock_guard<std::mutex> lock(run->mutex);
    run->status = to_string(RunOutcome::Error);
    final_status = run->status;
    result = run->result;
    artifacts = run->artifacts;
  }
  Json::Value summary = run_summary_to_json(run->id, final_status, run->config, result, artifacts, /*duration_ms=*/0);
  summary["truncated"] = run_is_truncated(RunOutcome::Error);
  summary["error"] = message;
  // persist_run_summary() first, flag second -- same ordering as the success
  // path, so a throw in here leaves the run still eligible for a later attempt
  // rather than silently marked done.
  persist_run_summary(summary);
  run->finalized.store(true, std::memory_order_relaxed);
  append_log(run, "error", "Diagnostic run failed: " + message);
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
  const char *download_value = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "download");
  if (download_value && *download_value) {
    if (!query.empty()) {
      query += "&";
    }
    query += "download=" + std::string(download_value);
  }
  // MHD strips the query string from `url`, so each parameter the handlers use has to be
  // read back explicitly. `run` selects which run's kernel-log download is being named
  // (plan 3.4) -- without it here, /api/dmesg?download=1&run=... arrives with no run at
  // all and the response silently carries no filename.
  const char *run_value = MHD_lookup_connection_value(connection, MHD_GET_ARGUMENT_KIND, "run");
  if (run_value && *run_value) {
    if (!query.empty()) {
      query += "&";
    }
    query += "run=" + std::string(run_value);
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
  if (path == "/api/dmesg" && query_value(query, "download") == "1" && status_code == MHD_HTTP_OK) {
    // The name is GENERATED here from the run's own metadata (plan 3.4); the fixed
    // "dmesg.txt" is gone. The client sends a run id and nothing else -- a filename
    // arriving in a request would land in this header, where a CR/LF is header injection,
    // and would not have to match the run it claims to describe.
    //
    // Reaching a 200 already means the handler resolved the run, so this lookup cannot
    // name a run the response does not belong to.
    const std::string filename = server->dmesg_download_filename(query_value(query, "run"));
    if (!filename.empty()) {
      const std::string disposition = "attachment; filename=\"" + filename + "\"";
      MHD_add_response_header(response, "Content-Disposition", disposition.c_str());
    }
  }
  const MhdRequestResult ret = MHD_queue_response(connection, status_code, response);
  MHD_destroy_response(response);
  delete buffer;
  *con_cls = nullptr;
  return ret;
}

}  // namespace v4l2diag
