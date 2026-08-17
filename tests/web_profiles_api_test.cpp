// Regression tests over the real HTTP API, not the core helper.
//
// The earlier "disk and API agree" check called the same core function twice, so
// it could not catch a handler that mishandled the result. These start an actual
// WebServer and drive it with plain sockets.
#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/web/web_server.hpp"

#include "v4l2diag/core/config_version.hpp"
#include "v4l2diag/core/run_result_json.hpp"

#include <json/json.h>

#include <arpa/inet.h>
#include <cstring>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>

#include <cctype>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

bool check(bool condition, const std::string &what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

std::string make_temp_dir(const char *tag) {
  std::string pattern = std::string("/tmp/v4l2diag-webapi-") + tag + "-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : std::string("/tmp/v4l2diag-webapi-") + tag;
}

void write_file(const std::string &path, const std::string &text) {
  std::ofstream out(path, std::ios::trunc);
  out << text;
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in.good()) {
    return std::string();
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Response {
  int status = 0;
  std::string body;
  // The raw header block. Kept because Content-Disposition is part of the contract for
  // /api/dmesg (plan 3.4) -- the filename the browser saves under is a header, so a test
  // that only reads the body cannot see it at all.
  std::string headers;
};

// The value of a response header, or an empty string. Case-insensitive on the name, as
// HTTP is.
std::string header_value(const Response &response, const std::string &name) {
  std::string lowered_headers;
  lowered_headers.reserve(response.headers.size());
  for (char c : response.headers) {
    lowered_headers.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  std::string lowered_name;
  for (char c : name) {
    lowered_name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  const std::size_t at = lowered_headers.find(lowered_name + ":");
  if (at == std::string::npos) {
    return std::string();
  }
  const std::size_t value_start = response.headers.find(':', at) + 1;
  const std::size_t line_end = response.headers.find("\r\n", value_start);
  std::string value = response.headers.substr(value_start, line_end - value_start);
  const std::size_t first = value.find_first_not_of(" \t");
  return first == std::string::npos ? std::string() : value.substr(first);
}

// Minimal HTTP/1.1 client: enough for one request per connection, no chunking.
Response request(unsigned short port, const std::string &method, const std::string &path,
                 const std::string &body = std::string()) {
  Response out;
  const int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return out;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");
  if (connect(fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
    close(fd);
    return out;
  }

  std::ostringstream req;
  req << method << " " << path << " HTTP/1.1\r\n"
      << "Host: 127.0.0.1\r\n"
      << "Content-Type: application/json\r\n"
      << "Content-Length: " << body.size() << "\r\n"
      << "Connection: close\r\n\r\n"
      << body;
  const std::string text = req.str();
  if (send(fd, text.data(), text.size(), 0) < 0) {
    close(fd);
    return out;
  }

  std::string raw;
  char buffer[4096];
  ssize_t got = 0;
  while ((got = recv(fd, buffer, sizeof(buffer), 0)) > 0) {
    raw.append(buffer, static_cast<std::size_t>(got));
  }
  close(fd);

  const std::size_t line_end = raw.find("\r\n");
  if (line_end != std::string::npos) {
    std::istringstream status_line(raw.substr(0, line_end));
    std::string version;
    status_line >> version >> out.status;
  }
  const std::size_t split = raw.find("\r\n\r\n");
  if (split != std::string::npos) {
    out.headers = raw.substr(0, split + 2);
    out.body = raw.substr(split + 4);
  }
  return out;
}

Json::Value as_json(const std::string &text) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream in(text);
  Json::parseFromStream(builder, in, &root, &errors);
  return root;
}

std::string v2_profile(const std::string &id, bool enabled = true) {
  return R"({
  "schema_version": 2,
  "id": ")" +
         id + R"(",
  "name": "Bench-rig",
  "description": "hardware rig",
  "enabled": )" +
         (enabled ? "true" : "false") + R"(,
  "camera_match": {"driver": "uvcvideo", "card": "USB Camera", "bus_info": "usb-1"},
  "defaults": {
    "trigger_mode": "hardware", "memory_backends": ["mmap"], "test_selectors": ["stable"],
    "report_formats": ["json", "html"], "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "hardware",
     "gpio": {"chip_id": 0, "line_number": 108}}
  ],
  "camera_bindings": []
})";
}

// The current schema (v5): role-based routing, and no defaults.report_formats --
// every run writes all three artifacts (plan 2.10.2b).
std::string v4_profile(const std::string &id) {
  return R"({
  "schema_version": 5,
  "id": ")" +
         id + R"(",
  "name": "Current",
  "description": "",
  "defaults": {
    "trigger_mode": "hardware", "memory_backends": ["mmap"], "test_selectors": ["stable"],
    "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "hardware",
     "gpio": {"chip_id": 0, "line_number": 4}}
  ],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})";
}

// Adds the routing a v2/v3 draft cannot supply, exactly as the migration form
// would. Deliberately explicit: migration never derives it (plan 2.5.2).
Json::Value with_master_binding(Json::Value draft) {
  if (draft["role_bindings"].empty() && !draft["trigger_channels"].empty()) {
    Json::Value binding(Json::objectValue);
    binding["role"] = "master";
    binding["trigger_channel_id"] = draft["trigger_channels"][0]["id"];
    draft["role_bindings"] = Json::Value(Json::arrayValue);
    draft["role_bindings"].append(binding);
  }
  return draft;
}

const Json::Value *find_config(const Json::Value &configs, const std::string &file) {
  for (const auto &item : configs) {
    if (item["file"].asString() == file) {
      return &item;
    }
  }
  return nullptr;
}

}  // namespace

int main() {
  bool ok = true;

  const std::string config_dir = make_temp_dir("config");
  const std::string report_dir = make_temp_dir("reports");
  const std::string web_root = make_temp_dir("web");

  // One legacy profile, one already current, one unreadable, one from a newer build.
  write_file(config_dir + "/legacy.json", v2_profile("legacy"));
  write_file(config_dir + "/disabled.json", v2_profile("disabled", /*enabled=*/false));
  write_file(config_dir + "/current.json", v4_profile("current"));
  write_file(config_dir + "/broken.json", "{ not json");
  write_file(config_dir + "/future.json", R"({"schema_version": 99, "id": "future", "name": "Future"})");
  // At the current schema, parses fine, but a field is invalid: a software channel
  // with no fire control. Not runnable, and it needs a draft just as much as a
  // legacy config does -- the form has to show the values the user must fix.
  write_file(config_dir + "/invalid-current.json", R"({
  "schema_version": 5,
  "id": "invalid-current",
  "name": "Invalid",
  "description": "",
  "defaults": {
    "trigger_mode": "software", "memory_backends": ["mmap"], "test_selectors": ["stable"],
    "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "software",
     "control_device": {"kind": "capture"}, "setup": [], "fire": [], "teardown": []}
  ],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})");

  v4l2diag::WebServerOptions options;
  options.bind_address = "127.0.0.1";
  options.port = 18765;
  options.max_port = 18799;
  options.web_root = web_root;
  options.report_root = report_dir;
  options.config_directory = config_dir;
  options.open_browser = false;

  v4l2diag::WebServer server(options);
  std::string error;
  if (!check(server.start(&error), "the test server did not start: " + error)) {
    return 1;
  }
  const unsigned short port = server.port();

  // --- GET /api/profiles ---------------------------------------------------
  {
    const Response res = request(port, "GET", "/api/profiles");
    ok &= check(res.status == 200, "GET /api/profiles returned " + std::to_string(res.status));
    const Json::Value body = as_json(res.body);

    // Only the current profile is runnable.
    ok &= check(body["profiles"].size() == 1,
                "expected 1 runnable profile, got " + std::to_string(body["profiles"].size()));
    if (body["profiles"].size() == 1) {
      ok &= check(body["profiles"][0]["id"].asString() == "current",
                  "the runnable profile is not the current-schema one");
      ok &= check(!body["profiles"][0].isMember("enabled"), "the API still serialises the dropped \"enabled\"");
    }
    ok &= check(body["schema_version"].asInt() == v4l2diag::kProfileSchemaVersion,
                "GET /api/profiles does not advertise the schema version");

    // v5 removed the field from the schema (plan 2.10). Checked STRUCTURALLY on the
    // defaults object rather than by searching the document text: a migration report's
    // dropped[] entry legitimately still names the old field, so a text search would
    // either miss the real thing or trip over the explanation.
    if (body["profiles"].size() == 1) {
      const Json::Value &defaults = body["profiles"][0]["defaults"];
      ok &= check(defaults.isObject(), "the profile's defaults object is missing");
      ok &= check(!defaults.isMember("report_formats"), "a current profile's wire defaults still carry report_formats");
      // The neighbouring preference fields are still there, so the check is not passing
      // just because `defaults` came back empty.
      ok &= check(defaults.isMember("trigger_mode") && defaults.isMember("memory_backends") &&
                      defaults.isMember("test_selectors"),
                  "the profile's defaults lost the fields v5 kept");
    }

    // Every stored file is reported.
    ok &=
        check(body["configs"].size() == 6, "expected 6 stored configs, got " + std::to_string(body["configs"].size()));

    // A current config that fails field validation: not runnable, but the draft is
    // still sent, otherwise the migration form comes up empty for exactly the
    // configs the user has to repair.
    const Json::Value *invalid = find_config(body["configs"], "invalid-current.json");
    if (check(invalid != nullptr, "invalid-current.json was not reported")) {
      ok &= check((*invalid)["state"].asString() == "current", "invalid-current.json is not reported current");
      ok &= check(!(*invalid)["usable_for_run"].asBool(), "a config with an invalid field is advertised as runnable");
      ok &= check((*invalid)["needs_user_input"].asBool(), "an invalid config does not ask for user input");
      ok &= check((*invalid)["invalid"].size() > 0, "the invalid field was not reported");
      ok &= check((*invalid)["has_draft"].asBool(), "a current-but-invalid config advertises no draft");
      // has_draft and draft_profile come from one source, so they cannot disagree.
      ok &= check((*invalid)["has_draft"].asBool() == (*invalid).isMember("draft_profile"),
                  "has_draft disagrees with the presence of draft_profile");
      ok &= check((*invalid)["draft_profile"]["id"].asString() == "invalid-current",
                  "the draft for an invalid current config lost its values");
    }

    // A legacy profile: not runnable, needs review, carries a usable draft.
    const Json::Value *legacy = find_config(body["configs"], "legacy.json");
    if (check(legacy != nullptr, "legacy.json was not reported")) {
      ok &= check((*legacy)["state"].asString() == "legacy", "legacy.json is not reported legacy");
      ok &= check(!(*legacy)["usable_for_run"].asBool(), "a legacy config is advertised as runnable");
      ok &= check((*legacy)["needs_user_input"].asBool(), "a legacy config does not ask for review");
      ok &= check((*legacy)["has_draft"].asBool(), "a legacy config advertises no draft");
      ok &= check((*legacy)["profile_id"].asString() == "legacy", "the config carries no reliable id");
      // The draft is what prefills the migration form.
      ok &= check((*legacy).isMember("draft_profile"), "the legacy config carries no draft_profile");
      const Json::Value &draft = (*legacy)["draft_profile"];
      ok &= check(draft["id"].asString() == "legacy", "the draft lost the profile id");
      ok &= check(draft["name"].asString() == "Bench-rig", "the draft lost the stored name");
      ok &= check(draft["schema_version"].asInt() == v4l2diag::kProfileSchemaVersion,
                  "the draft is not at the current schema version");
      ok &= check(draft["trigger_channels"].size() == 1, "the draft lost the trigger channel");
      ok &= check(!draft.isMember("enabled"), "the draft reintroduces the dropped \"enabled\"");
    }

    // enabled:false must not come back active.
    const Json::Value *disabled = find_config(body["configs"], "disabled.json");
    if (check(disabled != nullptr, "disabled.json was not reported")) {
      ok &= check(!(*disabled)["usable_for_run"].asBool(), "a v2 profile with enabled:false is advertised as runnable");
      bool mentions_enabled = false;
      bool carries_old_value = false;
      for (const auto &item : (*disabled)["dropped"]) {
        const std::string text = item.asString();
        if (text.find("enabled") != std::string::npos) {
          mentions_enabled = true;
          // The old value has to be in the report. A profile deliberately hidden
          // with enabled:false must not read the same on the migration screen as
          // one that was active.
          carries_old_value = text.find("false") != std::string::npos;
        }
      }
      ok &= check(mentions_enabled, "dropping \"enabled\" was not reported over the wire");
      ok &= check(carries_old_value, "the dropped \"enabled\" report does not say it was false");
    }

    // And the enabled:true case must read differently, or the report says nothing.
    if (legacy != nullptr) {
      bool says_true = false;
      for (const auto &item : (*legacy)["dropped"]) {
        const std::string text = item.asString();
        if (text.find("enabled") != std::string::npos && text.find("true") != std::string::npos) {
          says_true = true;
        }
      }
      ok &= check(says_true, "enabled:true and enabled:false produce the same generic report text");
    }

    // Unreadable configs are reported, without a draft or an id.
    for (const auto &pair :
         std::vector<std::pair<std::string, std::string>>{{"broken.json", "malformed"}, {"future.json", "future"}}) {
      const Json::Value *entry = find_config(body["configs"], pair.first);
      if (check(entry != nullptr, pair.first + " was not reported")) {
        ok &= check((*entry)["state"].asString() == pair.second, pair.first + " is not reported " + pair.second);
        ok &= check(!(*entry)["has_draft"].asBool(), pair.first + " advertises a draft it cannot have");
        ok &= check((*entry)["profile_id"].asString().empty(), pair.first + " reports a profile id");
        ok &= check(!(*entry)["error"].asString().empty(), pair.first + " reports no error text");
      }
    }
  }

  // --- POST /api/profiles: strict about the schema version -----------------
  {
    // No schema_version at all: Malformed, with structured migration detail.
    const Response res = request(port, "POST", "/api/profiles", R"({"id": "nover", "name": "No Version"})");
    ok &= check(res.status == 400, "a schema-less POST returned " + std::to_string(res.status));
    const Json::Value body = as_json(res.body);
    ok &= check(body.isMember("migration"), "a refused POST carries no structured migration detail");
    ok &= check(body["migration"]["state"].asString() == "malformed", "a schema-less POST was not reported malformed");
    ok &= check(!body["error"].asString().empty(), "a refused POST carries no error text");
  }
  {
    // A legacy body is not saved silently.
    const Response res = request(port, "POST", "/api/profiles", v2_profile("posted-legacy"));
    ok &= check(res.status == 400, "a legacy POST returned " + std::to_string(res.status));
    const Json::Value body = as_json(res.body);
    ok &= check(body["migration"]["state"].asString() == "legacy", "a legacy POST was not reported legacy");
    ok &= check(read_file(config_dir + "/posted-legacy.json").empty(), "a legacy POST was written to disk");
  }
  {
    // A field-level problem is reported per field, not as an id error.
    std::string text = v4_profile("badline");
    const std::string from = R"("line_number": 4)";
    text.replace(text.find(from), from.size(), R"("line_number": -9)");
    const Response res = request(port, "POST", "/api/profiles", text);
    ok &= check(res.status == 400, "an invalid-field POST returned " + std::to_string(res.status));
    const Json::Value body = as_json(res.body);
    bool mentions_line = false;
    for (const auto &item : body["migration"]["invalid"]) {
      if (item.asString().find("line_number") != std::string::npos) {
        mentions_line = true;
      }
    }
    ok &= check(mentions_line, "the invalid GPIO line was not reported as a field");
  }

  // --- POST /api/profiles/import: preview, never a silent write ------------
  {
    const Response res = request(port, "POST", "/api/profiles/import", v2_profile("imported-legacy"));
    ok &= check(res.status == 200, "a legacy import returned " + std::to_string(res.status));
    const Json::Value body = as_json(res.body);
    ok &= check(body.isMember("imported") && !body["imported"].asBool(),
                "a legacy import did not report itself as not imported");
    ok &= check(body["migration"]["state"].asString() == "legacy", "a legacy import was not reported legacy");
    ok &= check(body.isMember("draft_profile"), "a legacy import returned no draft to review");
    ok &= check(body["draft_profile"]["schema_version"].asInt() == v4l2diag::kProfileSchemaVersion,
                "the import draft is not at the current schema version");
    ok &= check(read_file(config_dir + "/imported-legacy.json").empty(),
                "a legacy import was written to disk without review");
  }
  {
    const Response res =
        request(port, "POST", "/api/profiles/import", R"({"schema_version": 99, "id": "fut", "name": "Fut"})");
    ok &= check(res.status == 400, "a future import returned " + std::to_string(res.status));
    const Json::Value body = as_json(res.body);
    ok &= check(body["migration"]["state"].asString() == "future", "a future import was not reported future");
  }

  // --- Explicit save makes a migrated profile current and runnable ---------
  {
    const Json::Value listed = as_json(request(port, "GET", "/api/profiles").body);
    const Json::Value *legacy = find_config(listed["configs"], "legacy.json");
    if (check(legacy != nullptr && legacy->isMember("draft_profile"), "no draft available to save")) {
      Json::StreamWriterBuilder builder;
      // Unrouted first: v4 refuses it, which is the 2.5.2 rule over HTTP.
      const Response unrouted =
          request(port, "POST", "/api/profiles", Json::writeString(builder, (*legacy)["draft_profile"]));
      ok &= check(unrouted.status == 400,
                  "a draft with no role_bindings was accepted: " + std::to_string(unrouted.status));
      const std::string draft = Json::writeString(builder, with_master_binding((*legacy)["draft_profile"]));
      const Response saved = request(port, "POST", "/api/profiles", draft);
      ok &= check(saved.status == 200 || saved.status == 201,
                  "saving a reviewed draft returned " + std::to_string(saved.status) + ": " + saved.body);

      const Json::Value after = as_json(request(port, "GET", "/api/profiles").body);
      bool runnable = false;
      for (const auto &item : after["profiles"]) {
        if (item["id"].asString() == "legacy") {
          runnable = true;
        }
      }
      ok &= check(runnable, "the saved profile is still not runnable");
      const Json::Value *entry = find_config(after["configs"], "legacy.json");
      if (check(entry != nullptr, "legacy.json vanished after the save")) {
        ok &= check((*entry)["state"].asString() == "current", "the saved config is still reported legacy");
        ok &= check((*entry)["usable_for_run"].asBool(), "the saved config is still not runnable");
      }
      const std::string on_disk = read_file(config_dir + "/legacy.json");
      ok &= check(on_disk.find("\"enabled\"") == std::string::npos,
                  "the saved file still carries the dropped \"enabled\"");
    }
  }

  // --- An unwritable report directory fails the run ------------------------
  //
  // The guarantee is that a completed run has all three artifacts. If report writing
  // throws, the worker's exception guard must turn the run into an error -- not leave
  // it "completed" with report links pointing at files that were never written.
  {
    const std::string blocked_reports = make_temp_dir("blocked");
    const std::string blocker = blocked_reports + "/not-a-dir";
    write_file(blocker, "x");

    v4l2diag::WebServerOptions blocked_options = options;
    blocked_options.report_root = blocker;  // reports/<run> cannot be created under a file
    blocked_options.port = 18830;
    blocked_options.max_port = 18860;
    v4l2diag::WebServer blocked(blocked_options);
    std::string blocked_error;
    if (check(blocked.start(&blocked_error), "the blocked-report server did not start: " + blocked_error)) {
      Json::Value body(Json::objectValue);
      body["trigger_mode"] = "free-run";
      body["master"]["path"] = "/dev/null";
      body["test_selectors"].append("t01-device-compliance");
      Json::StreamWriterBuilder builder;
      const Response started = request(blocked.port(), "POST", "/api/runs", Json::writeString(builder, body));
      const std::string run_id = as_json(started.body)["id"].asString();
      if (check(!run_id.empty(), "the blocked-report run returned no id")) {
        Json::Value entry;
        for (int i = 0; i < 400 && entry.isNull(); i++) {
          const Json::Value listed = as_json(request(blocked.port(), "GET", "/api/runs").body);
          for (const auto &item : listed["runs"]) {
            const std::string status = item["status"].asString();
            if (item["id"].asString() == run_id &&
                (status == "completed" || status == "stopped" || status == "error")) {
              entry = item;
            }
          }
          if (entry.isNull()) {
            usleep(25 * 1000);
          }
        }
        if (check(!entry.isNull(), "the blocked-report run never reached a terminal state")) {
          ok &= check(entry["status"].asString() != "completed",
                      "a run whose reports could not be written is reported completed");
          ok &= check(entry["status"].asString() == "error",
                      "the failed run is not reported as an error: " + entry["status"].asString());
          // No phantom links: every advertised report must exist on disk.
          for (const auto &report : entry["reports"]) {
            ok &= check(false, "history advertises a report that was never written: " + report["format"].asString());
          }
          // ...but the KEY must still be there as an empty array (plan 2.7). It used to
          // be created only inside the artifact loop, so a run that wrote nothing had no
          // "reports" key at all and the Dashboard did `undefined.map(...)`.
          ok &= check(entry.isMember("reports"), "a run with no artifacts has no \"reports\" key at all");
          ok &= check(!entry["reports"].isNull(), "\"reports\" is null rather than an empty array");
          ok &= check(entry["reports"].isArray(), "\"reports\" is not an array when empty");
          ok &= check(entry["reports"].size() == 0, "\"reports\" is not empty for a run that wrote nothing");
          // Same rule for the other run-summary collections.
          for (const char *field : {"camera_paths", "slaves", "role_bindings"}) {
            ok &= check(entry.isMember(field), std::string("\"") + field + "\" key is missing from the summary");
            ok &= check(entry[field].isArray(), std::string("\"") + field + "\" is not an array");
          }
          ok &= check(entry["slaves"].size() == 0, "a single-camera run reports slaves");
          // No report_formats anywhere in the summary: structurally, not by text.
          ok &= check(!entry.isMember("report_formats"), "the run summary still carries report_formats");
          // The backend wire field the frontend RunSummary type now mirrors.
          ok &= check(entry.isMember("trigger_mode"), "the run summary lost its trigger_mode field");
          ok &= check(entry["trigger_mode"].asString() == "free-run", "the run summary reports the wrong mode");
        }
      }
      blocked.stop();
    }
    unlink(blocker.c_str());
    rmdir(blocked_reports.c_str());
  }

  // --- A software profile needs a fire control ----------------------------
  //
  // Locks the contract the create form has to satisfy: a software channel is driven
  // by a control write, so one with no fire control cannot fire anything.
  {
    const std::string software_channel = R"(
    {"id": "channel-s", "name": "Channel S", "description": "", "type": "software",
     "control_device": {"kind": "capture", "driver": "", "card": "", "bus_info": "", "sysfs_name": ""},
     "setup": [], "fire": %FIRE%, "teardown": []})";
    const std::string body = R"({
  "schema_version": 5,
  "id": "softrig",
  "name": "Soft Rig",
  "description": "",
  "defaults": {
    "trigger_mode": "software", "memory_backends": ["mmap"], "test_selectors": ["stable"],
    "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0
  },
  "trigger_channels": [%CHANNEL%],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-s"}]
})";

    const auto build = [&](const std::string &fire) {
      std::string channel = software_channel;
      channel.replace(channel.find("%FIRE%"), std::string("%FIRE%").size(), fire);
      std::string out = body;
      out.replace(out.find("%CHANNEL%"), std::string("%CHANNEL%").size(), channel);
      return out;
    };

    // Refused: no fire control.
    const Response empty = request(port, "POST", "/api/profiles", build("[]"));
    ok &= check(empty.status == 400, "a software profile with no fire control was accepted: " +
                                         std::to_string(empty.status) + " " + empty.body);
    // The message comes back through the migration report's invalid[] list, which
    // points at the field rather than giving one opaque error.
    ok &= check(empty.body.find("fire requires at least one control") != std::string::npos,
                "the refusal does not name the offending field: " + empty.body);
    ok &= check(empty.body.find("channel-s") != std::string::npos,
                "the refusal does not name the offending channel: " + empty.body);
    ok &= check(read_file(config_dir + "/softrig.json").empty(),
                "a software profile with no fire control reached the disk");

    // Accepted: one fire control, which is what the form must produce.
    const Response valid = request(port, "POST", "/api/profiles",
                                   build(R"([{"id": 10094871, "name": "Trigger Mode", "type": 2, "value": 1}])"));
    ok &= check(valid.status == 200 || valid.status == 201,
                "a valid software profile was refused: " + std::to_string(valid.status) + " " + valid.body);
    ok &= check(!read_file(config_dir + "/softrig.json").empty(), "the valid software profile was not stored");
    unlink((config_dir + "/softrig.json").c_str());
  }

  // --- Migration commit keeps the source file's identity -------------------
  //
  // POST /api/profiles writes "<profile.id>.json". The two cases below are the
  // ones it gets wrong: they left the original file on disk, so it kept being
  // reported as needing migration forever.
  {
    // File name differs from the profile id inside it.
    write_file(config_dir + "/rig-alpha.json", v2_profile("alpha"));
    const Json::Value listed = as_json(request(port, "GET", "/api/profiles").body);
    const Json::Value *entry = find_config(listed["configs"], "rig-alpha.json");
    if (check(entry != nullptr && entry->isMember("draft_profile"), "rig-alpha.json carries no draft")) {
      ok &= check((*entry)["profile_id"].asString() == "alpha",
                  "the config id does not report the profile id stored inside the file");
      Json::StreamWriterBuilder builder;
      const std::string draft = Json::writeString(builder, with_master_binding((*entry)["draft_profile"]));
      const Response committed = request(port, "PUT", "/api/profiles/configs/rig-alpha.json/migrate", draft);
      ok &= check(committed.status == 200,
                  "migrating rig-alpha.json returned " + std::to_string(committed.status) + ": " + committed.body);

      const Json::Value after = as_json(request(port, "GET", "/api/profiles").body);
      bool runnable = false;
      for (const auto &item : after["profiles"]) {
        if (item["id"].asString() == "alpha") {
          runnable = true;
        }
      }
      ok &= check(runnable, "the migrated profile is not runnable");
      // The whole point: no leftover legacy file still asking to be migrated.
      ok &= check(find_config(after["configs"], "rig-alpha.json") == nullptr,
                  "the original rig-alpha.json survived the migration and still needs migrating");
      ok &= check(find_config(after["configs"], "alpha.json") != nullptr,
                  "the migrated profile was not written under its own id");
    }
    unlink((config_dir + "/rig-alpha.json").c_str());
    unlink((config_dir + "/alpha.json").c_str());
  }

  {
    // The user changes the id on the migration form.
    write_file(config_dir + "/renamed.json", v2_profile("old-id"));
    const Json::Value listed = as_json(request(port, "GET", "/api/profiles").body);
    const Json::Value *entry = find_config(listed["configs"], "renamed.json");
    if (check(entry != nullptr && entry->isMember("draft_profile"), "renamed.json carries no draft")) {
      Json::Value edited = with_master_binding((*entry)["draft_profile"]);
      edited["id"] = "new-id";
      Json::StreamWriterBuilder builder;
      const Response committed =
          request(port, "PUT", "/api/profiles/configs/renamed.json/migrate", Json::writeString(builder, edited));
      ok &= check(committed.status == 200,
                  "migrating with a changed id returned " + std::to_string(committed.status) + ": " + committed.body);

      const Json::Value after = as_json(request(port, "GET", "/api/profiles").body);
      bool has_new = false;
      bool has_old = false;
      for (const auto &item : after["profiles"]) {
        has_new = has_new || item["id"].asString() == "new-id";
        has_old = has_old || item["id"].asString() == "old-id";
      }
      ok &= check(has_new, "the renamed profile is not runnable under its new id");
      ok &= check(!has_old, "the pre-rename id is still a runnable profile");
      ok &= check(find_config(after["configs"], "renamed.json") == nullptr,
                  "the source file survived a migration that renamed the profile");
      ok &= check(find_config(after["configs"], "new-id.json") != nullptr,
                  "the renamed profile was not written under its new id");
    }
    unlink((config_dir + "/renamed.json").c_str());
    unlink((config_dir + "/new-id.json").c_str());
  }

  // The endpoint only accepts a file it enumerated itself, so it cannot be
  // pointed outside the config directory.
  {
    const Response missing = request(port, "PUT", "/api/profiles/configs/nope.json/migrate", v4_profile("nope"));
    ok &= check(missing.status == 404, "migrating an unknown config returned " + std::to_string(missing.status));
    const Response traversal =
        request(port, "PUT", "/api/profiles/configs/..%2F..%2Fetc%2Fpasswd/migrate", v4_profile("x"));
    ok &=
        check(traversal.status == 404, "a traversing config id was not rejected: " + std::to_string(traversal.status));
  }

  // --- Only a config that needs migrating may be committed -----------------
  //
  // Membership in configs[] alone is not enough. A valid current body aimed at a
  // Future, Malformed or already-runnable config would otherwise rewrite or delete
  // that file through this endpoint.
  {
    struct Ineligible {
      const char *file;
      const char *why;
    };
    const Ineligible cases[] = {
        {"future.json", "a config from a newer build"},
        {"broken.json", "an unreadable config"},
        {"current.json", "an already-runnable config"},
    };
    for (const auto &item : cases) {
      const std::string full = config_dir + "/" + item.file;
      const std::string before = read_file(full);
      ok &= check(!before.empty(), std::string("fixture missing: ") + item.file);
      // A body that would be perfectly acceptable on its own: the refusal has to
      // come from the source's state, not from the payload.
      const Response res =
          request(port, "PUT", std::string("/api/profiles/configs/") + item.file + "/migrate", v4_profile("takeover"));
      ok &= check(res.status == 409, std::string("migrating ") + item.why + " returned " + std::to_string(res.status) +
                                         " instead of 409: " + res.body);
      // Byte for byte: not rewritten, not deleted.
      ok &= check(read_file(full) == before,
                  std::string("migrating ") + item.why + " (" + item.file + ") modified the file");
      ok &= check(!read_file(full).empty(), std::string("migrating ") + item.why + " deleted " + item.file);
      // And nothing was written under the id the request tried to claim.
      ok &= check(read_file(config_dir + "/takeover.json").empty(),
                  std::string("migrating ") + item.why + " wrote a new profile anyway");
      // Restore, so each case is judged against its own fixture rather than
      // against whatever an earlier failing case left behind.
      write_file(full, before);
      unlink((config_dir + "/takeover.json").c_str());
    }
  }

  // --- A rename must not overwrite another stored config -------------------
  {
    // The target is a config from a newer build: invisible to get_profile(), so a
    // runnable-only clash check never sees it, yet the rename would rename over it.
    write_file(config_dir + "/src-a.json", v2_profile("src-a"));
    write_file(config_dir + "/blocker.json", R"({"schema_version": 99, "id": "blocker", "name": "Newer"})");
    const std::string blocker_before = read_file(config_dir + "/blocker.json");

    const Json::Value listed = as_json(request(port, "GET", "/api/profiles").body);
    const Json::Value *entry = find_config(listed["configs"], "src-a.json");
    if (check(entry != nullptr && entry->isMember("draft_profile"), "src-a.json carries no draft")) {
      Json::Value edited = with_master_binding((*entry)["draft_profile"]);
      edited["id"] = "blocker";  // would land on blocker.json
      Json::StreamWriterBuilder builder;
      const Response res =
          request(port, "PUT", "/api/profiles/configs/src-a.json/migrate", Json::writeString(builder, edited));
      ok &= check(res.status == 409,
                  "renaming onto an unreadable config returned " + std::to_string(res.status) + ": " + res.body);
      ok &= check(read_file(config_dir + "/blocker.json") == blocker_before,
                  "the migration overwrote a config that was invisible to the runnable list");
      ok &= check(!read_file(config_dir + "/src-a.json").empty(), "the refused migration removed its source");
    }
    unlink((config_dir + "/src-a.json").c_str());
    unlink((config_dir + "/blocker.json").c_str());
  }

  {
    // Source and an unrelated Current config share a profile id. The old check
    // compared profile.id against the source's own id, so it never fired here.
    write_file(config_dir + "/twin-legacy.json", v2_profile("twin"));
    write_file(config_dir + "/twin.json", v4_profile("twin"));
    const std::string twin_before = read_file(config_dir + "/twin.json");

    const Json::Value listed = as_json(request(port, "GET", "/api/profiles").body);
    const Json::Value *entry = find_config(listed["configs"], "twin-legacy.json");
    if (check(entry != nullptr && entry->isMember("draft_profile"), "twin-legacy.json carries no draft")) {
      Json::StreamWriterBuilder builder;
      // id unchanged ("twin"), so this is the id-collision case, not a rename.
      const Response res = request(port, "PUT", "/api/profiles/configs/twin-legacy.json/migrate",
                                   Json::writeString(builder, with_master_binding((*entry)["draft_profile"])));
      ok &= check(res.status == 409, "migrating onto another config's profile id returned " +
                                         std::to_string(res.status) + ": " + res.body);
      ok &= check(read_file(config_dir + "/twin.json") == twin_before,
                  "the migration overwrote a different config that already defined this profile id");
    }
    unlink((config_dir + "/twin-legacy.json").c_str());
    unlink((config_dir + "/twin.json").c_str());
  }

  {
    // Same collision, but the write is IN PLACE: source "inplace.json" holds
    // profile "inplace", so the target path is its own file. The target-path check
    // is legitimately skipped there -- but the id check must not be, and guarding
    // the whole loop with !in_place made it dead in exactly this case.
    write_file(config_dir + "/inplace.json", v2_profile("inplace"));
    write_file(config_dir + "/owner.json", v4_profile("inplace"));
    const std::string source_before = read_file(config_dir + "/inplace.json");
    const std::string owner_before = read_file(config_dir + "/owner.json");

    const Json::Value listed = as_json(request(port, "GET", "/api/profiles").body);
    const Json::Value *entry = find_config(listed["configs"], "inplace.json");
    if (check(entry != nullptr && entry->isMember("draft_profile"), "inplace.json carries no draft")) {
      ok &= check((*entry)["profile_id"].asString() == "inplace",
                  "the fixture is not the in-place case: file name and profile id must match");
      Json::StreamWriterBuilder builder;
      const Response res = request(port, "PUT", "/api/profiles/configs/inplace.json/migrate",
                                   Json::writeString(builder, with_master_binding((*entry)["draft_profile"])));
      ok &= check(res.status == 409, "an in-place migration onto another config's profile id returned " +
                                         std::to_string(res.status) + ": " + res.body);
      ok &= check(read_file(config_dir + "/inplace.json") == source_before,
                  "the refused in-place migration rewrote its source");
      ok &= check(read_file(config_dir + "/owner.json") == owner_before,
                  "the refused in-place migration touched the config that owns the id");
    }
    unlink((config_dir + "/inplace.json").c_str());
    unlink((config_dir + "/owner.json").c_str());
  }

  // --- A run is refused for the same reason the CLI refuses it -------------
  //
  // The API and the CLI share resolve_role_bindings(), so an incomplete routing has
  // to come back with the same words rather than two hand-written messages.
  {
    // "current" binds master only. A two-camera run expects master and slave-1.
    Json::Value body(Json::objectValue);
    body["trigger_mode"] = "hardware";
    body["trigger_profile_id"] = "current";
    body["master"]["path"] = "/dev/null";
    Json::Value slave(Json::objectValue);
    slave["path"] = "/dev/zero";
    body["slaves"].append(slave);
    body["test_selectors"].append("t01-device-compliance");
    Json::StreamWriterBuilder builder;
    const Response res = request(port, "POST", "/api/runs", Json::writeString(builder, body));
    ok &= check(res.status == 400,
                "a run with an unbound slave-1 was accepted: " + std::to_string(res.status) + " " + res.body);
    ok &=
        check(res.body.find("slave-1") != std::string::npos, "the refusal does not name the missing role: " + res.body);
    // Exactly the shared resolver wording, so the two callers cannot drift.
    ok &= check(res.body.find("does not bind these run roles") != std::string::npos,
                "the API is not using the shared resolver wording: " + res.body);
  }

  {
    // "current" holds a HARDWARE channel, so a software-trigger run against it must
    // be refused -- with the same words the CLI uses, since both go through
    // describe_mode_mismatch().
    Json::Value body(Json::objectValue);
    body["trigger_mode"] = "software";
    body["trigger_profile_id"] = "current";
    body["master"]["path"] = "/dev/null";
    body["test_selectors"].append("t01-device-compliance");
    Json::StreamWriterBuilder builder;
    const Response res = request(port, "POST", "/api/runs", Json::writeString(builder, body));
    ok &= check(res.status == 400, "a hardware channel was accepted for a software run: " + res.body);
    ok &= check(res.body.find("hardware channel") != std::string::npos,
                "the refusal does not describe the channel/mode mismatch: " + res.body);
  }

  {
    // Free-run plus a trigger profile is contradictory. Normalised centrally, so the
    // run that comes back reports free-run and NO profile rather than both.
    Json::Value body(Json::objectValue);
    body["trigger_mode"] = "free-run";
    body["trigger_profile_id"] = "current";
    body["master"]["path"] = "/dev/null";
    body["test_selectors"].append("t01-device-compliance");
    Json::StreamWriterBuilder builder;
    const Response res = request(port, "POST", "/api/runs", Json::writeString(builder, body));
    ok &= check(res.status == 200 || res.status == 202,
                "a free-run request was refused: " + std::to_string(res.status) + " " + res.body);
    const Json::Value started = as_json(res.body);
    const std::string run_id = started["id"].asString();
    if (check(!run_id.empty(), "the free-run request returned no run id")) {
      // The summary only enters the history once the run reaches a terminal state,
      // so wait for that rather than polling a window that may not have opened yet.
      Json::Value entry;
      for (int i = 0; i < 400 && entry.isNull(); i++) {
        const Json::Value listed = as_json(request(port, "GET", "/api/runs").body);
        for (const auto &item : listed["runs"]) {
          const std::string status = item["status"].asString();
          if (item["id"].asString() == run_id && (status == "completed" || status == "stopped" || status == "error")) {
            entry = item;
          }
        }
        if (entry.isNull()) {
          usleep(25 * 1000);
        }
      }
      if (check(!entry.isNull(), "the free-run run never reached a terminal state in the run list")) {
        ok &= check(entry["trigger_mode"].asString() == "free-run", "the run did not report free-run");
        ok &= check(entry["trigger_profile_id"].asString().empty(),
                    "a free-run run reports a trigger profile: \"" + entry["trigger_profile_id"].asString() + "\"");
        ok &= check(entry["role_bindings"].empty(), "a free-run run reports role bindings");
      }
    }
  }

  // --- GET /api/runs/{id} carries the run-level trigger contract -----------
  {
    // The detail serializer must state the routing once, at run level, and the
    // camera object must be exactly role + path + tests. It used to repeat
    // trigger_mode/trigger_rate_hz/pulse_width_ms per camera.
    Json::Value body(Json::objectValue);
    body["trigger_mode"] = "free-run";
    body["master"]["path"] = "/dev/null";
    body["test_selectors"].append("t01-device-compliance");
    Json::StreamWriterBuilder builder;
    const Response started = request(port, "POST", "/api/runs", Json::writeString(builder, body));
    const std::string run_id = as_json(started.body)["id"].asString();
    if (check(!run_id.empty(), "the detail-check run returned no id")) {
      Json::Value detail;
      for (int i = 0; i < 400; i++) {
        detail = as_json(request(port, "GET", "/api/runs/" + run_id).body);
        const std::string status = detail["status"].asString();
        if (status == "completed" || status == "stopped" || status == "error") {
          break;
        }
        usleep(25 * 1000);
      }
      if (check(detail.isMember("result"), "the run detail carries no result object")) {
        const Json::Value &run_result = detail["result"];
        ok &= check(run_result.isMember("trigger_mode"), "the result is missing run-level trigger_mode");
        ok &= check(run_result.isMember("trigger_profile_id"), "the result is missing run-level trigger_profile_id");
        ok &= check(run_result.isMember("role_bindings"), "the result is missing run-level role_bindings");
        ok &= check(run_result["trigger_mode"].asString() == "free-run", "the result lost the trigger mode");
        // Free-run routes nothing.
        ok &= check(run_result["trigger_profile_id"].asString().empty(), "a free-run result names a trigger profile");
        ok &= check(run_result["role_bindings"].empty(), "a free-run result carries role bindings");
        // The structured result never had the field, and must not gain one.
        ok &= check(!run_result.isMember("report_formats"), "the structured result carries report_formats");
        for (const auto &camera_obj : run_result["cameras"]) {
          ok &= check(!camera_obj.isMember("report_formats"), "a result camera object carries report_formats");
        }
        // Not RENDERED under free-run: RunResult still holds default numbers (they
        // are plain doubles), but nothing is driven so the values are meaningless and
        // must not reach the wire. Same rule in JSON/Markdown/HTML.
        ok &= check(!run_result.isMember("trigger_rate_hz"), "a free-run result reports a trigger rate");
        ok &= check(!run_result.isMember("pulse_width_ms"), "a free-run result reports a pulse width");

        if (check(run_result["cameras"].size() == 1, "the result camera array is not as expected")) {
          const Json::Value &camera = run_result["cameras"][0];
          ok &= check(camera.isMember("role"), "the result camera object has no role");
          ok &= check(camera["camera_path"].asString() == "/dev/null", "the result camera lost its path");
          ok &= check(camera.isMember("tests"), "the result camera lost its tests");
          // Each of these is run-level; repeating it per camera is what let two
          // copies disagree.
          for (const char *field : {"profile_id", "trigger_channel_id", "trigger_mode", "trigger_profile_id",
                                    "role_bindings", "trigger_rate_hz", "pulse_width_ms"}) {
            ok &= check(!camera.isMember(field),
                        std::string("the result camera object still carries \"") + field + "\", which is run-level");
          }
        }
      }
    }
  }

  {
    // A triggered run needs the run-level profile; free-run does not route at all.
    Json::Value body(Json::objectValue);
    body["trigger_mode"] = "hardware";
    body["master"]["path"] = "/dev/null";
    Json::StreamWriterBuilder builder;
    const Response res = request(port, "POST", "/api/runs", Json::writeString(builder, body));
    ok &= check(res.status == 400, "a triggered run with no trigger profile was accepted: " + res.body);
    ok &= check(res.body.find("trigger profile") != std::string::npos,
                "the refusal does not mention the missing trigger profile: " + res.body);
  }

  // --- The dmesg endpoint is GONE (plan 5.4) -------------------------------
  {
    // DMESG became a produced artifact (3.4), so nothing calls this endpoint any more:
    // measured 2026-08-08, zero references in the frontend and in the report HTML. An
    // endpoint nobody calls is still reachable, so the surface it exposed -- running
    // journalctl, resolving a run id, writing a Content-Disposition header -- is removed
    // rather than left guarded.
    //
    // The ten security assertions that used to live here went with it. They protected a
    // path that no longer exists; keeping them would have tested a handler the server
    // does not have.
    for (const char *dmesg_path :
         {"/api/dmesg", "/api/dmesg?download=1&run=web-run-7", "/api/dmesg?download=1&run=../../etc/passwd"}) {
      const Response response = request(port, "GET", dmesg_path);
      ok &= check(response.status == 404, std::string("the removed dmesg endpoint still answers (status ") +
                                              std::to_string(response.status) + "): " + dmesg_path);
      // Nothing leaks through the 404 either: no kernel log, no download header.
      ok &= check(response.body.find("kernel:") == std::string::npos &&
                      response.body.find("Linux version") == std::string::npos,
                  std::string("a 404 still returned kernel log content: ") + dmesg_path);
      ok &= check(header_value(response, "Content-Disposition").empty(),
                  std::string("a 404 still produced a download header: ") + dmesg_path);
    }

    // /api/profiles/validate goes with it (plan 5.4): implemented, never called by the
    // frontend, and covered by no test. POST /api/profiles still validates -- it shares
    // the same code path -- so nothing that was checked stops being checked.
    const Response validate = request(port, "POST", "/api/profiles/validate", v4_profile("probe"));
    ok &= check(validate.status == 404,
                "the removed validate endpoint still answers (status " + std::to_string(validate.status) + ")");
  }

  server.stop();

  // --- Restart fallback: the structured result survives a restart ----------
  //
  // find_run() only sees the in-memory list, so before this every historical run
  // returned 404 -- and plan 2.6 relies on reading the canonical JSON back. The run id
  // is NEVER pasted into a path: the artifact is located through the runs-index entry
  // the server itself wrote.
  {
    const std::string persist_reports = make_temp_dir("persist");
    std::string canonical_run_id;

    // Phase 1: a real run, on a server that then goes away.
    {
      v4l2diag::WebServerOptions first = options;
      first.report_root = persist_reports;
      first.port = 18870;
      first.max_port = 18899;
      v4l2diag::WebServer server_a(first);
      std::string start_error;
      if (check(server_a.start(&start_error), "the persist server did not start: " + start_error)) {
        Json::Value body(Json::objectValue);
        body["trigger_mode"] = "free-run";
        body["master"]["path"] = "/dev/null";
        body["test_selectors"].append("t01-device-compliance");
        Json::StreamWriterBuilder builder;
        const Response started = request(server_a.port(), "POST", "/api/runs", Json::writeString(builder, body));
        canonical_run_id = as_json(started.body)["id"].asString();
        ok &= check(!canonical_run_id.empty(), "the persisted run returned no id");
        for (int i = 0; i < 400; i++) {
          const Json::Value detail = as_json(request(server_a.port(), "GET", "/api/runs/" + canonical_run_id).body);
          const std::string status = detail["status"].asString();
          if (status == "completed" || status == "stopped" || status == "error") {
            break;
          }
          usleep(25 * 1000);
        }
        server_a.stop();
      }
    }

    // Phase 2: a brand-new server over the same report root. Nothing is in memory.
    {
      v4l2diag::WebServerOptions second = options;
      second.report_root = persist_reports;
      second.port = 18900;
      second.max_port = 18929;
      v4l2diag::WebServer server_b(second);
      std::string start_error;
      if (check(server_b.start(&start_error), "the restarted server did not start: " + start_error) &&
          check(!canonical_run_id.empty(), "no run id to look up after the restart")) {
        const unsigned short port_b = server_b.port();

        // The structured result comes back, from disk, in the SAME contract.
        const Response detail = request(port_b, "GET", "/api/runs/" + canonical_run_id);
        ok &= check(detail.status == 200,
                    "a historical run returned " + std::to_string(detail.status) + " after a restart: " + detail.body);
        const Json::Value restored = as_json(detail.body);
        ok &= check(restored["id"].asString() == canonical_run_id, "the restored run reports a different id");
        ok &= check(restored.isMember("result"), "the restored run carries no result object");
        ok &= check(restored["result"]["result_schema_version"].asInt() == v4l2diag::kResultSchemaVersion,
                    "the restored result is not the canonical schema");
        // Not an empty stand-in: the run really happened and its camera is there.
        ok &= check(restored["result"]["cameras"].size() == 1,
                    "the restored result has no cameras; an empty result was served");
        ok &= check(restored["result"]["cameras"][0]["camera_path"].asString() == "/dev/null",
                    "the restored camera lost its path");
        ok &= check(restored["result"]["trigger_mode"].asString() == "free-run",
                    "the restored result lost its trigger mode");

        // /reports works from the index metadata rather than from memory.
        const Response reports = request(port_b, "GET", "/api/runs/" + canonical_run_id + "/reports");
        ok &= check(reports.status == 200,
                    "historical /reports returned " + std::to_string(reports.status) + ": " + reports.body);
        ok &= check(as_json(reports.body)["reports"].size() == 3,
                    "historical /reports does not list the three artifacts");

        // /logs is not persisted, and says so instead of claiming the run is missing.
        const Response logs = request(port_b, "GET", "/api/runs/" + canonical_run_id + "/logs");
        ok &= check(logs.body.find("historical_logs_unavailable") != std::string::npos,
                    "historical /logs did not report historical_logs_unavailable: " + logs.body);
        ok &= check(logs.body.find("run not found") == std::string::npos,
                    "historical /logs claimed the run does not exist: " + logs.body);

        // An id that is in no index entry stays a 404 -- the lookup is index-driven,
        // so it cannot be steered at a path.
        for (const char *bogus : {"no-such-run", "..", "../..", "/etc/passwd", "web-run-"}) {
          const Response res = request(port_b, "GET", std::string("/api/runs/") + bogus);
          ok &= check(res.status == 404 || res.status == 400, std::string("an id absent from the index returned ") +
                                                                  std::to_string(res.status) + " for \"" + bogus +
                                                                  "\": " + res.body);
          ok &= check(res.body.find("cameras") == std::string::npos,
                      std::string("a result was served for an id absent from the index: ") + bogus);
        }
        server_b.stop();
      }
    }

    // Phase 2a: the in-memory run wins over the on-disk artifact.
    {
      // Same server, same run, but the artifact on disk is replaced with a DIFFERENT
      // result. The live run is still in memory, so that is what must be served --
      // otherwise a stale or tampered file would override the run that just executed.
      v4l2diag::WebServerOptions live = options;
      live.report_root = make_temp_dir("live");
      live.port = 19020;
      live.max_port = 19049;
      v4l2diag::WebServer server_m(live);
      std::string start_error;
      if (check(server_m.start(&start_error), "the memory-priority server did not start: " + start_error)) {
        Json::Value body(Json::objectValue);
        body["trigger_mode"] = "free-run";
        body["master"]["path"] = "/dev/null";
        body["test_selectors"].append("t01-device-compliance");
        Json::StreamWriterBuilder builder;
        const Response started = request(server_m.port(), "POST", "/api/runs", Json::writeString(builder, body));
        const std::string live_id = as_json(started.body)["id"].asString();
        if (check(!live_id.empty(), "the live run returned no id")) {
          for (int i = 0; i < 400; i++) {
            const Json::Value detail = as_json(request(server_m.port(), "GET", "/api/runs/" + live_id).body);
            const std::string status = detail["status"].asString();
            if (status == "completed" || status == "stopped" || status == "error") {
              break;
            }
            usleep(25 * 1000);
          }
          // Tamper with the artifact while the run is still in memory.
          const std::string run_dir = live.report_root + "/web-run-" + live_id;
          if (DIR *dir = opendir(run_dir.c_str())) {
            while (dirent *entry = readdir(dir)) {
              const std::string name = entry->d_name;
              if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
                write_file(run_dir + "/" + name, R"({
  "result_schema_version": 1, "project_name": "TAMPERED", "run_mode": "sequential",
  "trigger_mode": "free-run", "cameras": []
})");
              }
            }
            closedir(dir);
          }
          const Json::Value served = as_json(request(server_m.port(), "GET", "/api/runs/" + live_id).body)["result"];
          ok &=
              check(served["project_name"].asString() != "TAMPERED", "the on-disk artifact overrode the in-memory run");
          ok &= check(served["cameras"].size() == 1,
                      "the in-memory run's cameras were replaced by the artifact's empty list");
        }
        server_m.stop();
      }
    }

    // Phase 2b: /reports must not serve anything outside the report root.
    {
      v4l2diag::WebServerOptions guard = options;
      guard.report_root = persist_reports;
      guard.port = 18960;
      guard.max_port = 18989;
      v4l2diag::WebServer server_g(guard);
      std::string start_error;
      if (check(server_g.start(&start_error), "the traversal-guard server did not start: " + start_error)) {
        const unsigned short port_g = server_g.port();

        // A secret outside the report root, and a symlink to it inside a run directory.
        const std::string outside = make_temp_dir("outside");
        write_file(outside + "/secret.json", "{\"secret\": true}");
        const std::string run_dir = persist_reports + "/web-run-" + canonical_run_id;
        const std::string link = run_dir + "/leak.json";
        ok &= check(symlink((outside + "/secret.json").c_str(), link.c_str()) == 0,
                    "could not stage a symlink out of the report root");

        struct Attempt {
          const char *path;
          const char *what;
        };
        const Attempt attempts[] = {
            {"/reports/../../etc/passwd", "a .. traversal"},
            {"/reports/..%2f..%2fetc%2fpasswd", "an encoded .. traversal"},
            {"//etc/passwd", "an absolute path"},
            {"/reports//etc/passwd", "an absolute path after the prefix"},
        };
        for (const auto &attempt : attempts) {
          const Response res = request(port_g, "GET", attempt.path);
          ok &= check(res.body.find("root:") == std::string::npos,
                      std::string("a system file was served through ") + attempt.what);
          ok &= check(res.status != 200 || res.body.empty(),
                      std::string("a traversal returned 200: ") + attempt.what + " -> " + attempt.path);
        }
        // The symlink resolves outside, so it must not be served even though its path
        // string contains no "..".
        const Response leaked = request(port_g, "GET", "/reports/" + canonical_run_id + "/leak.json");
        ok &= check(leaked.body.find("secret") == std::string::npos,
                    "a symlink pointing out of the report root was served: " + leaked.body);
        ok &= check(leaked.status == 404,
                    "a symlink out of the report root returned " + std::to_string(leaked.status) + " instead of 404");

        // A file sitting in the run directory that the RECORD never declared. Present
        // on disk, so a filesystem-driven lookup would happily serve it; the record is
        // the authority.
        write_file(run_dir + "/undeclared.json", "{\"planted\": true}");
        const Response undeclared = request(port_g, "GET", "/reports/" + canonical_run_id + "/undeclared.json");
        ok &= check(undeclared.status == 404,
                    "an undeclared file present in the run directory was served: " + std::to_string(undeclared.status));
        ok &= check(undeclared.body.find("planted") == std::string::npos,
                    "an undeclared file's contents were served: " + undeclared.body);
        unlink((run_dir + "/undeclared.json").c_str());

        // A symlink INSIDE the report root, pointing at another run's directory.
        //
        // Deliberately replaces a DECLARED artifact: an undeclared name is stopped by
        // the record check, so it would never exercise the directory boundary. The
        // root-boundary check alone passes this and serves another run's result under
        // this id.
        {
          // Find a filename this run's record really advertises.
          const Json::Value reports_now =
              as_json(request(port_g, "GET", "/api/runs/" + canonical_run_id + "/reports").body);
          std::string declared_json;
          for (const auto &report : reports_now["reports"]) {
            const std::string url = report["url"].asString();
            const std::size_t slash = url.find_last_of('/');
            const std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
            if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
              declared_json = name;
            }
          }
          if (check(!declared_json.empty(), "the run's record declares no JSON artifact to hijack")) {
            const std::string other_dir = persist_reports + "/web-run-other-run";
            ok &= check(mkdir(other_dir.c_str(), 0755) == 0, "could not stage a second run directory");
            write_file(other_dir + "/diagnostic-report.json",
                       R"({"result_schema_version": 1, "project_name": "OTHER-RUN", "cameras": []})");

            const std::string hijacked = run_dir + "/" + declared_json;
            const std::string saved = read_file(hijacked);
            unlink(hijacked.c_str());
            ok &= check(symlink((other_dir + "/diagnostic-report.json").c_str(), hijacked.c_str()) == 0,
                        "could not stage a cross-run symlink over a declared artifact");

            // Both routes must refuse it: the file download and the structured result.
            const Response crossed = request(port_g, "GET", "/reports/" + canonical_run_id + "/" + declared_json);
            ok &= check(crossed.body.find("OTHER-RUN") == std::string::npos,
                        "a symlink into another run's directory was served: " + crossed.body);
            ok &= check(crossed.status == 404,
                        "a cross-run symlink returned " + std::to_string(crossed.status) + " instead of 404");
            const Response crossed_result = request(port_g, "GET", "/api/runs/" + canonical_run_id);
            ok &= check(crossed_result.body.find("OTHER-RUN") == std::string::npos,
                        "the structured result came from another run's artifact: " + crossed_result.body);

            unlink(hijacked.c_str());
            write_file(hijacked, saved);
            unlink((other_dir + "/diagnostic-report.json").c_str());
            rmdir(other_dir.c_str());
          }
        }

        // An unknown id, and a prefix of a real one, cannot download anything.
        for (const std::string &wrong_id :
             {std::string("no-such-run"), canonical_run_id.substr(0, canonical_run_id.size() - 2),
              canonical_run_id + "x"}) {
          const Response res = request(port_g, "GET", "/reports/" + wrong_id + "/diagnostic-report.json");
          ok &= check(res.status == 404, "report download with id \"" + wrong_id + "\" returned " +
                                             std::to_string(res.status) + " instead of 404");
          ok &= check(res.body.find("result_schema_version") == std::string::npos,
                      "a report was served for id \"" + wrong_id + "\": " + res.body);
        }

        // Index/run-id mismatch, both directions. The lookup must be an EXACT match:
        // a prefix comparison would let a truncated id select a real entry, and a
        // longer one select an entry it does not name.
        ok &= check(canonical_run_id.size() > 4, "the run id is too short to truncate for this check");
        for (const std::string &wrong :
             {canonical_run_id + "x", canonical_run_id.substr(0, canonical_run_id.size() - 2)}) {
          const Response mismatch = request(port_g, "GET", "/api/runs/" + wrong);
          ok &= check(mismatch.status == 404,
                      "id \"" + wrong + "\" returned " + std::to_string(mismatch.status) + " instead of 404");
          ok &= check(mismatch.body.find("cameras") == std::string::npos,
                      "a result was served for mismatched id \"" + wrong + "\": " + mismatch.body);
          ok &= check(mismatch.body.find("structured_result_unavailable") == std::string::npos,
                      "a mismatched id \"" + wrong + "\" was treated as a known run: " + mismatch.body);
        }

        unlink(link.c_str());
        unlink((outside + "/secret.json").c_str());
        rmdir(outside.c_str());
        server_g.stop();
      }
    }

    // Phase 2c: a LEGACY artifact still restores, through the adapter.
    {
      // Overwrite the canonical JSON with what an earlier build wrote: "project"
      // instead of "project_name", per-camera "path", and no run-level trigger
      // contract. The known names are mapped; the absent ones are NOT invented.
      const std::string run_dir = persist_reports + "/web-run-" + canonical_run_id;
      std::string json_path;
      if (DIR *dir = opendir(run_dir.c_str())) {
        while (dirent *entry = readdir(dir)) {
          const std::string name = entry->d_name;
          if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
            json_path = run_dir + "/" + name;
          }
        }
        closedir(dir);
      }
      if (check(!json_path.empty(), "the persisted run wrote no JSON artifact")) {
        write_file(json_path, R"({
  "project": "v4l2-camera-diagnostic",
  "started_at_utc": "2026-01-01T00:00:00Z",
  "finished_at_utc": "2026-01-01T00:01:00Z",
  "host_name": "old-host",
  "run_mode": "sequential",
  "cameras": [
    {"path": "/dev/video9", "profile_id": "legacy-profile", "trigger_channel_id": "legacy-channel",
     "trigger_mode": "hardware", "trigger_rate_hz": 30.0, "pulse_width_ms": 13.0,
     "memory_backends": ["mmap"], "tests": []}
  ]
})");
        v4l2diag::WebServerOptions legacy_options = options;
        legacy_options.report_root = persist_reports;
        legacy_options.port = 18990;
        legacy_options.max_port = 19019;
        v4l2diag::WebServer server_l(legacy_options);
        std::string start_error;
        if (check(server_l.start(&start_error), "the legacy-artifact server did not start: " + start_error)) {
          const Response res = request(server_l.port(), "GET", "/api/runs/" + canonical_run_id);
          ok &= check(res.status == 200, "a legacy artifact returned " + std::to_string(res.status) + ": " + res.body);
          const Json::Value restored = as_json(res.body)["result"];
          ok &= check(restored["project_name"].asString() == "v4l2-camera-diagnostic",
                      "legacy \"project\" was not mapped over HTTP");
          if (check(restored["cameras"].size() == 1, "the legacy camera was lost over HTTP")) {
            ok &= check(restored["cameras"][0]["camera_path"].asString() == "/dev/video9",
                        "legacy \"path\" was not mapped over HTTP");
          }
          // Nothing invented: the legacy file had no run-level contract.
          ok &= check(restored["trigger_profile_id"].asString().empty(),
                      "a trigger_profile_id was invented from a legacy artifact");
          ok &= check(restored["role_bindings"].empty(), "role_bindings were invented from a legacy artifact");
          ok &= check(!restored.isMember("trigger_rate_hz"),
                      "trigger timing was lifted from a legacy artifact's per-camera fields");
          server_l.stop();
        }
      }
    }

    // Phase 3: the index entry survives but its JSON artifact does not.
    {
      // Remove only the JSON. The run is still known, so the answer must say the
      // structured result is unavailable -- not that the run does not exist, and not
      // an empty result.
      const std::string run_dir = persist_reports + "/web-run-" + canonical_run_id;
      const Json::Value listing = as_json(std::string("{}"));
      (void)listing;
      std::string json_path;
      if (DIR *dir = opendir(run_dir.c_str())) {
        while (dirent *entry = readdir(dir)) {
          const std::string name = entry->d_name;
          if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
            json_path = run_dir + "/" + name;
          }
        }
        closedir(dir);
      }
      if (check(!json_path.empty(), "the persisted run wrote no JSON artifact")) {
        unlink(json_path.c_str());
        v4l2diag::WebServerOptions third = options;
        third.report_root = persist_reports;
        third.port = 18930;
        third.max_port = 18959;
        v4l2diag::WebServer server_c(third);
        std::string start_error;
        if (check(server_c.start(&start_error), "the third server did not start: " + start_error)) {
          const Response res = request(server_c.port(), "GET", "/api/runs/" + canonical_run_id);
          ok &= check(res.body.find("structured_result_unavailable") != std::string::npos,
                      "a missing JSON artifact did not report structured_result_unavailable: " + res.body);
          ok &= check(res.body.find("run not found") == std::string::npos,
                      "a known run with no artifact was reported as missing: " + res.body);
          ok &= check(res.body.find("\"cameras\"") == std::string::npos,
                      "an empty result was served instead of an explicit error: " + res.body);
          server_c.stop();
        }
      }
    }
  }

  // --- Positive control: a supported index IS written ----------------------
  //
  // Without this, the "unchanged" assertions below could pass simply because the
  // write path is never reached, which would make the guard look load-bearing
  // when it is not.
  {
    const std::string dir = make_temp_dir("index-ok");
    const std::string index = dir + "/runs-index.json";

    v4l2diag::WebServerOptions opts = options;
    opts.report_root = dir;
    opts.port = 18860;
    opts.max_port = 18880;
    v4l2diag::WebServer writable(opts);
    std::string start_error;
    if (check(writable.start(&start_error), "the control server did not start: " + start_error)) {
      const Response started = request(writable.port(), "POST", "/api/runs",
                                       R"({"master": {"path": "/dev/null"}, "trigger_mode": "free-run",
                                           "test_selectors": ["t01-device-compliance"],
                                           "report_formats": ["json"]})");
      ok &= check(started.status == 200 || started.status == 202,
                  "the control run was not accepted: " + std::to_string(started.status) + " " + started.body);
      // Poll the file: the run status is set before the history write, so waiting
      // on status alone races the write.
      bool written = false;
      for (int i = 0; i < 200 && !written; i++) {
        written = !read_file(index).empty();
        if (!written) {
          usleep(50 * 1000);
        }
      }
      writable.stop();
      ok &= check(written, "a completed run never wrote runs-index.json, so the write path is unproven");
      if (written) {
        const Json::Value document = as_json(read_file(index));
        ok &= check(document["schema_version"].asInt() == v4l2diag::kRunsIndexSchemaVersion,
                    "the written index carries no schema version");
        ok &= check(document["runs"].isArray() && document["runs"].size() >= 1,
                    "the written index carries no run entries");
      }
    }
    unlink(index.c_str());
    rmdir(dir.c_str());
  }

  // --- An unsupported runs-index.json is never truncated -------------------
  for (const auto &pair : std::vector<std::pair<std::string, std::string>>{
           {"future", R"({"schema_version": 99, "runs": [{"id": "old-run"}]})"},
           {"malformed", R"({"schema_version": 1, "runs": "not an array"})"},
           {"garbage", "{ not json at all"}}) {
    const std::string dir = make_temp_dir("index");
    const std::string index = dir + "/runs-index.json";
    write_file(index, pair.second);
    const std::string before = read_file(index);

    v4l2diag::WebServerOptions opts = options;
    opts.report_root = dir;
    opts.port = 18801;
    opts.max_port = 18850;
    v4l2diag::WebServer guarded(opts);
    std::string start_error;
    if (check(guarded.start(&start_error), "the guard server did not start: " + start_error)) {
      ok &= check(read_file(index) == before,
                  "an unsupported runs-index.json (" + pair.first + ") was modified on startup");

      // Now actually complete a run, which is what writes the index. The run
      // itself fails (no camera in a test environment) but it still reaches
      // persist_run_summary -- the write that must be refused.
      const Response started = request(guarded.port(), "POST", "/api/runs",
                                       R"({"master": {"path": "/dev/null"}, "trigger_mode": "free-run",
                                           "test_selectors": ["t01-device-compliance"],
                                           "report_formats": ["json"]})");
      // The run must be accepted, otherwise the write path is never reached and
      // the "unchanged" assertion below would pass vacuously.
      ok &= check(started.status == 200 || started.status == 202,
                  "the guarded run was not accepted: " + std::to_string(started.status) + " " + started.body);
      bool finished = false;
      for (int i = 0; i < 200 && !finished; i++) {
        const Json::Value runs = as_json(request(guarded.port(), "GET", "/api/runs").body);
        for (const auto &item : runs["runs"]) {
          const std::string status = item["status"].asString();
          if (status == "completed" || status == "stopped" || status == "error") {
            finished = true;
          }
        }
        if (!finished) {
          usleep(50 * 1000);
        }
      }
      ok &= check(finished, "no run reached a terminal state, so no history write was attempted");
      // stop() joins the worker, so the check after it sees the final state.
      guarded.stop();
    }
    ok &= check(read_file(index) == before,
                "an unsupported runs-index.json (" + pair.first + ") was modified on shutdown");
    unlink(index.c_str());
    rmdir(dir.c_str());
  }

  // --- hardware-trigger run sets trigger_profile_file in the history entry --
  //
  // Regression: run_config_from_json set trigger_profile_id but never
  // trigger_profile_file. The web server must resolve the filename from the
  // profile registry after validation so report_writer can name the artifacts.
  // Failing to do so caused every hardware-trigger web run to end in "error"
  // with "cannot name this run's artifacts: a hardware-trigger run has no
  // usable Trigger Profile source file (got \"\")".
  {
    // Add a fresh profile to the shared config_dir; clean it up at the end.
    const std::string bench_rig_profile_path = config_dir + "/bench-rig-hw-regression.json";
    write_file(bench_rig_profile_path, v4_profile("bench-rig-hw-regression"));

    const std::string hw_dir = make_temp_dir("hw-profile-file");
    v4l2diag::WebServerOptions hw_opts = options;
    hw_opts.report_root = hw_dir;
    hw_opts.port = 18937;
    hw_opts.max_port = 18960;
    v4l2diag::WebServer hw_server(hw_opts);
    std::string hw_error;
    if (check(hw_server.start(&hw_error), "the hw-profile-file server did not start: " + hw_error)) {
      const unsigned short hw_port = hw_server.port();

      Json::Value body(Json::objectValue);
      body["trigger_mode"] = "hardware";
      body["trigger_profile_id"] = "bench-rig-hw-regression";
      body["master"]["path"] = "/dev/null";
      body["test_selectors"].append("t01-device-compliance");
      Json::StreamWriterBuilder builder;
      const Response started = request(hw_port, "POST", "/api/runs", Json::writeString(builder, body));
      ok &= check(started.status == 200 || started.status == 202,
                  "the hw-profile-file run was not accepted: " + std::to_string(started.status) + " " + started.body);
      const std::string run_id = as_json(started.body)["id"].asString();

      Json::Value final_runs;
      if (check(!run_id.empty(), "the hw-profile-file run returned no id")) {
        for (int i = 0; i < 400; i++) {
          final_runs = as_json(request(hw_port, "GET", "/api/runs").body);
          bool finished = false;
          for (const auto &entry : final_runs["runs"]) {
            if (entry["id"].asString() == run_id) {
              const std::string status = entry["status"].asString();
              if (status == "completed" || status == "stopped" || status == "error") {
                finished = true;
              }
              break;
            }
          }
          if (finished) {
            break;
          }
          usleep(25 * 1000);
        }

        bool found = false;
        for (const auto &entry : final_runs["runs"]) {
          if (entry["id"].asString() == run_id) {
            found = true;
            ok &= check(entry["trigger_profile_file"].asString() == "bench-rig-hw-regression.json",
                        "hardware-trigger run did not set trigger_profile_file in history: \"" +
                            entry["trigger_profile_file"].asString() + "\"");
            break;
          }
        }
        ok &= check(found, "the hw-profile-file run was not found in the history");
      }
      hw_server.stop();
    }
    unlink(bench_rig_profile_path.c_str());
  }

  for (const char *name : {"legacy.json", "disabled.json", "current.json", "broken.json", "future.json",
                           "invalid-current.json", "bench-rig-hw-regression.json"}) {
    unlink((config_dir + "/" + name).c_str());
  }
  unlink((report_dir + "/runs-index.json").c_str());
  rmdir(config_dir.c_str());
  rmdir(report_dir.c_str());
  rmdir(web_root.c_str());

  return ok ? 0 : 1;
}
