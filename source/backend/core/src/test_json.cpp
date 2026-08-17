#include "v4l2diag/core/test_json.hpp"

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

Json::Value test_to_json(const TestDefinition &test) {
  Json::Value out(Json::objectValue);
  out["id"] = test.id;
  out["name"] = test.name;
  out["category"] = test.category;
  out["description"] = test.description;
  out["uses_trigger"] = test.uses_trigger;
  // Declared before the loop: a test supporting no mode used to produce no key at all,
  // and the UI then did `undefined.map(...)`. An empty array is a shape a consumer can
  // rely on; a missing key is not.
  out["supported_trigger_modes"] = Json::Value(Json::arrayValue);
  for (TriggerMode mode : {TriggerMode::Hardware, TriggerMode::Software, TriggerMode::FreeRun}) {
    if (supports_trigger_mode(test, mode)) {
      out["supported_trigger_modes"].append(to_string(mode));
    }
  }
  out["requires_dmabuf"] = test.requires_dmabuf;
  out["tags"] = Json::Value(Json::arrayValue);
  for (const auto &tag : test.tags) {
    out["tags"].append(tag);
  }
  // Layer is backend metadata. The web UI groups on these two fields instead of
  // parsing the test number out of the id, which used to drop anything past t26
  // into an "Other Diagnostics" bucket.
  out["layer"] = layer_number(test.layer);
  out["layer_name"] = layer_name(test.layer);
  return out;
}

}  // namespace v4l2diag
