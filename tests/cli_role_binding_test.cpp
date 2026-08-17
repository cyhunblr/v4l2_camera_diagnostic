// CLI contract for role-based routing (plan 2.5.4 - 2.5.6).
//
// Drives the real binary, because the rules being locked here are argument
// parsing and refusal behaviour -- a unit test of the resolver cannot show that
// the CLI stopped taking `--trigger-channel` or stopped picking a channel for the
// user. The CLI must not have its own idea of a valid routing: it goes through the
// same resolve_role_bindings() seam the web server uses.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
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

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-cli-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : "/tmp/v4l2diag-cli-test";
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in.good()) {
    return std::string();
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

bool file_exists(const std::string &path) {
  return std::ifstream(path).good();
}

struct Outcome {
  int status = 0;
  std::string output;  // stdout and stderr combined
};

// The binary path is injected by CMake so the test does not guess at the build
// layout.
const char *binary() {
  return V4L2DIAG_CLI_BINARY;
}

Outcome run_cli(const std::string &args) {
  const std::string command = std::string(binary()) + " " + args + " 2>&1";
  Outcome out;
  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe) {
    out.status = -1;
    return out;
  }
  std::array<char, 4096> buffer{};
  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    out.output += buffer.data();
  }
  const int closed = pclose(pipe);
  out.status = WIFEXITED(closed) ? WEXITSTATUS(closed) : -1;
  return out;
}

}  // namespace

int main() {
  bool ok = true;
  const std::string dir = make_temp_dir();
  const std::string quoted_dir = "'" + dir + "'";

  // --- 1. `profiles add` refuses to invent a binding -----------------------
  {
    // Exactly one channel, which is the tempting case: the old CLI would have
    // taken it silently. A profile with no routing cannot start a triggered run,
    // so it is refused at save time rather than stored and failed later.
    const Outcome res = run_cli("profiles add --id single --name Single --gpio 0:0:100:MASTER --config-dir " +
                                quoted_dir);
    ok &= check(res.status == 2, "profiles add with one channel and no --bind-role exited " +
                                    std::to_string(res.status) + ": " + res.output);
    ok &= check(res.output.find("--bind-role") != std::string::npos,
                "the refusal does not point at --bind-role: " + res.output);
    ok &= check(!file_exists(dir + "/single.json"), "a profile with no role binding was written to disk");
  }

  // --- 2. An explicit master binding produces a valid v4 profile -----------
  {
    const Outcome res = run_cli("profiles add --id rig --name Rig --gpio 0:0:100:MASTER"
                                " --bind-role master:gpio-0 --config-dir " +
                                quoted_dir);
    ok &= check(res.status == 0, "profiles add with an explicit binding exited " + std::to_string(res.status) + ": " +
                                    res.output);
    const std::string stored = read_file(dir + "/rig.json");
    ok &= check(!stored.empty(), "the profile was not written");
    // v5: no report_formats in a freshly written profile (plan 2.10).
    ok &= check(stored.find("\"schema_version\" : 5") != std::string::npos ||
                    stored.find("\"schema_version\": 5") != std::string::npos,
                "the stored profile is not at schema v5: " + stored);
    ok &= check(stored.find("report_formats") == std::string::npos,
                "a newly written profile still carries report_formats: " + stored);
    ok &= check(stored.find("role_bindings") != std::string::npos, "the stored profile has no role_bindings");
    ok &= check(stored.find("\"master\"") != std::string::npos, "the stored profile does not bind master");
    ok &= check(stored.find("gpio-0") != std::string::npos, "the stored binding lost its channel id");
    // v4 dropped physical matching, so neither key may reappear.
    ok &= check(stored.find("camera_match") == std::string::npos, "the stored profile still carries camera_match");
    ok &= check(stored.find("camera_bindings") == std::string::npos,
                "the stored profile still carries camera_bindings");
  }

  // --- 3. Two channels, two roles -----------------------------------------
  {
    const Outcome res = run_cli("profiles add --id pair --name Pair --gpio 0:0:100:M --gpio 1:0:101:S"
                                " --bind-role master:gpio-0 --bind-role slave-1:gpio-1 --config-dir " +
                                quoted_dir);
    ok &= check(res.status == 0, "a two-role profile was refused: " + res.output);
    const std::string stored = read_file(dir + "/pair.json");
    ok &= check(stored.find("slave-1") != std::string::npos, "the slave-1 binding was not stored");
    ok &= check(stored.find("gpio-1") != std::string::npos, "the slave-1 channel was not stored");
  }

  // --- 4. The central validator rejects bad bindings ----------------------
  {
    struct Case {
      const char *args;
      const char *what;
    };
    // Exit code 2 is an argument-level refusal (the CLI parsed nothing usable);
    // 1 is a validator refusal. Both are failures -- what matters is that the
    // profile is never written, and that the rules come from
    // validate_device_profile() rather than from a second copy in the CLI.
    const Case cases[] = {
        {"--bind-role master:gpio-9", "a binding naming a channel the profile does not define"},
        {"--bind-role master:gpio-0 --bind-role master:gpio-0", "a duplicate role"},
        {"--bind-role primary:gpio-0", "a free-text role name"},
        {"--bind-role slave-0:gpio-0", "slave-0, which the 1-based numbering never produces"},
    };
    for (const auto &item : cases) {
      const Outcome res = run_cli(std::string("profiles add --id bad --name Bad --gpio 0:0:100:M ") + item.args +
                                  " --config-dir " + quoted_dir);
      ok &= check(res.status != 0, std::string("profiles add accepted ") + item.what + ": " + res.output);
      ok &= check(!file_exists(dir + "/bad.json"),
                  std::string("a profile with ") + item.what + " reached the disk");
    }
  }

  // --- 5. `--trigger-channel` is gone -------------------------------------
  {
    // Not silently ignored: an unknown option is an error, so a script still
    // passing it fails loudly instead of running with a routing nobody chose.
    const Outcome res = run_cli("run --camera /dev/null --trigger-channel gpio-0 --config-dir " + quoted_dir);
    ok &= check(res.status == 2, "--trigger-channel was still accepted: " + res.output);
    ok &= check(res.output.find("Unknown option") != std::string::npos,
                "--trigger-channel was not reported as an unknown option: " + res.output);
  }
  {
    // And the help text no longer advertises it, but does advertise --bind-role.
    const Outcome res = run_cli("--help");
    ok &= check(res.output.find("--trigger-channel") == std::string::npos,
                "the help text still lists --trigger-channel");
    ok &= check(res.output.find("--bind-role") != std::string::npos, "the help text does not document --bind-role");
  }

  // --- 6. A run refuses an incomplete routing, in the shared wording -------
  {
    // "rig" binds master only. A two-camera run expects master and slave-1, so it
    // must be refused rather than completed -- and the message has to come from
    // describe_role_resolution(), the same text the API returns.
    const Outcome res = run_cli("run --camera /dev/null --camera /dev/zero --trigger-mode hardware"
                                " --profile rig --config-dir " +
                                quoted_dir);
    ok &= check(res.status == 2, "a run with an unbound slave-1 was not refused: " + res.output);
    ok &= check(res.output.find("slave-1") != std::string::npos,
                "the refusal does not name the missing role: " + res.output);
    ok &= check(res.output.find("does not bind these run roles") != std::string::npos,
                "the CLI is not using the shared resolver wording: " + res.output);
  }

  // --- 6b. A slave-only profile cannot be saved ---------------------------
  {
    // Every run has a master, so binding only slave-1 produces a profile that can
    // never start one. Refused at save time by the central validator.
    const Outcome res = run_cli("profiles add --id slaveonly --name SlaveOnly --gpio 0:0:100:M"
                                " --bind-role slave-1:gpio-0 --config-dir " +
                                quoted_dir);
    ok &= check(res.status != 0, "a slave-only profile was saved: " + res.output);
    ok &= check(res.output.find("master") != std::string::npos,
                "the refusal does not name the missing master role: " + res.output);
    ok &= check(!file_exists(dir + "/slaveonly.json"), "a slave-only profile reached the disk");
  }

  // --- 6c. Channel type must match the trigger mode -----------------------
  {
    // "rig" holds a HARDWARE channel. A software-trigger run against it has to be
    // refused before it starts -- this check lived only in the web server, so the
    // CLI would have opened the wrong kind of trigger.
    const Outcome res = run_cli("run --camera /dev/null --trigger-mode software --profile rig --config-dir " +
                                quoted_dir);
    ok &= check(res.status == 2, "a hardware channel was accepted for a software run: " + res.output);
    ok &= check(res.output.find("hardware channel") != std::string::npos &&
                    res.output.find("software") != std::string::npos,
                "the refusal does not describe the channel/mode mismatch: " + res.output);
  }

  // --- 7. A triggered run requires --profile ------------------------------
  {
    const Outcome res = run_cli("run --camera /dev/null --trigger-mode hardware --config-dir " + quoted_dir);
    ok &= check(res.status == 2, "a triggered run without --profile was accepted: " + res.output);
    ok &= check(res.output.find("--profile") != std::string::npos,
                "the refusal does not mention --profile: " + res.output);
  }

  // --- 8. Free-run asks for no routing ------------------------------------
  {
    // /dev/null is not a camera, so the run itself cannot succeed; what matters is
    // that it is not refused for a MISSING ROUTING. Free-run does not route.
    //
    // Pinned to one fast test by id: the default selector runs the whole suite,
    // and against a non-camera every test burns its full timeout -- that alone
    // took ~55s of the CTest wall clock.
    const Outcome res = run_cli("run --camera /dev/null --trigger-mode free-run --tests t01-device-compliance"
                                " --report json --output-dir " +
                                quoted_dir + " --config-dir " + quoted_dir);
    ok &= check(res.output.find("does not bind these run roles") == std::string::npos,
                "free-run demanded a role binding: " + res.output);
    ok &= check(res.output.find("--profile") == std::string::npos,
                "free-run demanded a trigger profile: " + res.output);
  }

  // --- 9. An unwritable report directory fails loudly ---------------------
  {
    // A report that cannot be written is a failed run. Before the guard in main(),
    // ReportWriteError escaped as an uncaught exception and the process aborted with
    // no usable message -- and, worse, could look like it had produced reports.
    const std::string blocker = dir + "/not-a-dir";
    { std::ofstream out(blocker); out << "x"; }
    const Outcome res = run_cli("run --camera /dev/null --trigger-mode free-run --tests t01-device-compliance"
                               " --output-dir '" + blocker + "/reports' --config-dir " + quoted_dir);
    ok &= check(res.status != 0, "an unwritable report directory returned success: " + res.output);
    // Not a crash: a message the user can act on.
    ok &= check(res.output.find("Report generation failed") != std::string::npos,
                "the CLI did not report the failure in words: " + res.output);
    ok &= check(res.output.find("Aborted") == std::string::npos &&
                    res.output.find("terminate called") == std::string::npos,
                "the CLI died on an uncaught exception instead of reporting it: " + res.output);
    unlink(blocker.c_str());
  }

  for (const char *name : {"rig.json", "pair.json", "single.json", "bad.json", "slaveonly.json"}) {
    unlink((dir + "/" + name).c_str());
  }
  rmdir(dir.c_str());
  return ok ? 0 : 1;
}
