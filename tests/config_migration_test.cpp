// Config schema versioning and migration contract (plan item 2.4).
//
// The backend contract and the state shown to the user are separate concerns:
// MigrationReport is the contract, migration_report_to_json is the wire model.
// The UI that renders it lands in a later phase, but the wire shape is fixed
// here so that phase has nothing to invent.
#include "v4l2diag/core/config_migration.hpp"

#include "v4l2diag/core/profile_registry.hpp"

#include <json/json.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <cstring>
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
  std::string pattern = "/tmp/v4l2diag-migration-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : "/tmp/v4l2diag-migration-test";
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

bool file_exists(const std::string &path) {
  return std::ifstream(path).good();
}

bool has(const std::vector<std::string> &values, const std::string &needle) {
  return std::any_of(values.begin(), values.end(),
                     [&](const std::string &value) { return value.find(needle) != std::string::npos; });
}

std::string join(const std::vector<std::string> &values) {
  std::string out;
  for (const auto &value : values) {
    if (!out.empty()) out += ", ";
    out += value;
  }
  return out;
}

// A complete v2 profile: everything the old schema required, including the
// `enabled` flag that v3 removes.
std::string v2_profile(const std::string &id = "anvil") {
  return R"({
  "schema_version": 2,
  "id": ")" + id + R"(",
  "name": "Anvil",
  "description": "hardware rig",
  "enabled": true,
  "camera_match": {"driver": "uvcvideo", "card": "USB Camera", "bus_info": "usb-1"},
  "defaults": {
    "trigger_mode": "hardware",
    "memory_backends": ["mmap"],
    "test_selectors": ["stable"],
    "report_formats": ["json", "html"],
    "trigger_rate_hz": 10.0,
    "pulse_width_ms": 5.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "hardware",
     "gpio": {"chip_id": 0, "line_number": 108}}
  ],
  "camera_bindings": []
})";
}

// A v3 profile: Legacy now. Carries the physical matcher and camera_bindings that
// v4 dropped, so it exercises the 2.5.2 migration rules.
std::string v3_profile(const std::string &id = "anvil") {
  return R"({
  "schema_version": 3,
  "id": ")" + id + R"(",
  "name": "Anvil",
  "description": "hardware rig",
  "camera_match": {"driver": "uvcvideo", "card": "USB Camera", "bus_info": "usb-1"},
  "defaults": {
    "trigger_mode": "hardware",
    "memory_backends": ["mmap"],
    "test_selectors": ["stable"],
    "report_formats": ["json", "html"],
    "trigger_rate_hz": 10.0,
    "pulse_width_ms": 5.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "hardware",
     "gpio": {"chip_id": 0, "line_number": 108}}
  ],
  "camera_bindings": []
})";
}

// The current schema (v5): no defaults.report_formats -- every run writes all three
// artifacts, so there is no preference to store (plan 2.10.2b: current fixtures must
// not carry the field).
std::string v4_profile(const std::string &id = "anvil") {
  return R"({
  "schema_version": 5,
  "id": ")" + id + R"(",
  "name": "Anvil",
  "description": "hardware rig",
  "defaults": {
    "trigger_mode": "hardware",
    "memory_backends": ["mmap"],
    "test_selectors": ["stable"],
    "trigger_rate_hz": 10.0,
    "pulse_width_ms": 5.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "hardware",
     "gpio": {"chip_id": 0, "line_number": 108}}
  ],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})";
}

// Fills in the routing a v2/v3 draft cannot supply, as the user would on the
// migration form. Deliberately explicit in every test: nothing derives it.
void complete_routing(v4l2diag::DeviceProfile *profile) {
  if (profile->role_bindings.empty() && !profile->trigger_channels.empty()) {
    v4l2diag::RoleBinding binding;
    binding.role = "master";
    binding.trigger_channel_id = profile->trigger_channels.front().id;
    profile->role_bindings.push_back(binding);
  }
}

}  // namespace

int main() {
  using v4l2diag::ConfigVersionState;
  using v4l2diag::DeviceProfile;
  using v4l2diag::MigrationReport;
  bool ok = true;

  // --- 1. Version classification: current / legacy / future / malformed ----
  {
    int version = -1;
    ok &= check(v4l2diag::classify_profile_version(parse(v4_profile()), &version) == ConfigVersionState::Current,
                "a current-version profile was not classified Current");
    ok &= check(version == v4l2diag::kProfileSchemaVersion, "the current version was read wrongly");

    for (const auto &pair : std::vector<std::pair<std::string, int>>{{v2_profile(), 2}, {v3_profile(), 3}}) {
      version = -1;
      ok &= check(v4l2diag::classify_profile_version(parse(pair.first), &version) == ConfigVersionState::Legacy,
                  "an older profile was not classified Legacy");
      ok &= check(version == pair.second, "the legacy version was read wrongly");
    }

    version = -1;
    ok &= check(v4l2diag::classify_profile_version(parse(R"({"schema_version": 99, "id": "x"})"), &version) ==
                    ConfigVersionState::Future,
                "a newer-than-supported profile was not classified Future");
    ok &= check(version == 99, "the future version was read wrongly");

    for (const char *text : {R"([])", R"("a string")", R"({"schema_version": "two", "id": "x"})",
                             R"({"schema_version": 0, "id": "x"})", R"({"schema_version": -1, "id": "x"})"}) {
      version = -1;
      ok &= check(v4l2diag::classify_profile_version(parse(text), &version) == ConfigVersionState::Malformed,
                  std::string("this was not classified Malformed: ") + text);
    }

    // A document with no version at all is not guessed at.
    version = -1;
    ok &= check(v4l2diag::classify_profile_version(parse(R"({"id": "x", "name": "X"})"), &version) ==
                    ConfigVersionState::Malformed,
                "a profile with no schema_version was not classified Malformed");
  }

  // --- 2. Future and malformed configs are refused, with a reason ----------
  {
    DeviceProfile profile;
    MigrationReport report;
    ok &= check(!v4l2diag::migrate_profile_json(parse(R"({"schema_version": 99, "id": "x"})"), &profile, &report),
                "a Future profile was accepted");
    ok &= check(report.state == ConfigVersionState::Future, "a Future profile reported the wrong state");
    ok &= check(!report.error.empty(), "a refused Future profile gave no error");
    ok &= check(!report.usable_for_run(), "a Future profile was marked usable for a run");

    MigrationReport bad;
    ok &= check(!v4l2diag::migrate_profile_json(parse(R"([1,2,3])"), &profile, &bad),
                "a malformed document was accepted");
    ok &= check(bad.state == ConfigVersionState::Malformed, "a malformed document reported the wrong state");
    ok &= check(!bad.error.empty(), "a malformed document gave no error");
    ok &= check(!bad.usable_for_run(), "a malformed document was marked usable for a run");
  }

  // --- 3. A current config migrates nothing -------------------------------
  {
    DeviceProfile profile;
    MigrationReport report;
    ok &= check(v4l2diag::migrate_profile_json(parse(v4_profile()), &profile, &report),
                "a current profile failed to load");
    ok &= check(report.state == ConfigVersionState::Current, "a current profile was reported as migrated");
    ok &= check(!report.changed(), "a current profile reported changes: migrated={" + join(report.migrated) +
                                       "} dropped={" + join(report.dropped) + "}");
    ok &= check(report.required.empty(), "a current profile demanded user input");
    ok &= check(report.usable_for_run(), "a current profile was not usable for a run");
    ok &= check(profile.schema_version == v4l2diag::kProfileSchemaVersion, "the loaded profile has the wrong version");
    ok &= check(profile.id == "anvil" && profile.trigger_channels.size() == 1, "a current profile lost content");
  }

  // --- 4. v2 -> v3 reports migrated, dropped and required separately ------
  {
    DeviceProfile profile;
    MigrationReport report;
    ok &= check(v4l2diag::migrate_profile_json(parse(v2_profile()), &profile, &report),
                "a v2 profile failed to migrate");
    ok &= check(report.state == ConfigVersionState::Legacy, "a v2 profile was not reported Legacy");
    ok &= check(report.source_version == 2 && report.target_version == v4l2diag::kProfileSchemaVersion,
                "the migration reported the wrong version pair");

    // `enabled` is gone from the target schema: dropped, not migrated.
    ok &= check(has(report.dropped, "enabled"), "removal of \"enabled\" was not reported as dropped");
    ok &= check(!has(report.migrated, "enabled"), "\"enabled\" was reported as migrated");
    ok &= check(!has(report.required, "enabled"), "\"enabled\" was reported as required");

    // Content the user configured survives.
    ok &= check(profile.id == "anvil" && profile.name == "Anvil", "migration lost the profile identity");
    ok &= check(profile.trigger_channels.size() == 1 && profile.trigger_channels.front().id == "channel-a",
                "migration lost the trigger channel");
    ok &= check(profile.defaults.trigger_rate_hz == 10.0 && profile.defaults.pulse_width_ms == 5.0,
                "migration lost the trigger defaults");
    ok &= check(profile.schema_version == v4l2diag::kProfileSchemaVersion,
                "a migrated profile kept its old version number");

    // Nothing is missing, but a Legacy config is still NOT runnable: the user
    // must review the migration and save it at the current schema. Promoting it
    // automatically would decide things the stored file never said.
    // v4 requires role_bindings, and a v2/v3 file cannot supply them: physical
    // camera bindings do not map to run roles (plan 2.5.2). So this IS reported as
    // a required field -- the user completes the form before saving.
    ok &= check(has(report.required, "role_bindings"),
                "a v2 profile did not report role_bindings as required: required={" + join(report.required) + "}");
    ok &= check(report.invalid.empty(), "a complete v2 profile reported an invalid field");
    ok &= check(!report.usable_for_run(), "a complete v2 profile was runnable before an explicit save");
    ok &= check(report.needs_user_input(), "a complete v2 profile did not ask for review");
    ok &= check(report.has_draft(), "a complete v2 profile produced no draft to prefill the form");
  }

  // --- 4b. A v2 profile with enabled:false must not come back active -------
  {
    std::string text = v2_profile();
    const std::string from = R"("enabled": true)";
    text.replace(text.find(from), from.size(), R"("enabled": false)");

    DeviceProfile profile;
    MigrationReport report;
    ok &= check(v4l2diag::migrate_profile_json(parse(text), &profile, &report),
                "a disabled v2 profile failed to migrate");
    // v3 has no `enabled`, so dropping it cannot be allowed to silently activate
    // a profile the user had switched off.
    ok &= check(has(report.dropped, "enabled"), "dropping \"enabled\" was not reported");
    ok &= check(!report.usable_for_run(), "a v2 profile with enabled:false became runnable on migration");
    ok &= check(report.needs_user_input(), "a v2 profile with enabled:false did not ask for review");

    // And the registry must not list it as a selectable profile.
    const std::string dir = make_temp_dir();
    write_file(dir + "/disabled.json", text);
    v4l2diag::ProfileRegistry registry(dir);
    ok &= check(registry.list_profiles().empty(),
                "a v2 profile with enabled:false was listed as runnable");
    DeviceProfile fetched;
    ok &= check(!registry.get_profile("anvil", &fetched), "a disabled v2 profile was selectable by id");
    // But its values are available as a draft, so the migration form can prefill.
    const auto configs = registry.stored_configs();
    ok &= check(configs.size() == 1 && configs.front().report.has_draft(),
                "no draft was kept for the disabled v2 profile");
    ok &= check(configs.front().profile_id == "anvil", "the draft carried no reliable config id");
    ok &= check(configs.front().draft.name == "Anvil", "the draft lost the stored values");
    unlink((dir + "/disabled.json").c_str());
    rmdir(dir.c_str());
  }

  // --- 5. Missing mandatory fields block the run until supplied -----------
  {
    // A v2 profile with no name and no trigger channel: both mandatory.
    DeviceProfile profile;
    MigrationReport report;
    const bool loaded = v4l2diag::migrate_profile_json(
        parse(R"({"schema_version": 2, "id": "half", "enabled": true, "defaults": {}, "trigger_channels": []})"),
        &profile, &report);
    ok &= check(loaded, "an incomplete legacy profile was refused outright instead of reported");
    ok &= check(report.state == ConfigVersionState::Legacy, "an incomplete legacy profile reported the wrong state");
    ok &= check(has(report.required, "name"), "a missing name was not reported as required");
    ok &= check(has(report.required, "trigger_channels"),
                "a missing trigger channel was not reported as required");
    ok &= check(report.needs_user_input(), "an incomplete profile was not marked as needing input");
    ok &= check(!report.usable_for_run(),
                "an incomplete profile was usable for a run: required={" + join(report.required) + "}");

    // Filling them in makes it usable, through the same entry point.
    // Filling the fields in removes the `required` entries, but the config is
    // still Legacy so it stays unrunnable until saved.
    DeviceProfile filled;
    MigrationReport filled_report;
    ok &= check(v4l2diag::migrate_profile_json(parse(v2_profile()), &filled, &filled_report),
                "the completed profile failed to migrate");
    // Only role_bindings stays required: id/name/channels came from the file, but
    // the routing cannot be derived from a physical binding (plan 2.5.2).
    ok &= check(!has(filled_report.required, "id") && !has(filled_report.required, "name") &&
                    !has(filled_report.required, "trigger_channels"),
                "the completed profile still reported fields the file supplied: required={" +
                    join(filled_report.required) + "}");
    ok &= check(has(filled_report.required, "role_bindings"),
                "role_bindings was not the remaining required field");
    ok &= check(!filled_report.usable_for_run(), "a completed Legacy profile became runnable without a save");
  }

  // --- 6. A pdf-only legacy defaults list is handled by the migration -----
  {
    std::string text = v2_profile();
    const std::string from = R"("report_formats": ["json", "html"])";
    const std::string to = R"("report_formats": ["pdf"])";
    text.replace(text.find(from), from.size(), to);

    DeviceProfile profile;
    MigrationReport report;
    ok &= check(v4l2diag::migrate_profile_json(parse(text), &profile, &report),
                "a pdf-only legacy profile failed to migrate");
    // v5 removed the field outright, so the whole list is reported as dropped --
    // including the "pdf" that used to need its own fallback (plan 2.10).
    ok &= check(has(report.dropped, "report_formats"),
                "the dropped report_formats was not reported: dropped={" + join(report.dropped) + "}");
    ok &= check(has(report.dropped, "pdf"),
                "the dropped report_formats report lost its old value: dropped={" + join(report.dropped) + "}");
    // role_bindings is expected (v4); nothing else should be flagged.
    ok &= check(report.required.size() == 1 && has(report.required, "role_bindings"),
                "a pdf-only legacy profile reported unrelated required fields: required={" +
                    join(report.required) + "}");
    ok &= check(report.invalid.empty(), "a pdf-only legacy profile reported an invalid field");
    ok &= check(!report.usable_for_run(), "a pdf-only legacy profile became runnable without a save");
  }

  // --- 7. Loading a legacy profile never rewrites the stored file ----------
  {
    const std::string dir = make_temp_dir();
    const std::string path = dir + "/anvil.json";
    write_file(path, v2_profile());
    const std::string before = read_file(path);

    {
      v4l2diag::ProfileRegistry registry(dir);
      // Not runnable, and not selectable by id.
      ok &= check(registry.list_profiles().empty(), "a legacy profile was listed as runnable");
      DeviceProfile fetched;
      ok &= check(!registry.get_profile("anvil", &fetched), "a legacy profile was selectable by id");

      // Its migrated values are available as a draft, at the current version.
      const auto configs = registry.stored_configs();
      ok &= check(configs.size() == 1, "the registry did not report the legacy config");
      ok &= check(configs.front().report.state == ConfigVersionState::Legacy,
                  "the stored config was not reported Legacy");
      ok &= check(configs.front().report.has_draft(), "the legacy config carried no draft");
      ok &= check(configs.front().profile_id == "anvil", "the stored config carried no reliable id");
      ok &= check(configs.front().draft.schema_version == v4l2diag::kProfileSchemaVersion,
                  "the draft was not migrated to the current version");
      ok &= check(configs.front().draft.trigger_channels.size() == 1, "the draft lost the trigger channel");
    }

    ok &= check(read_file(path) == before, "loading a legacy profile rewrote the stored file");

    // An explicit save of the draft is what upgrades the file and makes the
    // profile runnable.
    {
      v4l2diag::ProfileRegistry registry(dir);
      const auto configs = registry.stored_configs();
      DeviceProfile draft = configs.front().draft;
      // The draft alone is not saveable: v4 needs a routing, and migration
      // deliberately did not invent one. This is the user completing the form.
      std::string refusal;
      ok &= check(!registry.add_or_update_profile(draft, &refusal),
                  "a draft with no role_bindings was saved as-is");
      v4l2diag::RoleBinding chosen;
      chosen.role = "master";
      chosen.trigger_channel_id = draft.trigger_channels.front().id;
      draft.role_bindings.push_back(chosen);
      std::string error;
      ok &= check(registry.add_or_update_profile(draft, &error), "saving a migrated draft failed: " + error);
    }

    // Reloading now finds a runnable, current profile.
    {
      v4l2diag::ProfileRegistry registry(dir);
      DeviceProfile fetched;
      ok &= check(registry.get_profile("anvil", &fetched), "the saved profile is still not selectable");
      ok &= check(fetched.schema_version == v4l2diag::kProfileSchemaVersion,
                  "the saved profile is not at the current version");
      const auto configs = registry.stored_configs();
      ok &= check(configs.size() == 1 && configs.front().report.state == ConfigVersionState::Current,
                  "the saved config is still reported Legacy");
      ok &= check(configs.front().report.usable_for_run(), "the saved config is still not runnable");
    }
    const std::string after = read_file(path);
    ok &= check(after != before, "an explicit save did not rewrite the file");
    const std::string version = std::to_string(v4l2diag::kProfileSchemaVersion);
    ok &= check(after.find("\"schema_version\" : " + version) != std::string::npos ||
                    after.find("\"schema_version\": " + version) != std::string::npos,
                "the saved file does not carry the current schema version (" + version + ")");
    ok &= check(after.find("\"enabled\"") == std::string::npos, "the saved file still writes the dropped \"enabled\"");

    unlink(path.c_str());
    rmdir(dir.c_str());
  }

  // --- 8. Malformed and future files do not disappear silently -------------
  {
    const std::string dir = make_temp_dir();
    write_file(dir + "/broken.json", "{ this is not json");
    write_file(dir + "/future.json", R"({"schema_version": 99, "id": "future", "name": "Future"})");
    write_file(dir + "/good.json", v2_profile("good"));

    v4l2diag::ProfileRegistry registry(dir);
    // None are runnable: two are unreadable and the third is Legacy, awaiting
    // an explicit save.
    ok &= check(registry.list_profiles().empty(), "the registry listed unusable profiles as runnable");

    const auto reports = registry.stored_configs();
    ok &= check(reports.size() == 3, "the registry reported " + std::to_string(reports.size()) +
                                         " configs, expected 3 (broken, future, good)");
    bool saw_future = false;
    bool saw_malformed = false;
    for (const auto &entry : reports) {
      if (entry.report.state == ConfigVersionState::Future) saw_future = true;
      if (entry.report.state == ConfigVersionState::Malformed) saw_malformed = true;
    }
    ok &= check(saw_future, "the Future config was not reported");
    ok &= check(saw_malformed, "the malformed config was not reported");

    // Only the readable one carries a draft; the other two carry none.
    int drafts = 0;
    for (const auto &entry : reports) {
      if (entry.report.has_draft()) {
        drafts++;
        ok &= check(entry.profile_id == "good", "the draft came from the wrong file");
      } else {
        ok &= check(entry.profile_id.empty(), "an unreadable config reported a profile id");
      }
    }
    ok &= check(drafts == 1, "expected exactly one draft, got " + std::to_string(drafts));

    unlink((dir + "/broken.json").c_str());
    unlink((dir + "/future.json").c_str());
    unlink((dir + "/good.json").c_str());
    rmdir(dir.c_str());
  }

  // --- 8b. Field-level validation blocks a run, reported per field --------
  {
    struct Case {
      const char *what;
      const char *patch_from;
      const char *patch_to;
      const char *expect;
    };
    const Case cases[] = {
        {"negative GPIO line", R"("line_number": 108)", R"("line_number": -3)", "line_number"},
        {"out-of-range trigger rate", R"("trigger_rate_hz": 10.0)", R"("trigger_rate_hz": 5000.0)",
         "trigger_rate_hz"},
        {"out-of-range pulse width", R"("pulse_width_ms": 5.0)", R"("pulse_width_ms": 900.0)", "pulse_width_ms"},
        {"unknown trigger mode", R"("trigger_mode": "hardware")", R"("trigger_mode": "telepathy")",
         "trigger_mode"},
        {"unknown memory backend", R"("memory_backends": ["mmap"])", R"("memory_backends": ["papyrus"])",
         "memory_backends"},
        {"unknown channel type", R"("type": "hardware")", R"("type": "smoke-signal")", "type"},
        {"invalid channel id", R"("id": "channel-a")", R"("id": "Bad Id!")", "id"},
    };
    for (const auto &item : cases) {
      std::string text = v4_profile();
      const std::size_t at = text.find(item.patch_from);
      if (!check(at != std::string::npos, std::string("could not build the case: ") + item.what)) {
        ok = false;
        continue;
      }
      text.replace(at, std::strlen(item.patch_from), item.patch_to);

      DeviceProfile profile;
      MigrationReport report;
      v4l2diag::migrate_profile_json(parse(text), &profile, &report);
      ok &= check(has(report.invalid, item.expect),
                  std::string(item.what) + " was not reported as an invalid field: invalid={" +
                      join(report.invalid) + "}");
      ok &= check(!report.usable_for_run(), std::string(item.what) + " did not block the run");
      ok &= check(report.needs_user_input(), std::string(item.what) + " did not ask for user input");
    }

    // An unrecognised control_device.kind. Same rule as trigger_mode and channel
    // type: the kind decides which device node the control writes are aimed at, so
    // it used to fall back to "capture" and quietly write to the wrong device.
    {
      const std::string text = R"({
        "schema_version": 5, "id": "soft", "name": "Soft",
        "defaults": {"trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
        "trigger_channels": [{"id": "channel-a", "type": "software",
          "control_device": {"kind": "smoke-signal"},
          "fire": [{"id": 1, "name": "Trigger", "type": 1, "value": 1}]}],
        "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
      })";
      DeviceProfile profile;
      MigrationReport report;
      v4l2diag::migrate_profile_json(parse(text), &profile, &report);
      ok &= check(has(report.invalid, "control_device.kind"),
                  "an unknown control_device.kind was guessed at instead of reported: invalid={" +
                      join(report.invalid) + "}");
      ok &= check(!report.usable_for_run(), "an unknown control_device.kind did not block the run");
      ok &= check(report.needs_user_input(), "an unknown control_device.kind did not ask for user input");
    }

    // Every recognised kind still parses, so the strict check did not just reject
    // everything.
    for (const char *kind : {"capture", "video", "subdevice"}) {
      const std::string text = std::string(R"({
        "schema_version": 5, "id": "soft", "name": "Soft",
        "defaults": {"trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
        "trigger_channels": [{"id": "channel-a", "type": "software",
          "control_device": {"kind": ")") +
                               kind + R"("},
          "fire": [{"id": 1, "name": "Trigger", "type": 1, "value": 1}]}],
        "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
      })";
      DeviceProfile profile;
      MigrationReport report;
      v4l2diag::migrate_profile_json(parse(text), &profile, &report);
      ok &= check(report.invalid.empty(),
                  std::string("control_device.kind \"") + kind + "\" was rejected: invalid={" +
                      join(report.invalid) + "}");
    }

    // A software channel with no fire control, and a dangling binding.
    {
      const std::string text = R"({
        "schema_version": 5, "id": "soft", "name": "Soft",
        "defaults": {"trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
        "trigger_channels": [{"id": "channel-a", "type": "software", "fire": []}],
        "role_bindings": [{"role": "master", "trigger_channel_id": "channel-z"}]
      })";
      DeviceProfile profile;
      MigrationReport report;
      v4l2diag::migrate_profile_json(parse(text), &profile, &report);
      ok &= check(has(report.invalid, "fire"), "a software channel with no fire control was accepted");
      ok &= check(has(report.invalid, "channel-z"), "a dangling role binding was accepted");
      ok &= check(!report.usable_for_run(), "an invalid channel set did not block the run");
    }

    // Duplicate channel ids.
    {
      const std::string text = R"({
        "schema_version": 5, "id": "dup", "name": "Dup",
        "defaults": {"trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
        "trigger_channels": [
          {"id": "channel-a", "type": "hardware", "gpio": {"line_number": 1}},
          {"id": "channel-a", "type": "hardware", "gpio": {"line_number": 2}}
        ],
        "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
      })";
      DeviceProfile profile;
      MigrationReport report;
      v4l2diag::migrate_profile_json(parse(text), &profile, &report);
      ok &= check(has(report.invalid, "duplicated"), "a duplicate channel id was accepted");
      ok &= check(!report.usable_for_run(), "a duplicate channel id did not block the run");
    }

    // And a config that fails validation never reaches the runnable list.
    {
      std::string text = v4_profile();
      const std::string from = R"("line_number": 108)";
      text.replace(text.find(from), from.size(), R"("line_number": -3)");
      const std::string dir = make_temp_dir();
      write_file(dir + "/bad.json", text);
      v4l2diag::ProfileRegistry registry(dir);
      ok &= check(registry.list_profiles().empty(), "a config with an invalid field was listed as runnable");
      const auto configs = registry.stored_configs();
      ok &= check(configs.size() == 1 && !configs.front().report.invalid.empty(),
                  "the invalid field was not reported for the stored config");
      unlink((dir + "/bad.json").c_str());
      rmdir(dir.c_str());
    }
  }

  // --- 9. The wire model carries every part of the report -----------------
  {
    DeviceProfile profile;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(v2_profile()), &profile, &report);
    const Json::Value json = v4l2diag::migration_report_to_json(report);

    for (const char *field : {"state", "source_version", "target_version", "migrated", "dropped", "required",
                              "invalid", "usable_for_run", "needs_user_input", "has_draft", "error"}) {
      ok &= check(json.isMember(field), std::string("the wire model is missing \"") + field + "\"");
    }
    ok &= check(json["state"].asString() == "legacy", "the wire model reported the wrong state string");
    ok &= check(json["source_version"].asInt() == 2, "the wire model reported the wrong source version");
    ok &= check(json["target_version"].asInt() == v4l2diag::kProfileSchemaVersion,
                "the wire model reported the wrong target version");
    ok &= check(json["dropped"].isArray() && json["dropped"].size() >= 1, "the wire model lost the dropped list");
    ok &= check(json["migrated"].isArray(), "the wire model does not send migrated as an array");
    ok &= check(json["required"].isArray(), "the wire model does not send required as an array");
    ok &= check(json["invalid"].isArray(), "the wire model does not send invalid as an array");
    ok &= check(!json["usable_for_run"].asBool(), "the wire model says a Legacy profile is runnable");
    ok &= check(json["needs_user_input"].asBool(), "the wire model says a Legacy profile needs no review");
    ok &= check(json["has_draft"].asBool(), "the wire model does not advertise the Legacy draft");

    // The three states the UI must distinguish all round-trip.
    for (const auto &pair : std::vector<std::pair<ConfigVersionState, const char *>>{
             {ConfigVersionState::Current, "current"},
             {ConfigVersionState::Legacy, "legacy"},
             {ConfigVersionState::Future, "future"},
             {ConfigVersionState::Malformed, "malformed"}}) {
      ok &= check(std::string(v4l2diag::to_string(pair.first)) == pair.second,
                  std::string("state string changed for ") + pair.second);
    }
  }

  // --- 10. Disk and API/import paths agree --------------------------------
  {
    const std::string dir = make_temp_dir();
    const std::string path = dir + "/anvil.json";
    write_file(path, v2_profile());

    DeviceProfile from_disk;
    MigrationReport disk_report;
    ok &= check(v4l2diag::migrate_profile_file(path, &from_disk, &disk_report), "the file path failed to migrate");

    DeviceProfile from_api;
    MigrationReport api_report;
    ok &= check(v4l2diag::migrate_profile_json(parse(v2_profile()), &from_api, &api_report),
                "the API path failed to migrate");

    ok &= check(disk_report.state == api_report.state, "disk and API disagree on the version state");
    ok &= check(disk_report.source_version == api_report.source_version, "disk and API disagree on the source version");
    ok &= check(disk_report.migrated == api_report.migrated, "disk and API report different migrated fields");
    ok &= check(disk_report.dropped == api_report.dropped, "disk and API report different dropped fields");
    ok &= check(disk_report.required == api_report.required, "disk and API report different required fields");

    ok &= check(from_disk.id == from_api.id && from_disk.name == from_api.name &&
                    from_disk.schema_version == from_api.schema_version &&
                    from_disk.trigger_channels.size() == from_api.trigger_channels.size() &&
                    from_disk.defaults.trigger_rate_hz == from_api.defaults.trigger_rate_hz &&
                    from_disk.defaults.test_selectors == from_api.defaults.test_selectors,
                "disk and API produced different profiles from the same JSON");

    unlink(path.c_str());
    rmdir(dir.c_str());
  }

  // --- 11. runs-index.json versioning -------------------------------------
  {
    // A versionless index is what older builds wrote: readable, and it gains the
    // version the next time it is written.
    const auto legacy = v4l2diag::classify_runs_index(parse(R"([{"id": "run-1"}])"));
    ok &= check(legacy.state == ConfigVersionState::Legacy, "a versionless runs index was not classified Legacy");
    ok &= check(legacy.source_version == 0, "a versionless runs index reported a version");

    const auto current = v4l2diag::classify_runs_index(
        parse(R"({"schema_version": 1, "runs": [{"id": "run-1"}]})"));
    ok &= check(current.state == ConfigVersionState::Current, "a current runs index was not classified Current");
    ok &= check(current.source_version == v4l2diag::kRunsIndexSchemaVersion,
                "a current runs index reported the wrong version");

    const auto future = v4l2diag::classify_runs_index(parse(R"({"schema_version": 99, "runs": []})"));
    ok &= check(future.state == ConfigVersionState::Future, "a newer runs index was not classified Future");

    for (const char *text : {R"("nope")", R"(42)"}) {
      ok &= check(v4l2diag::classify_runs_index(parse(text)).state == ConfigVersionState::Malformed,
                  std::string("this runs index was not classified Malformed: ") + text);
    }
  }

  // --- 11b. role_bindings must bind master specifically --------------------
  {
    // A slave-only profile can never start a run: every run has a master. Merely
    // "some binding present" was not enough of a rule.
    const std::string text = R"({
        "schema_version": 5, "id": "slaveonly", "name": "Slave Only",
        "defaults": {"trigger_mode": "hardware", "trigger_rate_hz": 10.0, "pulse_width_ms": 5.0},
        "trigger_channels": [{"id": "channel-a", "type": "hardware", "gpio": {"line_number": 1}}],
        "role_bindings": [{"role": "slave-1", "trigger_channel_id": "channel-a"}]
      })";
    DeviceProfile profile;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(text), &profile, &report);
    ok &= check(has(report.required, "role_bindings"),
                "a slave-only profile did not report role_bindings as required: required={" +
                    join(report.required) + "}");
    ok &= check(!report.usable_for_run(), "a slave-only profile was runnable");

    // And the central validator refuses to save it.
    std::string error;
    ok &= check(!v4l2diag::validate_device_profile(profile, &error),
                "validate_device_profile() accepted a slave-only triggered profile");
    ok &= check(error.find("master") != std::string::npos,
                "the refusal does not name the missing master role: " + error);

    const std::string dir = make_temp_dir();
    v4l2diag::ProfileRegistry registry(dir);
    ok &= check(!registry.add_or_update_profile(profile, &error),
                "the registry saved a slave-only triggered profile");
    ok &= check(!file_exists(dir + "/slaveonly.json"), "a slave-only profile reached the disk");
    rmdir(dir.c_str());
  }

  // --- 11c. role_bindings is a v4 field, ignored in older files -------------
  {
    // A hand-edited v3 file carrying role_bindings must still go through the
    // migration form: no released build wrote that key at v3, so trusting it would
    // let someone skip the review step plan 2.5.2 exists for.
    std::string text = v3_profile();
    const std::string anchor = R"("camera_bindings": [])";
    text.replace(text.find(anchor), anchor.size(),
                 R"("camera_bindings": [], "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}])");

    DeviceProfile profile;
    MigrationReport report;
    ok &= check(v4l2diag::migrate_profile_json(parse(text), &profile, &report),
                "a v3 profile with a hand-added role_bindings failed to migrate");
    ok &= check(report.state == ConfigVersionState::Legacy, "the hand-edited v3 file was not classified Legacy");
    ok &= check(profile.role_bindings.empty(),
                "role_bindings was read from a v3 file, which predates the field");
    ok &= check(has(report.required, "role_bindings"),
                "a v3 file with a hand-added role_bindings skipped the required check: required={" +
                    join(report.required) + "}");
    ok &= check(!report.usable_for_run(), "a hand-edited v3 file became runnable");
    ok &= check(has(report.dropped, "role_bindings"),
                "ignoring the premature role_bindings was not reported: dropped={" + join(report.dropped) + "}");

    // At v4 the same key IS read.
    DeviceProfile v4;
    MigrationReport v4_report;
    v4l2diag::migrate_profile_json(parse(v4_profile()), &v4, &v4_report);
    ok &= check(v4.role_bindings.size() == 1 && v4.role_bindings.front().role == "master",
                "role_bindings was not read from a v4 file");
    ok &= check(!has(v4_report.required, "role_bindings"), "a complete v4 profile still demanded role_bindings");
  }

  // --- 12. Committing a migration replaces the source file -----------------
  //
  // add_or_update_profile() writes "<profile.id>.json" and knows nothing about
  // where the config came from, so a file whose name differs from the id inside it
  // was left behind, still reported as needing migration.
  {
    const std::string dir = make_temp_dir();
    write_file(dir + "/rig-alpha.json", v2_profile());  // id inside is "anvil"
    v4l2diag::ProfileRegistry registry(dir);
    ok &= check(registry.stored_configs().size() == 1, "the source config was not enumerated");
    ok &= check(registry.list_profiles().empty(), "the legacy config was runnable before the migration");

    DeviceProfile draft = registry.stored_configs().front().draft;
    complete_routing(&draft);
    std::string error;
    ok &= check(registry.migrate_config("rig-alpha.json", draft, &error),
                "committing the migration failed: " + error);
    ok &= check(!file_exists(dir + "/rig-alpha.json"), "the source file survived the migration");
    ok &= check(file_exists(dir + "/anvil.json"), "the migrated profile was not written under its id");
    ok &= check(registry.stored_configs().size() == 1, "the migration left a second config behind");
    ok &= check(registry.list_profiles().size() == 1, "the migrated profile is not runnable");

    unlink((dir + "/anvil.json").c_str());
    rmdir(dir.c_str());
  }

  {
    // Same file name as the id: the source must not be deleted after being
    // rewritten in place.
    const std::string dir = make_temp_dir();
    write_file(dir + "/anvil.json", v2_profile());
    v4l2diag::ProfileRegistry registry(dir);
    DeviceProfile draft = registry.stored_configs().front().draft;
    complete_routing(&draft);
    std::string error;
    ok &= check(registry.migrate_config("anvil.json", draft, &error), "in-place migration failed: " + error);
    ok &= check(file_exists(dir + "/anvil.json"), "migrating in place deleted the file it had just written");
    ok &= check(registry.list_profiles().size() == 1, "the in-place migrated profile is not runnable");
    unlink((dir + "/anvil.json").c_str());
    rmdir(dir.c_str());
  }

  {
    // Only a config that needs repairing is eligible. Membership alone let a valid
    // current body rewrite or delete a Future, Malformed or runnable file.
    const std::string dir = make_temp_dir();
    write_file(dir + "/future.json", R"({"schema_version": 99, "id": "future", "name": "Newer"})");
    write_file(dir + "/broken.json", "{ not json");
    write_file(dir + "/current.json", v4_profile());  // id "anvil", runnable
    v4l2diag::ProfileRegistry registry(dir);

    DeviceProfile takeover;
    MigrationReport parsed;
    v4l2diag::migrate_profile_json(parse(v4_profile("takeover")), &takeover, &parsed);
    ok &= check(parsed.usable_for_run(), "the takeover payload is not itself valid, so the test proves nothing");

    for (const char *file : {"future.json", "broken.json", "current.json"}) {
      const std::string before = read_file(dir + "/" + file);
      std::string error;
      ok &= check(!registry.migrate_config(file, takeover, &error),
                  std::string("an ineligible config was migrated: ") + file);
      ok &= check(read_file(dir + "/" + file) == before,
                  std::string("migrating an ineligible config modified ") + file);
    }
    ok &= check(!file_exists(dir + "/takeover.json"), "an ineligible migration still wrote a new profile");

    for (const char *file : {"future.json", "broken.json", "current.json"}) {
      unlink((dir + "/" + file).c_str());
    }
    rmdir(dir.c_str());
  }

  {
    // Channel-less profiles are refused by the central validator, so every save
    // path shares the rule rather than migrate_config() alone.
    const std::string dir = make_temp_dir();
    v4l2diag::ProfileRegistry registry(dir);
    DeviceProfile complete;
    MigrationReport report;
    v4l2diag::migrate_profile_json(parse(v4_profile()), &complete, &report);

    DeviceProfile channel_less = complete;
    channel_less.trigger_channels.clear();
    std::string error;
    ok &= check(!v4l2diag::validate_device_profile(channel_less, &error),
                "validate_device_profile() accepted a profile with no trigger channels");
    ok &= check(!registry.add_or_update_profile(channel_less, &error),
                "add_or_update_profile() saved a profile with no trigger channels");
    ok &= check(!file_exists(dir + "/anvil.json"), "a channel-less profile reached the disk");
    // The complete one still saves, so the rule did not reject everything.
    ok &= check(registry.add_or_update_profile(complete, &error), "a complete profile was refused: " + error);
    unlink((dir + "/anvil.json").c_str());
    rmdir(dir.c_str());
  }

  {
    // Duplicate profile ids are refused even when the write is in place.
    //
    // The target-path check can be skipped for an in-place write -- a file may
    // replace itself -- but the id check cannot: an in-place migration still
    // promotes this config to a profile id another file already defines, leaving two
    // configs claiming one id and load order deciding which wins. Guarding the
    // whole loop with `!in_place` made it dead in exactly this case.
    const std::string dir = make_temp_dir();
    write_file(dir + "/twin.json", v2_profile("twin"));   // in place: twin -> twin.json
    write_file(dir + "/other.json", v4_profile("twin"));  // already defines "twin"
    const std::string twin_before = read_file(dir + "/twin.json");
    const std::string other_before = read_file(dir + "/other.json");

    v4l2diag::ProfileRegistry registry(dir);
    const auto configs = registry.stored_configs();
    const auto source = std::find_if(configs.begin(), configs.end(),
                                     [](const v4l2diag::ProfileRegistry::StoredConfig &c) {
                                       return c.file == "twin.json";
                                     });
    if (check(source != configs.end(), "twin.json was not enumerated")) {
      // Completed, so the refusal comes from the id collision rather than from the
      // missing routing an earlier check would catch first.
      DeviceProfile draft = source->draft;
      complete_routing(&draft);
      std::string error;
      ok &= check(!registry.migrate_config("twin.json", draft, &error),
                  "an in-place migration onto another config's profile id was accepted");
      ok &= check(error.find("other.json") != std::string::npos,
                  "the refusal does not name the config that already defines the id: " + error);
      ok &= check(read_file(dir + "/twin.json") == twin_before, "the refused in-place migration rewrote its source");
      ok &= check(read_file(dir + "/other.json") == other_before,
                  "the refused in-place migration touched the config that owns the id");
    }
    unlink((dir + "/twin.json").c_str());
    unlink((dir + "/other.json").c_str());
    rmdir(dir.c_str());
  }

  {
    // Replacing a *different* file is two steps, so it is not one atomic
    // operation. This covers the unlink() failure branch: the half-written target
    // must not survive.
    //
    // What it does NOT prove is that the original config file is preserved -- the
    // staging below replaces that file with a directory of the same name, so there
    // is no original content left to check. Preservation in the real failure mode
    // (a source that exists but cannot be unlinked, e.g. a read-only directory)
    // follows from the code never touching the source after unlink() fails, but is
    // not exercised here.
    //
    // The source is made undeletable by being a directory: unlink() fails with
    // EISDIR while the read that produced the draft still worked.
    const std::string dir = make_temp_dir();
    write_file(dir + "/src.json", v2_profile("renamed-away"));
    v4l2diag::ProfileRegistry registry(dir);
    ok &= check(registry.stored_configs().size() == 1, "the source config was not enumerated");
    DeviceProfile draft = registry.stored_configs().front().draft;
    complete_routing(&draft);
    ok &= check(draft.id == "renamed-away", "the draft lost its id, so the target path would not differ");

    // Swap the file for a directory of the same name, behind the registry's back.
    unlink((dir + "/src.json").c_str());
    ok &= check(mkdir((dir + "/src.json").c_str(), 0755) == 0, "could not stage an undeletable source");

    std::string error;
    ok &= check(!registry.migrate_config("src.json", draft, &error),
                "a migration whose source could not be removed reported success");
    ok &= check(error.find("rolled back") != std::string::npos,
                "the failure does not say the migration was rolled back: " + error);
    // The rollback itself succeeded here, so the message must be the clean one and
    // not the both-files-on-disk warning.
    ok &= check(error.find("must be resolved by hand") == std::string::npos,
                "a successful rollback was reported as a partial failure: " + error);
    // The half-finished target must not survive.
    ok &= check(!file_exists(dir + "/renamed-away.json"),
                "the new target survived a migration that could not remove its source");

    rmdir((dir + "/src.json").c_str());
    rmdir(dir.c_str());
  }

  {
    // Only files this registry enumerated are addressable, so a path cannot be
    // traversed out of the config directory and an invalid draft is refused.
    const std::string dir = make_temp_dir();
    write_file(dir + "/anvil.json", v2_profile());
    v4l2diag::ProfileRegistry registry(dir);
    DeviceProfile draft = registry.stored_configs().front().draft;
    complete_routing(&draft);
    std::string error;
    ok &= check(!registry.migrate_config("../escape.json", draft, &error),
                "a traversing source file name was accepted");
    ok &= check(!registry.migrate_config("/etc/passwd", draft, &error), "an absolute source path was accepted");
    ok &= check(!registry.migrate_config("absent.json", draft, &error), "an unknown source file was accepted");

    DeviceProfile broken = draft;
    broken.trigger_channels.clear();
    ok &= check(!registry.migrate_config("anvil.json", broken, &error), "an invalid draft was committed");
    ok &= check(file_exists(dir + "/anvil.json"), "a refused migration removed the source file");
    unlink((dir + "/anvil.json").c_str());
    rmdir(dir.c_str());
  }

  return ok ? 0 : 1;
}
