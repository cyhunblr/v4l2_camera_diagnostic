#include "v4l2diag/core/report_writer.hpp"

#include "v4l2diag/core/duration_format.hpp"
#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/result_card.hpp"
#include "v4l2diag/core/test_content.hpp"

#include "v4l2diag/core/run_result_json.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace v4l2diag {

namespace {

bool ensure_directory(const std::string &path) {
  std::string partial;
  for (char c : path) {
    partial.push_back(c);
    if (c == '/' && partial.size() > 1) {
      mkdir(partial.c_str(), 0755);
    }
  }
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

// Renders an ISO-8601 "%Y-%m-%dT%H:%M:%SZ" timestamp as "%Y-%m-%d %H:%M:%S UTC"
// for display; falls back to the raw value if it doesn't match the expected shape.
std::string readable_utc(const std::string &iso_timestamp) {
  std::tm tm{};
  if (strptime(iso_timestamp.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm) == nullptr) {
    return iso_timestamp;
  }
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &tm);
  return buffer;
}

std::string readable_duration(const std::string &started_at_utc, const std::string &finished_at_utc) {
  std::tm start_tm{};
  std::tm finish_tm{};
  if (strptime(started_at_utc.c_str(), "%Y-%m-%dT%H:%M:%SZ", &start_tm) == nullptr ||
      strptime(finished_at_utc.c_str(), "%Y-%m-%dT%H:%M:%SZ", &finish_tm) == nullptr) {
    return "";
  }
  const time_t start_epoch = timegm(&start_tm);
  const time_t finish_epoch = timegm(&finish_tm);
  if (finish_epoch < start_epoch) {
    return "";
  }
  long total_sec = static_cast<long>(finish_epoch - start_epoch);
  const long hours = total_sec / 3600;
  total_sec %= 3600;
  const long minutes = total_sec / 60;
  const long seconds = total_sec % 60;
  std::ostringstream out;
  if (hours > 0)
    out << hours << "h ";
  if (hours > 0 || minutes > 0)
    out << minutes << "m ";
  out << seconds << "s";
  return out.str();
}

// The shared escaper (plan 3.1). Was a private copy here; the card renderer needs the
// same one, and two copies of an escaper is one too many.
using v4l2diag::html_escape;

// The shared sentinel rule (plan 3.1): every surface that shows a metric applies it.
using v4l2diag::is_sentinel_metric;

bool looks_like_resolution_label(const std::string &value) {
  const std::size_t separator = value.find('x');
  if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
    return false;
  }
  return std::all_of(value.begin(), value.begin() + separator, [](unsigned char c) { return std::isdigit(c) != 0; }) &&
         std::all_of(value.begin() + separator + 1, value.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// Ticks land on 1/2/5 x 10^n steps so axis labels read as round numbers. The
// previous "max * 1.18" scaling produced ticks like 5.9 / 11.8 / 17.7.
struct AxisTicks {
  std::vector<double> values;
  double min = 0.0;
  double max = 1.0;
};

// Data colours. Sequential blue ramp (steps 250..700) for an ordered category
// axis, one accent for nominal categories, and a 4-step ordinal ramp for the
// ordered Mean/P95/Max series. The verdict colours (--pass/--warn/--fail) are
// deliberately absent: reusing them for a data series made an ordinary bar read
// as a warning or a failure.
const char *const kSequentialRamp[] = {"#86b6ef", "#6da7ec", "#5598e7", "#3987e5", "#2a78d6",
                                       "#256abf", "#1c5cab", "#184f95", "#104281", "#0d366b"};
constexpr std::size_t kSequentialRampSize = sizeof(kSequentialRamp) / sizeof(kSequentialRamp[0]);
const char *const kSingleHue = "#2a78d6";
const char *const kOrdinalSeries[] = {"#86b6ef", "#3987e5", "#1c5cab", "#0d366b"};
constexpr std::size_t kOrdinalSeriesSize = sizeof(kOrdinalSeries) / sizeof(kOrdinalSeries[0]);

// How the category axis labels are derived, and how they sort.
enum class CategoryKind {
  Statistic,
  PulseWidth,
  Fourcc,
  Resolution,
  ControlCombo,
};

enum class ColorRule {
  SingleHue,
  SequentialByCategory,
  OrdinalSeries,
};

// "ll1_bp0_wi1_mean_ms" -> "LED 1 - BYP 0 - WIN 1". The metric keys themselves
// never change: docs, the threshold registry and the tests reference them.
// Docs: docs/backend/tests/t18-control-sweep.md
// The shared one (plan 3.1).
using v4l2diag::control_combo_label;

// The shared one (plan 3.1). Was a private copy here.
using v4l2diag::upper_case;

struct MetricChartGroup {
  enum class Kind {
    StatisticDots,
    VerticalBars,
    HorizontalBars,
  };

  std::string title;
  std::string unit;
  std::string label_prefix;
  // Axis roles are fixed and never swapped: category_axis names the independent
  // variable, value_axis names the measured quantity. A horizontal chart draws
  // the category axis vertically -- it does not exchange the two labels, which
  // is what the old x_axis/y_axis pair did.
  std::string category_axis;
  std::string value_axis;
  std::vector<std::size_t> indices;
  Kind kind = Kind::StatisticDots;
  CategoryKind category_kind = CategoryKind::Statistic;
  ColorRule color_rule = ColorRule::SingleHue;
  bool wide = false;
};

struct BarItem {
  std::size_t metric_index = 0;
  std::size_t category_index = 0;
  std::size_t series_index = 0;
};

// Shared by the bar renderers and by the group builder, so the colour rule and
// the orientation are decided from the same categories the chart will draw.
struct BarLayout {
  std::vector<std::string> categories;
  std::vector<std::string> series;
  std::vector<BarItem> items;
  double max_value = 0.0;
};

// Per-test chart declarations. Each one fixes what goes on the category axis and
// what quantity the value axis measures, instead of inferring it from the chart
// title the way the old grouping did.
struct SweepChartSpec {
  const char *test_id;
  const char *metric_prefix;
  const char *title;
  const char *category_axis;
  const char *value_axis;
  CategoryKind category_kind;
  bool horizontal;
};

const SweepChartSpec kSweepChartSpecs[] = {
    {"t16-gpio-pulse-width", "hits_", "Trigger hits by pulse width", "Pulse width (ms)", "Trigger hits",
     CategoryKind::PulseWidth, false},
    {"t16-gpio-pulse-width", "lat_high_avg_", "HIGH edge latency by pulse width", "Pulse width (ms)",
     "Mean latency from HIGH", CategoryKind::PulseWidth, false},
    {"t16-gpio-pulse-width", "lat_low_avg_", "LOW edge latency by pulse width", "Pulse width (ms)",
     "Mean latency from LOW", CategoryKind::PulseWidth, false},
    {"t18-control-sweep", "ll", "Capture latency by ISX021 control combination", "Control combination",
     "Mean capture latency", CategoryKind::ControlCombo, true},
};

// Format / resolution sweeps share one shape: a repeated metric prefix names the
// category, and the unit names the measured quantity.
struct PrefixSweepSpec {
  const char *test_id;
  const char *category_axis;
  CategoryKind category_kind;
};

const PrefixSweepSpec kPrefixSweepSpecs[] = {
    {"t17-format-comparison", "Pixel format", CategoryKind::Fourcc},
    {"t19-resolution-sweep", "Resolution", CategoryKind::Resolution},
};

void render_status_distribution(std::ostream &out, int pass_count, int fail_count, int warn_count, int skip_count) {
  const int total = pass_count + fail_count + warn_count + skip_count;
  out << "<div class=\"section result-distribution\"><div class=\"section-header\"><h2>Result Distribution</h2></div>";
  out << "<div class=\"distribution-body\"><div class=\"distribution-track\" role=\"img\" "
         "aria-label=\"Test result distribution\">";
  const struct {
    const char *name;
    const char *css_class;
    int count;
  } statuses[] = {
      {"Passed", "pass", pass_count},
      {"Warned", "warn", warn_count},
      {"Failed", "fail", fail_count},
      {"Skipped", "skip", skip_count},
  };
  if (total == 0) {
    out << "<span class=\"distribution-empty\">No test results</span>";
  } else {
    for (const auto &status : statuses) {
      if (status.count == 0)
        continue;
      const double width = 100.0 * status.count / total;
      out << "<span class=\"distribution-segment " << status.css_class << "\" style=\"width:" << std::fixed
          << std::setprecision(3) << width << "%\" title=\"" << status.name << ": " << status.count << "\"></span>";
    }
  }
  out << "</div><div class=\"distribution-legend\">";
  for (const auto &status : statuses) {
    out << "<span class=\"legend-item\"><span class=\"legend-dot " << status.css_class << "\"></span><span>"
        << status.name << "</span><strong>" << status.count << "</strong></span>";
  }
  out << "</div></div></div>";
}

// Axis chrome. The caption names the quantity itself; the old "X: " / "Y: "
// prefixes added noise without saying anything.
// The canonical chart width (design-spec S8). The viewBox MUST equal the width the chart
// actually renders at: with a narrower viewBox the browser scales every SVG unit, so a
// declared font-size: 8px lands on screen at 8 * renderWidth / viewBoxWidth. At the old
// 460-unit box that was a 1.97x magnification -- the label sizes in the source said one
// thing and the rendered page showed another.
constexpr double kChartWidth = 906.0;

struct TimeoutProbePoint {
  int timeout_ms = 0;
  int hits = 0;
  int total = 0;
};

// A format or resolution that was enumerated but could not be measured emits no
// metrics at all, so it disappears from the chart completely. List those under it
// so "the camera only supports two formats" can be told apart from "we only
// managed to measure two of them".
void render_chart_omissions(std::ostream &out, const TestResult &test, const std::vector<MetricValue> &metrics,
                            const std::vector<MetricChartGroup> &groups) {
  const PrefixSweepSpec *spec = nullptr;
  for (const auto &candidate : kPrefixSweepSpecs) {
    if (test.id == candidate.test_id) {
      spec = &candidate;
      break;
    }
  }
  if (spec == nullptr) {
    return;
  }

  // The MEASURED set, taken from the metric names rather than from the charts.
  //
  // It used to come from the chart layout, which tied "was this measured?" to "did it get
  // drawn?". Those are different questions, and once the unapproved charts were removed
  // (plan 3.1, review round 3) every omission silently stopped being reported -- a
  // format that failed S_FMT simply vanished from the report.
  std::vector<std::string> charted;
  for (const auto &metric : metrics) {
    const std::size_t underscore = metric.name.find('_');
    if (underscore == std::string::npos || underscore == 0) {
      continue;
    }
    const std::string category = metric.name.substr(0, underscore);
    const std::string label = spec->category_kind == CategoryKind::Fourcc ? upper_case(category) : category;
    if (std::find(charted.begin(), charted.end(), label) == charted.end()) {
      charted.push_back(label);
    }
  }
  (void)groups;

  // With nothing charted there is no measured set to contrast against, and the
  // heuristic below would mark every "<label>: <value>" detail line as an
  // omission — including the line describing the one category that WAS
  // measured. A single-format sweep charts nothing (one bar is not a
  // comparison), which is exactly when that misfires.
  if (charted.empty()) {
    return;
  }

  // Later detail lines supersede earlier ones for the same category: a format
  // that logged "sizeimage=..." and then failed to stream should report the
  // failure, not the geometry.
  std::vector<std::pair<std::string, std::string>> omitted;
  for (const auto &detail : test.details) {
    const std::size_t colon = detail.find(':');
    if (colon == std::string::npos || colon == 0) {
      continue;
    }
    const std::string name = detail.substr(0, colon);
    if (name.find(' ') != std::string::npos) {
      continue;
    }
    const bool plausible = spec->category_kind == CategoryKind::Resolution ? looks_like_resolution_label(name)
                                                                           : name.size() >= 3 && name.size() <= 5;
    if (!plausible) {
      continue;
    }
    const std::string label = spec->category_kind == CategoryKind::Fourcc ? upper_case(name) : name;
    if (std::find(charted.begin(), charted.end(), label) != charted.end()) {
      continue;
    }
    std::string reason = detail.substr(colon + 1);
    while (!reason.empty() && reason.front() == ' ') {
      reason.erase(0, 1);
    }
    auto it = std::find_if(omitted.begin(), omitted.end(),
                           [&](const std::pair<std::string, std::string> &entry) { return entry.first == label; });
    if (it == omitted.end()) {
      omitted.push_back({label, reason});
    } else {
      it->second = reason;
    }
  }
  if (omitted.empty()) {
    return;
  }

  out << "<div class=\"chart-omissions\"><strong>Not measured</strong> (" << omitted.size() << " of "
      << (charted.size() + omitted.size()) << " enumerated): ";
  for (std::size_t i = 0; i < omitted.size(); ++i) {
    if (i) {
      out << "; ";
    }
    out << html_escape(omitted[i].first) << " <code>" << html_escape(omitted[i].second) << "</code>";
  }
  out << "</div>";
}

// Returns false when the stream could not be opened, or when anything went wrong up
// to and including close(). Silently returning void here is what let a full disk or
// an unwritable directory look like a successful run.
// The JSON artifact and the live API are the SAME document: both go through
// run_result_to_json(). They used to be two hand-maintained copies -- one streaming
// text here, one building a Json::Value in web_server.cpp -- and they had already
// drifted (`project` vs `project_name`, `path` vs `camera_path`). That only became
// load-bearing once the API started reading this file back after a restart.
bool write_json(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  Json::StreamWriterBuilder builder;
  builder["indentation"] = "  ";
  out << Json::writeString(builder, run_result_to_json(result)) << "\n";
  out.close();
  return static_cast<bool>(out);
}

// Returns false when the stream could not be opened, or when anything went wrong up
// to and including close(). Silently returning void here is what let a full disk or
// an unwritable directory look like a successful run.
bool write_markdown(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << "# V4L2 Camera Diagnostic Report\n\n";
  out << "- Project: `" << result.project_name << "`\n";
  out << "- Started: `" << result.started_at_utc << "`\n";
  out << "- Finished: `" << result.finished_at_utc << "`\n";
  out << "- Host: `" << result.host_name << "`\n";
  out << "- Kernel: `" << result.kernel_release << "` (`" << result.kernel_version << "`)\n";
  out << "- Run mode: `" << to_string(result.run_mode) << "`\n";
  out << "- Trigger mode: `" << to_string(result.trigger_mode) << "`\n";
  if (!result.trigger_profile_id.empty()) {
    out << "- Trigger profile: `" << result.trigger_profile_id << "`\n";
  }
  // Timing is a property of the run, not of each camera.
  if (result.trigger_mode != TriggerMode::FreeRun) {
    out << "- Trigger rate: `" << std::fixed << std::setprecision(2) << result.trigger_rate_hz << " Hz`\n";
    out << "- Pulse width: `" << std::fixed << std::setprecision(2) << result.pulse_width_ms << " ms`\n";
  }
  out << "\n";
  // One routing table per run, not per camera. Free-run routes nothing, so the
  // table is omitted entirely rather than printed empty.
  if (!result.role_bindings.empty()) {
    out << "### Trigger routing\n\n";
    out << "| Role | Trigger channel |\n";
    out << "| --- | --- |\n";
    for (const auto &binding : result.role_bindings) {
      out << "| " << binding.role << " | " << binding.trigger_channel_id << " |\n";
    }
    out << "\n";
  }

  for (const auto &camera : result.cameras) {
    out << "## Camera `" << camera.camera_path << "`\n\n";
    // Only the role: the profile, mode and timing are run-level (report model (a)).
    if (!camera.role.empty()) {
      out << "- Role: `" << camera.role << "`\n";
    }
    if (!camera.trigger_description.empty()) {
      out << "- Trigger detail: `" << camera.trigger_description << "`\n";
    }
    out << "- Backends:";
    for (auto backend : camera.memory_backends) {
      out << " `" << to_string(backend) << "`";
    }
    out << "\n\n";
    out << "| Test | Backend | Category | Status | Duration ms | Summary |\n";
    out << "| --- | --- | --- | --- | ---: | --- |\n";
    for (const auto &test : camera.tests) {
      out << "| `" << test.id << "` | `" << test.memory_backend << "` | " << test.category << " | "
          << to_string(test.status) << " | " << std::fixed << std::setprecision(3) << test.duration_ms << " | "
          << test.summary << " |\n";
    }
    out << "\n";
    for (const auto &test : camera.tests) {
      if (test.metrics.empty() && test.details.empty() && test.notes.empty() && test.warnings.empty()) {
        continue;
      }
      out << "### " << test.id << "\n\n";
      if (!test.metrics.empty()) {
        out << "| Metric | Value | Unit | Description |\n";
        out << "| --- | ---: | --- | --- |\n";
        for (const auto &metric : test.metrics) {
          out << "| " << metric.name << " | " << metric.value << " | " << metric.unit << " | " << metric.description
              << " |\n";
        }
        out << "\n";
      }
      // Prose first, as a blockquote, so it reads as commentary on the data
      // rather than as another data point in the bullet list.
      for (const auto &note : test.notes) {
        out << "> " << note << "\n\n";
      }
      for (const auto &detail : test.details) {
        out << "- " << detail << "\n";
      }
      out << "\n";
      // Warnings were previously omitted from the markdown report entirely.
      for (const auto &warning : test.warnings) {
        out << "**Warning:** " << warning << "\n\n";
      }
    }
  }

  // close() flushes; check the stream afterwards so a write that failed mid-file (a
  // full disk, a vanished mount) is reported rather than assumed complete.
  out.close();
  return static_cast<bool>(out);
}

// Returns false when the stream could not be opened, or when anything went wrong up
// The naming inputs a result carries (plan 3.5). One place, so the artifacts, the
// <title> and the toolbar's links cannot disagree.
ReportNaming naming_of(const RunResult &result) {
  ReportNaming naming;
  naming.started_at_utc = result.started_at_utc;
  naming.trigger_mode = result.trigger_mode;
  naming.trigger_profile_file = result.trigger_profile_file;
  naming.test_configuration_file = result.threshold_config_file;
  return naming;
}

// Export DMESG (plan 3.4).
//
// The DMESG control, as a plain relative link to a file this run wrote.
//
// The earlier model rendered a disabled button plus a script that asked, at load time,
// whether a server was there -- because the same HTML is read both served and straight
// off disk over file://. Producing the log as an artifact removes the question: the file
// travels with the report, so the link works in both cases and the script is gone.
//
// The client still names nothing. The filename comes from the run's own metadata through
// the same canonical naming the writer used, which is what keeps a client-supplied name
// out of any response header.
std::string render_dmesg_action(const RunResult &result, bool dmesg_log_present) {
  // DMESG is produced with the other artifacts when the run finishes, so this is a plain
  // relative link to a file that already exists -- the same shape as JSON and Markdown.
  //
  // That removes the whole live-resolution mechanism: no disabled state to explain, no
  // script to decide whether a server is there, and no run id in a URL. The client never
  // names the file; the name comes from the run's own metadata, which is what keeps a
  // client-supplied filename out of the response.
  // Rendered only when the file is really there. write_reports() writes the HTML last,
  // so by this point the kernel log either exists beside it or could not be produced --
  // and a control that cannot work is not shown at all rather than shown broken.
  const std::string name = dmesg_log_filename(naming_of(result));
  if (!dmesg_log_present) {
    return std::string();
  }
  std::ostringstream out;
  out << "<a class=\"export-pdf-btn\" href=\"" << html_escape(name) << "\" download>Export DMESG</a>";
  return out.str();
}

// The four export actions (plan 3.3), in the approved order.
//
// PDF prints; JSON and Markdown are plain relative links to the files this same run
// wrote, so an archived folder opened over file:// still works. Nothing is serialised in
// the browser and no name is derived a second time -- the hrefs come from the same
// canonical naming the writer used, which is the only way a link and a file cannot drift.
std::string render_export_toolbar(const RunResult &result, bool dmesg_log_present) {
  const ReportNaming naming = naming_of(result);
  std::ostringstream out;
  out << "<div class=\"export-row\"><div class=\"export-actions\">";
  out << "<button type=\"button\" class=\"export-pdf-btn\" onclick=\"window.print()\">Export PDF</button>";
  out << "<a class=\"export-pdf-btn\" href=\"" << html_escape(report_artifact_filename(naming, ReportFormat::Json))
      << "\" download>Export JSON</a>";
  out << "<a class=\"export-pdf-btn\" href=\"" << html_escape(report_artifact_filename(naming, ReportFormat::Markdown))
      << "\" download>Export Markdown</a>";
  out << render_dmesg_action(result, dmesg_log_present);
  out << "</div></div>";
  return out.str();
}

// to and including close(). Silently returning void here is what let a full disk or
// an unwritable directory look like a successful run.
bool write_html(const RunResult &result, const std::string &path, bool dmesg_log_present) {
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << R"(<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>)"
      << html_escape(report_document_title(naming_of(result))) << R"(</title>
<style>
:root { --pass: #16a34a; --fail: #dc2626; --warn: #d97706; --skip: #64748b; }
* { box-sizing: border-box; }
body { font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
       margin: 0; padding: 0; background: #f8fafc; color: #1e293b; line-height: 1.5; }
.container { max-width: 1100px; margin: 0 auto; padding: 40px 32px; }
.header { background: linear-gradient(135deg, #0f172a, #1e293b); color: white; padding: 48px 40px; border-radius: 12px; margin-bottom: 32px; }
.header h1 { margin: 0 0 8px; font-size: 28px; font-weight: 800; }
.header .subtitle { color: #94a3b8; font-size: 14px; margin: 0; }
.meta-groups { display: grid; grid-template-columns: repeat(auto-fit, minmax(220px, 1fr)); gap: 20px; margin-top: 24px; }
.meta-group { background: rgba(255,255,255,0.05); border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; padding: 14px 16px; }
.meta-group .group-title { color: #64748b; font-size: 10px; text-transform: uppercase; letter-spacing: 0.08em; font-weight: 700; margin-bottom: 10px; }
.meta-row { padding: 6px 0; font-size: 13px; }
.meta-row + .meta-row { border-top: 1px solid rgba(255,255,255,0.06); }
.meta-row .k { color: #94a3b8; display: block; margin-bottom: 2px; }
.meta-row .v { color: #f1f5f9; font-weight: 600; font-family: 'JetBrains Mono', monospace; font-size: 12px; word-break: break-word; }

.section { background: white; border: 1px solid #e2e8f0; border-radius: 10px; margin-bottom: 24px; overflow: hidden; }
.section-header { padding: 16px 24px; border-bottom: 1px solid #e2e8f0; display: flex; align-items: center; gap: 12px; }
.section-header h2 { margin: 0; font-size: 18px; }
.distribution-body { padding: 20px 24px; }
.distribution-track { height: 22px; display: flex; overflow: hidden; background: #e2e8f0; border-radius: 5px; }
.distribution-segment { display: block; min-width: 2px; }
.distribution-segment.pass, .legend-dot.pass { background: var(--pass); }
.distribution-segment.fail, .legend-dot.fail { background: var(--fail); }
.distribution-segment.warn, .legend-dot.warn { background: var(--warn); }
.distribution-segment.skip, .legend-dot.skip { background: var(--skip); }
.distribution-empty { width: 100%; color: #64748b; font-size: 11px; line-height: 22px; text-align: center; }
.distribution-legend { display: flex; flex-wrap: wrap; gap: 12px 24px; margin-top: 14px; }
.legend-item { display: grid; grid-template-columns: 8px auto auto; align-items: center; gap: 7px; color: #64748b; font-size: 12px; }
.legend-item strong { color: #1e293b; font-family: monospace; }
.legend-dot { width: 8px; height: 8px; border-radius: 50%; }

table.overview { width: 100%; border-collapse: collapse; font-size: 13px; }
table.overview th { background: #f8fafc; padding: 12px 16px; text-align: left; font-weight: 600;
                    color: #64748b; font-size: 11px; text-transform: uppercase; letter-spacing: 0.05em;
                    border-bottom: 1px solid #e2e8f0; }
table.overview td { padding: 12px 16px; border-bottom: 1px solid #f1f5f9; }
table.overview tr:last-child td { border-bottom: none; }
table.overview tr:hover { background: #f8fafc; }
table.overview .test-id { font-family: 'JetBrains Mono', monospace; font-weight: 600; color: #1e293b; }
table.overview .status-cell { font-weight: 700; font-size: 12px; text-transform: uppercase; }
/* review-plan 5.1.5: the capability and its state on one line, the state right-aligned,
   bold and in the semantic status colour. */
.capability-list, .kv { margin: 0; }
.capability-row, .kv-row { display: flex; align-items: baseline; gap: 12px; padding: 7px 0; border-bottom: 1px solid #eef2f6; font-size: 13px; }
.capability-row:last-child, .kv-row:last-child { border-bottom: 0; }
.capability-row dt, .kv-row dt { margin: 0; color: #354352; }
.capability-row dd { margin: 0 0 0 auto; font-weight: 800; font-size: 12px; letter-spacing: 0.04em; }
.capability-row dd.good { color: var(--pass); }
.capability-row dd.bad { color: var(--fail); }
.capability-row dd.unknown { color: var(--skip); }
.kv-row dd { margin: 0 0 0 auto; font-family: 'JetBrains Mono', ui-monospace, monospace; color: #17202b; }
/* review-plan 5.6.5: threshold-banded reliability bar. */
.reliability-bar { display: flex; align-items: center; gap: 10px; padding: 6px 0; font-size: 12px; }
.reliability-bar-label { flex: 0 0 90px; color: #354352; }
.reliability-bar-track { flex: 1; height: 8px; border-radius: 4px; background: #eef2f6; overflow: hidden; }
.reliability-bar-fill { height: 100%; border-radius: 4px; }
.reliability-bar-fill.reliability-good { background: #4b9b69; }
.reliability-bar-fill.reliability-warn { background: #b2872d; }
.reliability-bar-fill.reliability-bad { background: #b54c4c; }
.reliability-bar-value { flex: 0 0 40px; text-align: right; font-weight: 700; }
/* 5.6.6: the Open + STREAMON trend line, cycle order on x. */
.t06-trend-line { fill: none; stroke: #386fa4; stroke-width: 2; }
.t06-trend-point { fill: #386fa4; stroke: #fff; stroke-width: 1.5; }
/* review-plan 5.7.6: requested (line) vs allocated (bar) -- distinct geometry, not just
   colour, so the two series read correctly in black-and-white print too. */
.t07-allocated-bar { fill: #7bb7d9; }
.t07-requested-line { fill: none; stroke: #344054; stroke-width: 2; stroke-dasharray: 4 3; }
.t07-legend-requested { background: #344054; }
.t07-legend-allocated { background: #7bb7d9; }
/* 5.7.7: latency by allocated depth -- a point per depth, a range line when repeats at
   the same depth differ. */
.t07-depth-point { fill: #386fa4; }
.t07-depth-range { stroke: #386fa4; stroke-width: 2; }
.t07-depth-label { fill: #344054; font-size: 10px; font-weight: 700; }
.t07-legend-range { background: #386fa4; }
.t07-legend-mean { background: #386fa4; border-radius: 50%; }
/* review-plan 5.9.6/5.9.7: availability against a dashed review threshold, and the
   remaining post-requeue wait. Colours from t09-requeue-delay-preview.html. */
.t09-availability-line, .t09-wait-line { fill: none; stroke: #386fa4; stroke-width: 2; }
.t09-availability-point, .t09-wait-point { fill: #386fa4; stroke: #fff; stroke-width: 1.5; }
.t09-threshold { stroke: #b78420; stroke-width: 1; stroke-dasharray: 4 3; }
.t09-threshold-label { fill: #b78420; font-size: 9px; font-weight: 700; }
.t09-point-label { fill: #b54c4c; font-size: 10px; font-weight: 800; }
.t09-legend-availability { background: #386fa4; }
.t09-legend-threshold { background: transparent; border-top: 1px dashed #b78420; height: 0; }
/* review-plan 5.10.6: the flag state carries its own tone. An informational flag that was
   simply not observed is neutral -- never an error colour, because KEYFRAME=0 on a raw
   stream is normal rather than a fault. */
.flag-state { text-align: right; font-weight: 800; font-size: 11px; }
.flag-state.good { color: var(--pass); }
.flag-state.active { color: #2563eb; }
.flag-state.neutral { color: #64717e; }
/* 5.10.8: the boundary note against T21, quieter than the evidence above it. */
.boundary-note { margin: 12px 16px 0; color: #64717e; font-size: 11px; font-style: italic; }
/* review-plan 5.11.4/5.11.7: the full-frame bar is the primary result; the cache-sized
   ones are visibly a different series so 3x the full-frame figure is not read as camera
   throughput. */
.t11-full-bar { background: #2e6fa3; }
.t11-cache-bar { background: #71879a; }
/* review-plan 5.8.5/5.8.6: saturation-load bars and queue slots, colours and geometry
   from the approved preview (t08-buffer-saturation-preview.html). */
.load-row { display: grid; grid-template-columns: 74px 1fr 112px; gap: 10px; align-items: center; margin-top: 14px; font-size: 11px; }
.load-track { height: 18px; border: 1px solid #bcc6d0; background: #eef2f5; }
.load-bar { height: 100%; background: #3477aa; }
.interpretation { margin: 10px 0 0; color: #526171; font-size: 10px; }
.queue-row { display: grid; grid-template-columns: 74px 1fr 70px; gap: 10px; align-items: center; margin-top: 14px; font-size: 11px; }
.slots { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; }
.slot { min-height: 42px; display: flex; flex-direction: column; justify-content: center; padding: 5px 9px; border: 1px solid; border-radius: 3px; text-align: center; font-size: 10px; font-weight: 800; }
.slot small { margin-top: 1px; font-size: 8px; font-weight: 600; }
.slot.error { border-color: #b84c4c; background: #fff1f1; color: #932f2f; }
.slot.ready { border-color: #43835b; background: #eff8f2; color: #23633b; }
.queue-value { text-align: right; font-weight: 800; }
.t08-legend-error { background: #fff1f1; border: 1px solid #b84c4c; }
.t08-legend-ready { background: #eff8f2; border: 1px solid #43835b; }
/* 5.8.8: the decoded flag evidence row. */
.evidence-row { display: grid; grid-template-columns: 100px 1fr 1fr; gap: 12px; align-items: center; padding: 9px 0; border-bottom: 1px solid #e5eaee; font-size: 11px; }
.evidence-row:last-child { border-bottom: 0; }
.evidence-row .raw { text-align: right; color: #64717e; font-family: 'JetBrains Mono', ui-monospace, monospace; }
/* review-plan 5.7: two charts side by side, per the approved preview's ".charts" rule. */
/* One chart per row (design-spec: no side-by-side panels). Two charts sharing a row
   render the SAME viewBox at half the width, so one SVG unit stops being one CSS pixel
   and every declared font-size scales by the panel ratio. */
.section.charts { display: grid; grid-template-columns: 1fr; gap: 28px; }
/* Every chart renders its SVG at the SAME width, so one viewBox fits them all and the
   scale stays 1.0. Two things broke that: containers with different padding, and charts
   nested one section inside another -- each level added its own 16px and produced 868,
   886, 900 and 902px boxes for a single 906-unit viewBox.
   The fix is to give the chart box NO horizontal padding of its own and let the section
   that holds it be the only one that indents. */
/* T01's three Device Evidence sections side by side, verbatim from the approved preview.
   Without it they stack, which is the same content on a different page. */
.three-col { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 0; border-bottom: 1px solid #dfe5eb; }
.three-col .section { border-bottom: 0; border-right: 1px solid #dfe5eb; }
.three-col .section:last-child { border-right: 0; }
.chart-frame { padding: 0 16px; }
.section .section { padding: 0; border-bottom: 0; }
.metric-chart { padding: 14px 0; }
/* Exactly what the approved previews declare. The chart fills its container and keeps its
   proportions through aspect-ratio, so it never overflows and never needs to scroll.
   This replaces a fixed `width: 906px; min-width: 906px` plus `overflow-x: auto`, which
   was reasoned from "one SVG unit should be one CSS pixel, so an 8px label is really 8px".
   That reasoning had a visible cost the previews had already rejected: any card narrower
   than 906px grew a scrollbar under its chart -- a control the approved design does not
   have, on every chart, at common window widths. Label sizes scale with the chart, which
   is what the previews do. */
.chart-frame svg, .metric-chart svg { display: block; width: 100%; aspect-ratio: 906/240; height: auto;
                                      overflow: visible; }
/* Charts render at a FIXED 906px rather than filling whatever their container gives
   them. Chasing the container width does not converge: the staircase indent, the nested
   section and .metric-chart's own padding each subtract a different amount, and the
   measured widths walked 870 / 886 / 938 / 838 as each was "fixed" in turn.
   Pinning the rendered width to the viewBox width makes one SVG unit exactly one CSS
   pixel everywhere, which is the property S8 actually asks for. The card body gives the
   chart 968px, so a 906px chart fits without a scrollbar -- measured, not assumed. */

/* review-plan 5.3.4: T03's stacked two-phase timing bar, colours from the approved
   preview (docs/assets/previews/t03-unified-preview.html). */
.t03-phase-streamon { fill: #386fa4; }
.t03-phase-frame { fill: #7bb7d9; }
.t03-bar-label { fill: #fff; font-size: 10px; font-weight: 700; }
.t03-bar-note { fill: #344054; font-size: 10px; font-weight: 700; }
.legend { display: flex; gap: 16px; margin-top: 6px; font-size: 11px; color: #475467; }
.legend-item { display: flex; align-items: center; gap: 6px; }
.legend-swatch { display: inline-block; width: 10px; height: 10px; border-radius: 2px; }
/* review-plan 5.3.3: a threshold sits beside the measurement it constrains. */
.threshold-note { color: #64717e; font-weight: 500; font-size: 11px; }
/* 5.3.8: the retries/attempts section is visually secondary to the main summary. */
.kv-row dd { font-variant-numeric: tabular-nums; }
/* review-plan 5.2.5: the hexadecimal id under the control name, at lower weight -- it
   identifies the control without competing with its name. */
.control-id { display: block; margin-top: 2px; color: #64717e; font-size: 11px; font-family: 'JetBrains Mono', ui-monospace, monospace; }
/* 5.2.3: a control-class record is a full-width group heading, not a control. */
table.evidence tr.group-row td { background: #f1f5f9; color: #44515f; font-size: 11px; font-weight: 800; text-transform: uppercase; letter-spacing: 0.05em; }
/* 5.1.6: the total sits with the section heading, not as a key/value row. */
.section-count { margin: -4px 0 8px; color: #52606d; font-size: 12px; font-weight: 700; }
.format-list { margin: 0; padding-left: 18px; color: #354352; font-size: 13px; }
.format-list li { padding: 2px 0; }
table.overview .details-cell a { color: #2563eb; text-decoration: none; font-weight: 600; }
table.overview .details-cell a:hover { text-decoration: underline; }
/* Overview tables sit indented under their backend band (review-plan 3.3): the indent is
   what shows these rows belong to the band above them. */
table.overview { margin-left: 28px; width: calc(100% - 28px); }
table.overview .details-cell a { color: #2563eb; text-decoration: none; font-weight: 600; }
table.overview .details-cell a:hover { text-decoration: underline; }
/* Overview tables sit indented under their backend band (review-plan 3.3): the indent is
   what shows that these rows belong to the band above them. */
table.overview { margin-left: 28px; width: calc(100% - 28px); }
table.overview .status-cell.pass { color: var(--pass); }
table.overview .status-cell.fail { color: var(--fail); }
table.overview .status-cell.warn { color: var(--warn); }
table.overview .status-cell.skip { color: var(--skip); }
table.overview .duration { color: #64748b; font-family: monospace; }
table.overview .summary-text { color: #475569; }

/* Detailed Result Card Template (plan 3.1) -- transferred from the approved
   docs/assets/previews/detailed-result-card.css. Only the outer shell: the band, the
   card, the header and the section rhythm. Chart and table styles stay test-specific. */
/* review-plan 3.2 / 4.2: light grey ground, dark left accent, a visible BACKEND label
   and the value in monospace at high weight. */
.backend { display: flex; align-items: center; height: 42px; padding: 0 18px; border-top: 1px solid #aeb9c5; border-bottom: 1px solid #aeb9c5; border-left: 5px solid #44515f; background: #eef2f5; color: #44515f; font-size: 12px; font-weight: 750; text-transform: uppercase; }
.backend-label { letter-spacing: 0.08em; }
.backend strong { margin-left: 12px; color: #111820; font-size: 14px; font-family: 'JetBrains Mono', ui-monospace, monospace; font-weight: 800; }
.backend-note { margin-left: auto; color: #52606d; font-size: 11px; font-weight: 600; text-transform: none; }
.backend-gap { height: 16px; }
/* Status colours in one place. Without a modifier a card reads as PASS. */
/* One status colour for the whole report (design-spec §4). Test Results Overview is the
   reference, so the card header and the verdict cell read the same custom properties it
   does. Three literal palettes used to live here and in the verdict cells, which made the
   same PASS render as three different greens depending on the section. */
.test-card { --status: var(--pass); --status-text: var(--pass); }
.test-card.pass { --status: var(--pass); --status-text: var(--pass); }
.test-card.warn { --status: var(--warn); --status-text: var(--warn); }
.test-card.fail { --status: var(--fail); --status-text: var(--fail); }
.test-card.skip { --status: var(--skip); --status-text: var(--skip); }
.test-card { display: block; margin: 14px 0 44px 28px; border: 1px solid #cbd3dc; border-left: 5px solid var(--status); border-radius: 6px; overflow: hidden; background: #fff; }
.test-header { display: grid; grid-template-columns: 64px 1fr auto; gap: 12px; align-items: center; min-height: 56px; padding: 11px 16px; border-bottom: 1px solid #d6dde5; background: #f8fafb; }
.test-header .status { color: var(--status-text); font-size: 13px; font-weight: 800; }
.test-header h2 { margin: 0; font-size: 16px; font-weight: 750; text-transform: none; line-height: 1.25; }
.test-header .duration { color: #536171; font-size: 13px; font-weight: 400; font-variant-numeric: tabular-nums; }
.test-card .section-label { margin: 0 0 8px; color: #52606d; font-size: 11px; font-weight: 800; text-transform: uppercase; }
.test-card .section { padding: 16px; border-bottom: 1px solid #dfe5eb; }
.test-card .section:last-child { border-bottom: 0; }
.test-card .section p { margin: 0; color: #354352; font-size: 12px; }
.test-body { padding: 16px; }
/* Grid tables (design-spec S1). Header and rows share one grid-template, so the
   columns line up exactly; the horizontal padding sits on the ROW, not on each cell,
   which is what keeps the columns from creeping inward. Columns are equal 1fr: a
   fixed px width makes the gaps between columns visibly unequal. */
.grid-head, .grid-row { display: grid; gap: 0; padding: 6px 16px; }
.grid-head { color: #52606d; font-size: 10px; font-weight: 800; text-transform: uppercase; letter-spacing: .3px; border-bottom: 1px solid #aeb9c5; }
.grid-row { color: #2d3a47; font-size: 9px; border-bottom: 1px solid #f0f2f5; }
/* The Test Configuration table lists inputs, not findings, and the approved previews
   give it a tighter row rhythm than the measurement tables: 5px against 6px. One pixel
   per row is invisible on its own and plainly visible down a stack of eight parameters. */
.config-section .grid-head, .config-section .grid-row { padding-top: 5px; padding-bottom: 5px; }
.grid-row:last-child { border-bottom: 0; }
.cols-2 { grid-template-columns: repeat(2, 1fr); }
.cols-3 { grid-template-columns: repeat(3, 1fr); }
.cols-4 { grid-template-columns: repeat(4, 1fr); }
.cols-5 { grid-template-columns: repeat(5, 1fr); }
.cols-6 { grid-template-columns: repeat(6, 1fr); }
.cols-7 { grid-template-columns: repeat(7, 1fr); }
.cols-8 { grid-template-columns: repeat(8, 1fr); }
.cols-9 { grid-template-columns: repeat(9, 1fr); }
.cols-10 { grid-template-columns: repeat(10, 1fr); }
/* Only a verdict cell carries colour and weight; every other cell stays plain, so a
   coloured number can never be misread as a status (design-spec). */
.grid-row .verdict { font-weight: 800; letter-spacing: .3px; }
.grid-row .verdict.pass { color: var(--pass); }
.grid-row .verdict.warn { color: var(--warn); }
.grid-row .verdict.fail { color: var(--fail); }
.grid-row .verdict.skip { color: var(--skip); }
/* Item subheading under Measurement (S2): names the chart or table that follows. */
.item-label { margin: 0 0 8px; padding: 0 16px 5px 32px; color: #596776; font-size: 10px; font-weight: 800; text-transform: uppercase; letter-spacing: .3px; border-bottom: 1px solid #dfe5eb; }
.grid-row + .item-label { margin-top: 14px; }
/* Staircase (S3): section label at 21px, item label at 53px, item content at 69px.
   Scoped by has-items so a section without subheadings keeps its own indent. */
.has-items .grid-head, .has-items .grid-row, .has-items .chart-legend { padding-left: 48px; }
/* The chart box keeps the staircase alignment WITHOUT the indent eating into the 906px
   the SVG needs: 968 available - 48 indent = 920, which overflowed by 14px. Pulling the
   box out by the section padding restores the room while the left edge still lines up. */
.has-items .chart-frame { padding-left: 48px; margin-right: -20px; }
/* T26's warm-up column chart, verbatim from the approved preview. The gridline and the
   two text classes are shared with the other SVG charts, so they are declared once here
   rather than per test. */

.col-stab, .legend-stab { fill: #4b9b69; background: #4b9b69; }
.gridline { stroke: #e2e7ec; stroke-width: 1; }
.svg-label { fill: #657382; font-size: 8px; }
.svg-value { fill: #44515f; font-size: 9px; font-weight: 700; }
.axis-title { fill: #44515f; font-size: 9px; font-weight: 600; }
/* The rest of the SVG vocabulary, copied verbatim from the approved previews. T13's line
   and area, T14's and T23's columns, T16's two-series lines. Declared once here because
   the previews declare the same values in each file that uses them; a chart that names
   one of these classes without a rule renders as an invisible or unstyled shape. */
.axis { stroke: #9ba8b5; stroke-width: 1; }
.arrow { fill: #8d99a6; }
.axis-arrow { fill: #9ba8b5; }
.tick { stroke: #9ba8b5; stroke-width: 1; }
.axis-text { fill: #657382; font-size: 8px; }
.axis-label { fill: #44515f; font-size: 9px; font-weight: 600; }
.svg-axis-title { fill: #44515f; font-size: 9px; font-weight: 600; }
.area { fill: #e8f0f7; }
.line { fill: none; stroke: #2563a6; stroke-width: 2.5; }
.point { fill: #fff; stroke: #2563a6; stroke-width: 2; }
.point-miss { fill: #fff; stroke: #c55757; stroke-width: 2; }
.point-cliff { fill: #fff; stroke: #b8860b; stroke-width: 2; }
.lat-bar, .win-bar { fill: #2563a6; }
/* The preview tints one column of T14's distribution so P95 reads apart from min/mean/max. */
.lat-bar-p95 { fill: #71879a; }
.line-high { fill: none; stroke: #2563a6; stroke-width: 2.5; }
.line-low { fill: none; stroke: #71879a; stroke-width: 2.5; }
.point-high { fill: #fff; stroke: #2563a6; stroke-width: 2; }
.point-low { fill: #fff; stroke: #71879a; stroke-width: 2; }
/* Horizontal bar charts, verbatim from the approved previews: one row is a fixed-width
   label, a proportional track and the unit, and the axis under them repeats the same
   three-column grid so the ticks line up with the tracks. The value rides INSIDE the
   fill, which is what keeps a long row from pushing the unit column out of alignment. */
.bar-row { display: grid; grid-template-columns: 120px 1fr 74px; gap: 10px; align-items: center; margin: 6px 0; }
.bar-label { font-size: 10px; font-weight: 700; color: #52606d; }
.bar-track { height: 20px; border-radius: 3px; background: #eef2f5; overflow: hidden; }

.bar-fill { height: 100%; display: flex; align-items: center; padding: 0 6px; color: #fff; font-size: 9px; font-weight: 700; white-space: nowrap; }
.bar-info { font-size: 10px; color: #44515f; font-variant-numeric: tabular-nums; text-align: right; }
.bar-axis { display: grid; grid-template-columns: 120px 1fr 74px; gap: 10px; margin-top: 4px; }
.scale-in { display: flex; justify-content: space-between; font-size: 9px; color: #7a8693; font-variant-numeric: tabular-nums; }
.chart-axis-caption { margin: 6px 0 0; padding-left: 16px; color: #7a8693; font-size: 9px; }
/* Series colours. A measured quantity and its upper statistic are the same family in two
   weights, so mean/nonblock share one blue and max/p95/block share one grey. */
.bar-mean, .legend-mean, .bar-nonblock, .legend-nonblock { background: #2563a6; }
.bar-max, .legend-max, .bar-block, .legend-block, .bar-p95, .legend-p95 { background: #71879a; }
.bar-ok, .legend-ok { background: #4b9b69; }
/* T22 contrasts unique against identical payload pairs; the approved preview colours the
   identical bar as the finding, not as an error. */
.bar-uniq, .legend-uniq { background: #8fbcdb; }
/* T16 reads the two trigger edges against each other: the HIGH reference is measured,
   the LOW one derived, so they take the same two colours the preview uses. */
.bar-high, .legend-high { background: #2563a6; }
/* T24 sets the idle baseline against the loaded phase, and shows the delta the verdict
   is drawn from on its own. */
.bar-thr, .legend-thr { background: #8fbcdb; }
.bar-base, .legend-base { background: #2563a6; }
.bar-load, .legend-load { background: #b8860b; }
.bar-delta, .legend-delta { background: #2563a6; }
.bar-low, .legend-low { background: #b8860b; }
.bar-ident, .legend-ident { background: #c55757; }
.bar-miss, .legend-miss { background: #c83d4b; }
.chart-legend { display: flex; gap: 16px; margin: 0 0 8px; padding-left: 16px; color: #52606d; font-size: 9px; }
.legend-dot { display: inline-block; width: 10px; height: 10px; margin-right: 4px; border-radius: 2px; vertical-align: middle; }
/* Charts use a ~520-unit viewBox so one SVG unit renders at roughly one CSS
   pixel: with the old 760-unit box inside a 360px column every label was scaled
   down to about 7px. Wide charts (many categories, or horizontal rows) take the
   full row instead of being squeezed into a column. */
.metric-visuals { display: grid; grid-template-columns: 1fr; gap: 12px; margin-bottom: 12px; }
/* Aligned with .chart-frame: the approved previews frame a chart with whitespace, not a
   border and a shadow. These two classes are one shell drawn by two different renderers;
   while they differed, the same chart looked boxed or unboxed depending on the caller. */
.metric-chart { border: 0; background: transparent; padding: 14px 0; box-shadow: none; }
.metric-chart--wide { grid-column: 1 / -1; }
.metric-chart-title { color: #1e293b; font-size: 13px; font-weight: 750; margin-bottom: 8px; }
.metric-chart-title span { color: #94a3b8; font-size: 10px; font-weight: 600; margin-left: 4px; }
.metric-xy-chart svg { min-width: 0; }
.chart-grid { stroke: #eef2f7; stroke-width: 1; vector-effect: non-scaling-stroke; }
.chart-axis { stroke: #cbd5e1; stroke-width: 1; vector-effect: non-scaling-stroke; }
.axis-caption { fill: #475569; font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; font-size: 11.5px; font-weight: 700; }
.guide-line { stroke: #cbd5e1; stroke-width: 1; stroke-dasharray: 3 4; vector-effect: non-scaling-stroke; }
.dot-row-line { stroke: #f5f8fb; stroke-width: 16; stroke-linecap: round; vector-effect: non-scaling-stroke; }
.zero-baseline { stroke: #94a3b8; stroke-width: 1; vector-effect: non-scaling-stroke; }
.metric-line { fill: none; stroke: #2a78d6; stroke-width: 2; stroke-linejoin: round; stroke-linecap: round; vector-effect: non-scaling-stroke; }
.metric-point { fill: #2a78d6; stroke: #fff; stroke-width: 2; vector-effect: non-scaling-stroke; }
.axis-value, .axis-label, .point-value, .dot-label, .bar-point-value { font-family: 'JetBrains Mono', monospace; fill: #64748b; font-size: 11px; }
.dot-label { fill: #475569; font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; font-weight: 700; }
.point-value, .bar-point-value { fill: #1e293b; font-size: 10.5px; font-weight: 700; }
/* Bar fills come from the renderer: a sequential ramp when the category axis is
   an ordered quantity, one accent when it is nominal, the ordinal ramp for the
   Mean/P95/Max series. No tone-* cycle -- that gave distant categories the same
   colour and reused the verdict colours for plain data. */
.vertical-bar, .horizontal-bar { vector-effect: non-scaling-stroke; }
.chart-series-legend { display: flex; flex-wrap: wrap; gap: 6px 16px; align-items: center; margin-top: 8px; color: #475569; font-size: 11px; }
.chart-series-legend span { display: inline-flex; align-items: center; gap: 6px; }
.chart-series-legend i { width: 9px; height: 9px; border-radius: 2px; }
.chart-ramp-key { display: flex; align-items: center; gap: 8px; margin-top: 8px; color: #64748b; font-size: 10.5px; }
.chart-ramp-key i { display: flex; flex: 0 0 110px; height: 8px; border-radius: 2px; overflow: hidden; }
.chart-ramp-key b { flex: 1; }
.chart-omissions { margin: 0 0 12px; padding: 8px 11px; border-left: 3px solid #cbd5e1; background: #f8fafc; border-radius: 0 4px 4px 0; color: #475569; font-size: 12px; }
.chart-omissions strong { color: #1e293b; }
.chart-omissions code { font-family: 'JetBrains Mono', monospace; font-size: 11px; color: #b42318; }
.test-note { margin: 0 0 12px; padding: 10px 13px; border-left: 3px solid #94a3b8; background: #f8fafc; border-radius: 0 4px 4px 0; color: #334155; font-size: 13px; line-height: 1.65; }
.test-note + .test-note { margin-top: -4px; }
.test-note code { font-family: 'JetBrains Mono', monospace; font-size: 11.5px; color: #b42318; }
.hit-zone { opacity: 0.48; }
.hit-zone-high { fill: #dcfce7; }
.hit-zone-mid { fill: #fef3c7; }
.hit-zone-low { fill: #fee2e2; }
.distribution-area { fill: #bae6fd; opacity: 0.58; }
.distribution-line { stroke: #1c5cab; stroke-width: 2; }
.t13-probe-point { fill: #1c5cab; }
.threshold-zone { rx: 4; ry: 4; }
.threshold-risk { fill: #fecaca; }
.threshold-margin { fill: #fde68a; }
.threshold-safe { fill: #bbf7d0; }
.threshold-row { stroke: rgba(255, 255, 255, 0.82); stroke-width: 16; stroke-linecap: round; vector-effect: non-scaling-stroke; }
.threshold-label { fill: #475569; font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; font-size: 11px; font-weight: 700; }
/* The threshold markers are a risk ordering (first miss -> cliff -> production),
   so they keep a semantic red-to-green scale rather than the data palette. */
.threshold-point.marker-0 { fill: #dc2626; }
.threshold-point.marker-1 { fill: #d97706; }
.threshold-point.marker-2 { fill: #2a78d6; }
.threshold-point.marker-3 { fill: #15803d; }
.threshold-point { stroke: #fff; stroke-width: 2; vector-effect: non-scaling-stroke; }
.supporting-values { margin: 0 0 12px; }
.supporting-title { color: #64748b; font-size: 11px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.05em; margin-bottom: 6px; }
.metric-kv-list { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 230px), 1fr)); gap: 0 24px; margin: 0; border-top: 1px solid #e2e8f0; }
.metric-kv-row { display: grid; grid-template-columns: minmax(0, 1fr) auto; gap: 12px; padding: 8px 0; border-bottom: 1px solid #e2e8f0; }
.metric-kv-row dt { color: #64748b; font-size: 11px; overflow-wrap: anywhere; }
.metric-kv-row dd { margin: 0; color: #1e293b; font-family: monospace; font-size: 12px; font-weight: 700; text-align: right; }
.metric-kv-row dd span { color: #94a3b8; font-size: 10px; }
.detail-list { background: #f8fafc; border-radius: 6px; padding: 12px 16px; font-family: 'JetBrains Mono', monospace;
               font-size: 12px; line-height: 1.8; color: #475569; white-space: pre-wrap; }
.warnings-box { background: #fffbeb; border: 1px solid #fde68a; border-radius: 6px; padding: 10px 14px; margin-top: 8px;
                color: #92400e; font-size: 13px; }
.footer { text-align: center; color: #94a3b8; font-size: 12px; margin-top: 40px; padding-top: 24px; border-top: 1px solid #e2e8f0; }
/* Export toolbar (plan 3.3), transferred from the layout approved in plan 1.7.

   The toolbar belongs to the report header, not to the viewport. The earlier
   `position: fixed; right: 20px` anchored it to the window, so on a wide screen the
   buttons sat outside the dark header entirely. It now rides the normal document flow
   and scrolls with the title.

   Its own row, below the title group: beside the title the four buttons (527px) left
   381px for a 426px H1, which wrapped to two lines. On its own row they have the full
   inner width and the title is untouched. */
.export-row { display: flex; justify-content: flex-end;
              /* The same 24px rhythm the header already used between the subtitle and
                 the metadata cards -- no separator or nested panel is introduced. The
                 margin lives on this row, so print (where the row is display:none)
                 loses the space with it and the printed header geometry is unchanged. */
              margin: 24px 0; }
.export-actions { display: flex; flex-wrap: wrap; justify-content: flex-end; align-content: center; gap: 10px; }
.export-pdf-btn { display: flex; align-items: center; gap: 8px; text-decoration: none;
                  background: #0f172a; color: white; border: none; border-radius: 8px; padding: 10px 18px;
                  font-size: 13px; font-weight: 700; font-family: inherit; cursor: pointer; box-shadow: 0 4px 12px rgba(0,0,0,0.2); }
.export-pdf-btn:hover { background: #1e293b; }
/* A real disabled control: unfocusable and unclickable, which only the disabled
   attribute gives. An ARIA-only placeholder still takes focus and still fires clicks. */
.export-pdf-btn:disabled { background: #334155; color: #94a3b8; cursor: not-allowed; box-shadow: none; }
.export-pdf-btn:disabled:hover { background: #334155; }
.export-note { text-align: right; color: #94a3b8; font-size: 11px; margin: -16px 0 24px; }
@media (max-width: 700px) {
  .container { padding: 20px 12px; }
  .header { padding: 28px 20px; border-radius: 8px; }
  .test-card { margin: 12px; }
  .test-header { grid-template-columns: 1fr; row-gap: 4px; }
  table.overview thead { display: none; }
  table.overview tbody { display: block; }
  table.overview tr { display: grid; grid-template-columns: auto auto; gap: 6px 12px; padding: 12px 14px; }
  table.overview td { padding: 0; border: none; }
  table.overview tr + tr { border-top: 1px solid #e2e8f0; }
  /* review-plan 3.9: with the header row hidden, each cell has to name its own field --
     "Status: FAIL", not a bare "FAIL". The Details link sits below the test information. */
  table.overview td::before { content: attr(data-label) ": "; color: #64748b; font-weight: 600; }
  table.overview td:nth-child(1) { grid-column: 1 / 3; grid-row: 1; overflow-wrap: anywhere; }
  table.overview td:nth-child(1)::before { content: none; }
  table.overview td:nth-child(2) { grid-column: 1; grid-row: 2; }
  table.overview td:nth-child(3) { grid-column: 2; grid-row: 2; }
  table.overview td:nth-child(4) { grid-column: 1 / 3; grid-row: 3; }
  table.overview td:nth-child(4)::before { content: none; }
  /* 3.9: the band keeps its full width on a narrow screen. */
  .backend { margin-left: 0; }
  table.overview { margin-left: 0; width: 100%; }
  /* The chart viewBox now matches its rendered width, so the label sizes no
     longer need to be scaled up to compensate for a 760-unit box. */
  /* Centred when the row cannot hold four buttons, so a wrapped 2x2 does not sit
     lopsided against the right edge. */
  .export-row { justify-content: center; }
  .export-actions { justify-content: center; }
  .export-note { text-align: center; }
}
/* Page geometry, from review-plan 6.8. Its SINGLE source is this rule: a second
   margin declaration would fight it, and the user's print-dialog margin choice
   could no longer take effect. The dialog can still override these values --
   6.8 says so, and the application does not try to prevent it. */
@page { size: A4; margin: 12mm 10mm 14mm; }
/* Status colour is meaning, not decoration: with background-graphics off, a PASS
   and a FAIL row would otherwise print identically. 6.8 rules 3-4. */
* { print-color-adjust: exact; -webkit-print-color-adjust: exact; }
@media print { body { background: white; }
               /* review-plan 3.10 / 6.4: the head repeats on every continuation page, so a
                  reader never meets a bare column of values, and a row is never split. */
               table.overview thead { display: table-header-group; }
               table.overview tr { break-inside: avoid; page-break-inside: avoid; }
               /* 3.10 / 6.5: a band must not be orphaned from its first row. */
               .backend { break-after: avoid; page-break-after: avoid; }
               /* review-plan 3.10 / 6.4: the head repeats on every continuation page, so a
                  reader never meets a bare column of values; and a row is never split. */
               table.overview thead { display: table-header-group; }
               table.overview tr { break-inside: avoid; page-break-inside: avoid; }
               /* 3.10 / 6.5: a band must not be orphaned from its first row. */
               .backend { break-after: avoid; page-break-after: avoid; }
               /* No container padding in print: the page margin comes from @page
                  alone (6.8 rules 1-2). Vertical breathing room stays. */
               .container { padding: 20px 0; }
               .header { break-inside: avoid; }
               .result-distribution { break-inside: avoid; }
               /* review-plan 6.5: Detailed Results always opens a fresh page. */
               .detailed-results { break-before: page; page-break-before: always; }
               /* 6.6: a card that fits stays whole; a card longer than the printable area
                  splits at its BLOCK boundaries, never mid-block. break-inside: avoid on
                  the card would instead push a long card onto a page it still overflows. */
               .test-card { break-inside: auto; }
               .test-card .test-header { break-after: avoid; page-break-after: avoid; }
               .test-card .section { break-inside: avoid; page-break-inside: avoid; }
               .test-card > .summary { break-inside: avoid; page-break-inside: avoid; }
               .test-card .test-note, .test-card .warnings-box { break-inside: avoid; }
               /* 6.6 / 6.7: a single row, chart or configuration item never splits, and a
                  long table repeats its head. */
               table.evidence thead { display: table-header-group; }
               table.evidence tr { break-inside: avoid; page-break-inside: avoid; }
               .metric-chart { break-inside: avoid; page-break-inside: avoid; }
               .export-row, .export-note { display: none; }
               /* A printed column is narrower than a screen row, so a narrow chart
                  would be blown up while a wide one shrank. Cap the narrow ones and
                  let the wide ones use the full text width. */

               /* Page geometry is declared once, above this block. */ }
</style></head><body>
<div class="container">
)";

  // Header
  out << "<div class=\"header\"><div class=\"header-title-group\">";
  out << "<h1>V4L2 Camera Diagnostic Report</h1>";
  out << "<p class=\"subtitle\">Automated hardware diagnostic test results</p>";
  out << "</div>";
  // The four-button export toolbar, transferred from the layout approved in plan 1.7:
  // its own row inside the header, BELOW the title. An earlier attempt put it above the
  // H1, which made the export actions read as the report's primary content.
  out << render_export_toolbar(result, dmesg_log_present);
  out << "<div class=\"meta-groups\">";

  out << "<div class=\"meta-group\"><div class=\"group-title\">Run</div>";
  out << "<div class=\"meta-row\"><span class=\"k\">Started</span><span class=\"v\">"
      << html_escape(readable_utc(result.started_at_utc)) << "</span></div>";
  out << "<div class=\"meta-row\"><span class=\"k\">Finished</span><span class=\"v\">"
      << html_escape(readable_utc(result.finished_at_utc)) << "</span></div>";
  const std::string duration = readable_duration(result.started_at_utc, result.finished_at_utc);
  if (!duration.empty()) {
    out << "<div class=\"meta-row\"><span class=\"k\">Duration</span><span class=\"v\">" << html_escape(duration)
        << "</span></div>";
  }
  // review-plan 1.5. A CLI run has no server-side id; the row stays and says so rather
  // than disappearing, because an absent row reads as missing information.
  out << "<div class=\"meta-row\"><span class=\"k\">Run ID</span><span class=\"v\">"
      << (result.run_id.empty() ? std::string("Not applicable (CLI run)") : html_escape(result.run_id))
      << "</span></div>";
  out << "</div>";

  out << "<div class=\"meta-group\"><div class=\"group-title\">System</div>";
  out << "<div class=\"meta-row\"><span class=\"k\">Host</span><span class=\"v\">" << html_escape(result.host_name)
      << "</span></div>";
  out << "<div class=\"meta-row\"><span class=\"k\">Kernel release</span><span class=\"v\">"
      << html_escape(result.kernel_release) << "</span></div>";
  out << "<div class=\"meta-row\"><span class=\"k\">Kernel version</span><span class=\"v\">"
      << html_escape(result.kernel_version) << "</span></div>";
  out << "</div>";

  if (!result.cameras.empty()) {
    out << "<div class=\"meta-group\"><div class=\"group-title\">Camera</div>";
    out << "<div class=\"meta-row\"><span class=\"k\">Device</span><span class=\"v\">"
        << html_escape(result.cameras[0].camera_path) << "</span></div>";
    // review-plan 1.7: the Trigger Profile row is ALWAYS shown here. Under free-run its
    // value states that none is required; the row is never hidden.
    out << "<div class=\"meta-row\"><span class=\"k\">Trigger Profile</span><span class=\"v\">"
        << (result.trigger_mode == TriggerMode::FreeRun
                ? std::string("Not required (free-run)")
                : (result.trigger_profile_id.empty() ? std::string("Unavailable")
                                                     : html_escape(result.trigger_profile_id)))
        << "</span></div>";
    // The backends this camera actually ran, in the order the runner used them.
    std::string backends;
    for (const auto &backend : result.cameras[0].memory_backends) {
      if (!backends.empty()) {
        backends += ", ";
      }
      backends += upper_case(to_string(backend));
    }
    if (backends.empty()) {
      // Fall back to the backends the tests actually ran on. Deduplicated by exact value:
      // a substring check against the accumulated string would match "MMAP" inside
      // "MMAP, ..." only after the first entry, so every later test appended again.
      std::vector<std::string> seen;
      for (const auto &test : result.cameras[0].tests) {
        if (std::find(seen.begin(), seen.end(), test.memory_backend) != seen.end()) {
          continue;
        }
        seen.push_back(test.memory_backend);
        if (!backends.empty()) {
          backends += ", ";
        }
        backends += upper_case(test.memory_backend);
      }
    }
    out << "<div class=\"meta-row\"><span class=\"k\">Backend</span><span class=\"v\">"
        << (backends.empty() ? std::string("Unavailable") : html_escape(backends)) << "</span></div>";
    out << "<div class=\"meta-row\"><span class=\"k\">Role</span><span class=\"v\">"
        << html_escape(result.cameras[0].role) << "</span></div>";
    out << "</div>";

    out << "<div class=\"meta-group\"><div class=\"group-title\">Trigger</div>";
    // Run-level, like JSON and Markdown. Free-run shows only the mode.
    out << "<div class=\"meta-row\"><span class=\"k\">Mode</span><span class=\"v\">" << to_string(result.trigger_mode)
        << "</span></div>";
    // The Trigger Profile row belongs to the CAMERA card (review-plan 1.7); this card
    // carries Mode, Channel and -- under a triggered mode -- the rate and width (1.8).
    for (const auto &binding : result.role_bindings) {
      out << "<div class=\"meta-row\"><span class=\"k\">" << html_escape(binding.role) << "</span><span class=\"v\">"
          << html_escape(binding.trigger_channel_id) << "</span></div>";
    }
    // review-plan 1.8 lists Channel for BOTH free-run and triggered reports, so the row
    // stays either way and states the absence rather than vanishing.
    out << "<div class=\"meta-row\"><span class=\"k\">Channel</span><span class=\"v\">"
        << (result.cameras[0].trigger_description.empty()
                ? (result.trigger_mode == TriggerMode::FreeRun ? std::string("Not required (free-run)")
                                                               : std::string("Unavailable"))
                : html_escape(result.cameras[0].trigger_description))
        << "</span></div>";
    // Run-level, and not rendered under free-run: the values exist but are
    // meaningless when nothing is driven. Same rule as JSON and Markdown.
    if (result.trigger_mode != TriggerMode::FreeRun) {
      std::ostringstream rate_ss, pulse_ss;
      rate_ss << std::fixed << std::setprecision(2) << result.trigger_rate_hz;
      pulse_ss << std::fixed << std::setprecision(2) << result.pulse_width_ms;
      out << "<div class=\"meta-row\"><span class=\"k\">Nominal pulse rate</span><span class=\"v\">" << rate_ss.str()
          << " Hz</span></div>";
      out << "<div class=\"meta-row\"><span class=\"k\">Pulse width</span><span class=\"v\">" << pulse_ss.str()
          << " ms</span></div>";
      // review-plan 1.8 forbids a CONFIGURATION NOTE or any field serving the same
      // purpose here. What the pulse rate means belongs to each test's own documentation,
      // not to a paragraph inside a metadata card.
    }
    out << "</div>";
  }

  out << "</div></div>";

  for (const auto &camera : result.cameras) {
    // Count statuses
    int pass_count = 0, fail_count = 0, warn_count = 0, skip_count = 0;
    for (const auto &t : camera.tests) {
      switch (t.status) {
        case TestStatus::Pass:
          pass_count++;
          break;
        case TestStatus::Fail:
          fail_count++;
          break;
        case TestStatus::Warn:
          warn_count++;
          break;
        case TestStatus::Skipped:
          skip_count++;
          break;
      }
    }

    render_status_distribution(out, pass_count, fail_count, warn_count, skip_count);

    // Overview, grouped by backend (review-plan 3.1-3.8).
    //
    // Was a single flat table with a Backend column and a Summary column. The backend now
    // names its own group in a full-width band, and Summary became DETAILS: a link into the
    // test's own card, which is where the detail actually lives.
    out << "<div class=\"section\"><div class=\"section-header\"><h2>Test Results Overview</h2></div>";
    {
      std::string current_backend;
      bool table_open = false;
      for (const auto &test : camera.tests) {
        if (test.memory_backend != current_backend) {
          if (table_open) {
            out << "</tbody></table>";
          }
          out << render_backend_band(test.memory_backend, current_backend.empty());
          // 3.10: the head repeats on every print continuation page, so a reader never
          // meets a bare column of values.
          out << "<table class=\"overview\"><thead><tr><th>Test</th><th>Status</th><th>Duration</th>"
                 "<th>Details</th></tr></thead><tbody>";
          current_backend = test.memory_backend;
          table_open = true;
        }
        // 3.5: the number and the readable name. The slug stays in the anchor below.
        out << "<tr><td class=\"test-id\" data-label=\"Test\">" << html_escape(test_display_name(test.id, test.name))
            << "</td>";
        out << "<td class=\"status-cell " << card_status_class(test.status) << "\" data-label=\"Status\">"
            << card_status_text(test.status) << "</td>";
        // The shared formatter (plan 3.2), so a duration reads the same here as on the card
        // and in the web UI. Raw milliseconds stay in the JSON and Markdown artifacts.
        out << "<td class=\"duration\" data-label=\"Duration\">" << html_escape(format_duration_ms(test.duration_ms))
            << "</td>";
        out << "<td class=\"details-cell\" data-label=\"Details\"><a href=\"#"
            << html_escape(result_card_anchor(test.memory_backend, test.id)) << "\">View detailed result</a></td></tr>";
      }
      if (table_open) {
        out << "</tbody></table>";
      }
    }
    out << "</div>";

    // Detailed per-test sections
    out << "<div class=\"section detailed-results\"><div class=\"section-header\"><h2>Detailed Results</h2></div><div "
           "style=\"padding:8px 0\">";
    // The shared card shell (plan 3.1): backend band once per backend, then the cards.
    // Only the shell is shared -- the metric strips, charts, tables and prose below stay
    // test-specific.
    std::string current_backend;
    for (const auto &test : camera.tests) {
      if (test.memory_backend != current_backend) {
        out << render_backend_band(test.memory_backend, current_backend.empty());
        current_backend = test.memory_backend;
      }
      out << render_result_card_open(test);
      out << "<div class=\"test-body\">";

      // "Not measured" is a FINDING, not chart furniture: a format or resolution that was
      // enumerated but failed to stream must be reported whether or not this test draws a
      // chart. It used to live inside render_test_metrics(), so removing the unapproved
      // charts silently took the omission notices with it.
      render_chart_omissions(out, test, test.metrics, {});

      // The per-test content renderer (plan 3.1, review round 2): each approved test has
      // its OWN table with its own column names, because each measures something
      // different. This replaced a generic metric-kv-list that rendered every test
      // identically -- which matched none of the 22 approved previews.
      //
      // It also emits the Result block, the Metric definitions section and the notes,
      // because whether those appear and where is part of each test's approved
      // architecture rather than a property of the shell.
      out << render_test_content(test);

      // Warnings stay here: a warning belongs to the run, not to a test's content
      // architecture, and every test presents them the same way.
      if (!test.warnings.empty()) {
        out << "<div class=\"warnings-box\">";
        for (const auto &w : test.warnings)
          out << "⚠ " << html_escape(w) << "<br>";
        out << "</div>";
      }

      out << "</div>";
      out << render_result_card_close();
    }
    out << "</div></div>";
  }

  out << "<div class=\"footer\">Generated by v4l2-camera-diagnostic</div>";
  out << "</div></body></html>\n";

  // close() flushes; check the stream afterwards so a write that failed mid-file (a
  // full disk, a vanished mount) is reported rather than assumed complete.
  out.close();
  return static_cast<bool>(out);
}

}  // namespace

namespace {

// The three artifacts every run writes. Internal on purpose: exposing it would let a
// caller build a subset and break the guarantee plan 2.6 depends on.
//
// Order is fixed so the artifact list is stable across runs.
const std::vector<ReportFormat> &mandatory_formats() {
  static const std::vector<ReportFormat> formats = {ReportFormat::Html, ReportFormat::Json, ReportFormat::Markdown};
  return formats;
}

std::string artifact_path(const RunResult &result, const std::string &output_directory, ReportFormat format) {
  return output_directory + "/" + report_artifact_filename(naming_of(result), format);
}

}  // namespace

// Reads the current boot's kernel log. A FIXED command string: nothing from a request or
// from the run reaches a shell, and no privileged path is introduced -- journalctl reads
// the journal through group membership ("adm" / "systemd-journal") alone, where dmesg(1)
// would need CAP_SYSLOG wherever kernel.dmesg_restrict=1.
//
// Returns false when the log cannot be read at all. The caller then writes no file and
// the report links to none: an href to a file that was never written is a 404 the reader
// only discovers by clicking.
bool read_kernel_log(std::string *output) {
  FILE *pipe = popen("journalctl -k -b --no-pager 2>/dev/null", "r");
  if (pipe == nullptr) {
    return false;
  }
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    *output += buffer;
  }
  return pclose(pipe) == 0 && !output->empty();
}

// Writes the kernel log beside the other artifacts, under the run's canonical name.
// Returns false when there was nothing to write -- which is not a run failure: a machine
// without journal access still produces a complete report, just without this file.
bool write_dmesg_log(const RunResult &result, const std::string &output_directory) {
  std::string log;
  if (!read_kernel_log(&log)) {
    return false;
  }
  const std::string path = output_directory + "/" + dmesg_log_filename(naming_of(result));
  std::ofstream out(path);
  if (!out) {
    return false;
  }
  out << log;
  out.close();
  return out.good();
}

std::vector<ReportArtifact> write_reports(const RunResult &result, const std::string &output_directory) {
  // Name everything BEFORE creating anything. A run that cannot be named honestly -- a
  // triggered run whose Trigger Profile source file was never resolved -- must fail here
  // rather than leave a directory of mis-named artifacts that look correct (plan 3.5.2).
  std::string first_name;
  try {
    first_name = report_artifact_filename(naming_of(result), mandatory_formats().front());
  } catch (const ReportNamingError &error) {
    throw ReportWriteError(std::string("cannot name this run's artifacts: ") + error.what());
  }
  (void)first_name;

  if (!ensure_directory(output_directory)) {
    throw ReportWriteError("could not create the report output directory \"" + output_directory + "\"");
  }

  // The kernel log is written FIRST, because the HTML has to know whether to render a
  // link to it. It is not a mandatory artifact: a machine whose user is not in "adm"
  // still produces a complete report, just without this file and without the control.
  const bool dmesg_log_present = write_dmesg_log(result, output_directory);

  std::vector<ReportArtifact> artifacts;
  for (ReportFormat format : mandatory_formats()) {
    const std::string path = artifact_path(result, output_directory, format);
    bool written = false;
    switch (format) {
      case ReportFormat::Json:
        written = write_json(result, path);
        break;
      case ReportFormat::Markdown:
        written = write_markdown(result, path);
        break;
      case ReportFormat::Html:
        written = write_html(result, path, dmesg_log_present);
        break;
    }
    if (!written) {
      // All three are mandatory, so one failure fails the whole report. Throwing
      // rather than returning a shorter list means a caller cannot accidentally
      // advertise a file that is missing or half-written.
      throw ReportWriteError(std::string("could not write the ") + to_string(format) + " report to \"" + path + "\"");
    }
    // Appended only after the file is closed and the stream checked, so every entry
    // in the returned list names a file that really exists.
    artifacts.push_back({format, path});
  }

  return artifacts;
}

}  // namespace v4l2diag
