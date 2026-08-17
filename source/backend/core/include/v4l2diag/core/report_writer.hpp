#pragma once

#include "v4l2diag/core/types.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace v4l2diag {

struct ReportArtifact {
  ReportFormat format = ReportFormat::Json;
  std::string path;
};

// Thrown when a mandatory artifact could not be written. A run that cannot produce
// its reports has not succeeded, so this is an error rather than a shorter artifact
// list -- see write_reports().
class ReportWriteError : public std::runtime_error {
 public:
  explicit ReportWriteError(const std::string &what) : std::runtime_error(what) {}
};

// Writes the three artifacts every run produces: HTML, JSON and Markdown.
//
// There is deliberately no format-selection overload. The user does not choose
// formats (plan 2.10), and an API that let a caller ask for a subset would let any
// caller quietly break the "every run writes all three" guarantee -- which plan 2.6
// then relies on when it reads the canonical JSON back after a restart. The set
// lives inside report_writer.cpp as an implementation detail.
//
// `ReportFormat` and `ReportArtifact::format` stay: they are the IDENTITY of an
// emitted file, which is a different thing from a user preference.
//
// Throws ReportWriteError if the output directory cannot be created or any of the
// three files cannot be written completely. A returned artifact means the file is on
// disk and closed cleanly; the list is never partially filled on failure, so a caller
// can never advertise a report that does not exist.
std::vector<ReportArtifact> write_reports(const RunResult &result, const std::string &output_directory);

}  // namespace v4l2diag
