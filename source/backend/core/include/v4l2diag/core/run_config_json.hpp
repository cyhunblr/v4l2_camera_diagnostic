#pragma once

#include <json/json.h>

#include <string>

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

// Maps a run-request JSON body onto a RunConfig.
//
// Lives in core rather than the web server because it is pure data mapping with
// no HTTP involvement -- and because keeping it here makes it directly
// testable. It previously parsed "test_selectors" in two separate loops, so
// every selector was stored twice and each unmatched one warned twice.
RunConfig run_config_from_json(const Json::Value &root, const std::string &report_root,
                               const std::string &config_directory, const std::string &run_id);

}  // namespace v4l2diag
