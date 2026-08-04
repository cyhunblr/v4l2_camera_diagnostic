#include "v4l2diag/core/report_naming.hpp"

#include <algorithm>
#include <string>

namespace v4l2diag {

namespace {

// Everything that is not safe in a filename becomes '-'. Applied after the basename is
// taken, so a path can never survive as a separator.
bool safe_name_char(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || c == '-' || c == '_' || c == '.';
}

// The user's file name, reduced to something usable in a filename:
//   * only the basename (a path never reaches the result)
//   * the last extension removed ("anvil.json" -> "anvil", "a.tar.gz" -> "a.tar")
//   * unsafe characters replaced
//   * leading dots stripped, so ".json" cannot become a hidden-file fragment
//
// Returns an empty string when nothing usable remains; the caller substitutes
// "default" rather than emitting an empty path component.
std::string name_part(const std::string &raw) {
  const std::size_t slash = raw.find_last_of('/');
  std::string base = slash == std::string::npos ? raw : raw.substr(slash + 1);

  const std::size_t dot = base.find_last_of('.');
  if (dot == 0) {
    // A name that is nothing but an extension (".json") has no stem at all. Keeping
    // the extension as the name would put "_json_" where the user's file name belongs.
    return std::string();
  }
  if (dot != std::string::npos) {
    base = base.substr(0, dot);
  }

  std::string out;
  for (char c : base) {
    out.push_back(safe_name_char(c) ? c : '-');
  }
  // Leading dots and dashes would make an odd or hidden filename.
  const std::size_t first = out.find_first_not_of(".-");
  if (first == std::string::npos) {
    return std::string();
  }
  out = out.substr(first);
  const std::size_t last = out.find_last_not_of(".-");
  return out.substr(0, last + 1);
}

std::string part_or_default(const std::string &raw) {
  const std::string part = name_part(raw);
  return part.empty() ? "default" : part;
}

// "free-run", "hardware-trigger", "software-trigger" -- the filename spelling, which is
// not the same as the wire value ("hardware").
const char *mode_part(TriggerMode mode) {
  switch (mode) {
    case TriggerMode::Hardware:
      return "hardware-trigger";
    case TriggerMode::Software:
      return "software-trigger";
    case TriggerMode::FreeRun:
      return "free-run";
  }
  return "free-run";
}

// "2026-07-30T12:02:38Z" -> "2026-07-30_12-02-38". Anything unparseable falls back to a
// fixed stem so the name stays usable; the timestamp is a label, not an identity.
std::string timestamp_part(const std::string &started_at_utc) {
  std::string out;
  out.reserve(19);
  for (char c : started_at_utc) {
    if (out.size() >= 19) {
      break;
    }
    if (c == 'T') {
      out.push_back('_');
    } else if (c == ':') {
      out.push_back('-');
    } else if ((c >= '0' && c <= '9') || c == '-' || c == '_') {
      out.push_back(c);
    }
  }
  return out.size() == 19 ? out : std::string("unknown-time");
}

std::string stem(const ReportNaming &naming) {
  std::string out = timestamp_part(naming.started_at_utc);
  out += "_";
  out += mode_part(naming.trigger_mode);
  // Free-run routes nothing, so naming a Trigger Profile would claim a routing the run
  // never used -- the part is omitted outright rather than filled with "default".
  if (naming.trigger_mode != TriggerMode::FreeRun) {
    const std::string profile = name_part(naming.trigger_profile_file);
    if (profile.empty()) {
      // No fallback here. A triggered run routed through a profile, so a name without one
      // is a lie either way: omitting the part reads as free-run, and "default" names a
      // file the user never chose.
      throw ReportNamingError(std::string("a ") + mode_part(naming.trigger_mode) +
                              " run has no usable Trigger Profile source file (got \"" + naming.trigger_profile_file +
                              "\"); artifacts are not named without one, and there is no default");
    }
    out += "_" + profile;
  }
  out += "_" + part_or_default(naming.test_configuration_file);
  return out;
}

}  // namespace

std::string canonical_report_basename(const ReportNaming &naming) {
  return stem(naming) + "_v4l2_camera_diagnostic";
}

std::string report_artifact_filename(const ReportNaming &naming, ReportFormat format) {
  // Markdown's artifact id is "markdown" but its extension is ".md".
  const std::string extension = format == ReportFormat::Markdown ? "md" : to_string(format);
  return canonical_report_basename(naming) + "." + extension;
}

std::string dmesg_log_filename(const ReportNaming &naming) {
  return stem(naming) + "_dmesg.log";
}

std::string report_document_title(const ReportNaming &naming) {
  return canonical_report_basename(naming);
}

}  // namespace v4l2diag
