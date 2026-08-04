#include "v4l2diag/core/threshold_registry.hpp"

#include "v4l2diag/core/test_registry.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <iostream>
#include <set>
#include <vector>
#include <string>
#include <unistd.h>

namespace {

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-threshold-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : "/tmp/v4l2diag-threshold-test";
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in.good()) {
    return std::string();
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << "\n";
  }
  return condition;
}

// A preset as it was written before the tests were renumbered: every numeric
// prefix is one lower than today's, "t24-max-fps" is a test that no longer exists
// at all, and the threshold entry for the format sweep is meaningless because that
// test defines run parameters only.
void write_stale_config(const std::string &path) {
  std::ofstream out(path);
  out << R"({
  "schema_version": 2,
  "id": "default",
  "name": "Default",
  "description": "written before the tests were renumbered",
  "values": {
    "t22-sustained-capture": { "pass_rate_pct": 91 },
    "t16-format-comparison": { "throughput_reps": 12 },
    "t24-max-fps": { "min_fps": 42 }
  },
  "params": {
    "t16-format-comparison": { "throughput_reps": 12 },
    "t17-control-sweep": { "sample_count": 33 },
    "t24-max-fps": { "sample_count": 7 }
  }
})";
}

}  // namespace

int main() {
  const std::string dir = make_temp_dir();

  std::set<std::string> real_ids;
  for (const auto &test : v4l2diag::built_in_tests()) {
    real_ids.insert(test.id);
  }

  bool ok = true;

  // Every id the built-in default configures must be a real test id. A drift here
  // is what let a stale on-disk preset go unnoticed.
  const v4l2diag::ThresholdConfig defaults = v4l2diag::default_threshold_config();
  for (const auto &entry : defaults.values) {
    ok &= require(real_ids.count(entry.first) != 0,
                  "built-in default threshold references unknown test id: " + entry.first);
  }
  for (const auto &entry : v4l2diag::default_test_params()) {
    ok &=
        require(real_ids.count(entry.first) != 0, "built-in default params reference unknown test id: " + entry.first);
  }

  write_stale_config(dir + "/default.json");
  v4l2diag::ThresholdRegistry registry(dir);
  v4l2diag::ThresholdConfig loaded;
  if (!require(registry.get_config("default", &loaded), "stale config was not loaded at all")) {
    return 1;
  }

  // Nothing a stored preset contains may introduce a test that does not exist:
  // that made the configuration page render cards for dead tests while hiding
  // every live one.
  for (const auto &entry : loaded.values) {
    ok &= require(real_ids.count(entry.first) != 0, "loaded config kept unknown value test id: " + entry.first);
  }
  for (const auto &entry : loaded.params) {
    ok &= require(real_ids.count(entry.first) != 0, "loaded config kept unknown param test id: " + entry.first);
  }

  // Renumbered ids are migrated by suffix, so configured values survive.
  ok &= require(loaded.values.count("t23-sustained-capture") == 1,
                "t22-sustained-capture was not migrated to t23-sustained-capture");
  ok &= require(loaded.values.count("t22-sustained-capture") == 0,
                "the stale value id was kept alongside the migrated one");
  ok &= require(loaded.values["t23-sustained-capture"]["pass_rate_pct"] == 91,
                "migrated threshold lost its configured value");
  ok &=
      require(loaded.params.count("t18-control-sweep") == 1, "t17-control-sweep was not migrated to t18-control-sweep");
  ok &=
      require(loaded.params["t18-control-sweep"]["sample_count"] == 33, "migrated parameter lost its configured value");
  ok &= require(loaded.params.count("t17-format-comparison") == 1,
                "t16-format-comparison was not migrated to t17-format-comparison");
  ok &= require(loaded.params["t17-format-comparison"]["throughput_reps"] == 12,
                "migrated parameter lost its configured value");

  // A suffix that matches nothing is dropped rather than carried forward.
  ok &= require(loaded.values.count("t24-max-fps") == 0, "a removed test was kept in values");
  ok &= require(loaded.params.count("t24-max-fps") == 0, "a removed test was kept in params");

  // The format sweep defines run parameters but no verdict thresholds, so a stored
  // threshold for it is meaningless even after the id is migrated.
  ok &= require(loaded.values.count("t17-format-comparison") == 0,
                "a threshold was kept for a test that defines run parameters only");

  // resolve() still yields a fully-populated config with the migrated value on top.
  const v4l2diag::ThresholdConfig resolved = registry.resolve("default");
  ok &= require(resolved.values.size() == defaults.values.size(),
                "resolve() did not return the full built-in threshold set");
  ok &= require(resolved.get_param("t18-control-sweep", "sample_count") == 33,
                "resolve() did not overlay the migrated parameter");

  // An exact id always wins over a migrated one.
  {
    const std::string second = make_temp_dir();
    std::ofstream out(second + "/default.json");
    out << R"({"id":"default","name":"Default","values":{},"params":{
      "t17-control-sweep": { "sample_count": 1 },
      "t18-control-sweep": { "sample_count": 2 }
    }})";
    out.close();
    v4l2diag::ThresholdRegistry other(second);
    v4l2diag::ThresholdConfig both;
    other.get_config("default", &both);
    ok &=
        require(both.params["t18-control-sweep"]["sample_count"] == 2, "a migrated id overwrote an already-current id");
    unlink((second + "/default.json").c_str());
    rmdir(second.c_str());
  }

  // The default preset is read-only. remove_config() already refused it, but
  // add_or_update_config() did not, so PUT /api/thresholds/default could
  // overwrite it -- the UI's read-only badge was client-side only.
  {
    const std::string guard_dir = make_temp_dir();
    v4l2diag::ThresholdRegistry registry(guard_dir);

    v4l2diag::ThresholdConfig overwrite;
    overwrite.id = "default";
    overwrite.name = "Hijacked";
    overwrite.description = "should never be written";
    overwrite.params["t13-poll-timeout-cliff"]["sample_count"] = 1;

    // The constructor seeds default.json, so the invariant is that the
    // rejected write leaves that file byte-for-byte untouched.
    const std::string default_path = guard_dir + "/default.json";
    const std::string before = read_file(default_path);
    ok &= require(!before.empty(), "the constructor did not seed default.json");

    std::string error;
    ok &= require(!registry.add_or_update_config(overwrite, &error),
                  "add_or_update_config accepted the reserved \"default\" id");
    ok &= require(!error.empty(), "rejecting the default preset produced no error message");
    ok &= require(read_file(default_path) == before, "the rejected write still modified default.json");

    // import_config() funnels through add_or_update_config(), so the same id
    // must be refused when it arrives as imported JSON.
    std::string import_error;
    ok &= require(!registry.import_config(R"({"id":"default","name":"Hijacked"})", &import_error),
                  "import_config accepted the reserved \"default\" id");
    ok &= require(read_file(default_path) == before, "a rejected import still modified default.json");

    // The built-in default must still be readable and unchanged.
    v4l2diag::ThresholdConfig fallback;
    ok &= require(registry.get_config("default", &fallback), "the built-in default became unreadable");
    ok &= require(fallback.name != "Hijacked", "the built-in default was replaced");

    // A non-reserved id still round-trips.
    v4l2diag::ThresholdConfig custom = v4l2diag::default_threshold_config();
    custom.id = "stress-test";
    custom.name = "Stress Test";
    ok &= require(registry.add_or_update_config(custom, &error),
                  "add_or_update_config rejected a normal id: " + error);
    v4l2diag::ThresholdConfig read_back;
    ok &= require(registry.get_config("stress-test", &read_back) && read_back.name == "Stress Test",
                  "a normal config did not round-trip");

    unlink((guard_dir + "/stress-test.json").c_str());
    unlink((guard_dir + "/default.json").c_str());
    rmdir(guard_dir.c_str());
  }

  unlink((dir + "/default.json").c_str());
  rmdir(dir.c_str());

  if (!ok) {
    return 1;
  }
  std::cout << "threshold_registry tests passed\n";
  return 0;
}
