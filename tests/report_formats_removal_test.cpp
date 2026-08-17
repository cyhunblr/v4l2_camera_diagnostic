// v4 -> v5: report_formats leaves the schema (plan 2.10, 2.10.1, 2.10.2).
//
// The user never picks a format again: every run writes HTML, JSON and Markdown.
// So `defaults.report_formats` is dropped from the profile schema.
//
// This migration is LOSSLESS and needs no user input, which makes it different from
// every other Legacy case in 2.4/2.5. It gets one narrow exception -- a v4 profile
// whose only difference is this field stays runnable -- and this file is what keeps
// that exception narrow: enabled:false, a missing role binding, Future and Malformed
// must not benefit from it.
#include "v4l2diag/core/config_migration.hpp"

#include "v4l2diag/core/profile_registry.hpp"

#include <json/json.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
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

Json::Value parse(const std::string &text) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream in(text);
  Json::parseFromStream(builder, in, &root, &errors);
  return root;
}

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-formats-removal-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : "/tmp/v4l2diag-formats-removal";
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in.good()) {
    return std::string();
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void write_file(const std::string &path, const std::string &text) {
  std::ofstream out(path, std::ios::trunc);
  out << text;
}

bool has(const std::vector<std::string> &values, const std::string &needle) {
  return std::any_of(values.begin(), values.end(),
                     [&](const std::string &value) { return value.find(needle) != std::string::npos; });
}

std::string join(const std::vector<std::string> &values) {
  std::string out;
  for (const auto &value : values) {
    if (!out.empty()) {
      out += ", ";
    }
    out += value;
  }
  return out;
}

// A complete, valid v4 profile -- WITH report_formats, which is exactly what makes
// it a legacy fixture (see plan 2.10.2b: only v4 legacy fixtures carry the field).
std::string v4_profile(const std::string &id = "bench-rig", const std::string &extra = std::string()) {
  return R"({
  "schema_version": 4,
  "id": ")" +
         id + R"(",
  "name": "Bench-rig",
  "description": "hardware rig",)" +
         extra + R"(
  "defaults": {
    "trigger_mode": "hardware", "memory_backends": ["mmap"], "test_selectors": ["stable"],
    "report_formats": ["json", "html"], "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "hardware",
     "gpio": {"chip_id": 0, "line_number": 108}}
  ],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})";
}

}  // namespace

int main() {
  using v4l2diag::ConfigVersionState;
  using v4l2diag::DeviceProfile;
  using v4l2diag::MigrationReport;
  bool ok = true;

  // --- 1. The current schema is v5 ----------------------------------------
  {
    ok &= check(v4l2diag::kProfileSchemaVersion == 5,
                "the current schema version is " + std::to_string(v4l2diag::kProfileSchemaVersion) + ", expected 5");
    int version = -1;
    ok &= check(v4l2diag::classify_profile_version(parse(v4_profile()), &version) == ConfigVersionState::Legacy,
                "a v4 profile is no longer classified Legacy");
    ok &= check(version == 4, "the v4 version was read wrongly");
  }

  // --- 2. A valid v4 profile stays usable, losslessly ----------------------
  {
    // The whole point of the narrow exception: nothing is asked of the user, so the
    // profile is not taken away from them either.
    DeviceProfile profile;
    MigrationReport report;
    ok &= check(v4l2diag::migrate_profile_json(parse(v4_profile()), &profile, &report),
                "a valid v4 profile failed to migrate");
    ok &= check(report.state == ConfigVersionState::Legacy, "a v4 profile is not Legacy");
    ok &= check(report.usable_for_run(),
                "a valid v4 profile whose only difference is report_formats is not usable for a run");
    ok &= check(!report.needs_user_input(), "a lossless v4 -> v5 migration asks for user input");
    ok &= check(report.required.empty(),
                "the lossless migration produced required fields: {" + join(report.required) + "}");
    ok &=
        check(report.invalid.empty(), "the lossless migration produced invalid fields: {" + join(report.invalid) + "}");

    // Reported with its old value, so the user can see what was discarded.
    ok &= check(has(report.dropped, "report_formats"),
                "dropping report_formats was not reported: dropped={" + join(report.dropped) + "}");
    ok &=
        check(has(report.dropped, "json") && has(report.dropped, "html"),
              "the dropped report_formats report does not carry the old value: dropped={" + join(report.dropped) + "}");

    // And the field is gone from the migrated profile.
    ok &= check(profile.schema_version == v4l2diag::kProfileSchemaVersion,
                "the migrated profile kept its old version number");
  }

  // --- 3. Loading does not rewrite the stored file -------------------------
  {
    // "The original config file is never modified silently" survives from 2.4. The
    // exception makes the profile runnable; it does not make the loader a writer.
    const std::string dir = make_temp_dir();
    const std::string path = dir + "/bench-rig.json";
    write_file(path, v4_profile());
    const std::string before = read_file(path);

    v4l2diag::ProfileRegistry registry(dir);
    ok &= check(registry.list_profiles().size() == 1,
                "a valid v4 profile is not listed as runnable, got " + std::to_string(registry.list_profiles().size()));
    DeviceProfile fetched;
    ok &= check(registry.get_profile("bench-rig", &fetched), "a valid v4 profile is not selectable by id");
    ok &= check(read_file(path) == before, "loading a v4 profile rewrote the stored file");

    // The report still explains what happened.
    const auto configs = registry.stored_configs();
    if (check(configs.size() == 1, "the stored config was not enumerated")) {
      ok &= check(has(configs.front().report.dropped, "report_formats"),
                  "the API report does not mention the dropped report_formats");
      ok &= check(configs.front().report.usable_for_run(), "the stored v4 config is not usable for a run");
    }

    // --- 4. An explicit save upgrades the file to v5 ---------------------
    {
      DeviceProfile draft = configs.front().draft;
      std::string error;
      ok &= check(registry.add_or_update_profile(draft, &error), "saving the migrated draft failed: " + error);
      const std::string after = read_file(path);
      ok &= check(after != before, "an explicit save did not rewrite the file");
      const std::string version = std::to_string(v4l2diag::kProfileSchemaVersion);
      ok &= check(after.find("\"schema_version\" : " + version) != std::string::npos ||
                      after.find("\"schema_version\": " + version) != std::string::npos,
                  "the saved file is not at schema v" + version);
      ok &= check(after.find("report_formats") == std::string::npos, "the saved v5 file still writes report_formats");
    }
    unlink(path.c_str());
    rmdir(dir.c_str());
  }

  // --- 5. The exception is narrow: enabled:false cannot use it ------------
  {
    // A v2 profile deliberately switched off must not become runnable just because
    // the report_formats path is lossless.
    const std::string text = R"({
  "schema_version": 2, "id": "disabled", "name": "Disabled", "enabled": false,
  "camera_match": {"driver": "", "card": "", "bus_info": ""},
  "defaults": {"trigger_mode": "hardware", "report_formats": ["json"],
               "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
  "trigger_channels": [{"id": "channel-a", "type": "hardware", "gpio": {"line_number": 1}}],
  "camera_bindings": []
})";
    DeviceProfile profile;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(text), &profile, &report);
    ok &= check(!report.usable_for_run(), "a v2 profile with enabled:false became runnable via the v5 exception");
    ok &= check(report.needs_user_input(), "a v2 profile with enabled:false does not ask for review");
    ok &= check(has(report.dropped, "enabled"), "dropping enabled was not reported");

    const std::string dir = make_temp_dir();
    write_file(dir + "/disabled.json", text);
    v4l2diag::ProfileRegistry registry(dir);
    ok &= check(registry.list_profiles().empty(), "a disabled v2 profile was listed as runnable");
    unlink((dir + "/disabled.json").c_str());
    rmdir(dir.c_str());
  }

  // --- 6. The exception is narrow: a missing role binding cannot use it ---
  {
    // v4 without role_bindings is a v3-era shape: the user still has to supply the
    // routing, so it is not lossless and not runnable.
    const std::string text = R"({
  "schema_version": 4, "id": "unrouted", "name": "Unrouted",
  "defaults": {"trigger_mode": "hardware", "report_formats": ["json"],
               "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
  "trigger_channels": [{"id": "channel-a", "type": "hardware", "gpio": {"line_number": 1}}],
  "role_bindings": []
})";
    DeviceProfile profile;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(text), &profile, &report);
    ok &= check(!report.usable_for_run(), "a v4 profile with no role_bindings became runnable via the v5 exception");
    ok &= check(has(report.required, "role_bindings"), "the missing role binding was not reported as required");
  }

  // --- 7. Future and Malformed keep being refused -------------------------
  {
    DeviceProfile profile;
    MigrationReport future;
    ok &= check(!v4l2diag::migrate_profile_json(parse(R"({"schema_version": 99, "id": "x"})"), &profile, &future),
                "a Future profile was accepted");
    ok &= check(future.state == ConfigVersionState::Future && !future.usable_for_run(),
                "a Future profile is no longer refused");

    MigrationReport bad;
    ok &= check(!v4l2diag::migrate_profile_json(parse(R"([1,2,3])"), &profile, &bad),
                "a Malformed document was accepted");
    ok &= check(bad.state == ConfigVersionState::Malformed && !bad.usable_for_run(),
                "a Malformed document is no longer refused");
  }

  // --- 8. A v5 profile carries no report_formats at all -------------------
  {
    // The current-schema fixture must not contain the field (plan 2.10.2b).
    const std::string v5 = R"({
  "schema_version": 5, "id": "current", "name": "Current",
  "defaults": {"trigger_mode": "hardware", "memory_backends": ["mmap"],
               "test_selectors": ["stable"], "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
  "trigger_channels": [{"id": "channel-a", "type": "hardware", "gpio": {"line_number": 4}}],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})";
    DeviceProfile profile;
    MigrationReport report;
    ok &= check(v4l2diag::migrate_profile_json(parse(v5), &profile, &report), "a v5 profile failed to load");
    ok &= check(report.state == ConfigVersionState::Current, "a v5 profile is not Current");
    ok &= check(!report.changed(), "a v5 profile reported changes: migrated={" + join(report.migrated) + "} dropped={" +
                                       join(report.dropped) + "}");
    ok &= check(report.usable_for_run(), "a v5 profile is not usable for a run");

    // Round-trips without the field reappearing.
    const std::string dir = make_temp_dir();
    v4l2diag::ProfileRegistry registry(dir);
    std::string error;
    ok &= check(registry.add_or_update_profile(profile, &error), "saving a v5 profile failed: " + error);
    ok &= check(read_file(dir + "/current.json").find("report_formats") == std::string::npos,
                "a saved v5 profile writes report_formats");
    unlink((dir + "/current.json").c_str());
    rmdir(dir.c_str());
  }

  // --- 8b. The exception is reachable ONLY from v4 -------------------------
  {
    // A v2 profile that is otherwise complete -- id, name, channels, and even a
    // hand-written role_bindings -- still must not be runnable. Two independent
    // things stop it, and this pins both:
    //
    //   * lossless_upgrade requires source_version == 4, so a v2 file can never
    //     set it however clean it looks;
    //   * its v2-era role_bindings is ignored (predates the field), which leaves a
    //     required entry.
    //
    // Without the first condition the second alone would be doing the work, and the
    // exception would silently widen the day the field-reading rules change.
    const std::string text = R"({
  "schema_version": 2, "id": "old", "name": "Old", "enabled": true,
  "camera_match": {"driver": "uvcvideo", "card": "C", "bus_info": "b"},
  "defaults": {"trigger_mode": "hardware", "report_formats": ["json"],
               "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
  "trigger_channels": [{"id": "channel-a", "type": "hardware", "gpio": {"line_number": 1}}],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})";
    DeviceProfile profile;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(text), &profile, &report);
    ok &= check(!report.lossless_upgrade, "a v2 profile was treated as a lossless upgrade; the exception is v4-only");
    ok &= check(!report.usable_for_run(), "an otherwise-complete v2 profile became runnable");
    ok &= check(report.needs_user_input(), "an otherwise-complete v2 profile does not ask for review");
  }

  // --- 8c. A v4 profile with any OTHER difference is not lossless ----------
  {
    // v4 has no `enabled`, so a stray one is an extra dropped entry. The exception
    // requires report_formats to be the ONLY difference, so this must not qualify --
    // even though the file is a v4 and otherwise valid.
    DeviceProfile profile;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(v4_profile("stray", R"(
  "enabled": false,)")),
                                   &profile, &report);
    ok &= check(!report.lossless_upgrade, "a v4 profile with an extra dropped field was treated as a lossless upgrade");
  }

  // --- 9. A v4 file with no report_formats needs nothing dropped ----------
  {
    const std::string text = R"({
  "schema_version": 4, "id": "lean", "name": "Lean",
  "defaults": {"trigger_mode": "hardware", "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
  "trigger_channels": [{"id": "channel-a", "type": "hardware", "gpio": {"line_number": 1}}],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})";
    DeviceProfile profile;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(text), &profile, &report);
    ok &=
        check(!has(report.dropped, "report_formats"), "a v4 file without report_formats still reported it as dropped");
    ok &= check(report.usable_for_run(), "a lean v4 profile is not usable for a run");
    ok &= check(!report.needs_user_input(), "a lean v4 profile asks for user input");
  }

  return ok ? 0 : 1;
}
