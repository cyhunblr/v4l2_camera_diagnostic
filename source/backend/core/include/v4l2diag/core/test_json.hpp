#pragma once

#include <json/json.h>

#include "v4l2diag/core/test_registry.hpp"

namespace v4l2diag {

// Wire representation of a test definition, as served by GET /api/tests.
//
// Lives in core rather than the web server so the contract the web UI depends
// on can be tested directly. In particular "layer"/"layer_name" must be present:
// the UI groups tests by them instead of parsing the number out of the id.
Json::Value test_to_json(const TestDefinition &test);

}  // namespace v4l2diag
