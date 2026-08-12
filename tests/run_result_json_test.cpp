// The canonical structured result: one serializer for the live API and the JSON
// artifact (plan 2.6.2), plus the reader that restores it after a restart (2.6.3).
//
// The two used to be hand-maintained copies and had already drifted -- `project_name`
// vs `project`, `camera_path` vs `path`, empty arrays one wrote and the other
// omitted. That is only a cosmetic problem until the API starts reading the artifact
// back, at which point "the restored result" and "the result served live" stop being
// the same document.
#include "v4l2diag/core/run_result_json.hpp"

#include <json/json.h>

#include <iostream>
#include <sstream>
#include <string>
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

std::string to_text(const Json::Value &value) {
  Json::StreamWriterBuilder builder;
  return Json::writeString(builder, value);
}

// A run result with every run-level field populated, so a round-trip has something
// to lose.
v4l2diag::RunResult sample() {
  v4l2diag::RunResult result;
  result.project_name = "v4l2-camera-diagnostic";
  result.started_at_utc = "2026-08-04T10:00:00Z";
  result.finished_at_utc = "2026-08-04T10:06:33Z";
  result.host_name = "ice00";
  result.kernel_release = "5.15.0-139-generic";
  result.kernel_version = "#1 SMP";
  result.output_directory = "/tmp/reports/run-1";
  result.run_mode = v4l2diag::RunMode::Sequential;
  result.trigger_mode = v4l2diag::TriggerMode::Hardware;
  result.trigger_profile_id = "bench-rig";
  result.trigger_rate_hz = 12.5;
  result.pulse_width_ms = 3.25;
  {
    v4l2diag::RoleBinding master;
    master.role = "master";
    master.trigger_channel_id = "trigger-channel-a";
    result.role_bindings.push_back(master);
    v4l2diag::RoleBinding slave;
    slave.role = "slave-1";
    slave.trigger_channel_id = "trigger-channel-b";
    result.role_bindings.push_back(slave);
  }

  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video0";
  camera.role = "master";
  camera.trigger_description = "gpiochip0 line 108";
  camera.memory_backends.push_back(v4l2diag::MemoryBackend::Mmap);

  v4l2diag::TestResult test;
  test.id = "t14-trigger-latency";
  test.name = "Trigger Latency";
  test.category = "performance";
  test.memory_backend = "mmap";
  test.status = v4l2diag::TestStatus::Pass;
  test.summary = "Latency is stable.";
  test.duration_ms = 1050.0;
  test.details.push_back("coarse: 150ms -> 10/10");
  test.notes.push_back("Only one pixel format is offered.");
  test.warnings.push_back("Sample count was reduced.");
  v4l2diag::MetricValue metric;
  metric.name = "latency_mean";
  metric.value = 4.25;
  metric.unit = "ms";
  test.metrics.push_back(metric);
  camera.tests.push_back(test);
  result.cameras.push_back(camera);
  return result;
}

}  // namespace

int main() {
  bool ok = true;
  const v4l2diag::RunResult original = sample();
  const Json::Value doc = v4l2diag::run_result_to_json(original);

  // --- 1. The document is versioned --------------------------------------
  {
    ok &= check(doc.isMember("result_schema_version"), "the result document carries no schema version");
    ok &= check(doc["result_schema_version"].asInt() == v4l2diag::kResultSchemaVersion,
                "the result document reports the wrong schema version");
  }

  // --- 2. One shape: the canonical field names ---------------------------
  {
    // These are the two names that had drifted between the API and the artifact.
    ok &= check(doc.isMember("project_name"), "the canonical document is missing project_name");
    ok &= check(!doc.isMember("project"), "the canonical document still writes the legacy \"project\" key");
    if (check(doc["cameras"].size() == 1, "the camera array is not as expected")) {
      const Json::Value &camera = doc["cameras"][0];
      ok &= check(camera.isMember("camera_path"), "the canonical camera object is missing camera_path");
      ok &= check(!camera.isMember("path"), "the canonical camera object still writes the legacy \"path\" key");
      // Run-level facts are not repeated per camera (plan 2.5, report model (a)).
      for (const char *field : {"profile_id", "trigger_channel_id", "trigger_mode", "trigger_profile_id",
                                "role_bindings", "trigger_rate_hz", "pulse_width_ms"}) {
        ok &= check(!camera.isMember(field),
                    std::string("the camera object still carries the run-level \"") + field + "\"");
      }
    }
  }

  // --- 3. Empty arrays are written, never omitted ------------------------
  {
    // A missing key becomes undefined.map() in the UI, so the shape must not depend
    // on whether a list happened to be empty.
    v4l2diag::RunResult bare;
    bare.cameras.push_back(v4l2diag::CameraRunResult());
    const Json::Value empty = v4l2diag::run_result_to_json(bare);
    for (const char *field : {"role_bindings", "cameras"}) {
      ok &= check(empty[field].isArray(), std::string("\"") + field + "\" is not an array when empty");
    }
    if (empty["cameras"].size() == 1) {
      for (const char *field : {"memory_backends", "tests"}) {
        ok &= check(empty["cameras"][0][field].isArray(),
                    std::string("camera \"") + field + "\" is not an array when empty");
      }
    }
  }

  // --- 4. Round-trip: the restored result equals the live one -------------
  {
    v4l2diag::RunResult restored;
    std::string error;
    ok &= check(v4l2diag::run_result_from_json(doc, &restored, &error),
                "the canonical document did not parse back: " + error);

    ok &= check(restored.project_name == original.project_name, "project_name was lost");
    ok &= check(restored.started_at_utc == original.started_at_utc, "started_at_utc was lost");
    ok &= check(restored.finished_at_utc == original.finished_at_utc, "finished_at_utc was lost");
    ok &= check(restored.host_name == original.host_name, "host_name was lost");
    ok &= check(restored.kernel_release == original.kernel_release, "kernel_release was lost");
    ok &= check(restored.run_mode == original.run_mode, "run_mode was lost");
    ok &= check(restored.trigger_mode == original.trigger_mode, "trigger_mode was lost");
    ok &= check(restored.trigger_profile_id == original.trigger_profile_id, "trigger_profile_id was lost");
    ok &= check(restored.trigger_rate_hz == original.trigger_rate_hz, "trigger_rate_hz was lost");
    ok &= check(restored.pulse_width_ms == original.pulse_width_ms, "pulse_width_ms was lost");
    ok &= check(restored.role_bindings.size() == 2, "role_bindings were lost");
    if (restored.role_bindings.size() == 2) {
      ok &= check(restored.role_bindings[0].role == "master" &&
                      restored.role_bindings[0].trigger_channel_id == "trigger-channel-a",
                  "the master binding did not round-trip");
      ok &= check(restored.role_bindings[1].role == "slave-1", "the slave-1 binding did not round-trip");
    }

    if (check(restored.cameras.size() == 1, "the camera did not round-trip")) {
      const auto &camera = restored.cameras[0];
      ok &= check(camera.camera_path == "/dev/video0", "camera_path did not round-trip");
      ok &= check(camera.role == "master", "the camera role did not round-trip");
      ok &= check(camera.trigger_description == "gpiochip0 line 108", "trigger_description did not round-trip");
      ok &= check(camera.memory_backends.size() == 1, "memory_backends did not round-trip");
      if (check(camera.tests.size() == 1, "the test did not round-trip")) {
        const auto &test = camera.tests[0];
        ok &= check(test.id == "t14-trigger-latency", "the test id did not round-trip");
        ok &= check(test.status == v4l2diag::TestStatus::Pass, "the test status did not round-trip");
        ok &= check(test.summary == "Latency is stable.", "the test summary did not round-trip");
        ok &= check(test.duration_ms == 1050.0, "the test duration did not round-trip");
        ok &= check(test.details.size() == 1, "the test details did not round-trip");
        ok &= check(test.notes.size() == 1, "the test notes did not round-trip");
        ok &= check(test.warnings.size() == 1, "the test warnings did not round-trip");
        if (check(test.metrics.size() == 1, "the test metrics did not round-trip")) {
          ok &= check(
              test.metrics[0].name == "latency_mean" && test.metrics[0].value == 4.25 && test.metrics[0].unit == "ms",
              "a metric did not round-trip intact");
        }
      }
    }

    // Serialising the restored result yields the same document, so nothing silently
    // changes shape on the way through.
    ok &= check(to_text(v4l2diag::run_result_to_json(restored)) == to_text(doc),
                "re-serialising a restored result produced a different document");
  }

  // --- 5. Legacy artifacts: known names are normalised -------------------
  {
    // What earlier builds wrote. Only the field names are adapted; nothing is
    // invented.
    const std::string legacy = R"({
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
})";
    v4l2diag::RunResult restored;
    std::string error;
    ok &= check(v4l2diag::run_result_from_json(parse(legacy), &restored, &error),
                "a legacy artifact was refused: " + error);
    ok &= check(restored.project_name == "v4l2-camera-diagnostic", "legacy \"project\" was not mapped");
    ok &= check(restored.host_name == "old-host", "legacy host_name was lost");
    if (check(restored.cameras.size() == 1, "the legacy camera was lost")) {
      ok &= check(restored.cameras[0].camera_path == "/dev/video9", "legacy \"path\" was not mapped");
    }

    // --- 6. Absent fields are NOT invented ----------------------------
    //
    // A legacy artifact has no run-level trigger contract. Reading a plausible value
    // out of the old per-camera fields would put a routing decision in the report
    // that the run never made.
    ok &= check(restored.trigger_profile_id.empty(),
                "a trigger_profile_id was invented for a legacy artifact: \"" + restored.trigger_profile_id + "\"");
    ok &= check(restored.role_bindings.empty(), "role_bindings were invented for a legacy artifact");
    ok &= check(restored.cameras.empty() || restored.cameras[0].role.empty(),
                "a camera role was invented for a legacy artifact");
    const v4l2diag::RunResult untouched;
    ok &= check(
        restored.trigger_rate_hz == untouched.trigger_rate_hz && restored.pulse_width_ms == untouched.pulse_width_ms,
        "trigger timing was taken from a legacy artifact's per-camera fields");
  }

  // --- 7. A newer schema is refused, not parsed as current ---------------
  {
    Json::Value future = doc;
    future["result_schema_version"] = v4l2diag::kResultSchemaVersion + 1;
    v4l2diag::RunResult restored;
    std::string error;
    ok &= check(!v4l2diag::run_result_from_json(future, &restored, &error),
                "a result document from a newer build was parsed as if it were current");
    ok &= check(error.find("newer") != std::string::npos || error.find("schema") != std::string::npos,
                "the refusal does not explain the version problem: " + error);
  }

  // --- 7b. Only a supported version is accepted --------------------------
  {
    // v1 is the only version that exists. 0, negatives and unknown older numbers are
    // not "close enough" -- they describe a shape this build has never written, so
    // parsing them as current would serve a wrong result confidently.
    //
    // A document with NO version is different: that is a pre-versioning artifact and
    // goes through the legacy adapter (covered in section 5).
    for (const int version : {0, -1, 99}) {
      Json::Value bad = doc;
      bad["result_schema_version"] = version;
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                  "result_schema_version " + std::to_string(version) + " was accepted");
      ok &= check(!error.empty(), "result_schema_version " + std::to_string(version) + " was refused without a reason");
    }
    // Non-integral too.
    for (const char *text : {R"("one")", R"(1.5)", R"(null)", R"([1])"}) {
      Json::Value bad = doc;
      bad["result_schema_version"] = parse(text);
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                  std::string("a non-integral result_schema_version was accepted: ") + text);
    }
  }

  // --- 7c. Unknown enum values are refused, not defaulted ----------------
  {
    // Silently falling back to the enum's default would put a mode in the restored
    // result that the run never had -- sequential when it was parallel, free-run when
    // it was hardware.
    for (const char *field : {"run_mode", "trigger_mode"}) {
      Json::Value bad = doc;
      bad[field] = "telepathy";
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                  std::string("an unknown ") + field + " was accepted");
      ok &= check(error.find(field) != std::string::npos,
                  std::string("the refusal does not name the offending field (") + field + "): " + error);
    }
    // And an unknown test status, which decides how a result is presented.
    {
      Json::Value bad = doc;
      bad["cameras"][0]["tests"][0]["status"] = "maybe";
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error), "an unknown test status was accepted");
    }
  }

  // --- 7d. A present field must have the expected JSON type --------------
  {
    struct Case {
      const char *pointer;
      const char *replacement;
      const char *what;
    };
    // Each of these was previously iterated with a range-for, which silently yields
    // nothing for a non-array -- so a corrupt artifact looked like an empty one.
    const Case cases[] = {
        {"role_bindings", R"("not an array")", "role_bindings as a string"},
        {"role_bindings", R"({"role": "master"})", "role_bindings as an object"},
    };
    for (const auto &item : cases) {
      Json::Value bad = doc;
      bad[item.pointer] = parse(item.replacement);
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                  std::string("a malformed artifact was accepted: ") + item.what);
    }
    for (const char *field : {"memory_backends", "tests"}) {
      Json::Value bad = doc;
      bad["cameras"][0][field] = "not an array";
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                  std::string("a camera with a non-array \"") + field + "\" was accepted");
    }
    for (const char *field : {"metrics", "details", "notes", "warnings"}) {
      Json::Value bad = doc;
      bad["cameras"][0]["tests"][0][field] = "not an array";
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                  std::string("a test with a non-array \"") + field + "\" was accepted");
    }
  }

  // --- 7e. Unknown memory backends are refused, not dropped --------------
  {
    // Dropping it silently turned "this run used a backend I do not recognise" into
    // "this run used no backends", which reads as a successful narrower run.
    Json::Value bad = doc;
    bad["cameras"][0]["memory_backends"] = parse(R"(["mmap","papyrus"])");
    v4l2diag::RunResult restored;
    std::string error;
    ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                "an unknown memory backend was silently dropped instead of refused");
    ok &=
        check(error.find("papyrus") != std::string::npos, "the refusal does not name the offending backend: " + error);
  }

  // --- 7f. role_bindings entries must be well-formed ---------------------
  {
    for (const char *entry : {R"(["master"])", R"([{"role": "master"}])", R"([{"trigger_channel_id": "gpio-0"}])",
                              R"([{"role": "", "trigger_channel_id": ""}])"}) {
      Json::Value bad = doc;
      bad["role_bindings"] = parse(entry);
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(bad, &restored, &error),
                  std::string("a malformed role binding was accepted: ") + entry);
    }
  }

  // --- 7g. No JSON exception escapes the reader boundary -----------------
  {
    // asString()/asDouble() throw Json::LogicError on a non-scalar. A corrupt artifact
    // must come back as `false` plus a reason -- never as an exception that unwinds
    // through the HTTP handler and takes the server down.
    struct Case {
      const char *what;
      const char *json_pointer;  // dotted path, "[]" means first array element
    };
    const Case cases[] = {
        {"an object run_mode", "run_mode"},
        {"an object trigger_mode", "trigger_mode"},
        {"an object project_name", "project_name"},
        {"an object trigger_profile_id", "trigger_profile_id"},
        {"an object trigger_rate_hz", "trigger_rate_hz"},
        {"an object memory backend entry", "cameras[].memory_backends[]"},
        {"an object details entry", "cameras[].tests[].details[]"},
        {"an object metric value", "cameras[].tests[].metrics[].value"},
        {"an object duration_ms", "cameras[].tests[].duration_ms"},
        {"an object test status", "cameras[].tests[].status"},
        {"an object camera_path", "cameras[].camera_path"},
        {"an object role in a binding", "role_bindings[].role"},
    };
    for (const auto &item : cases) {
      Json::Value bad = doc;
      // Walk the dotted path and drop an object where a scalar belongs.
      Json::Value *node = &bad;
      std::string path = item.json_pointer;
      while (!path.empty()) {
        const std::size_t dot = path.find('.');
        std::string step = dot == std::string::npos ? path : path.substr(0, dot);
        path = dot == std::string::npos ? std::string() : path.substr(dot + 1);
        const bool indexed = step.size() > 2 && step.substr(step.size() - 2) == "[]";
        if (indexed) {
          step = step.substr(0, step.size() - 2);
        }
        node = &(*node)[step];
        if (indexed) {
          node = &(*node)[0u];
        }
      }
      *node = parse(R"({"unexpected": "object"})");

      v4l2diag::RunResult restored;
      std::string error;
      bool threw = false;
      bool accepted = false;
      try {
        accepted = v4l2diag::run_result_from_json(bad, &restored, &error);
      } catch (...) {
        threw = true;
      }
      ok &= check(!threw, std::string("the reader threw an exception on ") + item.what);
      ok &= check(!accepted, std::string("the reader accepted ") + item.what);
      ok &= check(!error.empty(), std::string("the reader gave no reason for ") + item.what);
      // The explicit per-field checks must be the ones doing the work. The boundary
      // catch() is a last line of defence and produces a generic jsoncpp message, so
      // seeing it here means a scalar read is no longer type-checked -- the contract
      // still holds from outside, but the reason stops naming the offending field.
      ok &= check(error.find("is malformed:") == std::string::npos,
                  std::string("only the boundary catch() refused ") + item.what +
                      "; the explicit field check is missing: " + error);
    }
  }

  // --- 8. Malformed documents are refused --------------------------------
  {
    for (const char *text : {R"([1,2,3])", R"("a string")", R"(42)", R"({"cameras": "not an array"})"}) {
      v4l2diag::RunResult restored;
      std::string error;
      ok &= check(!v4l2diag::run_result_from_json(parse(text), &restored, &error),
                  std::string("a malformed result document was accepted: ") + text);
      ok &= check(!error.empty(), std::string("a refused document gave no reason: ") + text);
    }
  }

  return ok ? 0 : 1;
}
