// Rules that need a run's TriggerMode together with its role routing (plan 2.5).
//
// These live in one place so the CLI, the web server AND the runner cannot drift
// into three ideas of what a valid run is. The runner is included deliberately:
// core is reachable directly, so a caller that never touches the CLI or the API
// must not be able to build a run that says "free-run" and names a profile.
#include "v4l2diag/core/run_routing.hpp"

#include "v4l2diag/core/diagnostic_runner.hpp"
#include "v4l2diag/core/profile_registry.hpp"

#include <fstream>
#include <iostream>
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

std::string make_temp_dir(const char *tag) {
  std::string pattern = std::string("/tmp/v4l2diag-run-routing-") + tag + "-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : std::string("/tmp/v4l2diag-run-routing-") + tag;
}

void write_file(const std::string &path, const std::string &text) {
  std::ofstream out(path, std::ios::trunc);
  out << text;
}

}  // namespace

int main() {
  using v4l2diag::TriggerMode;
  bool ok = true;

  // --- 1. Channel/mode compatibility --------------------------------------
  {
    ok &= check(v4l2diag::channel_matches_mode(TriggerMode::Hardware, /*channel_is_hardware=*/true),
                "a hardware channel was refused for a hardware run");
    ok &= check(!v4l2diag::channel_matches_mode(TriggerMode::Hardware, /*channel_is_hardware=*/false),
                "a software channel was accepted for a hardware run");
    ok &= check(v4l2diag::channel_matches_mode(TriggerMode::Software, /*channel_is_hardware=*/false),
                "a software channel was refused for a software run");
    ok &= check(!v4l2diag::channel_matches_mode(TriggerMode::Software, /*channel_is_hardware=*/true),
                "a hardware channel was accepted for a software run");
    // Free-run routes nothing, so there is no channel to be incompatible with.
    ok &= check(v4l2diag::channel_matches_mode(TriggerMode::FreeRun, true) &&
                    v4l2diag::channel_matches_mode(TriggerMode::FreeRun, false),
                "free-run demanded a particular channel type");

    // The message names the role and the channel, so a caller can act on it.
    const std::string mismatch =
        v4l2diag::describe_mode_mismatch(TriggerMode::Software, /*channel_is_hardware=*/true, "slave-1", "gpio-3");
    ok &= check(!mismatch.empty(), "an incompatible channel produced no message");
    ok &= check(mismatch.find("slave-1") != std::string::npos && mismatch.find("gpio-3") != std::string::npos,
                "the mismatch message does not name the role and channel: " + mismatch);
    ok &= check(v4l2diag::describe_mode_mismatch(TriggerMode::Hardware, true, "master", "gpio-0").empty(),
                "a compatible channel produced a message");
  }

  // --- 2. Free-run normalisation ------------------------------------------
  {
    ok &= check(v4l2diag::effective_trigger_profile_id(TriggerMode::FreeRun, "bench-rig").empty(),
                "free-run kept a trigger profile id");
    ok &= check(v4l2diag::effective_trigger_profile_id(TriggerMode::Hardware, "bench-rig") == "bench-rig",
                "a hardware run lost its trigger profile id");
    ok &= check(v4l2diag::effective_trigger_profile_id(TriggerMode::Software, "bench-rig") == "bench-rig",
                "a software run lost its trigger profile id");
  }

  // --- 3. The runner normalises at its own entry point --------------------
  {
    // Reached directly, without the CLI or the JSON parser in the way. Both of
    // those normalise too, so this is the case that proves the runner does not
    // simply trust them.
    const std::string config_dir = make_temp_dir("config");
    write_file(config_dir + "/bench-rig.json", R"({
  "schema_version": 4,
  "id": "bench-rig",
  "name": "Bench-rig",
  "description": "",
  "defaults": {
    "trigger_mode": "hardware", "memory_backends": ["mmap"], "test_selectors": ["stable"],
    "report_formats": ["json"], "trigger_rate_hz": 17.0, "pulse_width_ms": 4.0
  },
  "trigger_channels": [
    {"id": "channel-a", "name": "Channel A", "description": "", "type": "hardware",
     "gpio": {"chip_id": 0, "line_number": 9}}
  ],
  "role_bindings": [{"role": "master", "trigger_channel_id": "channel-a"}]
})");
    const std::string report_dir = make_temp_dir("reports");

    v4l2diag::ProfileRegistry profiles(config_dir);
    v4l2diag::DiagnosticRunner runner(&profiles);

    v4l2diag::RunConfig config;
    config.trigger_mode = TriggerMode::FreeRun;
    // The contradiction: free-run, yet a profile is named.
    config.trigger_profile_id = "bench-rig";
    config.master.path = "/dev/null";
    config.output_directory = report_dir;
    // One fast test: the point is the run-level contract, not the verdict. Against
    // a non-camera the default selector would burn every test's timeout.
    config.test_selectors.push_back("t01-device-compliance");

    const v4l2diag::RunResult result = runner.run(config);

    ok &= check(result.trigger_mode == TriggerMode::FreeRun, "the runner changed the trigger mode");
    ok &=
        check(result.trigger_profile_id.empty(),
              "the runner produced a free-run result naming a trigger profile: \"" + result.trigger_profile_id + "\"");
    ok &= check(result.role_bindings.empty(), "the runner produced a free-run result carrying role bindings");
    // The fields still HOLD values here -- they are plain doubles, so absence is not
    // representable -- but they must be the struct defaults rather than the
    // profile's 17 Hz / 4 ms. Under free-run those defaults are meaningless, and the
    // contract is that consumers do not render them; the report and API tests check
    // that they are absent from free-run OUTPUT.
    //
    // Compared against the default rather than "not 17" on purpose: free-run never
    // loads the profile, so an unnormalised runner would leave the default anyway
    // and "!= 17" would pass without proving anything. The triggered case below is
    // what shows the field is filled at all.
    const v4l2diag::RunResult untouched;
    ok &= check(
        result.trigger_rate_hz == untouched.trigger_rate_hz && result.pulse_width_ms == untouched.pulse_width_ms,
        "a free-run result carries profile timing instead of the default: " + std::to_string(result.trigger_rate_hz) +
            " Hz / " + std::to_string(result.pulse_width_ms) + " ms");

    // And a triggered run DOES carry all of it, so the normalisation is not just
    // blanking everything.
    v4l2diag::RunConfig triggered = config;
    triggered.trigger_mode = TriggerMode::Hardware;
    const v4l2diag::RunResult armed = runner.run(triggered);
    ok &= check(armed.trigger_profile_id == "bench-rig", "a hardware run lost its trigger profile id");
    ok &= check(armed.role_bindings.size() == 1 && armed.role_bindings.front().role == "master",
                "a hardware run lost its resolved routing");
    ok &= check(armed.trigger_rate_hz == 17.0, "a hardware run did not take the profile's trigger rate");
    ok &= check(armed.pulse_width_ms == 4.0, "a hardware run did not take the profile's pulse width");

    unlink((config_dir + "/bench-rig.json").c_str());
    rmdir(config_dir.c_str());
    rmdir(report_dir.c_str());
  }

  return ok ? 0 : 1;
}
