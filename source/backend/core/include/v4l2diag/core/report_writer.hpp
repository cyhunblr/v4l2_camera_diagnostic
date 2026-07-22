#pragma once

#include "v4l2diag/core/types.hpp"

#include <string>
#include <vector>

namespace v4l2diag {

struct ReportArtifact {
  ReportFormat format = ReportFormat::Json;
  std::string path;
};

std::vector<ReportArtifact> write_reports(const RunResult &result, const std::vector<ReportFormat> &formats,
                                          const std::string &output_directory);

}  // namespace v4l2diag
