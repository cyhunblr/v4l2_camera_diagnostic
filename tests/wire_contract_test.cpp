// Wire contracts: a collection is always an array, never a missing key (plan 2.7).
//
// Several keys were created only inside their loop, so an empty collection produced no
// key at all. The frontend then did `undefined.map(...)` -- a blank Dashboard for a run
// with no artifacts, and a blank test list for a test supporting no trigger mode.
//
// The rule under test is deliberately stricter than "does not crash": for every
// collection the key must be PRESENT, be an array, and be empty. A `null` or an absent
// key is a different shape, and the UI has to be able to rely on one.
#include "v4l2diag/core/test_json.hpp"

#include "v4l2diag/core/run_result_json.hpp"

#include <json/json.h>

#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const std::string &what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

// Present + array + empty. All three matter: an absent key and a null are both "not an
// empty array" as far as a consumer is concerned.
bool empty_array(const Json::Value &parent, const char *field, const std::string &context, bool *ok) {
  bool local = true;
  local &= check(parent.isMember(field), context + ": \"" + field + "\" key is missing entirely");
  local &= check(!parent[field].isNull(), context + ": \"" + field + "\" is null rather than an empty array");
  local &= check(parent[field].isArray(), context + ": \"" + field + "\" is not an array");
  local &= check(parent[field].empty(), context + ": \"" + field + "\" is not empty");
  local &= check(parent[field].size() == 0, context + ": \"" + field + "\" size() is not 0");
  *ok &= local;
  return local;
}

}  // namespace

int main() {
  bool ok = true;

  // --- test_to_json: a test supporting no trigger mode --------------------
  {
    // supported_trigger_modes was appended to inside the loop, so a test that
    // supports nothing produced no key.
    v4l2diag::TestDefinition bare;
    bare.id = "t00-nothing";
    bare.name = "Nothing";
    bare.category = "discovery";
    bare.layer = v4l2diag::TestLayer::Discovery;
    // Supports no trigger mode at all, and carries no tags.
    bare.trigger_mode_mask = 0;
    const Json::Value doc = v4l2diag::test_to_json(bare);
    empty_array(doc, "supported_trigger_modes", "test_to_json", &ok);
    empty_array(doc, "tags", "test_to_json", &ok);
  }

  // --- test_to_json: the populated case still works -----------------------
  {
    v4l2diag::TestDefinition full;
    full.id = "t14-trigger-latency";
    full.name = "Trigger Latency";
    full.category = "performance";
    full.layer = v4l2diag::TestLayer::Latency;
    full.uses_trigger = true;
    full.trigger_mode_mask = 0x01;  // hardware only
    full.tags.push_back("stable");
    const Json::Value doc = v4l2diag::test_to_json(full);
    ok &= check(doc["supported_trigger_modes"].isArray() && doc["supported_trigger_modes"].size() == 1,
                "a populated supported_trigger_modes was lost");
    ok &= check(doc["supported_trigger_modes"][0].asString() == "hardware", "the supported trigger mode changed value");
    ok &= check(doc["tags"].isArray() && doc["tags"].size() == 1, "a populated tags list was lost");
  }

  // --- run_result_to_json: every collection is an array -------------------
  {
    // The canonical result already guaranteed this (2.6.2); locked here too so the
    // rule is checked in one place for every wire model.
    v4l2diag::RunResult bare;
    const Json::Value doc = v4l2diag::run_result_to_json(bare);
    empty_array(doc, "role_bindings", "run_result_to_json", &ok);
    empty_array(doc, "cameras", "run_result_to_json", &ok);

    v4l2diag::RunResult one;
    one.cameras.push_back(v4l2diag::CameraRunResult());
    const Json::Value with_camera = v4l2diag::run_result_to_json(one);
    if (check(with_camera["cameras"].size() == 1, "the camera was lost")) {
      empty_array(with_camera["cameras"][0], "memory_backends", "camera", &ok);
      empty_array(with_camera["cameras"][0], "tests", "camera", &ok);
    }

    v4l2diag::RunResult with_test;
    v4l2diag::CameraRunResult camera;
    camera.tests.push_back(v4l2diag::TestResult());
    with_test.cameras.push_back(camera);
    const Json::Value test_doc = v4l2diag::run_result_to_json(with_test)["cameras"][0]["tests"][0];
    for (const char *field : {"metrics", "details", "notes", "warnings"}) {
      empty_array(test_doc, field, "test result", &ok);
    }
  }

  // --- report_formats is absent from every current wire model -------------
  {
    // Structural, not textual: a migration report's dropped[] entry still names the old
    // field on purpose (that is how the user learns what was discarded), so searching
    // the serialised text would conflate the two.
    //
    // The positive half matters as much: each object must still carry the fields v5
    // kept, otherwise an empty object would pass this vacuously.
    v4l2diag::TestDefinition test;
    test.id = "t01-device-compliance";
    test.layer = v4l2diag::TestLayer::Discovery;
    const Json::Value test_doc = v4l2diag::test_to_json(test);
    ok &= check(!test_doc.isMember("report_formats"), "test_to_json emits report_formats");
    ok &= check(test_doc.isMember("tags") && test_doc.isMember("layer"), "test_to_json lost the fields it should keep");

    v4l2diag::RunResult result;
    v4l2diag::CameraRunResult camera;
    camera.tests.push_back(v4l2diag::TestResult());
    result.cameras.push_back(camera);
    const Json::Value result_doc = v4l2diag::run_result_to_json(result);
    ok &= check(!result_doc.isMember("report_formats"), "run_result_to_json emits report_formats");
    ok &= check(result_doc.isMember("trigger_mode") && result_doc.isMember("role_bindings"),
                "run_result_to_json lost the run-level fields it should keep");
    ok &= check(!result_doc["cameras"][0].isMember("report_formats"), "a camera object emits report_formats");
    ok &= check(!result_doc["cameras"][0]["tests"][0].isMember("report_formats"),
                "a test result object emits report_formats");
  }

  return ok ? 0 : 1;
}
