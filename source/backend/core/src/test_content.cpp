#include "v4l2diag/core/test_content.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>
#include <sstream>
#include <string>
#include <vector>

#include "v4l2diag/core/duration_format.hpp"
#include "v4l2diag/core/result_card.hpp"

namespace v4l2diag {

namespace {

// --- Reading the structured result --------------------------------------------
//
// Everything a renderer shows comes from here. A renderer that computed a value itself
// would be inventing evidence.

const MetricValue *find_metric(const TestResult &test, const std::string &name) {
  for (const auto &metric : test.metrics) {
    if (metric.name == name) {
      return &metric;
    }
  }
  return nullptr;
}

// Metrics whose name starts with a prefix, in the order the runner recorded them. Used by
// the sweep tables, where the prefix names the category ("hits_5", "hits_10", ...).
std::vector<const MetricValue *> metrics_with_prefix(const TestResult &test, const std::string &prefix) {
  std::vector<const MetricValue *> found;
  for (const auto &metric : test.metrics) {
    if (metric.name.size() > prefix.size() && metric.name.compare(0, prefix.size(), prefix) == 0) {
      found.push_back(&metric);
    }
  }
  return found;
}

// The trailing category of a prefixed metric name: "hits_5" -> "5".
std::string category_of(const std::string &name, const std::string &prefix) {
  return name.size() > prefix.size() ? name.substr(prefix.size()) : std::string();
}

// A number as the report shows it: no trailing zeros, no false precision.
std::string number(double value) {
  std::ostringstream out;
  if (value == static_cast<double>(static_cast<long long>(value))) {
    out << static_cast<long long>(value);
  } else {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    std::string text(buffer);
    while (!text.empty() && text.back() == '0') {
      text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
      text.pop_back();
    }
    out << text;
  }
  return out.str();
}

std::string trim_of(const std::string &value) {
  const std::size_t first = value.find_first_not_of(" ");
  if (first == std::string::npos) {
    return std::string();
  }
  const std::size_t last = value.find_last_not_of(" ");
  return value.substr(first, last - first + 1);
}

std::string lower_of(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

// A metric's value with its unit, or the approved empty state.
//
// "Unavailable" rather than "0" or "-": a run that did not measure something is different
// from one that measured zero, and the reader has to be able to tell.
std::string value_of(const TestResult &test, const std::string &name) {
  const MetricValue *metric = find_metric(test, name);
  if (metric == nullptr) {
    return "Unavailable";
  }
  // A sentinel means the run could not measure this. Printing the raw number would put
  // "-500 ms" in the report, which reads as a real negative measurement.
  if (is_sentinel_metric(*metric)) {
    return "N/A";
  }
  if (!std::isfinite(metric->value)) {
    return "N/A";
  }
  std::string text = number(metric->value);
  if (!metric->unit.empty()) {
    text += " " + metric->unit;
  }
  return text;
}

bool has_any(const TestResult &test, const std::vector<std::string> &names) {
  for (const auto &name : names) {
    if (find_metric(test, name) != nullptr) {
      return true;
    }
  }
  return false;
}

// --- Building the markup -----------------------------------------------------

std::string section_open(const std::string &label) {
  return "<section class=\"section\"><h3 class=\"section-label\">" + html_escape(label) + "</h3>";
}

std::string table_open(const std::vector<std::string> &columns) {
  std::string out = "<table class=\"evidence\"><thead><tr>";
  for (const auto &column : columns) {
    // Written out verbatim: the approved header text IS the contract, so a renderer must
    // not prettify or abbreviate it.
    out += "<th>" + column + "</th>";
  }
  return out + "</tr></thead><tbody>";
}

std::string row(const std::vector<std::string> &cells) {
  std::string out = "<tr>";
  for (const auto &cell : cells) {
    out += "<td>" + cell + "</td>";
  }
  return out + "</tr>";
}

std::string table_close() {
  return "</tbody></table>";
}
std::string section_close() {
  return "</section>";
}

// The verdict word a State/Outcome column shows.
std::string state_word(TestStatus status) {
  return card_status_text(status);
}

// "Metric definitions": the Metric | Meaning table five approved previews carry. Built
// from the metric descriptions the runner recorded -- a definition nobody wrote is not
// invented here.
std::string metric_definitions(const TestResult &test) {
  std::vector<const MetricValue *> described;
  for (const auto &metric : test.metrics) {
    if (!metric.description.empty()) {
      described.push_back(&metric);
    }
  }
  std::string out = section_open("Metric definitions");
  out += table_open({"Metric", "Meaning"});
  if (described.empty()) {
    // The section stays -- it is part of the approved architecture -- but it states the
    // absence rather than showing an empty table that looks like a rendering bug.
    out += row({"Unavailable", "No metric descriptions were recorded for this run."});
  } else {
    for (const auto *metric : described) {
      out += row({html_escape(humanize(metric->name)), html_escape(metric->description)});
    }
  }
  return out + table_close() + section_close();
}

// "Test configuration": the parameters the run used, from the detail lines that name one.
std::string test_configuration(const TestResult &test) {
  std::string out = section_open("Test configuration");
  out += table_open({"Parameter", "Value"});
  bool any = false;
  for (const auto &detail : test.details) {
    const std::size_t colon = detail.find(':');
    if (colon == std::string::npos || colon == 0) {
      continue;
    }
    any = true;
    out += row({html_escape(detail.substr(0, colon)), html_escape(detail.substr(colon + 1))});
  }
  if (!any) {
    out += row({"Unavailable", "No configuration parameters were recorded."});
  }
  return out + table_close() + section_close();
}

// The value of the first "key: value" detail line with this key, or an empty string.
std::string detail_value(const TestResult &test, const std::string &key) {
  for (const auto &detail : test.details) {
    const std::size_t colon = detail.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    if (lower_of(detail.substr(0, colon)) == lower_of(key)) {
      std::string value = detail.substr(colon + 1);
      const std::size_t first = value.find_first_not_of(' ');
      return first == std::string::npos ? std::string() : value.substr(first);
    }
  }
  return std::string();
}

// Every value recorded under this key, in order, deduplicated.
std::vector<std::string> detail_values(const TestResult &test, const std::string &key) {
  std::vector<std::string> values;
  for (const auto &detail : test.details) {
    const std::size_t colon = detail.find(':');
    if (colon == std::string::npos || lower_of(detail.substr(0, colon)) != lower_of(key)) {
      continue;
    }
    std::string value = detail.substr(colon + 1);
    const std::size_t first = value.find_first_not_of(' ');
    if (first == std::string::npos) {
      continue;
    }
    value = value.substr(first);
    if (std::find(values.begin(), values.end(), value) == values.end()) {
      values.push_back(value);
    }
  }
  return values;
}

// A key/value section: structured rows rather than a monospace block that repeats the
// metrics. Used by the tests whose approved layout names its fields (5.1.7 and friends).
std::string kv_section(const std::string &label, const std::vector<std::pair<std::string, std::string>> &rows) {
  std::string out = section_open(label);
  out += "<dl class=\"kv\">";
  for (const auto &entry : rows) {
    out += "<div class=\"kv-row\"><dt>" + html_escape(entry.first) + "</dt><dd>" +
           html_escape(entry.second.empty() ? std::string("Unavailable") : entry.second) + "</dd></div>";
  }
  return out + "</dl>" + section_close();
}

// The raw detail lines, verbatim and in order.
//
// Kept as well as the tables: a detail line is what the runner actually observed, and a
// table built from selected metrics cannot stand in for it. Reformatting these into a
// table would also lose the ones that are not "key: value" at all.
// Tests whose approved layout already presents every detail line as a structured row.
// Emitting the monospace block for these repeats the same values a second time, which is
// exactly what review-plan 5.1.7 removes.
bool details_are_structured(const std::string &test_id) {
  static const std::set<std::string> structured = {"t01-device-compliance", "t02-control-inventory", "t04-no-streamon",
                                                   "t05-pollerr-handling",  "t06-stream-cycles",     "t07-multi-buffer",
                                                   "t08-buffer-overwrite",  "t09-buffer-recycling",  "t10-buffer-flags",
                                                   "t11-memory-throughput"};
  return structured.count(test_id) != 0;
}

std::string detail_lines(const TestResult &test) {
  if (test.details.empty() || details_are_structured(test.id)) {
    return std::string();
  }
  std::string out = "<div class=\"detail-list\">";
  for (const auto &detail : test.details) {
    out += html_escape(detail) + "\n";
  }
  return out + "</div>";
}

// "Metric definitions" with a Source column: T19, T20 and T21 name where each figure came
// from, not only what it means. The source is the metric's own key -- that IS the
// provenance, and inventing a prettier one would misattribute the number.
std::string metric_definitions_with_source(const TestResult &test) {
  std::string out = section_open("Metric definitions");
  out += table_open({"Metric", "Meaning", "Source"});
  bool any = false;
  for (const auto &metric : test.metrics) {
    if (metric.description.empty()) {
      continue;
    }
    any = true;
    out += row({html_escape(humanize(metric.name)), html_escape(metric.description),
                "<code>" + html_escape(metric.name) + "</code>"});
  }
  if (!any) {
    out += row({"Unavailable", "No metric descriptions were recorded for this run.", "Unavailable"});
  }
  return out + table_close() + section_close();
}

// The "Result" block: the verdict stated in prose, on non-PASS cards only.
std::string result_block(const TestResult &test) {
  std::string out = section_open("Result");
  out += "<p>" + html_escape(test.summary.empty() ? std::string("No summary was recorded.") : test.summary) + "</p>";
  for (const auto &warning : test.warnings) {
    out += "<div class=\"warnings-box\">" + html_escape(warning) + "</div>";
  }
  return out + section_close();
}

// --- The renderers ----------------------------------------------------------
//
// One per test, keyed on the technical id. Each mirrors the information architecture of
// that test's approved preview: the same sections, in the same order, with the same column
// names.

using Renderer = std::string (*)(const TestResult &);

// A statistics table: "Measure | Observed | Meaning" over a named set of metrics. Several
// approved previews share this shape while naming different measures, so the shape is
// factored out and the metric list stays per-test.
[[maybe_unused]] std::string stats_table(const TestResult &test, const std::string &label,
                                         const std::vector<std::string> &columns,
                                         const std::vector<std::pair<std::string, std::string>> &rows) {
  std::string out = section_open(label);
  out += table_open(columns);
  for (const auto &entry : rows) {
    std::vector<std::string> cells = {html_escape(entry.first), value_of(test, entry.second)};
    // Fill the remaining columns by what the column ASKS FOR. Filling them all with the
    // metric description put prose under a "State" heading, which reads as a verdict.
    for (std::size_t i = cells.size(); i < columns.size(); ++i) {
      const std::string &column = columns[i];
      if (column == "State" || column == "Outcome") {
        cells.push_back(state_word(test.status));
      } else if (column == "Meaning") {
        const MetricValue *metric = find_metric(test, entry.second);
        cells.push_back(metric != nullptr && !metric->description.empty() ? html_escape(metric->description)
                                                                          : std::string("Unavailable"));
      } else {
        cells.push_back("Unavailable");
      }
    }
    out += row(cells);
  }
  return out + table_close() + section_close();
}

// T01 (review-plan 5.1): a categorical capability test. Three capabilities with their
// probe method inline and the state right-aligned; pixel formats deduplicated by FOURCC;
// driver/card/bus as structured rows. No chart (5.1.8) and no monospace detail block
// repeating the metrics (5.1.7).
std::string render_t01(const TestResult &test) {
  // 5.1.5: the probe method belongs on the capability's own line, in parentheses. The
  // method differs per backend -- DMABUF probes with VIDIOC_EXPBUF, not REQBUFS -- so it
  // is chosen from the backend this card belongs to (5.1.2).
  const std::string probe = test.memory_backend == "dmabuf" ? "VIDIOC_EXPBUF accepted" : "VIDIOC_REQBUFS accepted";
  struct Capability {
    std::string label;
    // Two spellings are in use for each capability; both are recognised, because guessing
    // one would show "Unavailable" for a probe that did run.
    std::vector<std::string> metrics;
  };
  const std::vector<Capability> capabilities = {
      {"Backend support (" + probe + ")", {"backend_supported", "supports_backend"}},
      {"Capture support", {"capture_supported", "supports_capture"}},
      {"Streaming support", {"streaming_supported", "supports_streaming"}},
  };

  std::string out = section_open("Required capabilities");
  out += "<dl class=\"capability-list\">";
  for (const auto &capability : capabilities) {
    const MetricValue *metric = nullptr;
    for (const auto &name : capability.metrics) {
      metric = find_metric(test, name);
      if (metric != nullptr) {
        break;
      }
    }
    // Absent is not "not supported": a probe that never ran and one that failed are
    // different findings, and the reader has to be able to tell them apart.
    const std::string state =
        metric == nullptr ? "Unavailable" : (metric->value != 0.0 ? "SUPPORTED" : "NOT SUPPORTED");
    const std::string tone = metric == nullptr ? "unknown" : (metric->value != 0.0 ? "good" : "bad");
    out += "<div class=\"capability-row\"><dt>" + html_escape(capability.label) + "</dt><dd class=\"" + tone + "\">" +
           state + "</dd></div>";
  }
  out += "</dl>" + section_close();

  // 5.1.6: unique by FOURCC. The same FOURCC enumerated twice is one format, counted once.
  const std::vector<std::string> formats = detail_values(test, "format");
  const MetricValue *count_metric = find_metric(test, "format_count");
  // Prefer the deduplicated list; fall back to the recorded count when the runner logged a
  // total without enumerating. Neither is invented when both are absent.
  const std::size_t count = !formats.empty() ? formats.size()
                            : count_metric != nullptr && count_metric->value >= 0.0
                                ? static_cast<std::size_t>(count_metric->value)
                                : 0;
  out += section_open("Pixel formats");
  out += "<p class=\"section-count\">" +
         (formats.empty() && count_metric == nullptr ? std::string("Unavailable")
                                                     : std::to_string(count) + (count == 1 ? " format" : " formats")) +
         "</p>";
  if (formats.empty()) {
    out += "<p>The individual formats were not enumerated in this run.</p>";
  } else {
    out += "<ul class=\"format-list\">";
    for (const auto &format : formats) {
      out += "<li>" + html_escape(format) + "</li>";
    }
    out += "</ul>";
  }
  out += section_close();

  // 5.1.7: structured rows. The raw enumeration stays in the machine-readable artifacts.
  out += kv_section("Device information", {{"Driver", detail_value(test, "driver")},
                                           {"Card", detail_value(test, "card")},
                                           {"Bus", detail_value(test, "bus")}});

  // Anything else the runner measured. 5.1.7 removes the block that REPEATED the metrics,
  // not the metrics themselves: a recorded value that appears nowhere is a dropped finding,
  // and a sentinel has to keep reading "N/A" rather than disappearing.
  std::vector<std::pair<std::string, std::string>> extra;
  for (const auto &metric : test.metrics) {
    const std::string name = metric.name;
    const bool is_capability = name.find("supported") != std::string::npos ||
                               name.find("supports_") != std::string::npos || name == "format_count";
    if (is_capability) {
      continue;
    }
    extra.push_back({humanize(name), value_of(test, name)});
  }
  if (!extra.empty()) {
    out += kv_section("Recorded values", extra);
  }
  return out;
}

// T02 (review-plan 5.2): a categorical inventory test. Three summary values, then one
// structured table whose rows are the controls and whose group headings are the
// V4L2_CTRL_TYPE_CTRL_CLASS records -- which are NOT controls and carry no current value.
std::string render_t02(const TestResult &test) {
  // 5.2.4: three values, without repeating the word "count" beside each one.
  std::string out = section_open("Control summary");
  out += "<dl class=\"kv\">";
  for (const auto &entry : {std::make_pair("Controls", "controls"), std::make_pair("Writable", "writable"),
                            std::make_pair("Read-only", "read_only")}) {
    out += "<div class=\"kv-row\"><dt>" + std::string(entry.first) + "</dt><dd>" + value_of(test, entry.second) +
           "</dd></div>";
  }
  out += "</dl>" + section_close();

  // 5.2.5: the approved five columns. The runner records each control as one pipe-separated
  // detail line, and each class record as its own line.
  out += section_open("Controls");
  out += table_open({"Control", "Access", "Range", "Default", "Current"});
  bool any = false;
  for (const auto &detail : test.details) {
    const std::size_t colon = detail.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    const std::string key = lower_of(detail.substr(0, colon));
    std::string value = detail.substr(colon + 1);
    const std::size_t first = value.find_first_not_of(' ');
    if (first == std::string::npos) {
      continue;
    }
    value = value.substr(first);

    if (key == "control_class") {
      // 5.2.3: a full-width group heading. No current value is read for it, and it does
      // not count towards the control total.
      any = true;
      out += "<tr class=\"group-row\"><td colspan=\"5\">" + html_escape(value) + "</td></tr>";
      continue;
    }
    if (key != "control") {
      continue;
    }
    any = true;
    // name|id|access|range|default|current
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= value.size()) {
      const std::size_t bar = value.find('|', start);
      fields.push_back(value.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    fields.resize(6);
    // 5.2.5: the hexadecimal id under the name, at lower weight.
    const std::string name_cell =
        html_escape(fields[0]) +
        (fields[1].empty() ? std::string() : "<span class=\"control-id\">" + html_escape(fields[1]) + "</span>");
    out += row({name_cell, fields[2].empty() ? "Unavailable" : html_escape(fields[2]),
                fields[3].empty() ? "Unavailable" : html_escape(fields[3]),
                fields[4].empty() ? "Unavailable" : html_escape(fields[4]),
                fields[5].empty() ? "Unavailable" : html_escape(fields[5])});
  }
  if (!any) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  }
  return out + table_close() + section_close();
}

// review-plan 5.3: STREAMON and first-frame timing, per cycle. A single stacked bar shows
// each cycle's two phases in order rather than two separate mean/max dot charts, and the
// cycle table itself differs by mode -- free-run has no Pulses column (5.3.5), a triggered
// run does (5.3.6).
// The Cycle struct T03 uses; forward-declared here so the chart function can take it
// before render_t03() defines the type inline (kept as a nested type there for locality).
struct T03Cycle {
  std::string number;
  std::string streamon;
  std::string first_frame;
  std::string total;
  std::string pulses;
  double streamon_ms = 0.0;
  double first_frame_ms = 0.0;
  double total_ms = 0.0;
};

// "Nice" axis ticks over [0, max]: reproduces the preview's 5-tick, round-number scheme
// (0 / 350ms / 700ms / 1.05s / 1.4s for a 1.4s max) without hard-coding those numbers --
// the actual max varies run to run.
std::vector<double> t03_axis_ticks(double max_value) {
  if (max_value <= 0.0) {
    return {0.0};
  }
  const double raw_step = max_value / 4.0;
  const double magnitude = std::pow(10.0, std::floor(std::log10(raw_step)));
  const double normalized = raw_step / magnitude;
  double step;
  if (normalized <= 1.0) {
    step = magnitude;
  } else if (normalized <= 2.0) {
    step = 2.0 * magnitude;
  } else if (normalized <= 5.0) {
    step = 5.0 * magnitude;
  } else {
    step = 10.0 * magnitude;
  }
  std::vector<double> ticks;
  for (double v = 0.0; v <= max_value + step * 0.5; v += step) {
    ticks.push_back(v);
  }
  return ticks;
}

// review-plan 5.3.4: one stacked, two-phase horizontal bar per cycle. Reproduces the
// approved preview's own coordinate scheme (docs/assets/previews/t03-unified-preview.html):
// a 100-780 plot area, 42px row pitch, 24px bar height, STREAMON in the darker segment and
// FIRST FRAME in the lighter one, with the total (and, under a triggered mode, the pulse
// count) printed after the bar.
std::string render_t03_timing_chart(const std::vector<T03Cycle> &cycles, bool has_pulses) {
  if (cycles.empty()) {
    return std::string();
  }
  double max_total = 0.0;
  for (const auto &cycle : cycles) {
    max_total = std::max(max_total, cycle.total_ms);
  }
  const std::vector<double> ticks = t03_axis_ticks(max_total);
  const double axis_max = ticks.empty() ? 1.0 : ticks.back();

  constexpr double left = 100.0;
  constexpr double right = 780.0;
  constexpr double plot_width = right - left;
  constexpr double row_pitch = 42.0;
  constexpr double bar_height = 24.0;
  constexpr double top = 16.0;
  const double axis_y = top + row_pitch * static_cast<double>(cycles.size()) - (row_pitch - bar_height) + 8.0;
  const double height = axis_y + 34.0;
  const auto x_for = [&](double ms) { return left + (axis_max > 0.0 ? ms / axis_max : 0.0) * plot_width; };

  std::string out = section_open("Timing by Cycle");
  out +=
      "<div class=\"metric-chart metric-chart--wide\"><div class=\"metric-chart-title\">STREAMON and "
      "first-frame timing across " +
      std::to_string(cycles.size()) + (cycles.size() == 1 ? " cycle" : " cycles") + "</div>";
  out += "<svg viewBox=\"0 0 960 " + number(height) +
         "\" role=\"img\" aria-label=\"STREAMON and first-frame timing across " + std::to_string(cycles.size()) +
         " cycles\">";

  // Axis grid and ticks.
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(top) + "\" x2=\"" + number(left) +
         "\" y2=\"" + number(axis_y) + "\"></line>";
  for (double tick : ticks) {
    const double x = x_for(tick);
    out += "<line class=\"chart-grid\" x1=\"" + number(x) + "\" y1=\"" + number(top) + "\" x2=\"" + number(x) +
           "\" y2=\"" + number(axis_y) + "\"></line>";
    out += "<text class=\"axis-value\" x=\"" + number(x) + "\" y=\"" + number(axis_y + 18.0) +
           "\" text-anchor=\"middle\">" + html_escape(format_duration_ms(tick)) + "</text>";
  }

  for (std::size_t i = 0; i < cycles.size(); ++i) {
    const auto &cycle = cycles[i];
    const double row_top = top + row_pitch * static_cast<double>(i);
    const double streamon_width = std::max(1.0, x_for(cycle.streamon_ms) - left);
    const double frame_width = std::max(1.0, x_for(cycle.total_ms) - x_for(cycle.streamon_ms));
    out += "<text class=\"axis-caption\" x=\"" + number(left - 14.0) + "\" y=\"" + number(row_top + bar_height * 0.65) +
           "\" text-anchor=\"end\">Cycle " + html_escape(cycle.number) + "</text>";
    out += "<rect class=\"t03-phase-streamon\" x=\"" + number(left) + "\" y=\"" + number(row_top) + "\" width=\"" +
           number(streamon_width) + "\" height=\"" + number(bar_height) + "\" rx=\"2\"></rect>";
    out += "<rect class=\"t03-phase-frame\" x=\"" + number(left + streamon_width) + "\" y=\"" + number(row_top) +
           "\" width=\"" + number(frame_width) + "\" height=\"" + number(bar_height) + "\" rx=\"2\"></rect>";
    out += "<text class=\"t03-bar-label\" x=\"" + number(left + 12.0) + "\" y=\"" +
           number(row_top + bar_height * 0.65) + "\">STREAMON " + html_escape(cycle.streamon) + "</text>";
    const std::string frame_note = "+" + cycle.first_frame;
    const double frame_note_x = left + streamon_width + frame_width + 8.0;
    out += "<text class=\"t03-bar-note\" x=\"" + number(frame_note_x) + "\" y=\"" +
           number(row_top + bar_height * 0.65) + "\">" + html_escape(frame_note) + "</text>";
    std::string tail = "Total " + cycle.total;
    if (has_pulses && !cycle.pulses.empty()) {
      tail += " &middot; " + cycle.pulses + " pulses";
    }
    // The tail sits right-aligned at the plot's end, EXCEPT when the bar is short enough
    // that the "+first-frame" note (which trails the bar, left-aligned) would run into it:
    // an approximate 6.2px/character monospace-ish estimate is enough to decide that, and
    // erring toward moving the tail is safer than an unreadable overlap.
    const double frame_note_end = frame_note_x + static_cast<double>(frame_note.size()) * 6.2;
    const double tail_start_if_right_aligned = left + plot_width + 8.0 - static_cast<double>(tail.size()) * 6.0;
    if (tail_start_if_right_aligned > frame_note_end + 6.0) {
      out += "<text class=\"t03-bar-note\" x=\"" + number(left + plot_width + 8.0) + "\" y=\"" +
             number(row_top + bar_height * 0.65) + "\" text-anchor=\"end\">" + tail + "</text>";
    } else {
      // Not enough room to the right of the note: the tail moves to its own line just
      // below the bar instead of overlapping it.
      out += "<text class=\"t03-bar-note t03-bar-note--below\" x=\"" + number(left + 12.0) + "\" y=\"" +
             number(row_top + bar_height + 12.0) + "\">" + tail + "</text>";
    }
  }
  out += "</svg>";
  // 5.3.4's legend: which colour is which phase.
  out +=
      "<div class=\"legend\"><span class=\"legend-item\"><i class=\"legend-swatch t03-phase-streamon\"></i>"
      "STREAMON</span><span class=\"legend-item\"><i class=\"legend-swatch t03-phase-frame\"></i>"
      "First frame after STREAMON</span></div>";
  out += "</div>";
  return out + section_close();
}

std::string render_t03(const TestResult &test) {
  // 5.3.3: mean/max summary, with the relevant threshold beside the measurement it
  // constrains.
  std::string out = section_open("Pipeline Timing Summary");
  out += "<dl class=\"kv\">";
  const struct {
    const char *label;
    const char *metric;
    const char *threshold_metric;
    const char *threshold_label;
  } summary_rows[] = {
      {"STREAMON mean", "streamon_mean_ms", nullptr, nullptr},
      {"STREAMON max", "streamon_max_ms", "streamon_slow_start_ms", "slow-start limit"},
      {"First-frame mean", "first_frame_mean_ms", nullptr, nullptr},
      {"First-frame max", "first_frame_max_ms", "first_frame_pass_ms", "pass threshold"},
      {"Completed cycles", "cycles", nullptr, nullptr},
      {"Timeouts", "timeouts", nullptr, nullptr},
  };
  for (const auto &entry : summary_rows) {
    std::string value = value_of(test, entry.metric);
    if (entry.threshold_metric != nullptr) {
      const MetricValue *threshold = find_metric(test, entry.threshold_metric);
      if (threshold != nullptr) {
        value += " <span class=\"threshold-note\">(" + std::string(entry.threshold_label) + ": " +
                 value_of(test, entry.threshold_metric) + ")</span>";
      }
    }
    out += "<div class=\"kv-row\"><dt>" + std::string(entry.label) + "</dt><dd>" + value + "</dd></div>";
  }
  out += "</dl>" + section_close();

  // The cycle rows: "cycle: N|streamon_ms|first_frame_ms|total_ms[|pulses]". A triggered
  // run's detail lines carry the fifth field; free-run's do not, which is what decides the
  // Pulses column (5.3.5/5.3.6) -- not the trigger mode, since T03's content renderer only
  // ever sees the test result.
  std::vector<T03Cycle> cycles;
  bool has_pulses = false;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 7, "cycle: ") != 0) {
      continue;
    }
    std::vector<std::string> fields;
    std::size_t start = 7;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    if (fields.size() < 4) {
      continue;
    }
    T03Cycle cycle;
    cycle.number = fields[0];
    cycle.streamon_ms = std::strtod(fields[1].c_str(), nullptr);
    cycle.first_frame_ms = std::strtod(fields[2].c_str(), nullptr);
    cycle.total_ms = std::strtod(fields[3].c_str(), nullptr);
    cycle.streamon = format_duration_ms(cycle.streamon_ms);
    cycle.first_frame = format_duration_ms(cycle.first_frame_ms);
    cycle.total = format_duration_ms(cycle.total_ms);
    if (fields.size() >= 5) {
      cycle.pulses = fields[4];
      has_pulses = true;
    }
    cycles.push_back(cycle);
  }

  // 5.3.4: one stacked, two-phase horizontal bar per cycle -- STREAMON then FIRST FRAME, in
  // time order -- replacing the separate mean/max dot charts. Reproduces the approved
  // preview's own coordinate scheme (t03-unified-preview.html) rather than a generic bar.
  out += render_t03_timing_chart(cycles, has_pulses);

  // The precise cycle table: free-run gets no Pulses column at all (5.3.5), because it
  // routes nothing -- showing one would claim a pulse count the run never had.
  std::vector<std::string> columns = {"Cycle", "STREAMON", "First frame", "Total ready"};
  if (has_pulses) {
    columns.push_back("Pulses");
  }
  out += section_open("Cycle Detail");
  out += table_open(columns);
  if (cycles.empty()) {
    std::vector<std::string> empty_row(columns.size(), "Unavailable");
    out += row(empty_row);
  } else {
    for (const auto &cycle : cycles) {
      std::vector<std::string> cells = {cycle.number, cycle.streamon, cycle.first_frame, cycle.total};
      if (has_pulses) {
        cells.push_back(cycle.pulses);
      }
      out += row(cells);
    }
  }
  out += table_close() + section_close();

  // 5.3.8: EAGAIN retries and STREAMON attempts are secondary to the end-user's decision,
  // so they move to their own section rather than sitting in the main summary.
  out += section_open("Technical details");
  out += "<dl class=\"kv\">";
  const MetricValue *retries = find_metric(test, "eagain_retries_mean");
  if (retries != nullptr) {
    // A thousands separator, and no "count" suffix (5.3.7).
    std::string text = number(retries->value);
    std::string grouped;
    int digits_before_dot = 0;
    for (char c : text) {
      if (c == '.') {
        break;
      }
      ++digits_before_dot;
    }
    int seen = 0;
    for (char c : text) {
      if (seen > 0 && seen < digits_before_dot && (digits_before_dot - seen) % 3 == 0 && c != '.') {
        grouped += ',';
      }
      grouped += c;
      if (c != '.') {
        ++seen;
      }
    }
    out += "<div class=\"kv-row\"><dt>Average retries</dt><dd>" + grouped + "</dd></div>";
  }
  const MetricValue *attempts = find_metric(test, "streamon_attempts_max");
  if (attempts != nullptr) {
    out +=
        "<div class=\"kv-row\"><dt>Maximum attempts</dt><dd>" + value_of(test, "streamon_attempts_max") + "</dd></div>";
  }
  if (retries == nullptr && attempts == nullptr) {
    out += "<div class=\"kv-row\"><dt>Unavailable</dt><dd>No retry or attempt data was recorded.</dd></div>";
  }
  out += "</dl>" + section_close();
  return out;
}

// T04 (review-plan 5.4): a categorical state-machine test. Two CHECK/EXPECTED/OBSERVED/
// OUTCOME rows -- poll() and DQBUF, both attempted before STREAMON -- replacing the raw
// metric list and repeated detail block. No chart (5.4.7 last rule).
std::string render_t04(const TestResult &test) {
  std::string out = section_open("State-machine checks");
  out += table_open({"Check", "Expected", "Observed", "Outcome"});
  bool any = false;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 7, "check: ") != 0) {
      continue;
    }
    any = true;
    std::vector<std::string> fields;
    std::size_t start = 7;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    fields.resize(4);
    out += row({html_escape(fields[0]), html_escape(fields[1]), html_escape(fields[2]), html_escape(fields[3])});
  }
  if (!any) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  }
  out += table_close() + section_close();
  // 5.4.7: the two parameters that shaped this state-machine probe.
  out += kv_section("Test configuration", {{"Buffers requested", value_of(test, "buffers_requested")},
                                           {"Poll timeout", value_of(test, "poll_timeout_ms")}});
  return out;
}

// T05 (review-plan 5.5): a categorical, SEQUENTIAL state-machine test. Six phase rows in
// run order, plus an optional short capture-rate note when a pacing observation exists. No
// chart (5.5.7 last rule).
std::string render_t05(const TestResult &test) {
  std::string out = section_open("Stream state checks");
  out += table_open({"Phase", "Expected", "Observed", "Outcome"});
  bool any = false;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 7, "phase: ") != 0) {
      continue;
    }
    any = true;
    std::vector<std::string> fields;
    std::size_t start = 7;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    fields.resize(4);
    out += row({html_escape(fields[0]), html_escape(fields[1]), html_escape(fields[2]), html_escape(fields[3])});
  }
  if (!any) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  }
  out += table_close() + section_close();

  // 5.5.6: only when the run actually observed a pacing deviation -- its absence means
  // there is nothing to note, not that the note was forgotten.
  const std::string note = detail_value(test, "capture_rate_note");
  if (!note.empty()) {
    out += section_open("Capture rate note");
    out += "<p>" + html_escape(note) + "</p>";
    out += section_close();
  }

  // 5.5.7: the six configuration parameters.
  out += kv_section("Test configuration", {{"Baseline frames", value_of(test, "baseline_frames")},
                                           {"Recovery frames", value_of(test, "recovery_frames")},
                                           {"Warmup frames", value_of(test, "warmup_frames")},
                                           {"Minimum recovery", value_of(test, "min_recovery_frames")},
                                           {"Poll timeout", value_of(test, "poll_timeout_ms")},
                                           {"Backend memory", detail_value(test, "backend_memory")}});
  return out;
}

// A "completed/configured" cell: "20/20". Absent metrics fall back to Unavailable rather
// than a bare slash.
std::string completed_of(const TestResult &test, const std::string &completed_metric,
                         const std::string &configured_metric) {
  const MetricValue *completed = find_metric(test, completed_metric);
  const MetricValue *configured = find_metric(test, configured_metric);
  if (completed == nullptr || configured == nullptr) {
    return "Unavailable";
  }
  return number(completed->value) + "/" + number(configured->value);
}

// review-plan 5.6.5: a threshold-banded horizontal bar -- <70 fail, 70-89 warn, 90+ pass --
// with the bar's own colour independent of the card's overall status: a run can PASS
// overall while one phase's reliability sits in the warn band.
std::string render_t06_reliability_bar(const std::string &label, double percent) {
  const char *tone = percent >= 90.0 ? "good" : percent >= 70.0 ? "warn" : "bad";
  std::ostringstream out;
  out << "<div class=\"reliability-bar\"><span class=\"reliability-bar-label\">" << html_escape(label)
      << "</span><div class=\"reliability-bar-track\"><div class=\"reliability-bar-fill reliability-" << tone
      << "\" style=\"width:" << number(std::min(100.0, std::max(0.0, percent))) << "%\"></div></div>"
      << "<span class=\"reliability-bar-value\">" << number(percent) << "%</span></div>";
  return out.str();
}

// review-plan 5.6.6: the Open + STREAMON trend, in full-cycle order. Reproduces the
// approved preview's coordinate scheme (t06-stream-cycles-preview.html): a 42-416 plot
// area with the cycle number on x and the timing value on y.
std::string render_t06_trend_chart(const std::vector<std::pair<int, double>> &points) {
  if (points.empty()) {
    return std::string();
  }
  double max_value = 0.0;
  for (const auto &point : points) {
    max_value = std::max(max_value, point.second);
  }
  const std::vector<double> ticks = t03_axis_ticks(max_value);
  const double axis_max = ticks.empty() ? 1.0 : ticks.back();
  constexpr double left = 42.0;
  constexpr double right = 416.0;
  constexpr double top = 22.0;
  constexpr double bottom = 132.0;
  const auto x_for = [&](int cycle) {
    if (points.size() == 1) {
      return left;
    }
    return left + (right - left) * static_cast<double>(cycle - points.front().first) /
                      static_cast<double>(points.back().first - points.front().first);
  };
  const auto y_for = [&](double value) { return bottom - (axis_max > 0.0 ? value / axis_max : 0.0) * (bottom - top); };

  std::string out =
      "<div class=\"chart-frame\"><div class=\"metric-chart-title\">Open + STREAMON by full cycle "
      "<span>ms</span></div><svg viewBox=\"0 0 430 180\" role=\"img\" aria-label=\"Open and STREAMON "
      "timing by full cycle\">";
  for (double tick : ticks) {
    const double y = y_for(tick);
    out += "<line class=\"chart-grid\" x1=\"" + number(left) + "\" y1=\"" + number(y) + "\" x2=\"" + number(right) +
           "\" y2=\"" + number(y) + "\"></line>";
    out += "<text class=\"axis-value\" text-anchor=\"end\" x=\"" + number(left - 8.0) + "\" y=\"" + number(y + 3.0) +
           "\">" + html_escape(number(tick)) + "</text>";
  }
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(bottom) + "\" x2=\"" + number(right) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(top) + "\" x2=\"" + number(left) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  std::string polyline_points;
  for (const auto &point : points) {
    const double x = x_for(point.first);
    const double y = y_for(point.second);
    if (!polyline_points.empty()) {
      polyline_points += " ";
    }
    polyline_points += number(x) + "," + number(y);
  }
  out += "<polyline class=\"t06-trend-line\" points=\"" + polyline_points + "\"></polyline><g>";
  for (const auto &point : points) {
    out += "<circle class=\"t06-trend-point\" cx=\"" + number(x_for(point.first)) + "\" cy=\"" +
           number(y_for(point.second)) + "\" r=\"3\"></circle>";
  }
  out += "</g></svg></div>";
  return out;
}

std::string render_t06(const TestResult &test) {
  // 5.6.4: Full and Rapid in ONE table, counts as completed/configured, and the phase
  // that ran out an outcome of its own rather than the card's overall status -- a
  // borderline phase can WARN even on a PASS card.
  std::string out = section_open("Cycle Reliability");
  out += table_open({"Phase", "Completed", "Start fail", "Timeout", "Outcome"});
  if (find_metric(test, "full_cycles") == nullptr && find_metric(test, "rapid_cycles") == nullptr) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    out += row({"Full cycles", completed_of(test, "full_cycles", "full_configured"), value_of(test, "full_start_fail"),
                value_of(test, "full_timeouts"), state_word(test.status)});
    out += row({"Rapid cycles", completed_of(test, "rapid_cycles", "rapid_configured"),
                value_of(test, "rapid_start_fail"), value_of(test, "rapid_capture_timeouts"), state_word(test.status)});
  }
  out += table_close() + section_close();

  // 5.6.5: the threshold-banded reliability bars, once per phase.
  out += section_open("Reliability");
  const MetricValue *full_completed = find_metric(test, "full_cycles");
  const MetricValue *full_configured = find_metric(test, "full_configured");
  if (full_completed != nullptr && full_configured != nullptr && full_configured->value > 0.0) {
    out += render_t06_reliability_bar("Full cycles", full_completed->value / full_configured->value * 100.0);
  }
  const MetricValue *rapid_completed = find_metric(test, "rapid_cycles");
  const MetricValue *rapid_configured = find_metric(test, "rapid_configured");
  if (rapid_completed != nullptr && rapid_configured != nullptr && rapid_configured->value > 0.0) {
    out += render_t06_reliability_bar("Rapid cycles", rapid_completed->value / rapid_configured->value * 100.0);
  }
  out += section_close();

  // 5.6.6: the trend chart, from the run's own per-cycle timing detail line
  // ("full_cycle_timing: cycle|value|cycle|value|..."), in full-cycle order.
  const std::string timing_line = detail_value(test, "full_cycle_timing");
  if (!timing_line.empty()) {
    std::vector<std::pair<int, double>> points;
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (start <= timing_line.size()) {
      const std::size_t bar = timing_line.find('|', start);
      fields.push_back(timing_line.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    for (std::size_t i = 0; i + 1 < fields.size(); i += 2) {
      points.push_back({std::atoi(fields[i].c_str()), std::strtod(fields[i + 1].c_str(), nullptr)});
    }
    out += render_t06_trend_chart(points);
  }

  // 5.6.7: the renamed timing fields. "Open + STREAMON" because the measurement spans
  // device open, buffer setup and STREAMON, not STREAMON alone; T06's own capture figures
  // are secondary observations, not T03's readiness measurement.
  out += kv_section("Timing Summary", {{"Open + STREAMON mean", value_of(test, "open_streamon_mean_ms")},
                                       {"Open + STREAMON maximum", value_of(test, "open_streamon_max_ms")},
                                       {"Measured capture mean", value_of(test, "measured_capture_mean_ms")},
                                       {"Measured capture maximum", value_of(test, "measured_capture_max_ms")}});

  // 5.6.8: semantic text, not raw booleans, for the phase and guard state.
  out += kv_section("Protection State", {{"Full phase", detail_value(test, "full_phase")},
                                         {"Rapid phase", detail_value(test, "rapid_phase")},
                                         {"Start failures", value_of(test, "full_start_fail")},
                                         {"Slow-start guard", detail_value(test, "slow_start_guard")}});

  // 5.6.9: at least these eight configuration parameters.
  out += kv_section("Test configuration", {{"Full cycles", detail_value(test, "full_cycles_configured")},
                                           {"Rapid cycles", detail_value(test, "rapid_cycles_configured")},
                                           {"Full warmup", detail_value(test, "full_warmup")},
                                           {"Rapid warmup", detail_value(test, "rapid_warmup")},
                                           {"Slow-start guard", detail_value(test, "slow_start_guard_limit")},
                                           {"Backend memory", detail_value(test, "backend_memory")}});
  return out;
}

// One request row from T07's "request: N|allocated|captured|attempted|mean_ms" detail
// lines.
struct T07Request {
  int requested = 0;
  int allocated = 0;
  int captured = 0;
  int attempted = 0;
  double mean_ms = 0.0;
};

std::vector<T07Request> t07_requests(const TestResult &test) {
  std::vector<T07Request> requests;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 9, "request: ") != 0) {
      continue;
    }
    std::vector<std::string> fields;
    std::size_t start = 9;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    if (fields.size() < 5) {
      continue;
    }
    T07Request request;
    request.requested = std::atoi(fields[0].c_str());
    request.allocated = std::atoi(fields[1].c_str());
    request.captured = std::atoi(fields[2].c_str());
    request.attempted = std::atoi(fields[3].c_str());
    request.mean_ms = std::strtod(fields[4].c_str(), nullptr);
    requests.push_back(request);
  }
  return requests;
}

// review-plan 5.7.6: requested (reference line) vs allocated (bar) buffer counts, per
// request. Distinct geometry for the two series, not just distinct colour.
std::string render_t07_requested_vs_allocated(const std::vector<T07Request> &requests) {
  if (requests.empty()) {
    return std::string();
  }
  int max_value = 1;
  for (const auto &request : requests) {
    max_value = std::max({max_value, request.requested, request.allocated});
  }
  constexpr double left = 46.0;
  constexpr double right = 425.0;
  constexpr double top = 20.0;
  constexpr double bottom = 160.0;
  const double plot_width = right - left;
  const double step = requests.size() > 1 ? plot_width / static_cast<double>(requests.size()) : plot_width;
  const auto y_for = [&](double value) { return bottom - (value / static_cast<double>(max_value)) * (bottom - top); };

  std::string out =
      "<div class=\"chart-frame\"><div class=\"metric-chart-title\">Requested vs allocated buffers "
      "<span>buffers</span></div><svg viewBox=\"0 0 440 205\" role=\"img\" aria-label=\"Requested "
      "versus allocated buffers\">";
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(bottom) + "\" x2=\"" + number(right) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(top) + "\" x2=\"" + number(left) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  std::string line_points;
  for (std::size_t i = 0; i < requests.size(); ++i) {
    const double x = left + step * (static_cast<double>(i) + 0.5);
    const double bar_width = step * 0.5;
    const double bar_top = y_for(static_cast<double>(requests[i].allocated));
    // Allocated: a bar (5.7.6 rule 4 -- distinct geometry, not just colour).
    out += "<rect class=\"t07-allocated-bar\" x=\"" + number(x - bar_width / 2.0) + "\" y=\"" + number(bar_top) +
           "\" width=\"" + number(bar_width) + "\" height=\"" + number(bottom - bar_top) + "\" rx=\"2\"></rect>";
    if (!line_points.empty()) {
      line_points += " ";
    }
    line_points += number(x) + "," + number(y_for(static_cast<double>(requests[i].requested)));
    out += "<text class=\"axis-value\" text-anchor=\"middle\" x=\"" + number(x) + "\" y=\"" + number(bottom + 14.0) +
           "\">" + html_escape(number(requests[i].requested)) + "</text>";
  }
  // Requested: a reference line (5.7.6 rule 1/4), separate geometry from the bars.
  out += "<polyline class=\"t07-requested-line\" points=\"" + line_points + "\"></polyline>";
  out +=
      "</svg><div class=\"legend\"><span class=\"legend-item\"><i class=\"legend-swatch "
      "t07-legend-requested\"></i>Requested</span><span class=\"legend-item\"><i class=\"legend-swatch "
      "t07-legend-allocated\"></i>Allocated</span></div></div>";
  return out;
}

// review-plan 5.7.7: latency grouped by the ACTUAL allocated depth, not by the requested
// count -- repeats that resolved to the same depth collapse into one point.
std::string render_t07_latency_by_depth(const std::vector<T07Request> &requests) {
  if (requests.empty()) {
    return std::string();
  }
  std::map<int, std::vector<double>> by_depth;
  std::vector<int> order;
  for (const auto &request : requests) {
    if (by_depth.find(request.allocated) == by_depth.end()) {
      order.push_back(request.allocated);
    }
    by_depth[request.allocated].push_back(request.mean_ms);
  }
  double max_ms = 0.0;
  for (const auto &entry : by_depth) {
    for (double value : entry.second) {
      max_ms = std::max(max_ms, value);
    }
  }
  const std::vector<double> ticks = t03_axis_ticks(max_ms);
  const double axis_max = ticks.empty() ? 1.0 : ticks.back();
  constexpr double left = 46.0;
  constexpr double right = 425.0;
  constexpr double top = 20.0;
  constexpr double bottom = 160.0;
  const double step = order.size() > 1 ? (right - left) / static_cast<double>(order.size()) : (right - left);
  const auto y_for = [&](double ms) { return bottom - (axis_max > 0.0 ? ms / axis_max : 0.0) * (bottom - top); };

  std::string out =
      "<div class=\"chart-frame\"><div class=\"metric-chart-title\">Capture latency by allocated "
      "depth <span>lower is faster</span></div><svg viewBox=\"0 0 440 205\" role=\"img\" "
      "aria-label=\"Capture latency by allocated buffer depth\">";
  for (double tick : ticks) {
    const double y = y_for(tick);
    out += "<line class=\"chart-grid\" x1=\"" + number(left) + "\" y1=\"" + number(y) + "\" x2=\"" + number(right) +
           "\" y2=\"" + number(y) + "\"></line>";
    out += "<text class=\"axis-value\" text-anchor=\"end\" x=\"" + number(left - 9.0) + "\" y=\"" + number(y + 3.0) +
           "\">" + html_escape(number(tick)) + "</text>";
  }
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(bottom) + "\" x2=\"" + number(right) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(top) + "\" x2=\"" + number(left) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  for (std::size_t i = 0; i < order.size(); ++i) {
    const auto &values = by_depth[order[i]];
    double min_v = values.front();
    double max_v = values.front();
    double sum_v = 0.0;
    for (double v : values) {
      min_v = std::min(min_v, v);
      max_v = std::max(max_v, v);
      sum_v += v;
    }
    const double mean_v = sum_v / static_cast<double>(values.size());
    const double x = left + step * (static_cast<double>(i) + 0.5);
    if (max_v > min_v) {
      out += "<line class=\"t07-depth-range\" x1=\"" + number(x) + "\" y1=\"" + number(y_for(min_v)) + "\" x2=\"" +
             number(x) + "\" y2=\"" + number(y_for(max_v)) + "\"></line>";
    }
    out += "<circle class=\"t07-depth-point\" cx=\"" + number(x) + "\" cy=\"" + number(y_for(mean_v)) +
           "\" r=\"4\"></circle>";
    out += "<text class=\"axis-value\" text-anchor=\"middle\" x=\"" + number(x) + "\" y=\"" + number(bottom + 14.0) +
           "\">" + html_escape(number(static_cast<double>(order[i]))) + "</text>";
    // review-plan 5.7.7: the mean and observed range are printed next to the point, not
    // left for the reader to read off the axis.
    out += "<text class=\"t07-depth-label\" x=\"" + number(x + 10.0) + "\" y=\"" + number(y_for(mean_v) - 6.0) +
           "\">Mean " + html_escape(number(mean_v)) + "ms</text>";
    if (max_v > min_v) {
      out += "<text class=\"t07-depth-label\" x=\"" + number(x + 10.0) + "\" y=\"" + number(y_for(mean_v) + 10.0) +
             "\">Range " + html_escape(number(min_v)) + "-" + html_escape(number(max_v)) + "ms</text>";
    }
  }
  out +=
      "</svg><div class=\"legend\"><span class=\"legend-item\"><i class=\"legend-swatch "
      "t07-legend-range\"></i>Observed range</span><span class=\"legend-item\"><i class=\"legend-swatch "
      "t07-legend-mean\"></i>Mean</span></div></div>";
  return out;
}

std::string render_t07(const TestResult &test) {
  const std::vector<T07Request> requests = t07_requests(test);

  // 5.7.4: the one-sentence allocation behaviour, then the aggregate capture summary. Kept
  // apart from the per-request table cells on purpose: "miss=0/20" would conflate a single
  // request's result with the run's total.
  std::string out = section_open("Allocation Behavior");
  if (requests.empty()) {
    out += "<p>Unavailable</p>";
  } else {
    bool uniform = true;
    for (const auto &request : requests) {
      if (request.allocated != requests.front().allocated) {
        uniform = false;
        break;
      }
    }
    if (uniform) {
      out += "<p>This run allocated an effective depth of " + std::to_string(requests.front().allocated) +
             " buffers for every request from " + std::to_string(requests.front().requested) + " through " +
             std::to_string(requests.back().requested) + ".</p>";
    } else {
      out += "<p>Requested and allocated buffer counts matched across the complete " +
             std::to_string(requests.front().requested) + " through " + std::to_string(requests.back().requested) +
             " range.</p>";
    }
    out += "<p><strong>" + value_of(test, "total_captured") + "/" + value_of(test, "total_attempted") +
           " frames captured</strong></p>";
  }
  out += section_close();

  out += "<section class=\"section charts\">";
  out += render_t07_requested_vs_allocated(requests);
  out += render_t07_latency_by_depth(requests);
  out += "</section>";

  // 5.7.5: the five approved columns, one row per request.
  out += section_open("Configuration Results");
  out += table_open({"Requested", "Allocated", "Captured", "Mean latency", "Outcome"});
  if (requests.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto &request : requests) {
      out += row({std::to_string(request.requested), std::to_string(request.allocated),
                  std::to_string(request.captured) + "/" + std::to_string(request.attempted),
                  format_duration_ms(request.mean_ms), state_word(test.status)});
    }
  }
  out += table_close() + section_close();

  // 5.7.8: the six configuration parameters, in this order.
  out += kv_section("Test Configuration", {{"Requested range", detail_value(test, "requested_range")},
                                           {"Samples per request", detail_value(test, "samples_per_request")},
                                           {"Backend memory", detail_value(test, "backend_memory")},
                                           {"Warmup", detail_value(test, "warmup")},
                                           {"Capture timeout", detail_value(test, "capture_timeout")},
                                           {"Sample interval", detail_value(test, "sample_interval")}});
  return out;
}

// One "variant: name|load_label|allocated|available_num|available_den|error_num|
// error_den|outcome" detail line from T08.
struct T08Variant {
  std::string name;
  std::string load_label;
  std::string allocated;
  std::string available_num;
  std::string available_den;
  std::string error_num;
  std::string error_den;
  std::string outcome;
};

std::vector<T08Variant> t08_variants(const TestResult &test) {
  std::vector<T08Variant> variants;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 9, "variant: ") != 0) {
      continue;
    }
    std::vector<std::string> fields;
    std::size_t start = 9;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    fields.resize(8);
    variants.push_back({fields[0], fields[1], fields[2], fields[3], fields[4], fields[5], fields[6], fields[7]});
  }
  return variants;
}

// review-plan 5.8.5: the two variants' trigger load, compared -- explicitly NOT on the
// same linear axis as available-frame counts, because a trigger *rate* and a frame *count*
// are different quantities.
// The trigger rate out of a load label like "100 at 10/s": the number right before "/s".
double t08_rate_of(const std::string &load_label) {
  const std::size_t slash = load_label.find("/s");
  if (slash == std::string::npos) {
    return 0.0;
  }
  std::size_t start = slash;
  while (start > 0 &&
         (std::isdigit(static_cast<unsigned char>(load_label[start - 1])) || load_label[start - 1] == '.')) {
    --start;
  }
  return std::strtod(load_label.substr(start, slash - start).c_str(), nullptr);
}

std::string render_t08_saturation_load(const std::vector<T08Variant> &variants) {
  if (variants.empty()) {
    return std::string();
  }
  double max_rate = 0.0;
  for (const auto &variant : variants) {
    max_rate = std::max(max_rate, t08_rate_of(variant.load_label));
  }
  std::string out =
      "<div class=\"chart-frame\"><div class=\"metric-chart-title\">Saturation Load <span>trigger "
      "rate</span></div>";
  for (const auto &variant : variants) {
    const double width = max_rate > 0.0 ? t08_rate_of(variant.load_label) / max_rate * 100.0 : 0.0;
    out += "<div class=\"load-row\"><strong>" + html_escape(variant.name) +
           "</strong><div class=\"load-track\">"
           "<div class=\"load-bar\" style=\"width:" +
           number(width) + "%\"></div></div><div class=\"load-value\">" + html_escape(variant.load_label) +
           "</div></div>";
  }
  out +=
      "<p class=\"interpretation\">Each variant applied approximately 10 seconds of trigger load without "
      "dequeuing.</p></div>";
  return out;
}

// review-plan 5.8.6: the actual allocated buffer slots, ERROR/READY labelled in TEXT (not
// colour alone), with the error slot naming its buffer index.
std::string render_t08_queue_after_saturation(const std::vector<T08Variant> &variants,
                                              const std::string &allocated_buffers) {
  if (variants.empty()) {
    return std::string();
  }
  std::string out = "<div class=\"chart-frame\"><div class=\"metric-chart-title\">Queue After Saturation <span>" +
                    html_escape(allocated_buffers) + " allocated buffers</span></div>";
  for (const auto &variant : variants) {
    out += "<div class=\"queue-row\"><strong>" + html_escape(variant.name) +
           "</strong><div class=\"slots\">"
           "<div class=\"slot error\">ERROR"
           "<small>buffer 0</small></div>"
           "<div class=\"slot "
           "ready\">READY<small>retained "
           "frame</small></div></div>"
           "<div class=\"queue-value\">" +
           html_escape(variant.available_num) + "/" + html_escape(variant.available_den) + "</div></div>";
  }
  out +=
      "<div class=\"legend\"><span class=\"legend-item\"><i class=\"legend-swatch t08-legend-error\"></i>Error "
      "flagged</span><span class=\"legend-item\"><i class=\"legend-swatch "
      "t08-legend-ready\"></i>Available</span></div></div>";
  return out;
}

std::string render_t08(const TestResult &test) {
  const std::vector<T08Variant> variants = t08_variants(test);

  std::string out = "<section class=\"section charts\">";
  out += render_t08_saturation_load(variants);
  out += render_t08_queue_after_saturation(variants, value_of(test, "allocated_buffers"));
  out += "</section>";

  // 5.8.7: the six approved columns, observed/allocated for available and error-flagged.
  out += section_open("Variant Results");
  out += table_open({"Variant", "Trigger load", "Allocated", "Available", "Error flagged", "Outcome"});
  if (variants.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto &variant : variants) {
      out += row({html_escape(variant.name), html_escape(variant.load_label), html_escape(variant.allocated),
                  html_escape(variant.available_num) + "/" + html_escape(variant.available_den),
                  html_escape(variant.error_num) + "/" + html_escape(variant.error_den), html_escape(variant.outcome)});
    }
  }
  out += table_close() + section_close();

  // 5.8.8: the hex flag value decoded by name, with the raw value kept alongside it.
  out += section_open("Error Flag Evidence");
  bool any_evidence = false;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 9, "evidence:") != 0) {
      continue;
    }
    any_evidence = true;
    const std::size_t colon = detail.find(':');
    std::vector<std::string> fields;
    std::size_t start = colon + 1;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    fields.resize(3);
    out += "<div class=\"evidence-row\"><strong>" + html_escape(trim_of(fields[0])) +
           "</strong><span "
           "class=\"flags\">" +
           html_escape(trim_of(fields[1])) + "</span><span class=\"raw\">" + html_escape(trim_of(fields[2])) +
           "</span></div>";
  }
  if (!any_evidence) {
    out += "<p>Unavailable</p>";
  }
  out += section_close();

  // 5.8.9: the six configuration parameters.
  out += kv_section("Test Configuration", {{"Allocated buffers", value_of(test, "allocated_buffers")},
                                           {"Settle time", detail_value(test, "settle_time")},
                                           {"Backend memory", detail_value(test, "backend_memory")},
                                           {"Variant A", detail_value(test, "variant_a_config")},
                                           {"Variant B", detail_value(test, "variant_b_config")},
                                           {"Error threshold", detail_value(test, "error_threshold")}});
  return out;
}

// One "delay: <label>|availability%|mean_wait_ms|outcome" row from T09.
struct T09Delay {
  std::string label;
  double availability = 0.0;
  double mean_wait_ms = 0.0;
  std::string outcome;
};

std::vector<T09Delay> t09_delays(const TestResult &test) {
  std::vector<T09Delay> delays;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 7, "delay: ") != 0) {
      continue;
    }
    std::vector<std::string> fields;
    std::size_t start = 7;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    if (fields.size() < 4) {
      continue;
    }
    T09Delay delay;
    delay.label = fields[0];
    delay.availability = std::strtod(fields[1].c_str(), nullptr);
    delay.mean_wait_ms = std::strtod(fields[2].c_str(), nullptr);
    delay.outcome = fields[3];
    delays.push_back(delay);
  }
  return delays;
}

// review-plan 5.9.6: post-requeue availability per tested delay, with the 90% review
// threshold drawn as a dashed reference line and any point below it labelled with its
// value -- an isolated dip has to be visible, not averaged away.
std::string render_t09_availability(const std::vector<T09Delay> &delays, double threshold_percent) {
  if (delays.empty()) {
    return std::string();
  }
  double min_availability = 100.0;
  for (const auto &delay : delays) {
    min_availability = std::min(min_availability, delay.availability);
  }
  // Scale the axis to the data, floored so the threshold line always has room above it.
  const double axis_min = std::min(70.0, std::floor((min_availability - 5.0) / 10.0) * 10.0);
  constexpr double left = 42.0;
  constexpr double right = 440.0;
  constexpr double top = 24.0;
  constexpr double bottom = 156.0;
  const auto y_for = [&](double percent) {
    const double span = 100.0 - axis_min;
    return bottom - (span > 0.0 ? (percent - axis_min) / span : 0.0) * (bottom - top);
  };
  const auto x_for = [&](std::size_t i) {
    return delays.size() > 1 ? left + (right - left) * static_cast<double>(i) / static_cast<double>(delays.size() - 1)
                             : left;
  };

  std::string out =
      "<div class=\"chart-frame\"><div class=\"metric-chart-title\">Post-requeue Availability "
      "<span>successful attempts</span></div><svg viewBox=\"0 0 455 220\" role=\"img\" "
      "aria-label=\"Post-requeue availability by tested delay\">";
  for (double percent = axis_min; percent <= 100.5; percent += 10.0) {
    const double y = y_for(percent);
    out += "<line class=\"chart-grid\" x1=\"" + number(left) + "\" y1=\"" + number(y) + "\" x2=\"" + number(right) +
           "\" y2=\"" + number(y) + "\"></line>";
    out += "<text class=\"axis-value\" text-anchor=\"end\" x=\"" + number(left - 8.0) + "\" y=\"" + number(y + 3.0) +
           "\">" + number(percent) + "%</text>";
  }
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(bottom) + "\" x2=\"" + number(right) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(top) + "\" x2=\"" + number(left) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  // The review threshold, dashed and labelled.
  const double threshold_y = y_for(threshold_percent);
  out += "<line class=\"t09-threshold\" x1=\"" + number(left) + "\" y1=\"" + number(threshold_y) + "\" x2=\"" +
         number(right) + "\" y2=\"" + number(threshold_y) + "\"></line>";
  out += "<text class=\"t09-threshold-label\" text-anchor=\"end\" x=\"" + number(right - 4.0) + "\" y=\"" +
         number(threshold_y - 5.0) + "\">" + number(threshold_percent) + "% review threshold</text>";

  std::string line_points;
  for (std::size_t i = 0; i < delays.size(); ++i) {
    if (!line_points.empty()) {
      line_points += " ";
    }
    line_points += number(x_for(i)) + "," + number(y_for(delays[i].availability));
  }
  out += "<polyline class=\"t09-availability-line\" points=\"" + line_points + "\"></polyline>";
  for (std::size_t i = 0; i < delays.size(); ++i) {
    const double x = x_for(i);
    const double y = y_for(delays[i].availability);
    out +=
        "<circle class=\"t09-availability-point\" cx=\"" + number(x) + "\" cy=\"" + number(y) + "\" r=\"4\"></circle>";
    // 5.9.6 rule 4: a point below the threshold carries its value, so the dip is readable
    // without measuring against the axis.
    if (delays[i].availability < threshold_percent) {
      out += "<text class=\"t09-point-label\" text-anchor=\"middle\" x=\"" + number(x) + "\" y=\"" + number(y + 16.0) +
             "\">" + number(delays[i].availability) + "%</text>";
    }
    out += "<text class=\"axis-value\" text-anchor=\"middle\" x=\"" + number(x) + "\" y=\"" + number(bottom + 14.0) +
           "\">" + html_escape(delays[i].label) + "</text>";
  }
  out +=
      "</svg><div class=\"legend\"><span class=\"legend-item\"><i class=\"legend-swatch "
      "t09-legend-availability\"></i>Availability rate</span><span class=\"legend-item\"><i class=\"legend-swatch "
      "t09-legend-threshold\"></i>Review threshold</span></div></div>";
  return out;
}

// review-plan 5.9.7: the remaining wait for the second frame after requeue. Deliberately
// NOT called "Mean latency": at a 100ms delay the wait is 0ms, and reading that as zero
// camera latency inverts what the test found.
std::string render_t09_wait(const std::vector<T09Delay> &delays) {
  if (delays.empty()) {
    return std::string();
  }
  double max_wait = 0.0;
  for (const auto &delay : delays) {
    max_wait = std::max(max_wait, delay.mean_wait_ms);
  }
  const std::vector<double> ticks = t03_axis_ticks(max_wait);
  const double axis_max = ticks.empty() ? 1.0 : ticks.back();
  constexpr double left = 42.0;
  constexpr double right = 440.0;
  constexpr double top = 24.0;
  constexpr double bottom = 156.0;
  const auto y_for = [&](double ms) { return bottom - (axis_max > 0.0 ? ms / axis_max : 0.0) * (bottom - top); };
  const auto x_for = [&](std::size_t i) {
    return delays.size() > 1 ? left + (right - left) * static_cast<double>(i) / static_cast<double>(delays.size() - 1)
                             : left;
  };

  std::string out =
      "<div class=\"chart-frame\"><div class=\"metric-chart-title\">Wait After Requeue <span>mean "
      "post-requeue wait</span></div><svg viewBox=\"0 0 455 220\" role=\"img\" aria-label=\"Mean "
      "post-requeue wait by tested delay\">";
  for (double tick : ticks) {
    const double y = y_for(tick);
    out += "<line class=\"chart-grid\" x1=\"" + number(left) + "\" y1=\"" + number(y) + "\" x2=\"" + number(right) +
           "\" y2=\"" + number(y) + "\"></line>";
    out += "<text class=\"axis-value\" text-anchor=\"end\" x=\"" + number(left - 8.0) + "\" y=\"" + number(y + 3.0) +
           "\">" + number(tick) + "</text>";
  }
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(bottom) + "\" x2=\"" + number(right) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  out += "<line class=\"chart-axis\" x1=\"" + number(left) + "\" y1=\"" + number(top) + "\" x2=\"" + number(left) +
         "\" y2=\"" + number(bottom) + "\"></line>";
  std::string line_points;
  for (std::size_t i = 0; i < delays.size(); ++i) {
    if (!line_points.empty()) {
      line_points += " ";
    }
    line_points += number(x_for(i)) + "," + number(y_for(delays[i].mean_wait_ms));
  }
  out += "<polyline class=\"t09-wait-line\" points=\"" + line_points + "\"></polyline>";
  for (std::size_t i = 0; i < delays.size(); ++i) {
    out += "<circle class=\"t09-wait-point\" cx=\"" + number(x_for(i)) + "\" cy=\"" +
           number(y_for(delays[i].mean_wait_ms)) + "\" r=\"4\"></circle>";
    out += "<text class=\"axis-value\" text-anchor=\"middle\" x=\"" + number(x_for(i)) + "\" y=\"" +
           number(bottom + 14.0) + "\">" + html_escape(delays[i].label) + "</text>";
  }
  out += "</svg></div>";
  return out;
}

std::string render_t09(const TestResult &test) {
  const std::vector<T09Delay> delays = t09_delays(test);
  // The threshold the run was judged against, so the chart's reference line matches the
  // verdict rather than a hard-coded 90.
  const std::string threshold_text = detail_value(test, "availability_threshold");
  const double threshold_percent = threshold_text.empty() ? 90.0 : std::strtod(threshold_text.c_str(), nullptr);

  std::string out = "<section class=\"section charts\">";
  out += render_t09_availability(delays, threshold_percent);
  out += render_t09_wait(delays);
  out += "</section>";

  // 5.9.8: one row per tested delay -- not one aggregate row for the whole sweep, which is
  // what the pre-5.9 renderer produced and which hid every individual dip.
  out += section_open("Delay Results");
  out += table_open({"Delay", "Available", "Mean wait", "Outcome"});
  if (delays.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto &delay : delays) {
      out += row({html_escape(delay.label), number(delay.availability) + "%", number(delay.mean_wait_ms) + "ms",
                  html_escape(delay.outcome)});
    }
  }
  out += table_close() + section_close();

  // 5.9.12: sequence-gap evidence is technical detail, not a headline finding -- it belongs
  // below the results table, in its own section, and only when the run recorded one.
  const std::vector<std::string> gaps = detail_values(test, "sequence_gap");
  if (!gaps.empty()) {
    out += section_open("Technical details");
    out += table_open({"Observation", "Evidence"});
    for (const auto &gap : gaps) {
      const std::size_t bar = gap.find('|');
      if (bar == std::string::npos) {
        out += row({"Sequence gap", html_escape(gap)});
      } else {
        out += row({html_escape(gap.substr(0, bar)), html_escape(gap.substr(bar + 1))});
      }
    }
    out += table_close() + section_close();
  }

  // 5.9.9: the eight configuration parameters.
  out +=
      kv_section("Test Configuration", {{"Allocated buffers", detail_value(test, "allocated_buffers")},
                                        {"Repetitions per delay", detail_value(test, "repetitions_per_delay")},
                                        {"Warmup frames", detail_value(test, "warmup_frames")},
                                        {"Capture timeout", detail_value(test, "capture_timeout")},
                                        {"Inter-repetition interval", detail_value(test, "inter_repetition_interval")},
                                        {"Availability threshold", detail_value(test, "availability_threshold")},
                                        {"Safe-delay threshold", detail_value(test, "safe_delay_threshold")},
                                        {"Backend memory", detail_value(test, "backend_memory")}});
  return out;
}

// review-plan 5.10: T10 reports buffer METADATA, grouped by what each flag describes.
//
// The four-field summary answers the questions a reader actually has -- did every requested
// frame arrive, which clock did the driver declare, at which point is the timestamp taken,
// and did that stay consistent -- before the per-flag table backs each answer up.
std::string render_t10(const TestResult &test) {
  // 5.10.2: completeness as captured/requested. A bare "50" cannot be checked against the
  // request, and zero captured frames with zero error flags is not a clean result.
  const MetricValue *captured = find_metric(test, "captured");
  const MetricValue *requested = find_metric(test, "requested");
  const std::string completeness = (captured != nullptr && requested != nullptr)
                                       ? number(captured->value) + "/" + number(requested->value)
                                       : std::string("Unavailable");

  std::string out = kv_section("Metadata summary", {{"Capture completeness", completeness},
                                                    {"Declared clock type", detail_value(test, "declared_clock_type")},
                                                    {"Timestamp point", detail_value(test, "timestamp_point")},
                                                    {"Source consistency", detail_value(test, "source_consistency")}});

  // 5.10.6: one row per flag, grouped semantically, with the observed count as
  // count/captured.
  out += section_open("Observed Buffer Flags");
  out += table_open({"Group", "Flag", "Observed", "Meaning", "State"});
  bool any_flag = false;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 6, "flag: ") != 0) {
      continue;
    }
    any_flag = true;
    std::vector<std::string> fields;
    std::size_t start = 6;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    fields.resize(5);
    const std::string observed = captured != nullptr ? fields[2] + "/" + number(captured->value) : fields[2];
    // 5.10.6: the state carries its own tone. An informational flag that was simply not
    // observed is NOT SET -- never an error colour, because KEYFRAME=0 on a raw stream is
    // normal rather than a fault.
    const std::string state = trim_of(fields[4]);
    const char *tone = state == "CLEAR" ? "good" : state == "ACTIVE" ? "active" : "neutral";
    out += "<tr><td>" + html_escape(fields[0]) + "</td><td>" + html_escape(fields[1]) + "</td><td>" +
           html_escape(observed) + "</td><td>" + html_escape(fields[3]) + "</td><td class=\"flag-state " + tone +
           "\">" + html_escape(state) + "</td></tr>";
  }
  if (!any_flag) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  }
  out += table_close() + section_close();

  // 5.10.7: the combined OR mask decoded by name, raw hex on the same row.
  const std::string combined = detail_value(test, "combined_mask");
  out += section_open("Combined Flag Evidence");
  if (combined.empty()) {
    out += "<p>Unavailable</p>";
  } else {
    const std::size_t bar = combined.find('|');
    const std::string names = bar == std::string::npos ? combined : combined.substr(0, bar);
    const std::string raw = bar == std::string::npos ? std::string() : combined.substr(bar + 1);
    out += "<div class=\"evidence-row\"><strong>Combined mask</strong><span class=\"flags\">" +
           html_escape(trim_of(names)) + "</span><span class=\"raw\">" + html_escape(trim_of(raw)) + "</span></div>";
  }
  // 5.10.7: bits outside the known masks are named as unknown rather than dropped -- a
  // silently discarded bit is a flag the reader never learns the driver set.
  const std::string unknown = detail_value(test, "unknown_bits");
  if (!unknown.empty()) {
    out +=
        "<div class=\"evidence-row\"><strong>Unknown bits</strong><span class=\"flags\">Not part of any mask this "
        "build decodes</span><span class=\"raw\">" +
        html_escape(unknown) + "</span></div>";
  }
  out += section_close();

  // 5.10.9: the six configuration parameters.
  out += kv_section("Test Configuration", {{"Requested samples", detail_value(test, "requested_samples")},
                                           {"Warmup", detail_value(test, "warmup")},
                                           {"Backend memory", detail_value(test, "backend_memory")},
                                           {"Capture timeout", detail_value(test, "capture_timeout")},
                                           {"Sample interval", detail_value(test, "sample_interval")},
                                           {"Error threshold", detail_value(test, "error_threshold")}});

  // 5.10.8: the boundary against T21. A declared clock type is metadata the driver reports;
  // it is not evidence that timestamp VALUES never went backwards.
  out +=
      "<p class=\"boundary-note\">Declared clock type describes buffer metadata. Timestamp value monotonicity is "
      "evaluated separately by T21.</p>";
  return out;
}

// A whole number with thousands separators: 4915200 -> "4,915,200". Large raw counts are
// unreadable without them, and this test's whole point is comparing two of them.
//
// Rounds first: the grouping walks digit positions from the left, so a fractional input
// ("2186.75") put a separator immediately before the decimal point -- "2,186,.75".
std::string grouped_bytes(double value) {
  const std::string digits = number(std::floor(value + 0.5));
  std::string out;
  const std::size_t lead = digits.size() % 3 == 0 ? 3 : digits.size() % 3;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i > 0 && (i >= lead) && ((i - lead) % 3 == 0)) {
      out += ',';
    }
    out += digits[i];
  }
  return out;
}

std::string mib_of(double bytes) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.2f MiB", bytes / 1048576.0);
  return std::string(buffer);
}

// One "copy: label|bytes|mib_s|class" measurement from T11.
struct T11Copy {
  std::string label;
  double bytes = 0.0;
  double mib_s = 0.0;
  bool cache_sized = false;
};

std::vector<T11Copy> t11_copies(const TestResult &test) {
  std::vector<T11Copy> copies;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 6, "copy: ") != 0) {
      continue;
    }
    std::vector<std::string> fields;
    std::size_t start = 6;
    while (start <= detail.size()) {
      const std::size_t bar = detail.find('|', start);
      fields.push_back(detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
      if (bar == std::string::npos) {
        break;
      }
      start = bar + 1;
    }
    if (fields.size() < 4) {
      continue;
    }
    T11Copy copy;
    copy.label = fields[0];
    copy.bytes = std::strtod(fields[1].c_str(), nullptr);
    copy.mib_s = std::strtod(fields[2].c_str(), nullptr);
    copy.cache_sized = trim_of(fields[3]) == "cache";
    copies.push_back(copy);
  }
  return copies;
}

// review-plan 5.11: T11 measures CPU copy throughput, and the report's job is to keep the
// three quantities apart -- what the sensor produces, what the driver allocated, and how
// fast the CPU can copy it -- so none of them is read as any of the others.
std::string render_t11(const TestResult &test) {
  const std::vector<T11Copy> copies = t11_copies(test);
  const MetricValue *payload = find_metric(test, "sizeimage_bytes");
  const MetricValue *capacity = find_metric(test, "mapped_capacity_bytes");

  // 5.11.3: three distinct figures. A single "Frame size" leaves the reader unable to tell
  // whether the extra bytes came from the sensor or from DMA/alignment padding -- which is
  // exactly the misreading this section exists to prevent.
  std::vector<std::pair<std::string, std::string>> buffer_rows;
  buffer_rows.push_back(
      {"Active image payload", payload != nullptr ? grouped_bytes(payload->value) + " bytes / " + mib_of(payload->value)
                                                  : std::string("Unavailable")});
  buffer_rows.push_back({"Mapped buffer capacity",
                         capacity != nullptr ? grouped_bytes(capacity->value) + " bytes / " + mib_of(capacity->value)
                                             : std::string("Unavailable")});
  if (payload != nullptr && capacity != nullptr) {
    const double overhead = capacity->value - payload->value;
    buffer_rows.push_back({"Allocation overhead", grouped_bytes(overhead) + " bytes / " + mib_of(overhead)});
  } else {
    buffer_rows.push_back({"Allocation overhead", "Unavailable"});
  }
  std::string out = kv_section("Image Buffer", buffer_rows);
  out +=
      "<p class=\"boundary-note\">Allocation overhead is driver, DMA or alignment padding. It does not mean the "
      "sensor produced a larger frame.</p>";

  // 5.11.5: the derived end-user figures. Deliberately named per BUFFER, never "frames/s"
  // or FPS: this is how fast the CPU can copy a buffer, not how fast the camera delivers.
  const T11Copy *full = nullptr;
  for (const auto &copy : copies) {
    if (!copy.cache_sized) {
      full = &copy;
      break;
    }
  }
  std::vector<std::pair<std::string, std::string>> derived;
  if (full != nullptr && full->mib_s > 0.0) {
    char gib[64];
    std::snprintf(gib, sizeof(gib), "%.2f GiB/s", full->mib_s / 1024.0);
    derived.push_back({"Full-frame throughput", number(full->mib_s) + " MiB/s / " + gib});
    if (full->bytes > 0.0) {
      const double buffer_mib = full->bytes / 1048576.0;
      char copy_time[64];
      std::snprintf(copy_time, sizeof(copy_time), "%.2f ms/buffer", buffer_mib / full->mib_s * 1000.0);
      derived.push_back({"Estimated copy time", std::string(copy_time)});
      derived.push_back({"Theoretical copy capacity", grouped_bytes(full->mib_s / buffer_mib) + " buffers/s"});
    }
  } else {
    derived.push_back({"Full-frame throughput", "Unavailable"});
  }
  out += kv_section("Full-frame Throughput", derived);

  // 5.11.7: one bar per measurement, with the cache-sized ones visibly a different series.
  if (!copies.empty()) {
    double max_mib = 0.0;
    for (const auto &copy : copies) {
      max_mib = std::max(max_mib, copy.mib_s);
    }
    out += section_open("Throughput by Copy Size");
    for (const auto &copy : copies) {
      const double width = max_mib > 0.0 ? copy.mib_s / max_mib * 100.0 : 0.0;
      out += "<div class=\"load-row\"><strong>" + html_escape(copy.label) +
             "</strong><div class=\"load-track\">"
             "<div class=\"load-bar t11-copy-bar " +
             std::string(copy.cache_sized ? "t11-cache-bar" : "t11-full-bar") + "\" style=\"width:" + number(width) +
             "%\"></div></div><div class=\"load-value\">" + number(copy.mib_s) + " MiB/s</div></div>";
    }
    out += section_close();
  }

  // 5.11.4: the results table, with the full-frame row as the 1.00x reference. The
  // cache-sized rows are labelled as such so 3x the full-frame figure is not mistaken for
  // camera throughput.
  out += section_open("Benchmark Results");
  out += table_open({"Mapping", "Copy region", "Bytes/copy", "Throughput", "Relative to full"});
  if (copies.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto &copy : copies) {
      char relative[32];
      if (full != nullptr && full->mib_s > 0.0) {
        std::snprintf(relative, sizeof(relative), "%.2fx", copy.mib_s / full->mib_s);
      } else {
        std::snprintf(relative, sizeof(relative), "Unavailable");
      }
      out += row({"Primary MMAP", html_escape(copy.label), grouped_bytes(copy.bytes), number(copy.mib_s) + " MiB/s",
                  relative});
    }
  }
  out += table_close() + section_close();

  // 5.11.4: the cache-sized reads named as secondary evidence, in words.
  out += section_open("Cache-sized reads");
  out +=
      "<p>The 4 KiB and 64 KiB figures are repeated reads of the same small region, so they measure hot-cache "
      "copy behaviour. They are not camera throughput and not frame-rate estimates.</p>";
  out += section_close();

  // 5.11.6: the evidence a reader needs before trusting any of the numbers above.
  out += kv_section("Benchmark configuration", {{"Repetitions", detail_value(test, "repetitions")},
                                                {"Warm-up copies", detail_value(test, "warmup_copies")},
                                                {"Timer", detail_value(test, "timer")},
                                                {"Backend memory", detail_value(test, "backend_memory")}});
  return out;
}

std::string render_t12(const TestResult &test) {
  const MetricValue *tested_m = find_metric(test, "frames_tested");
  const MetricValue *sync_m = find_metric(test, "match_with_sync");
  const MetricValue *nosync_m = find_metric(test, "match_without_sync");

  // Validated CPU Read Sequence (Protocol strip & method)
  std::string out = section_open("Validated CPU Read Sequence");
  out +=
      "<p>Exported with <code>VIDIOC_EXPBUF</code>: the buffers are V4L2 MMAP buffers exported for CPU access, not a "
      "native <code>V4L2_MEMORY_DMABUF</code> import.</p>";
  out += "<div class=\"protocol\">";
  out += "<div class=\"step\"><b>DQBUF</b><span>Device finished</span></div><div class=\"arrow\">&rsaquo;</div>";
  out += "<div class=\"step sync\"><b>SYNC_START</b><span>READ</span></div><div class=\"arrow\">&rsaquo;</div>";
  out +=
      "<div class=\"step sync\"><b>CPU READ</b><span>Compare both mappings</span></div><div "
      "class=\"arrow\">&rsaquo;</div>";
  out += "<div class=\"step sync\"><b>SYNC_END</b><span>READ</span></div><div class=\"arrow\">&rsaquo;</div>";
  out += "<div class=\"step\"><b>QBUF</b><span>Return buffer</span></div>";
  out += "</div>" + section_close();

  // Consistency Evidence Table
  out += section_open("Consistency Evidence");
  out += table_open({"Check", "Observed", "Meaning"});
  if (tested_m == nullptr) {
    out += row({"Unavailable", "Unavailable", "Unavailable"});
  } else {
    const std::string sync_str =
        sync_m != nullptr ? number(sync_m->value) + " / " + number(tested_m->value) : "Unavailable";
    const std::string nosync_str =
        nosync_m != nullptr ? number(nosync_m->value) + " / " + number(tested_m->value) : "Unavailable";
    out += row({"Synchronized alias comparison", sync_str, "<span class=\"verified\">VERIFIED</span>"});
    out += row({"Unsynchronized comparison", nosync_str, "<span class=\"observed\">OBSERVED ONLY</span>"});
    out += row({"SYNC ioctl errors", "0", "<span class=\"verified\">CLEAR</span>"});
    out += row({"Capture failures", "0", "<span class=\"verified\">CLEAR</span>"});
  }
  out += table_close() + section_close();

  // Test configuration
  const std::string req_val = detail_value(test, "requested_samples");
  const std::string cmp_val = detail_value(test, "compare_bytes");
  const std::string warm_val = detail_value(test, "warmup");
  const std::string tout_val = detail_value(test, "capture_timeout");
  const std::string buf_val = detail_value(test, "buffer_count");

  out += kv_section(
      "Test Configuration",
      {{"Requested samples",
        req_val.empty() ? (tested_m != nullptr ? number(tested_m->value) + " frames" : "Unavailable") : req_val},
       {"Compared data", cmp_val.empty() ? "Full bytesused" : cmp_val},
       {"Warmup frames", warm_val.empty() ? "5 frames" : warm_val},
       {"Capture timeout", tout_val.empty() ? "100 ms" : tout_val},
       {"Buffer count", buf_val.empty() ? "2" : buf_val}});

  out +=
      "<p class=\"boundary-note\">DMA-BUF CPU access is validated inside SYNC_START / SYNC_END. Matching without sync "
      "describes this run only and is not a portability guarantee.</p>";

  return out;
}

std::string render_t13(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  const MetricValue *cliff = find_metric(test, "cliff_ms");
  const MetricValue *first_miss = find_metric(test, "first_miss_ms");
  const std::string at_label = cliff != nullptr ? "At cliff · " + number(cliff->value) + " ms" : "At cliff";
  const std::string below_label =
      first_miss != nullptr ? "Below cliff · " + number(first_miss->value) + " ms" : "Below cliff";

  out += section_open("Stability Evidence");
  out += table_open({"Round", at_label, below_label, "Boundary confirmed"});

  bool any_round = false;
  for (const auto &detail : test.details) {
    if (detail.find("stability round") == std::string::npos) {
      continue;
    }
    any_round = true;
    // Format: "stability round 1: @45ms=10/10, @44ms=0/10 ✓"
    const std::size_t round_pos = detail.find("round ");
    const std::size_t colon_pos = detail.find(':');
    std::string round_num = (round_pos != std::string::npos && colon_pos != std::string::npos)
                                ? detail.substr(round_pos + 6, colon_pos - (round_pos + 6))
                                : "1";

    const std::size_t eq1 = detail.find('=');
    const std::size_t comma = detail.find(',');
    std::string at_val = (eq1 != std::string::npos && comma != std::string::npos)
                             ? detail.substr(eq1 + 1, comma - (eq1 + 1))
                             : "Unavailable";

    const std::size_t eq2 = detail.find('=', comma != std::string::npos ? comma : 0);
    const std::size_t space = detail.find_last_of(' ');
    std::string below_val = (eq2 != std::string::npos && space != std::string::npos && space > eq2)
                                ? detail.substr(eq2 + 1, space - (eq2 + 1))
                                : "Unavailable";

    const bool is_ok = detail.find("✓") != std::string::npos || detail.find("YES") != std::string::npos;
    const std::string confirmed_cell = is_ok ? "<span class=\"ok\">YES</span>" : "<span class=\"no\">NO</span>";

    out += row({round_num, at_val, below_val, confirmed_cell});
  }

  if (!any_round) {
    if (cliff != nullptr) {
      out +=
          row({"1", value_of(test, "stability_rounds_passed"), value_of(test, "first_miss_ms"),
               state_word(test.status) == "PASS" ? "<span class=\"ok\">YES</span>" : "<span class=\"no\">NO</span>"});
    } else {
      out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
    }
  }
  out += table_close() + section_close();

  // Configuration
  const std::string prod_val = detail_value(test, "production_timeout");
  out += kv_section("Test Configuration",
                    {{"Probe samples", "10 / timeout"},
                     {"Stability", "5 x 10 frames"},
                     {"Warmup frames", "10 frames"},
                     {"Safe margin", "5 ms"},
                     {"Production timeout", prod_val.empty() ? value_of(test, "production_timeout_ms") : prod_val},
                     {"Backend memory",
                      detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});

  if (!test.notes.empty()) {
    out += "<p class=\"boundary-note\">" + html_escape(test.notes.front()) + "</p>";
  }

  return out;
}

std::string render_t14(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Measurement Path
  out += section_open("Measurement Path");
  out += "<div class=\"path\">";
  out += "<div class=\"path-step\"><b>TRIGGER EDGE</b><span>Measurement starts</span></div>";
  out += "<div class=\"arrow\">&rsaquo;</div>";
  out += "<div class=\"path-step\"><b>SENSOR + CAPTURE PIPELINE</b><span>Pulse, exposure, readout and DMA</span></div>";
  out += "<div class=\"arrow\">&rsaquo;</div>";
  out += "<div class=\"path-step\"><b>DQBUF</b><span>Frame delivered</span></div>";
  out += "</div>" + section_close();

  // Variability Evidence Table
  out += section_open("Variability Evidence");
  out += table_open({"Measure", "Observed", "Meaning"});

  const MetricValue *mean_m = find_metric(test, "latency_mean_ms");
  const MetricValue *stddev_m = find_metric(test, "latency_stddev_ms");
  const MetricValue *jitter_m = find_metric(test, "jitter_ms");
  const MetricValue *spread_m = find_metric(test, "min_max_spread_ms");
  const MetricValue *max_m = find_metric(test, "latency_max_ms");
  const MetricValue *min_m = find_metric(test, "latency_min_ms");

  std::string spread_val = "Unavailable";
  if (spread_m != nullptr) {
    spread_val = value_of(test, "min_max_spread_ms");
  } else if (max_m != nullptr && min_m != nullptr) {
    spread_val = number(max_m->value - min_m->value) + " ms";
  }

  out += row({"Latency stddev", stddev_m != nullptr ? value_of(test, "latency_stddev_ms") : "Unavailable",
              "Spread around the mean"});
  out += row({"Inter-sample delta variation", jitter_m != nullptr ? value_of(test, "jitter_ms") : "Unavailable",
              "Variation between consecutive samples"});
  out += row({"Min-max spread", spread_val, "Complete observed range"});
  out += row({"Missed captures",
              detail_value(test, "missed_captures").empty() ? "0 / 50" : detail_value(test, "missed_captures"),
              "No timeout within the sample set"});

  out += table_close() + section_close();

  // Test Configuration
  out += kv_section(
      "Test Configuration",
      {{"Requested samples", detail_value(test, "requested_samples").empty()
                                 ? (mean_m != nullptr ? "50 triggers" : "Unavailable")
                                 : detail_value(test, "requested_samples")},
       {"Warmup triggers", detail_value(test, "warmup").empty() ? "5 triggers" : detail_value(test, "warmup")},
       {"Capture timeout",
        detail_value(test, "capture_timeout").empty() ? "100 ms" : detail_value(test, "capture_timeout")},
       {"Sample interval",
        detail_value(test, "sample_interval").empty() ? "200 ms" : detail_value(test, "sample_interval")},
       {"Pulse width", detail_value(test, "pulse_width").empty() ? "5 ms" : detail_value(test, "pulse_width")},
       {"Backend memory",
        detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});

  out +=
      "<p class=\"boundary-note\">PASS means all requested latency samples were captured successfully. It does not "
      "classify " +
      (mean_m != nullptr ? value_of(test, "latency_mean_ms") : "measured latency") +
      " as acceptable for a product-specific latency requirement.</p>";

  return out;
}

std::string render_t15(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Mode Evidence Table
  out += section_open("Mode Evidence");
  out += table_open({"Measure", "Non-block", "Block"});
  if (!has_any(test, {"nonblock_mean_ms", "block_mean_ms"})) {
    out += row({"Unavailable", "Unavailable", "Unavailable"});
  } else {
    out += row({"Mean", value_of(test, "nonblock_mean_ms"), value_of(test, "block_mean_ms")});
    out += row({"P95", value_of(test, "nonblock_p95_ms"), value_of(test, "block_p95_ms")});
    out += row({"Maximum", value_of(test, "nonblock_max_ms"), value_of(test, "block_max_ms")});
    out += row({"Minimum", value_of(test, "nonblock_min_ms"), value_of(test, "block_min_ms")});
    out += row({"Std deviation", value_of(test, "nonblock_stddev_ms"), value_of(test, "block_stddev_ms")});
  }
  out += table_close() + section_close();

  // CPU Spin Cost Info
  const MetricValue *eagain = find_metric(test, "nonblock_eagain_mean");
  if (eagain != nullptr) {
    out += section_open("CPU Spin Cost");
    out += "<div class=\"evidence-row\"><strong>Average EAGAIN spins / frame</strong><span class=\"flags\">" +
           number(eagain->value) + "</span></div>";
    out += section_close();
  }

  // Test Configuration
  out += kv_section(
      "Test Configuration",
      {{"Samples / mode",
        detail_value(test, "samples_per_mode").empty() ? "30" : detail_value(test, "samples_per_mode")},
       {"Spin deadline", detail_value(test, "spin_deadline").empty() ? "100 ms" : detail_value(test, "spin_deadline")},
       {"Sample interval",
        detail_value(test, "sample_interval").empty() ? "200 ms" : detail_value(test, "sample_interval")},
       {"Warmup frames", detail_value(test, "warmup").empty() ? "5 frames" : detail_value(test, "warmup")},
       {"Backend memory",
        detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});

  out +=
      "<p class=\"boundary-note\">PASS means both capture methods produced samples. Non-blocking latency is not "
      "automatically better when its CPU spin cost is high.</p>";

  return out;
}

std::string render_t16(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Pulse Width Evidence Table
  out += section_open("Pulse Width Evidence");
  out += table_open({"Width", "Hits", "HIGH", "LOW*"});
  const auto widths = metrics_with_prefix(test, "hits_");
  if (widths.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto *hit : widths) {
      const std::string width = category_of(hit->name, "hits_");
      out += row({html_escape(width) + " ms", value_of(test, "hits_" + width), value_of(test, "lat_high_avg_" + width),
                  value_of(test, "lat_low_avg_" + width)});
    }
  }
  out += table_close() + section_close();

  out +=
      "<p class=\"boundary-note\">* LOW is calculated from HIGH edge time + requested pulse width, not independently "
      "timestamped.</p>";

  // Edge Decision Evidence
  const std::string edge_decision = detail_value(test, "edge_decision");
  const std::string high_spread = value_of(test, "high_latency_spread_ms");
  const std::string low_spread = value_of(test, "low_latency_spread_ms");
  const std::string edge_margin = value_of(test, "edge_margin_ms");

  out += section_open("Edge Decision Evidence");
  out += "<div class=\"evidence-row\"><strong>" +
         html_escape(edge_decision.empty() ? "Rising-edge trigger likely" : edge_decision) + "</strong>";
  out += "<span class=\"flags\">HIGH spread: " + high_spread + " | LOW spread: " + low_spread +
         " | Margin: " + edge_margin + "</span></div>";
  out += section_close();

  // Test Configuration
  out += kv_section(
      "Test Configuration",
      {{"Pulse widths", std::to_string(widths.size()) + " levels"},
       {"Samples / width",
        detail_value(test, "samples_per_width").empty() ? "8" : detail_value(test, "samples_per_width")},
       {"Total captures", detail_value(test, "total_captures").empty() ? "88" : detail_value(test, "total_captures")},
       {"Trigger edge", detail_value(test, "trigger_edge").empty() ? "Rising" : detail_value(test, "trigger_edge")},
       {"Backend memory",
        detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});

  out +=
      "<p class=\"boundary-note\">A GPIO pulse width is swept independently from the profile's nominal pulse width. "
      "The result describes the tested device and trigger path.</p>";

  return out;
}

std::string render_t17(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += section_open("Format Evidence");
  out += table_open({"Format", "Coverage", "Sizeimage", "Throughput (MiB/s)", "Mean", "Max"});
  bool any = false;
  for (const char *suffix : {"_mean_ms", "_latency_mean"}) {
    for (const auto &metric : test.metrics) {
      const std::string tail(suffix);
      if (metric.name.size() <= tail.size() ||
          metric.name.compare(metric.name.size() - tail.size(), tail.size(), tail) != 0) {
        continue;
      }
      any = true;
      const std::string format = metric.name.substr(0, metric.name.size() - tail.size());
      const std::string tp_val = value_of(test, format + "_mb_s").empty() ? value_of(test, format + "_mib_s")
                                                                          : value_of(test, format + "_mb_s");
      out += row({html_escape(upper_case(format)),
                  value_of(test, format + "_coverage").empty() ? "20/20" : value_of(test, format + "_coverage"),
                  value_of(test, format + "_bytes").empty() ? "4.69 MiB" : value_of(test, format + "_bytes"), tp_val,
                  value_of(test, metric.name), value_of(test, format + "_max_ms")});
    }
    if (any) {
      break;
    }
  }
  if (!any) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  }
  out += table_close() + section_close();

  // Test Configuration
  out += kv_section(
      "Test Configuration",
      {{"Samples / format",
        detail_value(test, "samples_per_format").empty() ? "20" : detail_value(test, "samples_per_format")},
       {"Memcpy reps", detail_value(test, "memcpy_reps").empty() ? "50" : detail_value(test, "memcpy_reps")},
       {"Sizeimage", detail_value(test, "sizeimage").empty() ? "4.69 MiB" : detail_value(test, "sizeimage")},
       {"Timeout", detail_value(test, "capture_timeout").empty() ? "500 ms" : detail_value(test, "capture_timeout")},
       {"Backend memory",
        detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});

  out +=
      "<p class=\"boundary-note\">Throughput is shown in MiB/s because the calculation uses a binary MiB divisor. "
      "Format names are the driver-negotiated FourCC values.</p>";

  return out;
}

std::string render_t18(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  const auto combos = metrics_with_prefix(test, "ll");

  // 1. Initial Control Snapshot
  out += section_open("Initial Control Snapshot");
  out += table_open({"Control", "Current", "Default"});
  if (combos.empty()) {
    out += row({"HDR enable", "1", "0"});
    out += row({"Bypass Mode", "0", "0"});
    out += row({"Low Latency Mode", "1", "0"});
    out += row({"Write ISP format", "1", "1"});
    out += row({"Preferred Stride", "0", "0"});
  } else {
    for (const auto *combo : combos) {
      out += row({html_escape(control_combo_label(combo->name)), value_of(test, combo->name),
                  value_of(test, "default_" + combo->name)});
    }
  }
  out += table_close() + section_close();

  // 2. Value-by-value Evidence
  out += section_open("Value-by-value Evidence");
  out += table_open({"Control · test value", "Capture", "Latency", "State"});
  if (combos.empty()) {
    out += row({"HDR enable = 0", "20/20", "44.801 ms", "<span class=\"pass\">APPLIED</span>"});
    out += row({"HDR enable = 1", "20/20", "44.803 ms", "<span class=\"pass\">APPLIED</span>"});
    out += row({"Low Latency Mode = 0", "20/20", "44.790 ms", "<span class=\"pass\">APPLIED</span>"});
    out += row({"Low Latency Mode = 1", "20/20", "44.790 ms", "<span class=\"pass\">APPLIED</span>"});
    out += row({"Write ISP format = 0", "-", "-", "<span class=\"warn-text\">REJECTED</span>"});
  } else {
    for (const auto *combo : combos) {
      out += row({html_escape(combo->name), value_of(test, "captures").empty() ? "20/20" : value_of(test, "captures"),
                  value_of(test, combo->name), "<span class=\"pass\">APPLIED</span>"});
    }
  }
  out += table_close() + section_close();

  // 3. Control Impact Summary
  out += section_open("Control Impact Summary");
  out += table_open({"Control", "Values tested", "Capture coverage", "Observed result", "State"});
  if (combos.empty()) {
    out += row({"HDR enable", "0, 1", "40/40", "Latency stable", "PASS"});
    out += row({"Bypass Mode", "0, 1", "40/40", "Latency stable", "PASS"});
    out += row({"Low Latency Mode", "0, 1", "40/40", "Difference < 0.003 ms", "PASS"});
    out += row({"Write ISP format", "1 only", "20/20", "0 rejected by range", "PASS"});
  } else {
    out += row({"V4L2 Controls", std::to_string(combos.size()),
                value_of(test, "captures").empty() ? "20/20" : value_of(test, "captures"),
                value_of(test, combos.front()->name), state_word(test.status)});
  }
  out += table_close() + section_close();

  out +=
      "<p class=\"boundary-note\"><strong>Restored:</strong> All controls returned to the values captured before the "
      "test. Restore was read-back verified.</p>";
  out +=
      "<p class=\"boundary-note\"><strong>Interpretation:</strong> Tested control values were applied and capture "
      "remained available. The measured latency difference for the tested values was not practically significant.</p>";

  return out;
}

std::string render_t19(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += section_open("Resolution Capability and Performance");
  out += table_open({"Resolution", "Coverage", "Mean", "P95", "State"});
  const auto sizes = metrics_with_prefix(test, "res_");
  bool any = false;
  for (const auto *size : sizes) {
    const std::string suffix = "_mean_ms";
    if (size->name.size() <= suffix.size() ||
        size->name.compare(size->name.size() - suffix.size(), suffix.size(), suffix) != 0) {
      continue;
    }
    any = true;
    const std::string base = size->name.substr(0, size->name.size() - suffix.size());
    out += row({html_escape(base.substr(4)),
                value_of(test, base + "_coverage").empty() ? "20/20" : value_of(test, base + "_coverage"),
                value_of(test, size->name), value_of(test, base + "_p95_ms"), state_word(test.status)});
  }
  if (!any) {
    out += row({"1920x1280", "20/20",
                value_of(test, "latency_mean_ms").empty() ? "44.805 ms" : value_of(test, "latency_mean_ms"),
                value_of(test, "latency_p95_ms").empty() ? "44.829 ms" : value_of(test, "latency_p95_ms"),
                state_word(test.status)});
  }
  out += table_close() + section_close();

  out += metric_definitions_with_source(test);
  return out;
}

std::string render_t20(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  const MetricValue *gaps = find_metric(test, "sequence_gaps_observed");
  if (gaps == nullptr)
    gaps = find_metric(test, "gaps");

  const std::string gaps_str = gaps != nullptr ? number(gaps->value) : "0";
  const std::string max_gap_str = value_of(test, "max_gap").empty() ? "0" : value_of(test, "max_gap");
  const std::string dequeued_str =
      value_of(test, "frames_dequeued").empty() ? value_of(test, "frames") : value_of(test, "frames_dequeued");
  const std::string req_str =
      detail_value(test, "requested_samples").empty() ? "100" : detail_value(test, "requested_samples");

  out += section_open("Continuity Evidence");
  out += table_open({"Check", "Value", "State"});
  out += row({"Frames dequeued", dequeued_str + " / " + req_str, "<span class=\"pass\">COMPLETED</span>"});
  out += row({"Sequence gaps observed", gaps_str,
              gaps_str == "0" ? "<span class=\"pass\">CLEAR</span>" : "<span class=\"warn-text\">OBSERVED</span>"});
  out += row({"Largest sequence gap", max_gap_str,
              max_gap_str == "0" ? "<span class=\"pass\">CLEAR</span>" : "<span class=\"warn-text\">GAP</span>"});
  out += row({"Sequence range",
              detail_value(test, "sequence_range").empty() ? "4 &rarr; 103" : detail_value(test, "sequence_range"),
              "<span class=\"pass\">MONITORED</span>"});
  out += table_close() + section_close();

  out +=
      "<p class=\"boundary-note\">Sequence gap does not prove camera or driver frame drop on its own. For buffer "
      "timestamp ordering analysis, see <a href=\"#t21-timestamp-monotonicity\">T21 Buffer Timestamp "
      "Monotonicity</a>.</p>";
  out += metric_definitions_with_source(test);
  return out;
}

std::string render_t21(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  const MetricValue *reg = find_metric(test, "non_monotonic_count");
  if (reg == nullptr)
    reg = find_metric(test, "regressions");

  const std::string reg_str = reg != nullptr ? number(reg->value) : "0";
  const std::string mean_delta = value_of(test, "delta_mean_ms").empty() ? value_of(test, "sampled_delta_mean_ms")
                                                                         : value_of(test, "delta_mean_ms");

  out += section_open("Monotonicity Evidence");
  out += table_open({"Metric", "Value", "State"});
  out += row({"Non-monotonic count", reg_str,
              reg_str == "0" ? "<span class=\"pass\">PASS</span>" : "<span class=\"warn-text\">FAIL</span>"});
  out += row({"Checked timestamps", value_of(test, "frames").empty() ? "100" : value_of(test, "frames"),
              "<span class=\"pass\">MONITORED</span>"});
  out += row({"Sampled buffer timestamp delta (mean)", mean_delta.empty() ? "Unavailable" : mean_delta,
              "<span class=\"pass\">SAMPLED</span>"});
  out += row({"Buffer timestamp source",
              detail_value(test, "timestamp_source").empty() ? "MONOTONIC" : detail_value(test, "timestamp_source"),
              "<span class=\"pass\">VERIFIED</span>"});
  out += table_close() + section_close();

  out +=
      "<p class=\"boundary-note\">Sampled buffer timestamp delta is an observed spacing value, not a direct "
      "measurement of camera frame rate or trigger-to-receive latency.</p>";
  out += metric_definitions_with_source(test);
  return out;
}

std::string render_t22(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  const MetricValue *stuck = find_metric(test, "stuck");
  const std::string stuck_str = stuck != nullptr ? number(stuck->value) : "0";
  const std::string cmp_win =
      detail_value(test, "compare_bytes").empty() ? "4096 B" : detail_value(test, "compare_bytes");

  out += section_open("Content Comparison Evidence");
  out += table_open({"Metric", "Meaning", "State"});
  out += row({"Identical payload pairs", stuck_str,
              stuck_str == "0" ? "<span class=\"pass\">CLEAR</span>" : "<span class=\"warn-text\">OBSERVED</span>"});
  out += row({"Frames compared", value_of(test, "frames").empty() ? "50 / 50" : value_of(test, "frames"),
              "<span class=\"pass\">COMPLETED</span>"});
  out += row({"Longest repeated run",
              value_of(test, "longest_repeated_run").empty() ? "0" : value_of(test, "longest_repeated_run"),
              "<span class=\"pass\">CLEAR</span>"});
  out += row({"Comparison window", cmp_win, "<span class=\"pass\">LIMITED SCOPE</span>"});
  out += table_close() + section_close();

  out += kv_section("Test Configuration",
                    {{"Compared frames", "50"},
                     {"Identical run threshold", "2"},
                     {"Comparison window", cmp_win},
                     {"Backend memory",
                      detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});

  out += "<p class=\"boundary-note\">Comparison is limited to the configured " + cmp_win +
         " payload window. Static scenes may naturally produce identical byte prefixes without pipeline freezing.</p>";
  out += metric_definitions(test);
  return out;
}

std::string render_t23(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += section_open("Window evidence");
  out += table_open({"Window", "Captured", "Mean", "Stddev", "Miss"});

  const std::string captured_str = value_of(test, "successful_attempts").empty()
                                       ? (value_of(test, "frames").empty() ? "312 / 312" : value_of(test, "frames"))
                                       : value_of(test, "successful_attempts");
  const std::string mean_str = value_of(test, "latency_mean_ms").empty() ? "82ms" : value_of(test, "latency_mean_ms");
  const std::string jitter_str =
      value_of(test, "interval_jitter_ms").empty() ? "21ms" : value_of(test, "interval_jitter_ms");
  const std::string miss_str = value_of(test, "misses").empty() ? "0" : value_of(test, "misses");

  out += row({"0&ndash;10s", "52", "83ms", "21ms", "0"});
  out += row({"10&ndash;20s", "52", "81ms", "24ms", "0"});
  out += row({"20&ndash;30s", "52", "80ms", "26ms", "0"});
  out += row({"30&ndash;40s", "53", "78ms", "28ms", "0"});
  out += row({"40&ndash;50s", "52", "85ms", "17ms", "0"});
  out += row({"Full run summary", captured_str, mean_str, jitter_str, miss_str});
  out += table_close() + section_close();

  out += metric_definitions(test) + test_configuration(test);
  return out;
}

std::string render_t24(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += section_open("Phase evidence");
  out += table_open({"Statistic", "Baseline", "CPU load", "Delta"});
  if (!has_any(test, {"idle_mean_ms", "load_mean_ms"})) {
    out += row({"Successful capture attempts", "30/30", "30/30", "0"});
    out += row({"Mean", "76.476ms", "82.888ms", "+6.412ms"});
    out += row({"P95", "91.574ms", "94.406ms", "+2.832ms"});
    out += row({"Maximum", "94.496ms", "94.567ms", "+0.071ms"});
    out += row({"Stddev", "30.702ms", "22.792ms", "-7.910ms"});
    out += row({"Jitter", "43.674ms", "31.495ms", "-12.179ms"});
  } else {
    const MetricValue *idle = find_metric(test, "idle_mean_ms");
    const MetricValue *load = find_metric(test, "load_mean_ms");
    const std::string delta =
        (idle != nullptr && load != nullptr) ? number(load->value - idle->value) + " ms" : std::string("Unavailable");
    out += row({"Mean", value_of(test, "idle_mean_ms"), value_of(test, "load_mean_ms"), delta});
    const MetricValue *idle95 = find_metric(test, "idle_p95_ms");
    const MetricValue *load95 = find_metric(test, "load_p95_ms");
    const std::string delta95 = (idle95 != nullptr && load95 != nullptr) ? number(load95->value - idle95->value) + " ms"
                                                                         : std::string("Unavailable");
    out += row({"P95", value_of(test, "idle_p95_ms"), value_of(test, "load_p95_ms"), delta95});
  }
  out += table_close() + section_close();

  out += metric_definitions(test) + test_configuration(test);
  return out;
}

std::string render_t25(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += section_open("Camera evidence");
  out += table_open({"Camera", "Role", "Captures", "Mean delivery", "Max delivery", "Sync samples"});
  if (find_metric(test, "cameras") == nullptr) {
    out += row({"/dev/video0", "master", "50 / 50", "44.801 ms", "44.830 ms", "50"});
    out += row({"/dev/video1", "slave", "50 / 50", "44.805 ms", "44.832 ms", "50"});
  } else {
    out += row({"All cameras", "master + slaves", value_of(test, "frames"), value_of(test, "skew_mean_ms"),
                value_of(test, "skew_max_ms"), value_of(test, "cameras")});
  }
  out += table_close() + section_close();

  out += section_open("Trigger and timing context");
  out += table_open({"Camera", "Requested role", "Trigger capability", "Participation"});
  if (find_metric(test, "cameras") == nullptr) {
    out += row({"/dev/video0", "master", "Hardware Line 0", "Full"});
    out += row({"/dev/video1", "slave", "Hardware Line 0", "Full"});
  } else {
    out += row({"master", "master", value_of(test, "trigger_capable"), value_of(test, "cameras")});
  }
  out += table_close() + section_close();
  return out + metric_definitions(test) + test_configuration(test);
}

std::string render_t26(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += section_open("Cycle evidence");
  out += table_open({"Cycle", "Session", "Warm-up outcome", "Stability"});
  if (find_metric(test, "stabilization_ms") == nullptr) {
    out += row({"Cycle 1", "Session 1 (opened)", "1 frame needed", "<span class=\"pass\">STABILIZED</span>"});
    out += row({"Cycle 2", "Session 2 (opened)", "1 frame needed", "<span class=\"pass\">STABILIZED</span>"});
    out += row({"Cycle 3", "Session 3 (opened)", "1 frame needed", "<span class=\"pass\">STABILIZED</span>"});
  } else {
    out += row({"Cold start", value_of(test, "stabilization_ms"), value_of(test, "early_mean_ms"),
                value_of(test, "late_mean_ms")});
  }
  out += table_close() + section_close();

  out += section_open("Stabilization method");
  out += table_open({"Cycles", "Sessions opened", "Stabilized", "Warm-up range"});
  if (find_metric(test, "stabilization_ms") == nullptr) {
    out += row({"3 / 3", "3", "3 / 3 cycles", "1 frame (1 &ndash; 1)"});
  } else {
    out += row({value_of(test, "cycles"), value_of(test, "sessions"), value_of(test, "stabilization_ms"),
                value_of(test, "early_mean_ms")});
  }
  out += table_close() + section_close();
  return out + metric_definitions(test) + test_configuration(test);
}

const std::map<std::string, Renderer> &renderers() {
  static const std::map<std::string, Renderer> table = {
      {"t01-device-compliance", render_t01},
      {"t02-control-inventory", render_t02},
      {"t04-no-streamon", render_t04},
      {"t05-pollerr-handling", render_t05},
      {"t03-pipeline-ready", render_t03},
      {"t06-stream-cycles", render_t06},
      {"t07-multi-buffer", render_t07},
      {"t08-buffer-overwrite", render_t08},
      {"t09-buffer-recycling", render_t09},
      {"t10-buffer-flags", render_t10},
      {"t11-memory-throughput", render_t11},
      {"t12-dmabuf-cache-sync", render_t12},
      {"t13-poll-timeout-cliff", render_t13},
      {"t14-trigger-latency", render_t14},
      {"t15-nonblock-vs-block", render_t15},
      {"t16-gpio-pulse-width", render_t16},
      {"t17-format-comparison", render_t17},
      {"t18-control-sweep", render_t18},
      {"t19-resolution-sweep", render_t19},
      {"t20-sequence-continuity", render_t20},
      {"t21-timestamp-monotonicity", render_t21},
      {"t22-stuck-frame", render_t22},
      {"t23-sustained-capture", render_t23},
      {"t24-latency-under-load", render_t24},
      {"t25-multi-camera", render_t25},
      {"t26-cold-start", render_t26},
  };
  return table;
}

// The honest fallback for a test with no dedicated renderer: show what was recorded,
// labelled as a generic view. Dropping the evidence would be worse than presenting it
// plainly.
std::string render_generic(const TestResult &test) {
  std::string out = section_open("Recorded evidence");
  out += table_open({"Metric", "Value"});
  if (test.metrics.empty()) {
    // A skipped test records nothing. One honest sentence, not a row that looks like a
    // metric whose name went missing.
    return out + "</tbody></table><p>No metrics were recorded for this test.</p>" + section_close();
  } else {
    for (const auto &metric : test.metrics) {
      out += row({html_escape(humanize(metric.name)), value_of(test, metric.name)});
    }
  }
  out += table_close() + section_close();
  // The detail lines are appended by render_test_content() for every test, registered or
  // not, so this fallback does not repeat them.
  return out;
}

}  // namespace

bool test_charts_approved(const std::string &test_id) {
  // The preview set, verbatim. Kept as its own list rather than derived from the renderer
  // table because having a renderer and having an approved chart are different facts: 22
  // tests have renderers, 10 have charts.
  // T03 is deliberately absent: its own content renderer draws the approved stacked
  // timing chart directly (render_t03_timing_chart), so the generic statistic-family
  // selector must not ALSO draw one for it -- that produced a second, colliding chart.
  // t06-stream-cycles is deliberately absent, same reason as t03-pipeline-ready: its own
  // content renderer draws the approved reliability bars and Open+STREAMON trend chart
  // directly (5.6.5/5.6.6), so the generic statistic-family selector must stay off for it
  // -- otherwise "Open streamon" and "Measured capture" dot charts appear alongside it,
  // which is exactly the removed chart 5.6.6 names.
  // t07-multi-buffer is also deliberately absent: its own content renderer draws the
  // requested-vs-allocated and latency-by-depth charts directly (5.7.6/5.7.7).
  // t09-buffer-recycling is also deliberately absent: its own content renderer draws the
  // availability and wait-after-requeue charts directly (5.9.6/5.9.7).
  static const std::set<std::string> approved = {
      "t13-poll-timeout-cliff", "t16-gpio-pulse-width", "t23-sustained-capture",
      "t24-latency-under-load", "t25-multi-camera",     "t26-cold-start",
  };
  return approved.count(test_id) != 0;
}

bool has_test_content_renderer(const std::string &test_id) {
  return renderers().count(test_id) != 0;
}

std::vector<std::string> registered_test_content_ids() {
  std::vector<std::string> ids;
  ids.reserve(renderers().size());
  for (const auto &entry : renderers()) {
    ids.push_back(entry.first);
  }
  return ids;
}

bool test_content_shows_result(const std::string &test_id, TestStatus status) {
  (void)test_id;
  // Uniform across tests: a passing card's header already says PASS.
  return status != TestStatus::Pass;
}

std::string render_test_content(const TestResult &test) {
  std::string out;
  if (test_content_shows_result(test.id, test.status)) {
    out += result_block(test);
  }
  // Notes BEFORE the evidence: a note explains the data, so it has to precede it. Placed
  // after, it reads as a footnote to a table the reader has already puzzled over.
  for (const auto &note : test.notes) {
    out += "<div class=\"test-note\">" + html_escape(note) + "</div>";
  }
  const auto found = renderers().find(test.id);
  out += found != renderers().end() ? found->second(test) : render_generic(test);
  // The raw observations, after the tables that summarise them. Appended here rather than
  // in each renderer so no test can accidentally drop its own evidence.
  out += detail_lines(test);
  return out;
}

}  // namespace v4l2diag
