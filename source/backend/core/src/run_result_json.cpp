#include "v4l2diag/core/run_result_json.hpp"

#include <string>
#include <utility>
#include <vector>

namespace v4l2diag {

namespace {

Json::Value metric_to_json(const MetricValue &metric) {
  Json::Value out(Json::objectValue);
  out["name"] = metric.name;
  out["unit"] = metric.unit;
  out["value"] = metric.value;
  out["description"] = metric.description;
  return out;
}

Json::Value string_array(const std::vector<std::string> &values) {
  // Always an array, even when empty: a missing key becomes undefined.map() in the
  // UI, so the shape must not depend on whether the list happened to be filled.
  Json::Value out(Json::arrayValue);
  for (const auto &value : values) {
    out.append(value);
  }
  return out;
}

Json::Value test_to_json(const TestResult &test) {
  Json::Value out(Json::objectValue);
  out["id"] = test.id;
  out["name"] = test.name;
  out["category"] = test.category;
  out["memory_backend"] = test.memory_backend;
  out["status"] = to_string(test.status);
  out["summary"] = test.summary;
  out["duration_ms"] = test.duration_ms;
  out["metrics"] = Json::Value(Json::arrayValue);
  for (const auto &metric : test.metrics) {
    out["metrics"].append(metric_to_json(metric));
  }
  out["details"] = string_array(test.details);
  out["notes"] = string_array(test.notes);
  out["warnings"] = string_array(test.warnings);
  return out;
}

// The approved camera contract (plan 2.5, report model (a)): role, path, tests. Every
// trigger fact is run-level, so none of them is repeated here.
Json::Value camera_to_json(const CameraRunResult &camera) {
  Json::Value out(Json::objectValue);
  out["camera_path"] = camera.camera_path;
  out["role"] = camera.role;
  // Kept per camera on purpose: this is the physical resolution (which GPIO line,
  // which control), not a routing decision restated.
  out["trigger_description"] = camera.trigger_description;
  out["memory_backends"] = Json::Value(Json::arrayValue);
  for (MemoryBackend backend : camera.memory_backends) {
    out["memory_backends"].append(to_string(backend));
  }
  out["tests"] = Json::Value(Json::arrayValue);
  for (const auto &test : camera.tests) {
    out["tests"].append(test_to_json(test));
  }
  return out;
}

// A present field must have the type the shape says it has. Iterating a non-array
// with a range-for silently yields nothing, so a corrupt artifact used to look like an
// empty one -- and calling .get() on a non-object throws Json::LogicError, which took
// the whole server down.
// asString()/asDouble() throw Json::LogicError on a non-scalar, so every scalar read
// goes through one of these. Absent is fine -- only a PRESENT field is type-checked --
// which keeps the legacy adapter working while a corrupt artifact is refused.
bool read_string(const Json::Value &parent, const char *field, const char *context, std::string *out,
                 std::string *error) {
  if (!parent.isMember(field)) {
    return true;
  }
  const Json::Value &value = parent[field];
  if (!value.isString()) {
    *error = std::string(context) + " field \"" + field + "\" is present but not a string";
    return false;
  }
  *out = value.asString();
  return true;
}

bool read_double(const Json::Value &parent, const char *field, const char *context, double *out, std::string *error) {
  if (!parent.isMember(field)) {
    return true;
  }
  const Json::Value &value = parent[field];
  if (!value.isNumeric()) {
    *error = std::string(context) + " field \"" + field + "\" is present but not a number";
    return false;
  }
  *out = value.asDouble();
  return true;
}

// Array ELEMENTS have no key to name, so they are checked in place.
bool element_string(const Json::Value &value, const char *context, std::string *out, std::string *error) {
  if (!value.isString()) {
    *error = std::string(context) + " contains an entry that is not a string";
    return false;
  }
  *out = value.asString();
  return true;
}

bool require_array(const Json::Value &parent, const char *field, const char *context, std::string *error) {
  if (!parent.isMember(field)) {
    return true;  // absent is fine; only a present field is type-checked
  }
  if (parent[field].isArray()) {
    return true;
  }
  *error = std::string(context) + " field \"" + field + "\" is present but not an array";
  return false;
}

bool metric_from_json(const Json::Value &root, MetricValue *metric, std::string *error) {
  return read_string(root, "name", "metric", &metric->name, error) &&
         read_string(root, "unit", "metric", &metric->unit, error) &&
         read_double(root, "value", "metric", &metric->value, error) &&
         read_string(root, "description", "metric", &metric->description, error);
}

bool test_from_json(const Json::Value &root, TestResult *test, std::string *error) {
  if (!root.isObject()) {
    *error = "a test entry is not an object";
    return false;
  }
  std::string status;
  if (!read_string(root, "id", "test", &test->id, error) || !read_string(root, "name", "test", &test->name, error) ||
      !read_string(root, "category", "test", &test->category, error) ||
      !read_string(root, "memory_backend", "test", &test->memory_backend, error) ||
      !read_string(root, "status", "test", &status, error) ||
      !read_string(root, "summary", "test", &test->summary, error) ||
      !read_double(root, "duration_ms", "test", &test->duration_ms, error)) {
    return false;
  }
  // An unreadable status is not guessed at: it decides how the result is presented.
  if (!status.empty() && !parse_test_status(status, &test->status)) {
    *error = "\"" + status + "\" is not a test status";
    return false;
  }
  for (const char *field : {"metrics", "details", "notes", "warnings"}) {
    if (!require_array(root, field, "test", error)) {
      return false;
    }
  }
  for (const auto &value : root["metrics"]) {
    if (!value.isObject()) {
      *error = "a metric entry is not an object";
      return false;
    }
    MetricValue metric;
    if (!metric_from_json(value, &metric, error)) {
      return false;
    }
    test->metrics.push_back(metric);
  }
  for (const auto &value : root["details"]) {
    std::string line;
    if (!element_string(value, "test \"details\"", &line, error)) {
      return false;
    }
    test->details.push_back(line);
  }
  for (const auto &value : root["notes"]) {
    std::string line;
    if (!element_string(value, "test \"notes\"", &line, error)) {
      return false;
    }
    test->notes.push_back(line);
  }
  for (const auto &value : root["warnings"]) {
    std::string line;
    if (!element_string(value, "test \"warnings\"", &line, error)) {
      return false;
    }
    test->warnings.push_back(line);
  }
  return true;
}

bool camera_from_json(const Json::Value &root, CameraRunResult *camera, std::string *error) {
  if (!root.isObject()) {
    *error = "a camera entry is not an object";
    return false;
  }
  // Legacy adapter: earlier builds wrote "path". Only the NAME is adapted, and the
  // value is type-checked either way.
  const char *path_field = root.isMember("camera_path") ? "camera_path" : "path";
  if (!read_string(root, path_field, "camera", &camera->camera_path, error) ||
      // Absent in a legacy artifact, and deliberately left empty rather than derived
      // from the camera's position: a role the run never assigned is not a fact.
      !read_string(root, "role", "camera", &camera->role, error) ||
      !read_string(root, "trigger_description", "camera", &camera->trigger_description, error)) {
    return false;
  }
  for (const char *field : {"memory_backends", "tests"}) {
    if (!require_array(root, field, "camera", error)) {
      return false;
    }
  }
  for (const auto &value : root["memory_backends"]) {
    std::string name;
    if (!element_string(value, "camera \"memory_backends\"", &name, error)) {
      return false;
    }
    MemoryBackend backend;
    // Refused, not dropped: silently discarding it turns "this run used a backend I
    // do not recognise" into "this run used no backends", which reads as a narrower
    // run that succeeded.
    if (!parse_memory_backend(name, &backend)) {
      *error = "\"" + name + "\" is not a memory backend";
      return false;
    }
    camera->memory_backends.push_back(backend);
  }
  for (const auto &value : root["tests"]) {
    TestResult test;
    if (!test_from_json(value, &test, error)) {
      return false;
    }
    camera->tests.push_back(test);
  }
  return true;
}

}  // namespace

Json::Value run_result_to_json(const RunResult &result) {
  Json::Value out(Json::objectValue);
  // Versioned so a reader can tell what shape it is looking at rather than guessing.
  out["result_schema_version"] = kResultSchemaVersion;
  out["project_name"] = result.project_name;
  out["started_at_utc"] = result.started_at_utc;
  out["finished_at_utc"] = result.finished_at_utc;
  out["host_name"] = result.host_name;
  out["kernel_release"] = result.kernel_release;
  out["kernel_version"] = result.kernel_version;
  out["output_directory"] = result.output_directory;
  out["run_mode"] = to_string(result.run_mode);

  // Run-level trigger contract (plan 2.5): stated once. A camera's channel is found
  // by taking its "role" and looking it up in "role_bindings".
  out["trigger_mode"] = to_string(result.trigger_mode);
  out["trigger_profile_id"] = result.trigger_profile_id;
  out["role_bindings"] = Json::Value(Json::arrayValue);
  for (const auto &binding : result.role_bindings) {
    Json::Value item(Json::objectValue);
    item["role"] = binding.role;
    item["trigger_channel_id"] = binding.trigger_channel_id;
    out["role_bindings"].append(item);
  }
  // Not rendered under free-run: the values exist but are meaningless when nothing is
  // driven, so the keys are omitted rather than carrying numbers with no meaning.
  if (result.trigger_mode != TriggerMode::FreeRun) {
    out["trigger_rate_hz"] = result.trigger_rate_hz;
    out["pulse_width_ms"] = result.pulse_width_ms;
  }

  out["cameras"] = Json::Value(Json::arrayValue);
  for (const auto &camera : result.cameras) {
    out["cameras"].append(camera_to_json(camera));
  }
  return out;
}

namespace {

bool run_result_from_json_checked(const Json::Value &root, RunResult *result, std::string &message) {
  if (!root.isObject()) {
    message = "the result document is not a JSON object";
    return false;
  }
  // A version this build does not understand is refused rather than parsed as if it
  // were current: guessing at an unknown shape is how a wrong result gets served
  // confidently. A document with no version at all is a pre-versioning artifact,
  // readable through the legacy adapter below.
  if (root.isMember("result_schema_version")) {
    const Json::Value &version = root["result_schema_version"];
    if (!version.isIntegral()) {
      message = "result_schema_version is not an integer";
      return false;
    }
    const int parsed_version = version.asInt();
    // Exactly the versions this build has written. v1 is the only one, so 0, negatives
    // and unknown older numbers are refused too -- they describe a shape that was never
    // produced, and "close enough" is how a wrong result gets served confidently.
    if (parsed_version != kResultSchemaVersion) {
      message = "result schema version " + std::to_string(parsed_version) + " is not supported (this build writes " +
                std::to_string(kResultSchemaVersion) + ")";
      return false;
    }
  }
  for (const char *field : {"cameras", "role_bindings"}) {
    if (!require_array(root, field, "result", &message)) {
      return false;
    }
  }

  RunResult parsed;
  // Legacy adapter: earlier builds wrote "project".
  const char *project_field = root.isMember("project_name") ? "project_name" : "project";
  if (!read_string(root, project_field, "result", &parsed.project_name, &message) ||
      !read_string(root, "started_at_utc", "result", &parsed.started_at_utc, &message) ||
      !read_string(root, "finished_at_utc", "result", &parsed.finished_at_utc, &message) ||
      !read_string(root, "host_name", "result", &parsed.host_name, &message) ||
      !read_string(root, "kernel_release", "result", &parsed.kernel_release, &message) ||
      !read_string(root, "kernel_version", "result", &parsed.kernel_version, &message) ||
      !read_string(root, "output_directory", "result", &parsed.output_directory, &message)) {
    return false;
  }
  // Unknown enum values are refused, not defaulted: silently falling back would put a
  // mode in the restored result the run never had -- sequential when it was parallel.
  std::string run_mode_text;
  if (!read_string(root, "run_mode", "result", &run_mode_text, &message)) {
    return false;
  }
  if (!run_mode_text.empty() && !parse_run_mode(run_mode_text, &parsed.run_mode)) {
    message = "\"" + run_mode_text + "\" is not a run_mode";
    return false;
  }

  // The run-level trigger contract exists only from the versioned shape onwards. A
  // legacy artifact carried these per camera, and they are NOT lifted: a routing
  // decision the run never recorded must not appear in the restored result.
  std::string trigger_mode_text;
  if (!read_string(root, "trigger_mode", "result", &trigger_mode_text, &message) ||
      !read_string(root, "trigger_profile_id", "result", &parsed.trigger_profile_id, &message)) {
    return false;
  }
  if (!trigger_mode_text.empty() && !parse_trigger_mode(trigger_mode_text, &parsed.trigger_mode)) {
    message = "\"" + trigger_mode_text + "\" is not a trigger_mode";
    return false;
  }
  for (const auto &value : root["role_bindings"]) {
    // Both halves are required: a binding naming no role, or a role bound to no
    // channel, routes nothing and is a corrupt record rather than a partial one.
    if (!value.isObject()) {
      message = "a role binding entry is not an object";
      return false;
    }
    RoleBinding binding;
    if (!read_string(value, "role", "role binding", &binding.role, &message) ||
        !read_string(value, "trigger_channel_id", "role binding", &binding.trigger_channel_id, &message)) {
      return false;
    }
    if (binding.role.empty() || binding.trigger_channel_id.empty()) {
      message = "a role binding is missing its role or trigger_channel_id";
      return false;
    }
    parsed.role_bindings.push_back(binding);
  }
  // Left at the struct default when absent, never taken from a legacy camera entry.
  if (!read_double(root, "trigger_rate_hz", "result", &parsed.trigger_rate_hz, &message) ||
      !read_double(root, "pulse_width_ms", "result", &parsed.pulse_width_ms, &message)) {
    return false;
  }

  for (const auto &value : root["cameras"]) {
    CameraRunResult camera;
    if (!camera_from_json(value, &camera, &message)) {
      return false;
    }
    parsed.cameras.push_back(camera);
  }

  if (result != nullptr) {
    *result = std::move(parsed);
  }
  return true;
}

}  // namespace

bool run_result_from_json(const Json::Value &root, RunResult *result, std::string *error) {
  std::string local;
  std::string &message = error != nullptr ? *error : local;
  message.clear();

  // Last line of defence at the PUBLIC boundary. Every scalar read above is
  // type-checked, so this should be unreachable -- but this reader takes a file from
  // disk, and jsoncpp signals a type error by throwing. An exception unwinding from
  // here would pass through the HTTP handler and take the server down over one corrupt
  // artifact, so it is converted into the same false + reason every other refusal uses.
  //
  // Deliberately NOT a substitute for the explicit checks: those produce messages that
  // name the offending field, which this cannot.
  try {
    return run_result_from_json_checked(root, result, message);
  } catch (const Json::Exception &error_from_json) {
    message = std::string("the result document is malformed: ") + error_from_json.what();
    return false;
  }
}

}  // namespace v4l2diag
