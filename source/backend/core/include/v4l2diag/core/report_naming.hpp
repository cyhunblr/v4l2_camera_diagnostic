#pragma once

#include <stdexcept>
#include <string>

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

// A run that cannot be named honestly.
//
// Raised when a hardware or software run has no resolvable Trigger Profile source file.
// Those modes route through a profile, so there is no honest name for such a run: omitting
// the part would claim a free-run that did route, and substituting "default" would name a
// file the user never chose. "default" is the TEST CONFIGURATION's fallback and nothing
// else's.
//
// Failing loudly is the point -- a silently mis-named artifact is worse than a failed
// write, because it looks correct in a folder of archived runs.
class ReportNamingError : public std::runtime_error {
 public:
  explicit ReportNamingError(const std::string &message) : std::runtime_error(message) {}
};

// Canonical artifact naming (plan 3.5).
//
// Everything a run writes takes its name from one place: the HTML, JSON and Markdown
// artifacts, the HTML <title>, the Export links inside the report, and the DMESG log.
// Separate string concatenations would drift, and the Export links in particular must
// match the files on disk exactly -- they are plain relative hrefs, so a mismatch is a
// dead link in an archived report.
struct ReportNaming {
  // Run start, as the backend emits it ("%Y-%m-%dT%H:%M:%SZ"). Reformatted to
  // YYYY-MM-DD_HH-mm-ss for the filename.
  std::string started_at_utc;
  TriggerMode trigger_mode = TriggerMode::FreeRun;

  // The SOURCE FILE NAME of the selected Trigger Profile, not its id.
  //
  // The two genuinely differ: a config file's name and the id inside it are
  // independent, so deriving the filename from the id would put the wrong name on
  // every artifact of that run. Empty (and always ignored) under free-run, which
  // routes nothing.
  std::string trigger_profile_file;

  // The source file name of the selected Test Configuration. Empty means the user
  // chose none, which the name renders as "default".
  std::string test_configuration_file;
};

// `<start>_<mode>[_<profile>]_<config>_v4l2_camera_diagnostic`, with no extension.
//
// A path, an extension or a separator never reaches the result: only the basename of
// whatever was supplied is used, and an unusable TEST CONFIGURATION name falls back to
// "default" rather than producing an empty or dangerous filename.
//
// Throws ReportNamingError when the trigger mode is hardware or software and the Trigger
// Profile source file is missing or reduces to nothing usable. There is no fallback for
// that part; see ReportNamingError.
std::string canonical_report_basename(const ReportNaming &naming);

// The basename plus the format's extension (".md" for Markdown, not ".markdown").
std::string report_artifact_filename(const ReportNaming &naming, ReportFormat format);

// `<start>_<mode>[_<profile>]_<config>_dmesg.log` -- the same stem with the
// "v4l2_camera_diagnostic" tail replaced.
std::string dmesg_log_filename(const ReportNaming &naming);

// The HTML <title>: the extensionless canonical base. The browser's print dialog
// suggests this as the PDF name, which is the whole reason it matches the artifacts.
std::string report_document_title(const ReportNaming &naming);

}  // namespace v4l2diag
