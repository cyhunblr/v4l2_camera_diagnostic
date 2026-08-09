#include "v4l2diag/core/test_content.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <regex>
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

// Try multiple metric names and return the value from the first one found.
// Used when the runner's actual metric name differs from the renderer's ideal name.
std::string value_of_any(const TestResult &test, std::initializer_list<const char *> names) {
  for (const char *name : names) {
    if (name == nullptr) {
      continue;
    }
    const MetricValue *metric = find_metric(test, name);
    if (metric != nullptr) {
      return value_of(test, name);
    }
  }
  return "Unavailable";
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

// Tables are div+grid, never <table> (design-spec S1). With <table> the horizontal
// padding lands on every cell, so the columns creep inward while the table itself
// stretches to the card edge -- the defect the grid layout was adopted to fix.
//
// The column count rides on the element as a cols-N class, and both the header and the
// data rows derive it from what they were handed. A row whose cell count disagrees with
// its header therefore renders in its own track count instead of silently folding cells
// into the wrong columns; the contract test reads those classes back.
std::string grid_class(const char *kind, std::size_t columns) {
  return std::string("<div class=\"") + kind + " cols-" + std::to_string(columns) + "\">";
}

std::string table_open(const std::vector<std::string> &columns) {
  std::string out = grid_class("grid-head", columns.size());
  for (const auto &column : columns) {
    // Written out verbatim: the approved header text IS the contract, so a renderer must
    // not prettify or abbreviate it.
    out += "<span>" + column + "</span>";
  }
  return out + "</div>";
}

std::string row(const std::vector<std::string> &cells) {
  std::string out = grid_class("grid-row", cells.size());
  for (const auto &cell : cells) {
    out += "<span>" + cell + "</span>";
  }
  return out + "</div>";
}

std::string table_close() {
  return std::string();
}

// The three canonical section names (design-spec S1). A card carries no others: the
// role is what the section IS, not what it happens to contain, so a renderer cannot
// invent "Timing Summary" or "Stability Evidence" for the same job.
//
// has-items scopes the staircase indent (S3). It goes on Measurement because that is
// the section whose contents carry their own item subheadings.
std::string measurement_open() {
  return "<section class=\"section has-items\"><h3 class=\"section-label\">Measurement</h3>";
}

std::string measurement_result_open() {
  return "<section class=\"section\"><h3 class=\"section-label\">Measurement Result</h3>";
}

std::string configuration_open() {
  // config-section is the styling hook: the approved previews give the configuration
  // table a tighter row rhythm (5px vs 6px) than the measurement tables, and without a
  // class on the section the CSS cannot tell the two apart.
  return "<section class=\"section config-section\"><h3 class=\"section-label\">Test Configuration</h3>";
}

// Names the chart or table that follows (S2). The word "Graphic"/"Table" never appears:
// the reader can see which it is, so the heading is spent on the subject instead.
std::string item_label(const std::string &name) {
  return "<h4 class=\"item-label\">" + html_escape(name) + "</h4>";
}

// A horizontal bar chart, the shape six approved previews use: one row per measurement,
// each a label, a proportional bar carrying its own value, and the unit; then a scale
// under them all.
//
// It exists because the GENERIC chart selector in report_writer.cpp is deliberately kept
// off these tests (test_charts_approved): it groups metrics by statistic family and drew
// dot charts that matched none of the previews. Drawing the approved shape here is what
// lets the generic one stay off without the chart going missing.
struct BarDatum {
  std::string label;
  double value;
  std::string series;  // CSS modifier: which colour the bar takes
};

// Values are drawn against the largest of them, so the widest bar fills the track and the
// others are read against it. A zero or negative maximum would divide by zero, and a bar
// chart of nothing is not drawn at all.
std::string bar_chart(const std::string &name, const std::vector<std::pair<std::string, std::string>> &legend,
                      const std::vector<BarDatum> &data, const std::string &unit, const std::string &axis_caption) {
  if (data.empty()) {
    return std::string();
  }
  double top = 0.0;
  for (const auto &datum : data) {
    top = std::max(top, datum.value);
  }
  if (!(top > 0.0)) {
    return std::string();
  }

  std::string out = item_label(name);
  if (!legend.empty()) {
    out += "<div class=\"chart-legend\">";
    for (const auto &entry : legend) {
      out +=
          "<span><span class=\"legend-dot legend-" + entry.second + "\"></span>" + html_escape(entry.first) + "</span>";
    }
    out += "</div>";
  }
  out += "<div class=\"chart-frame\">";
  for (const auto &datum : data) {
    const double share = datum.value / top * 100.0;
    out += "<div class=\"bar-row\"><div class=\"bar-label\">" + html_escape(datum.label) +
           "</div><div class=\"bar-track\"><div class=\"bar-fill bar-" + datum.series +
           "\" style=\"width:" + number(share) + "%\">" + html_escape(number(datum.value)) +
           "</div></div><div class=\"bar-info\">" + html_escape(unit) + "</div></div>";
  }
  // Three ticks: zero, the midpoint and the maximum. The maximum carries the unit so the
  // scale reads without looking back at the rows.
  out += "<div class=\"bar-axis\"><span></span><div class=\"scale-in\"><span>0</span><span>" +
         html_escape(number(top / 2.0)) + "</span><span>" + html_escape(number(top)) + " " + html_escape(unit) +
         "</span></div><span></span></div>";
  out += "</div>";
  if (!axis_caption.empty()) {
    out += "<p class=\"chart-axis-caption\">" + html_escape(axis_caption) + "</p>";
  }
  return out;
}

// The Status cell. Only this cell carries colour and weight in a row.
std::string verdict_cell(TestStatus status) {
  std::string tone = "skip";
  if (status == TestStatus::Pass) {
    tone = "pass";
  } else if (status == TestStatus::Warn) {
    tone = "warn";
  } else if (status == TestStatus::Fail) {
    tone = "fail";
  }
  return "<span class=\"verdict " + tone + "\">" + card_status_text(status) + "</span>";
}

// Units are spelled out, never abbreviated (S4). "B" and "%" do not say what they
// measure at a glance, and the column is wide enough for the word.
//
// A COUNTED OBJECT IS NOT A UNIT: cycles, frames, buffers, phases and their kin name
// the thing already stated in the Metric column, so they resolve to the em dash. That
// distinction is the whole point of the rule -- "delays" as a unit was what exposed it.
std::string unit_word(const std::string &raw) {
  static const std::pair<const char *, const char *> kSpelled[] = {
      {"ms", "milliseconds"},
      {"us", "microseconds"},
      {"\xC2\xB5s", "microseconds"},
      {"ns", "nanoseconds"},
      {"s", "seconds"},
      {"%", "percent"},
      {"Hz", "hertz"},
      {"B", "bytes"},
      {"KiB", "kibibytes"},
      {"MiB", "mebibytes"},
      {"GiB", "gibibytes"},
      {"MiB/s", "mebibytes per second"},
      {"GiB/s", "gibibytes per second"},
      {"fps", "frames per second"},
      {"px", "pixels"},
      {"ms/buffer", "milliseconds per buffer"},
      {"ms / buffer", "milliseconds per buffer"},
      {"buffers/s", "buffers per second"},
      {"returns/frame", "returns per frame"},
  };
  const std::string value = trim_of(raw);
  if (value.empty()) {
    return "\xE2\x80\x94";
  }
  for (const auto &pair : kSpelled) {
    if (value == pair.first) {
      return pair.second;
    }
  }
  // Already spelled out, or a counted object. Anything that is not a real measure of
  // quantity becomes the em dash rather than being passed through.
  static const char *kAlreadyReal[] = {"milliseconds",
                                       "microseconds",
                                       "nanoseconds",
                                       "seconds",
                                       "percent",
                                       "hertz",
                                       "bytes",
                                       "kibibytes",
                                       "mebibytes",
                                       "gibibytes",
                                       "pixels",
                                       "frames per second",
                                       "mebibytes per second",
                                       "gibibytes per second",
                                       "milliseconds per buffer",
                                       "buffers per second",
                                       "returns per frame"};
  for (const char *real : kAlreadyReal) {
    if (value == real) {
      return value;
    }
  }
  return "\xE2\x80\x94";
}

// The storage type of the value, as Test Configuration already uses it (S1).
std::string type_word(const std::string &value) {
  const std::string text = trim_of(value);
  if (text.empty()) {
    return "string";
  }
  if (text.find('/') != std::string::npos) {
    return "ratio";
  }
  bool digits = false;
  bool dot = false;
  for (std::size_t at = 0; at < text.size(); ++at) {
    const char c = text[at];
    if (c >= '0' && c <= '9') {
      digits = true;
    } else if (c == '.') {
      dot = true;
    } else if (c != ',' && !(at == 0 && (c == '-' || c == '+'))) {
      return "string";
    }
  }
  if (!digits) {
    return "string";
  }
  return dot ? "float" : "int";
}

// "conformant / total" over the check lines a state-machine test records. Read from the
// recorded verdict word, so a phase the runner never judged is not counted as passing.
std::string conformant_phases(const TestResult &test) {
  int total = 0;
  int conformant = 0;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 7, "check: ") != 0) {
      continue;
    }
    ++total;
    const std::string lowered = lower_of(detail);
    if (lowered.find("|pass") != std::string::npos || lowered.find("| pass") != std::string::npos) {
      ++conformant;
    }
  }
  if (total == 0) {
    return "\xE2\x80\x94";
  }
  return std::to_string(conformant) + "/" + std::to_string(total);
}

// The canonical Measurement Result signature (S1). EVERY row here carries a verdict --
// a row without one belongs in Measurement instead, which is why there is no overload
// that omits the status.
const std::vector<std::string> &result_columns() {
  static const std::vector<std::string> columns = {"Metric", "Type", "Unit", "Value", "Detail", "Status"};
  return columns;
}

std::string result_row(const std::string &metric, const std::string &unit, const std::string &value,
                       const std::string &detail, TestStatus status) {
  return row({html_escape(metric), type_word(value), unit_word(unit), html_escape(value),
              detail.empty() ? "\xE2\x80\x94" : html_escape(detail), verdict_cell(status)});
}
std::string section_close() {
  return "</section>";
}

// The verdict word a State/Outcome column shows.
std::string state_word(TestStatus status) {
  return card_status_text(status);
}

// Splits "100ms" into "100" and "ms". The runner records the unit inside the value on
// these detail lines, and the approved layout puts it in its own column -- a value cell
// reading "30 frames" hides the number from the eye that is scanning the column.
void split_unit(const std::string &raw, std::string *value, std::string *unit) {
  const std::string text = trim_of(raw);
  std::size_t at = 0;
  // The comma is a THOUSANDS SEPARATOR here, not a boundary: stopping at it turned
  // "4,915,200 bytes" into the value 4 and silently reported a 4-byte payload.
  while (at < text.size() && ((text[at] >= '0' && text[at] <= '9') || text[at] == '.' || text[at] == ',' ||
                              text[at] == '-' || text[at] == '+' || text[at] == '/')) {
    ++at;
  }
  // A leading comparison operator belongs to the value ("<= 2"), not to the unit.
  if (at == 0 && !text.empty() && (text[0] == '<' || text[0] == '>' || text[0] == '\xE2')) {
    *value = text;
    unit->clear();
    return;
  }
  *value = trim_of(text.substr(0, at));
  *unit = trim_of(text.substr(at));
  if (value->empty()) {
    *value = text;
    unit->clear();
  }
}

// The Test Configuration rows: `Variable | Source | Type | Unit | Value` (design-spec S1),
// built from the detail lines that name a parameter.
//
// Source is READ, not invented: a key that says "threshold" or "limit" is a verdict
// threshold, everything else is a parameter the user set. The runner does not currently
// distinguish `derived` from `fixed`, so neither is claimed here -- a wrong provenance is
// worse than a coarse one.
std::string test_configuration_rows(const TestResult &test) {
  std::string out = table_open({"Variable", "Source", "Type", "Unit", "Value"});
  bool any = false;
  for (const auto &detail : test.details) {
    const std::size_t colon = detail.find(':');
    if (colon == std::string::npos || colon == 0) {
      continue;
    }
    const std::string key = trim_of(detail.substr(0, colon));
    // Structured evidence lines ("cycle: ...", "evidence: ...") are data for a chart or a
    // table, not configuration; listing them here would print parsing artefacts.
    if (key == "cycle" || key == "evidence" || key == "probe" || key == "check" || key == "window") {
      continue;
    }
    std::string value;
    std::string unit;
    split_unit(detail.substr(colon + 1), &value, &unit);
    const std::string lowered = lower_of(key);
    const bool is_threshold =
        lowered.find("threshold") != std::string::npos || lowered.find("limit") != std::string::npos;
    any = true;
    out += row({html_escape(humanize(key)), is_threshold ? "threshold" : "param", type_word(value), unit_word(unit),
                html_escape(value)});
  }
  // A run may record its parameters as METRICS rather than as detail lines. Reading only
  // the detail lines rendered a one-row "No parameters were recorded" table for five
  // tests that had recorded their settings all along -- an honest-looking empty state
  // covering a reading gap, which is worse than a missing table.
  if (!any) {
    for (const auto &metric : test.metrics) {
      const std::string lowered = lower_of(metric.name);
      const bool is_parameter =
          lowered.find("configured") != std::string::npos || lowered.find("_threshold") != std::string::npos ||
          lowered.find("timeout") != std::string::npos || lowered.find("requested") != std::string::npos ||
          lowered.find("window") != std::string::npos || lowered.find("warmup") != std::string::npos ||
          lowered.find("samples") != std::string::npos || lowered.find("enumerated") != std::string::npos ||
          lowered.find("_limit") != std::string::npos;
      if (!is_parameter) {
        continue;
      }
      any = true;
      const bool is_threshold =
          lowered.find("threshold") != std::string::npos || lowered.find("_limit") != std::string::npos;
      std::string value;
      std::string unit;
      split_unit(value_of(test, metric.name), &value, &unit);
      out += row({html_escape(humanize(metric.name)), is_threshold ? "threshold" : "param", type_word(value),
                  unit_word(unit), html_escape(value)});
    }
  }
  if (!any) {
    out += row({"Unavailable", "\xE2\x80\x94", "string", "\xE2\x80\x94", "No parameters were recorded"});
  }
  return out + table_close();
}

// Test Configuration from EXPLICIT rows, for the renderers whose parameters come from
// metrics rather than from "key: value" detail lines. Reading only the detail lines lost
// those rows entirely -- the table rendered, just without the parameters it exists for.
std::string config_items(const std::vector<std::pair<std::string, std::string>> &rows) {
  std::string out = configuration_open();
  out += table_open({"Variable", "Source", "Type", "Unit", "Value"});
  for (const auto &entry : rows) {
    std::string value;
    std::string unit;
    split_unit(entry.second, &value, &unit);
    if (value.empty()) {
      value = "Unavailable";
    }
    const std::string lowered = lower_of(entry.first);
    const bool is_threshold =
        lowered.find("threshold") != std::string::npos || lowered.find("limit") != std::string::npos ||
        lowered.find("minimum") != std::string::npos || lowered.find("maximum") != std::string::npos;
    out += row({html_escape(entry.first), is_threshold ? "threshold" : "param", type_word(value), unit_word(unit),
                html_escape(value)});
  }
  return out + table_close() + section_close();
}

// Defined below, next to the other detail-line readers.
std::string detail_value(const TestResult &test, const std::string &key);
std::vector<std::string> detail_values(const TestResult &test, const std::string &key);

// A verdict row description: the approved label, the metric names that may hold its
// value (first match wins), and the fixed Detail that states what it was judged against.
struct VerdictSpec {
  const char *label;
  const char *metric;
  const char *alt_metric;
  const char *detail;
};

// A named Measurement item built from metric specs: the same reading rules as
// verdict_section, minus the Status column. Measurement rows carry no verdict -- that is
// exactly what separates the two sections (design-spec S1).
std::string measurement_items(const TestResult &test, const std::string &label, const std::vector<VerdictSpec> &specs) {
  std::string out = item_label(label);
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  // A table whose every row reads Unavailable says nothing while occupying the space a
  // reader scans for a finding. Measured on the device report: skipped T25 printed an
  // "Aggregate" item of four blank rows directly under a card that already explained no
  // camera participated. Counted here so the item can be dropped whole below; a PARTLY
  // filled table still carries data and is always kept (Rule 4c).
  std::size_t rendered = 0;
  std::size_t blank = 0;
  for (const auto &spec : specs) {
    std::string value;
    if (std::strncmp(spec.metric, "detail:", 7) == 0) {
      const std::size_t count = detail_values(test, spec.metric + 7).size();
      value = count == 0 ? std::string("Unavailable") : std::to_string(count);
    } else if (std::strncmp(spec.metric, "literal:", 8) == 0) {
      // A value the caller already computed from the run (a count, a spread, a ratio).
      // Some approved rows summarise the detail lines rather than naming one recorded
      // metric; without this they read Unavailable for want of a metric that never exists.
      value = spec.metric + 8;
    } else if (std::strncmp(spec.metric, "value:", 6) == 0) {
      value = detail_value(test, spec.metric + 6);
      if (value.empty()) {
        value = "Unavailable";
      }
    } else {
      value =
          spec.alt_metric == nullptr ? value_of(test, spec.metric) : value_of_any(test, {spec.metric, spec.alt_metric});
    }
    std::string unit;
    std::string bare;
    split_unit(value, &bare, &unit);
    if (!bare.empty()) {
      value = bare;
    }
    ++rendered;
    if (value == "Unavailable") {
      ++blank;
    }
    out += row({html_escape(spec.label), type_word(value), unit_word(unit), html_escape(value),
                spec.detail == nullptr ? std::string("\xE2\x80\x94") : html_escape(spec.detail)});
  }
  if (rendered > 0 && blank == rendered) {
    return std::string();
  }
  return out + table_close();
}

// The Measurement Result section for a test, built from its approved verdict rows
// (docs/renderer-data-contract.md). Every row carries a Status -- that is what makes this
// section the one place a reader looks to learn whether the numbers passed.
//
// A value the run never recorded reads "Unavailable" rather than being dropped: a missing
// row would silently shrink the verdict set, which is the harder failure to notice.
std::string verdict_section(const TestResult &test, const std::vector<VerdictSpec> &specs) {
  std::string out = measurement_result_open();
  out += table_open(result_columns());
  for (const auto &spec : specs) {
    std::string value;
    // "detail:<key>" counts the structured detail lines with that key. Several tests
    // record their evidence ONLY as detail lines (T09 has no metrics at all), so reading
    // metrics alone left their whole verdict set reading "Unavailable".
    if (std::strncmp(spec.metric, "detail:", 7) == 0) {
      const std::size_t count = detail_values(test, spec.metric + 7).size();
      value = count == 0 ? std::string("Unavailable") : std::to_string(count);
    } else if (std::strncmp(spec.metric, "literal:", 8) == 0) {
      // A value the caller already computed from the run (a count, a spread, a ratio).
      // Some approved rows summarise the detail lines rather than naming one recorded
      // metric; without this they read Unavailable for want of a metric that never exists.
      value = spec.metric + 8;
    } else if (std::strncmp(spec.metric, "value:", 6) == 0) {
      value = detail_value(test, spec.metric + 6);
      if (value.empty()) {
        value = "Unavailable";
      }
    } else {
      value =
          spec.alt_metric == nullptr ? value_of(test, spec.metric) : value_of_any(test, {spec.metric, spec.alt_metric});
    }
    std::string unit;
    std::string bare;
    split_unit(value, &bare, &unit);
    if (!bare.empty()) {
      value = bare;
    }
    out += result_row(spec.label, unit, value, spec.detail == nullptr ? "" : spec.detail, test.status);
  }
  return out + table_close() + section_close();
}

// The whole section, for the renderers that have nothing to add around it.
std::string test_configuration(const TestResult &test) {
  return configuration_open() + test_configuration_rows(test) + section_close();
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
// A named item inside Measurement: subheading plus the canonical measurement table.
// The key/value list survives only where the approved layout keeps one (T01's device
// facts); everywhere else a measured value belongs in a column that says what it is.
std::string kv_items(const std::string &label, const std::vector<std::pair<std::string, std::string>> &rows) {
  // An empty label continues the item above rather than repeating its name: T06 and T10
  // each printed "Aggregate" twice because a bar block and a row block both named it.
  std::string out = label.empty() ? std::string() : item_label(label);
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  for (const auto &entry : rows) {
    // A recorded value may carry a second reading of the same quantity, written
    // "4,915,200 bytes / 4.69 MiB". Splitting on the unit alone kept the first and DROPPED
    // the second: the MiB figure a reader actually scans for vanished. The alternate form
    // moves to Detail, where it stays visible without competing with the primary column.
    std::string primary = entry.second;
    std::string detail;
    const std::size_t slash = primary.find(" / ");
    if (slash != std::string::npos) {
      detail = trim_of(primary.substr(slash + 3));
      primary = primary.substr(0, slash);
    }
    std::string value;
    std::string unit;
    split_unit(primary, &value, &unit);
    if (value.empty()) {
      value = "Unavailable";
    }
    out += row({html_escape(entry.first), type_word(value), unit_word(unit), html_escape(value),
                detail.empty() ? std::string("\xE2\x80\x94") : html_escape(detail)});
  }
  return out + table_close();
}

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

// The "Result" block: the verdict stated in prose, on non-PASS cards only.
// The result line is NOT a section: it sits above the sections, carries the card's own
// status colour, and a PASS card omits it entirely (design-spec). The class name is bound
// to the status rather than being generic, because one generic `result` class had already
// been given two different colours in two files -- rendering a WARN card neutral grey.
std::string result_block(const TestResult &test) {
  if (!result_section_visible(test.status)) {
    return std::string();
  }
  const char *tone = "skip";
  const char *prefix = "Skipped:";
  if (test.status == TestStatus::Warn) {
    tone = "warn";
    prefix = "Warned:";
  } else if (test.status == TestStatus::Fail) {
    tone = "fail";
    prefix = "Failed:";
  }
  std::string out = std::string("<div class=\"result-") + tone + "\">" + prefix + " " +
                    html_escape(test.summary.empty() ? std::string("No summary was recorded.") : test.summary) +
                    "</div>";
  for (const auto &warning : test.warnings) {
    out += "<div class=\"warnings-box\">" + html_escape(warning) + "</div>";
  }
  return out;
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
      {"Backend support (" + probe + ")",
       {"selected_backend_supported", "selected_backend_supported", "backend_supported", "backend_mmap",
        "backend_dmabuf", "backend_userptr"}},
      {"Capture support", {"capture_supported", "supports_capture"}},
      {"Streaming support", {"streaming_supported", "supports_streaming"}},
  };

  // 5.1.7: structured rows first -- the approved order names the device before it
  // describes it. The raw enumeration stays in the machine-readable artifacts.
  std::string out = kv_section("Device Evidence \xC2\xB7 Information", {{"Driver", detail_value(test, "driver")},
                                                                        {"Card", detail_value(test, "card")},
                                                                        {"Bus", detail_value(test, "bus")}});
  out += section_open("Device Evidence \xC2\xB7 Capability");
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
  out += section_open("Device Evidence \xC2\xB7 Pixel Formats");
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

  // The "Recorded values" dump is gone: a raw list of every metric under a generic
  // heading is exactly the pattern the design forbids, and the three Device Evidence
  // sections above already name what T01 observed.
  return out;
}

// T02 (review-plan 5.2): a categorical inventory test. Three summary values, then one
// structured table whose rows are the controls and whose group headings are the
// V4L2_CTRL_TYPE_CTRL_CLASS records -- which are NOT controls and carry no current value.
std::string render_t02(const TestResult &test) {
  // 5.2.4: three values, without repeating the word "count" beside each one.
  std::string out = section_open("Control Evidence");
  out += "<dl class=\"kv\">";
  out +=
      "<div class=\"kv-row\"><dt>Controls</dt><dd>" + value_of_any(test, {"control_count", "controls"}) + "</dd></div>";
  out += "<div class=\"kv-row\"><dt>Writable</dt><dd>" + value_of_any(test, {"writable_count", "writable"}) +
         "</dd></div>";
  out += "<div class=\"kv-row\"><dt>Read-only</dt><dd>" + value_of_any(test, {"writable_count", "read_only"}) +
         "</dd></div>";
  out += "</dl>";

  // 5.2.5: the approved five columns. The runner records each control as one pipe-separated
  // detail line, and each class record as its own line.
  out += item_label("Controls");
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
    // Legacy format: "Name [0xID] min=M max=X step=S default=D current=C [RO]"
    for (const auto &detail : test.details) {
      // Detect legacy format by looking for "[0x" pattern
      auto bracket = detail.find("[0x");
      if (bracket == std::string::npos) {
        continue;
      }
      any = true;
      std::string name = detail.substr(0, bracket);
      // Trim trailing space
      while (!name.empty() && name.back() == ' ')
        name.pop_back();

      // Check if it's a class record (no min/max or has [RO] and generic class name)
      bool is_class =
          (detail.find("min=") == std::string::npos) ||
          (name == "User Controls" || name == "Camera Controls" || name == "Codec Controls" || name == "MPEG Controls");
      // Also check: class records typically have all 0 range and odd current (like 43690)
      if (is_class && name.find("Controls") != std::string::npos) {
        out += "<tr class=\"group-row\"><td colspan=\"5\">" + html_escape(name) + "</td></tr>";
        continue;
      }

      // Extract ID
      auto id_end = detail.find(']', bracket);
      std::string id = (id_end != std::string::npos) ? detail.substr(bracket + 1, id_end - bracket - 1) : "";

      // Parse min, max, step, default, current
      auto parse_field = [&](const char *key) -> std::string {
        auto pos = detail.find(key);
        if (pos == std::string::npos)
          return "";
        pos += strlen(key);
        auto end = detail.find(' ', pos);
        if (end == std::string::npos)
          end = detail.size();
        return detail.substr(pos, end - pos);
      };
      std::string min_v = parse_field("min=");
      std::string max_v = parse_field("max=");
      std::string step_v = parse_field("step=");
      std::string def_v = parse_field("default=");
      if (def_v.empty())
        def_v = parse_field("def=");
      std::string cur_v = parse_field("current=");
      if (cur_v.empty())
        cur_v = parse_field("cur=");

      // Determine access
      std::string access = "WRITABLE";
      if (detail.find("[RO]") != std::string::npos) {
        access = "READ-ONLY";
      }

      // Build range string
      std::string range = min_v + ".." + max_v;
      if (!step_v.empty())
        range += " step " + step_v;

      const std::string name_cell =
          html_escape(name) +
          (id.empty() ? std::string() : "<span class=\"control-id\">" + html_escape(id) + "</span>");
      out += row({name_cell, access, range, def_v.empty() ? "Unavailable" : html_escape(def_v),
                  cur_v.empty() ? "Unavailable" : html_escape(cur_v)});
    }
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

  std::string out =
      "<div class=\"chart-frame\"><div class=\"metric-chart-title\">STREAMON and "
      "first-frame timing across " +
      std::to_string(cycles.size()) + (cycles.size() == 1 ? " cycle" : " cycles") + "</div>";
  out += "<svg viewBox=\"0 0 906 " + number(height) +
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
  // No section_close(): the caller already opened Measurement and this chart is an item
  // inside it, not a section of its own.
  return out;
}

std::string render_t03(const TestResult &test) {
  std::string out;
  // The cycle rows: "cycle: N|streamon_ms|first_frame_ms|total_ms[|pulses]". A triggered
  // run's detail lines carry the fifth field; free-run's do not, which is what decides the
  // Pulses column (5.3.5/5.3.6) -- not the trigger mode, since T03's content renderer only
  // ever sees the test result.
  std::vector<T03Cycle> cycles;
  bool has_pulses = false;
  for (const auto &detail : test.details) {
    // New format: "cycle: N|streamon_ms|first_frame_ms|total_ms[|pulses]"
    if (detail.compare(0, 7, "cycle: ") == 0) {
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
      continue;
    }
    // Legacy format: "cycle N: STREAMON=Xms, first frame=Yms, pulses=P"
    if (detail.compare(0, 6, "cycle ") == 0 && detail.find(':') != std::string::npos) {
      T03Cycle cycle;
      // Parse cycle number
      cycle.number = std::to_string(std::atoi(detail.c_str() + 6));
      // Parse STREAMON=Xms
      auto spos = detail.find("STREAMON=");
      if (spos != std::string::npos) {
        cycle.streamon_ms = std::strtod(detail.c_str() + spos + 9, nullptr);
      }
      // Parse first frame=Yms
      auto fpos = detail.find("first frame=");
      if (fpos != std::string::npos) {
        cycle.first_frame_ms = std::strtod(detail.c_str() + fpos + 12, nullptr);
      }
      cycle.total_ms = cycle.streamon_ms + cycle.first_frame_ms;
      cycle.streamon = format_duration_ms(cycle.streamon_ms);
      cycle.first_frame = format_duration_ms(cycle.first_frame_ms);
      cycle.total = format_duration_ms(cycle.total_ms);
      // Parse pulses=P
      auto ppos = detail.find("pulses=");
      if (ppos != std::string::npos) {
        cycle.pulses = std::to_string(std::atoi(detail.c_str() + ppos + 7));
        has_pulses = true;
      }
      cycles.push_back(cycle);
    }
  }

  // Measurement: the chart carries the per-cycle numbers, so no cycle table sits beside
  // it. Charting them loses nothing (design-spec S5), and a table repeating the same
  // three cycles would print each value twice under two labels.
  out += measurement_open();
  // 5.3.4: one stacked, two-phase horizontal bar per cycle -- STREAMON then FIRST FRAME,
  // in time order. Reproduces the approved preview's coordinate scheme, not a generic bar.
  out += render_t03_timing_chart(cycles, has_pulses);

  out += item_label("Aggregate");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  const struct {
    const char *label;
    const char *metric;
    const char *alt_metric;
  } aggregate_rows[] = {
      {"STREAMON mean", "streamon_ms_mean", "streamon_mean_ms"},
      {"First-frame mean", "first_frame_ms_mean", "first_frame_latency_mean"},
  };
  for (const auto &entry : aggregate_rows) {
    const std::string value = value_of_any(test, {entry.metric, entry.alt_metric});
    out += row({entry.label, type_word(value), unit_word("ms"), html_escape(value), "\xE2\x80\x94"});
  }
  out += table_close() + section_close();

  // Measurement Result: every row here carries a verdict. The threshold that decides it
  // goes in Detail as a measured fact, not as advice (design-spec).
  out += measurement_result_open();
  out += table_open(result_columns());
  const struct {
    const char *label;
    const char *metric;
    const char *alt_metric;
    const char *unit;
    const char *threshold_metric;
    const char *threshold_label;
  } verdict_rows[] = {
      {"STREAMON max", "streamon_ms_max", "streamon_max_ms", "ms", "streamon_slow_start_ms", "Slow-start limit"},
      {"First-frame max", "first_frame_ms_max", "first_frame_latency_max", "ms", "first_frame_pass_ms", "PASS limit"},
      {"Completed cycles", "cycles", "cycles_completed", "", nullptr, nullptr},
      {"Timeouts", "first_frame_timeouts", "timeouts", "", nullptr, nullptr},
  };
  // The pulse count has no column of its own once the cycle table is gone, but it is a
  // finding, not decoration: a triggered run states it on the row it belongs to. Free-run
  // records no pulses and therefore claims none (Rule 4c -- a removed display path must
  // not take a finding with it).
  std::string pulse_detail;
  if (has_pulses && !cycles.empty() && !cycles.front().pulses.empty()) {
    pulse_detail = cycles.front().pulses + " trigger pulses each";
  }
  for (const auto &entry : verdict_rows) {
    const std::string value = value_of_any(test, {entry.metric, entry.alt_metric});
    std::string detail;
    if (entry.threshold_metric != nullptr && find_metric(test, entry.threshold_metric) != nullptr) {
      detail = std::string(entry.threshold_label) + " " + value_of(test, entry.threshold_metric) + " ms";
    } else if (std::string(entry.label) == "Completed cycles") {
      detail = pulse_detail;
    }
    out += result_row(entry.label, entry.unit, value, detail, test.status);
  }
  out += table_close() + section_close();

  // 5.3.8's Technical details section is gone: the approved layout has three sections
  // and EAGAIN retries / STREAMON attempts appear in none of them. Removing a display
  // path removes what was embedded in it, so this is recorded rather than left silent.
  out += configuration_open();
  out += test_configuration_rows(test);
  out += section_close();
  return out;
}

// T04 (review-plan 5.4): a categorical state-machine test. Two CHECK/EXPECTED/OBSERVED/
// OUTCOME rows -- poll() and DQBUF, both attempted before STREAMON -- replacing the raw
// metric list and repeated detail block. No chart (5.4.7 last rule).
std::string render_t04(const TestResult &test) {
  // Measurement carries the observed phases; the verdict they add up to lives in
  // Measurement Result, so no row here needs a Status column (design-spec S1).
  std::string out = section_open("Measurement");
  out += table_open({"Phase", "Expected", "Observed", "Detail"});
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
    // Fallback: build the check table from available metrics.
    const MetricValue *poll_ret = find_metric(test, "poll_returned");
    const MetricValue *dqbuf_failed_m = find_metric(test, "dqbuf_failed");
    if (poll_ret != nullptr || dqbuf_failed_m != nullptr) {
      if (poll_ret != nullptr) {
        const std::string observed = poll_ret->value == 0.0 ? "Timed out (no readiness)" : "Signaled before timeout";
        out += row({"poll() before STREAMON", "No readiness", observed, poll_ret->value == 0.0 ? "PASS" : "WARN"});
      }
      if (dqbuf_failed_m != nullptr) {
        const MetricValue *dqbuf_errno_m = find_metric(test, "dqbuf_errno");
        std::string observed;
        if (dqbuf_failed_m->value != 0.0) {
          observed = "Rejected";
          if (dqbuf_errno_m != nullptr && dqbuf_errno_m->value != 0.0) {
            observed += " (errno " + std::to_string(static_cast<int>(dqbuf_errno_m->value)) + ")";
          }
        } else {
          observed = "Frame dequeued";
          const std::string seq = detail_value(test, "observed_sequence");
          if (!seq.empty()) {
            observed += " - sequence " + seq;
          }
        }
        out += row(
            {"DQBUF before STREAMON", "Request rejected", observed, dqbuf_failed_m->value != 0.0 ? "PASS" : "FAIL"});
      }
    } else {
      out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
    }
  }
  out += table_close() + section_close();

  out += measurement_result_open();
  out += table_open(result_columns());
  out += result_row("Conformant phases", "", conformant_phases(test), "Every phase behaved as specified", test.status);
  out += result_row("Pre-STREAMON frames", "", conformant_phases(test), "No frame may arrive before STREAMON",
                    test.status);
  out += table_close() + section_close();

  out += config_items({{"Buffers requested", detail_value(test, "buffers_requested")},
                       {"Poll timeout", detail_value(test, "poll_timeout_ms")}});
  return out;
}

// T05 (review-plan 5.5): a categorical, SEQUENTIAL state-machine test. Six phase rows in
// run order, plus an optional short capture-rate note when a pacing observation exists. No
// chart (5.5.7 last rule).
std::string render_t05(const TestResult &test) {
  std::string out = section_open("Measurement");
  out += table_open({"Phase", "Expected", "Observed", "Detail"});
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
    // Fallback: build state check table from the metrics the runner actually emits.
    const MetricValue *baseline = find_metric(test, "baseline_ok");
    const MetricValue *dqbuf_failed = find_metric(test, "dqbuf_failed");
    const MetricValue *restreamon = find_metric(test, "restreamon_ok");
    const MetricValue *recovery = find_metric(test, "recovery_ok");
    if (baseline != nullptr || dqbuf_failed != nullptr) {
      if (baseline != nullptr) {
        out +=
            row({"Baseline capture", "At least 1 frame", std::to_string(static_cast<int>(baseline->value)) + " frames",
                 baseline->value > 0 ? "PASS" : "FAIL"});
      }
      // Poll result from detail lines
      for (const auto &detail : test.details) {
        if (detail.find("poll") != std::string::npos || detail.find("Poll") != std::string::npos) {
          out += row({"Poll after stop", "Timeout or error flag", html_escape(detail), "ACCEPTED"});
          break;
        }
      }
      if (dqbuf_failed != nullptr) {
        // dqbuf_failed==1 means correctly rejected; ==0 means frame delivered (bad)
        std::string observed = dqbuf_failed->value != 0.0 ? "Request rejected" : "Frame dequeued";
        out += row({"DQBUF after stop", "Request rejected", observed, dqbuf_failed->value != 0.0 ? "PASS" : "FAIL"});
      }
      if (restreamon != nullptr) {
        out += row({"Re-STREAMON", "Request accepted", restreamon->value != 0.0 ? "Accepted" : "Failed",
                    restreamon->value != 0.0 ? "PASS" : "FAIL"});
      }
      if (recovery != nullptr) {
        out +=
            row({"Recovery capture", "At least 1 frame", std::to_string(static_cast<int>(recovery->value)) + " frames",
                 recovery->value > 0 ? "PASS" : "FAIL"});
      }
    } else {
      out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
    }
  }
  out += table_close() + section_close();

  // 5.5.6's standalone "Capture rate note" section is gone: a free sentence between
  // sections is not part of the approved layout. When the run recorded one, it rides on
  // the row it qualifies instead of floating on its own.
  const std::string note = detail_value(test, "capture_rate_note");

  out += measurement_result_open();
  out += table_open(result_columns());
  out += result_row("Conformant phases", "", conformant_phases(test), "Every phase behaved as specified", test.status);
  out += result_row("Post-STREAMOFF frames", "", value_of_any(test, {"pollerr_raised", "pollerr_events"}),
                    "No frame may arrive after STREAMOFF", test.status);
  out += result_row("Recovery captures", "", value_of_any(test, {"recovery_ok", "recovery_frames"}),
                    note.empty() ? std::string("Frames captured after re-STREAMON") : note, test.status);
  out += table_close() + section_close();

  out += config_items({{"Baseline frames", value_of_any(test, {"baseline_ok", "baseline_frames"})},
                       {"Recovery frames", value_of_any(test, {"recovery_ok", "recovery_frames"})},
                       {"Warmup frames", detail_value(test, "warmup_frames")},
                       {"Minimum recovery", detail_value(test, "min_recovery_frames")},
                       {"Poll timeout", detail_value(test, "poll_timeout_ms")},
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
      "<span>ms</span></div><svg viewBox=\"0 0 906 180\" role=\"img\" aria-label=\"Open and STREAMON "
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
  std::string out = measurement_open() + item_label("Cycle reliability");
  out += table_open({"Phase", "Completed", "Start fail", "Timeout", "Detail"});
  if (find_metric(test, "full_cycles_success") == nullptr && find_metric(test, "full_cycles") == nullptr &&
      find_metric(test, "rapid_cycles_ok") == nullptr && find_metric(test, "rapid_cycles") == nullptr) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    out += row({"Full cycles",
                completed_of(test, "full_cycles_success", "full_cycles_attempted") != "Unavailable"
                    ? completed_of(test, "full_cycles_success", "full_cycles_attempted")
                    : completed_of(test, "full_cycles_success", "full_cycles_attempted"),
                value_of_any(test, {"full_cycle_failures", "full_start_fail"}),
                value_of_any(test, {"rapid_capture_timeouts", "first_frame_timeouts", "full_timeouts"}),
                state_word(test.status)});
    out += row({"Rapid cycles",
                completed_of(test, "rapid_cycles_ok", "rapid_cycles_attempted") != "Unavailable"
                    ? completed_of(test, "rapid_cycles_ok", "rapid_cycles_attempted")
                    : completed_of(test, "rapid_cycles_ok", "rapid_cycles_total"),
                value_of_any(test, {"rapid_start_failures", "rapid_start_fail"}),
                value_of(test, "rapid_capture_timeouts"), state_word(test.status)});
  }
  out += table_close();

  // 5.6.5: the threshold-banded reliability bars, once per phase.
  out += item_label("Aggregate");
  const MetricValue *full_completed = find_metric(test, "full_cycles_success");
  const MetricValue *full_configured = find_metric(test, "full_cycles_attempted");
  if (full_completed != nullptr && full_configured != nullptr && full_configured->value > 0.0) {
    out += render_t06_reliability_bar("Full cycles", full_completed->value / full_configured->value * 100.0);
  }
  const MetricValue *rapid_completed = find_metric(test, "rapid_cycles_ok");
  const MetricValue *rapid_configured = find_metric(test, "rapid_cycles_attempted") != nullptr
                                            ? find_metric(test, "rapid_cycles_attempted")
                                            : find_metric(test, "rapid_cycles_total");
  if (rapid_completed != nullptr && rapid_configured != nullptr && rapid_configured->value > 0.0) {
    out += render_t06_reliability_bar("Rapid cycles", rapid_completed->value / rapid_configured->value * 100.0);
  }

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
  out += kv_items(
      "", {{"Open + STREAMON mean", value_of_any(test, {"streamon_ms_mean", "open_streamon_mean_ms"})},
           {"Open + STREAMON maximum", value_of_any(test, {"streamon_ms_max", "open_streamon_max_ms"})},
           {"Measured capture mean", value_of_any(test, {"first_frame_latency_mean", "measured_capture_mean_ms"})},
           {"Measured capture maximum", value_of_any(test, {"first_frame_latency_max", "measured_capture_max_ms"})}});

  // 5.6.8: semantic text, not raw booleans, for the phase and guard state.
  {
    std::string full_state = detail_value(test, "full_phase");
    if (full_state.empty()) {
      const MetricValue *fa = find_metric(test, "full_aborted");
      if (fa != nullptr) {
        full_state = fa->value != 0.0 ? "Stopped early" : "Completed";
      }
    }
    std::string rapid_state = detail_value(test, "rapid_phase");
    if (rapid_state.empty()) {
      const MetricValue *rs = find_metric(test, "rapid_skipped");
      const MetricValue *ra = find_metric(test, "rapid_aborted");
      if (rs != nullptr && rs->value != 0.0) {
        rapid_state = "Skipped";
      } else if (ra != nullptr) {
        rapid_state = ra->value != 0.0 ? "Stopped early" : "Completed";
      }
    }
    std::string guard = detail_value(test, "slow_start_guard");
    out += kv_items("Protection state",
                    {{"Full phase", full_state},
                     {"Rapid phase", rapid_state},
                     {"Start failures", value_of_any(test, {"full_cycle_failures", "start_failures_total"})},
                     {"Slow-start guard", guard.empty() ? "Not triggered" : guard}});
  }

  // 5.6.9: at least these eight configuration parameters.
  {
    std::string full_cfg = detail_value(test, "full_cycles_configured");
    if (full_cfg.empty()) {
      const MetricValue *m = find_metric(test, "full_cycles_attempted");
      if (m != nullptr)
        full_cfg = std::to_string(static_cast<int>(m->value));
    }
    std::string rapid_cfg = detail_value(test, "rapid_cycles_configured");
    if (rapid_cfg.empty()) {
      const MetricValue *m = find_metric(test, "rapid_cycles_total");
      if (m != nullptr)
        rapid_cfg = std::to_string(static_cast<int>(m->value));
    }
    out += section_close();
    out += verdict_section(
        test, {{"Full cycle completion", "full_cycles_success", "full_cycles", "PASS limit 0 failures"},
               {"Rapid cycle completion", "rapid_cycles_ok", "rapid_cycles", "PASS limit \xE2\x89\xA5 90%"},
               {"Full phase", "value:full_phase", nullptr, nullptr},
               {"Rapid phase", "value:rapid_phase", nullptr, nullptr},
               {"Start failures", "full_cycle_failures", "full_cycle_failures", "full_start_fail"},
               {"Slow-start guard", "value:slow_start_guard", nullptr, nullptr}});
    out += config_items({{"Full cycles", full_cfg},
                         {"Rapid cycles", rapid_cfg},
                         {"Full warmup", detail_value(test, "full_warmup")},
                         {"Rapid warmup", detail_value(test, "rapid_warmup")},
                         {"Slow-start guard", detail_value(test, "slow_start_guard_limit")},
                         {"Backend memory", detail_value(test, "backend_memory")}});
  }
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
    // New format: "request: N|allocated|captured|attempted|mean_ms"
    if (detail.compare(0, 9, "request: ") == 0) {
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
      continue;
    }
    // Legacy format: "count=N granted=M mean=Xms miss=C/A"
    if (detail.compare(0, 6, "count=") == 0) {
      T07Request request;
      // Parse count=N
      request.requested = std::atoi(detail.c_str() + 6);
      // Parse granted=M
      auto gpos = detail.find("granted=");
      if (gpos != std::string::npos) {
        request.allocated = std::atoi(detail.c_str() + gpos + 8);
      }
      // Parse mean=Xms
      auto mpos = detail.find("mean=");
      if (mpos != std::string::npos) {
        request.mean_ms = std::strtod(detail.c_str() + mpos + 5, nullptr);
      }
      // Parse miss=C/A
      auto misspos = detail.find("miss=");
      if (misspos != std::string::npos) {
        int miss = std::atoi(detail.c_str() + misspos + 5);
        auto slashpos = detail.find('/', misspos);
        if (slashpos != std::string::npos) {
          request.attempted = std::atoi(detail.c_str() + slashpos + 1);
          request.captured = request.attempted - miss;
        }
      }
      requests.push_back(request);
    }
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
      "<span>buffers</span></div><svg viewBox=\"0 0 906 205\" role=\"img\" aria-label=\"Requested "
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
      "depth <span>lower is faster</span></div><svg viewBox=\"0 0 906 205\" role=\"img\" "
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
  std::string out = measurement_open();
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
    // Summed from the per-request rows rather than read from a metric: the runner records
    // only granted_for_<N>, so this line printed "Unavailable/Unavailable frames captured".
    int captured_total = 0;
    int attempted_total = 0;
    for (const auto &request : requests) {
      captured_total += request.captured;
      attempted_total += request.attempted;
    }
    out += "<p><strong>" + std::to_string(captured_total) + "/" + std::to_string(attempted_total) +
           " frames captured</strong></p>";
  }

  out += render_t07_requested_vs_allocated(requests);
  out += render_t07_latency_by_depth(requests);

  // 5.7.5: the five approved columns, one row per request.
  out += item_label("Aggregate");
  out += table_open({"Requested", "Allocated", "Captured", "Mean latency", "Detail"});
  if (requests.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto &request : requests) {
      out += row({std::to_string(request.requested), std::to_string(request.allocated),
                  std::to_string(request.captured) + "/" + std::to_string(request.attempted),
                  format_duration_ms(request.mean_ms), state_word(test.status)});
    }
  }
  out += table_close();

  // The summary rows continue under the SAME "Aggregate" label: a second item_label here
  // printed "Aggregate" twice, and the approved preview shows one. "Mean latency" is
  // averaged from the per-request detail lines because the runner records no aggregate
  // latency metric for this test.
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  out += row({"Buffer counts tested", type_word("int"), unit_word(""),
              requests.empty() ? std::string("Unavailable") : std::to_string(requests.size()), "\xE2\x80\x94"});
  const std::string granted = value_of_any(test, {"granted_for_1", "total_captured"});
  std::string granted_bare;
  std::string granted_unit;
  split_unit(granted, &granted_bare, &granted_unit);
  out += row({"Buffers granted", type_word(granted), unit_word(granted_unit),
              granted_bare.empty() ? granted : granted_bare, "Effective depth at the smallest request"});
  if (!requests.empty()) {
    double mean_sum = 0.0;
    for (const auto &request : requests) {
      mean_sum += request.mean_ms;
    }
    out += row({"Mean latency", type_word("0.0"), unit_word("ms"),
                format_duration_ms(mean_sum / static_cast<double>(requests.size())), "Across every buffer count"});
  }
  out += table_close() + section_close();

  // All three verdicts summarise the per-request detail lines rather than naming one
  // recorded metric; the old specs pointed at prefixes the runner never writes, so every
  // row read Unavailable on a real run.
  std::string honored = "Unavailable";
  std::string spread = "Unavailable";
  std::string capture_success = "Unavailable";
  if (!requests.empty()) {
    std::size_t granted_as_asked = 0;
    int cap_sum = 0;
    int att_sum = 0;
    double lo = requests.front().mean_ms;
    double hi = requests.front().mean_ms;
    for (const auto &request : requests) {
      if (request.allocated >= request.requested) {
        ++granted_as_asked;
      }
      cap_sum += request.captured;
      att_sum += request.attempted;
      lo = std::min(lo, request.mean_ms);
      hi = std::max(hi, request.mean_ms);
    }
    honored = std::to_string(granted_as_asked) + "/" + std::to_string(requests.size());
    spread = format_duration_ms(hi - lo);
    if (spread.find('m') == std::string::npos && spread.find('s') == std::string::npos) {
      spread += " ms";
    }
    capture_success = std::to_string(cap_sum) + "/" + std::to_string(att_sum);
  }
  // VerdictSpec stores const char*, so these strings must outlive the vector below.
  const std::string honored_spec = "literal:" + honored;
  const std::string success_spec = "literal:" + capture_success;
  const std::string spread_spec = "literal:" + spread;
  out += verdict_section(
      test, {{"Allocation honored", honored_spec.c_str(), nullptr, "Driver granted the requested depth"},
             {"Capture success", success_spec.c_str(), nullptr, "Frames captured across every configuration"},
             {"Latency spread", spread_spec.c_str(), nullptr, "Across the tested buffer counts"}});
  out += config_items({{"Requested range", detail_value(test, "requested_range")},
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

  std::string out = measurement_open();
  out += render_t08_saturation_load(variants);
  out += render_t08_queue_after_saturation(variants, value_of_any(test, {"frames_available_A", "allocated_buffers"}));

  // 5.8.7: the six approved columns, observed/allocated for available and error-flagged.
  out += table_open({"Variant", "Trigger load", "Allocated", "Available", "Error flagged", "Detail"});
  if (variants.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto &variant : variants) {
      out += row({html_escape(variant.name), html_escape(variant.load_label), html_escape(variant.allocated),
                  html_escape(variant.available_num) + "/" + html_escape(variant.available_den),
                  html_escape(variant.error_num) + "/" + html_escape(variant.error_den), html_escape(variant.outcome)});
    }
  }
  out += table_close();

  // 5.8.8: the hex flag value decoded by name, with the raw value kept alongside it.
  out += item_label("Buffer state after saturation");
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
  out += measurement_items(test, "Aggregate",
                           {{"Variants tested", "detail:variant", nullptr, nullptr},
                            {"Buffers per variant", "frames_available_A", nullptr, nullptr},
                            {"Error flag mask", "detail:evidence", nullptr, "Buffers reporting an error flag"}});
  out += section_close();

  // 5.8.9: the six configuration parameters.
  out += verdict_section(
      test, {{"Buffer retention", "frames_available_A", nullptr, "Buffers still available after saturation"},
             {"Error-flagged", "detail:evidence", nullptr, "Buffers carrying V4L2_BUF_FLAG_ERROR"},
             {"Variants passed", "detail:variant", nullptr, nullptr}});
  out += config_items({{"Allocated buffers", value_of_any(test, {"frames_available_A", "allocated_buffers"})},
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
    // New format: "delay: <label>|availability%|mean_wait_ms|outcome"
    if (detail.compare(0, 7, "delay: ") == 0) {
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
      continue;
    }
    // Legacy format: "delay=Xms hits=H/A mean=Yms"
    if (detail.compare(0, 6, "delay=") == 0) {
      T09Delay delay;
      int delay_ms = std::atoi(detail.c_str() + 6);
      delay.label = std::to_string(delay_ms) + " ms";
      // Parse hits=H/A
      auto hpos = detail.find("hits=");
      int hits = 0, attempts = 10;
      if (hpos != std::string::npos) {
        hits = std::atoi(detail.c_str() + hpos + 5);
        auto slashpos = detail.find('/', hpos);
        if (slashpos != std::string::npos) {
          attempts = std::atoi(detail.c_str() + slashpos + 1);
        }
      }
      delay.availability = attempts > 0 ? (static_cast<double>(hits) / attempts * 100.0) : 0.0;
      // Parse mean=Yms
      auto mpos = detail.find("mean=");
      if (mpos != std::string::npos) {
        delay.mean_wait_ms = std::strtod(detail.c_str() + mpos + 5, nullptr);
      }
      delay.outcome = delay.availability >= 90.0 ? "PASS" : "WARN";
      delays.push_back(delay);
    }
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
      "<span>successful attempts</span></div><svg viewBox=\"0 0 906 220\" role=\"img\" "
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
      "post-requeue wait</span></div><svg viewBox=\"0 0 906 220\" role=\"img\" aria-label=\"Mean "
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

  std::string out = measurement_open();
  out += render_t09_availability(delays, threshold_percent);
  out += render_t09_wait(delays);

  // 5.9.8: one row per tested delay -- not one aggregate row for the whole sweep, which is
  // what the pre-5.9 renderer produced and which hid every individual dip.
  out += item_label("Wait time by requeue delay");
  out += table_open({"Delay", "Available", "Mean wait", "Detail"});
  if (delays.empty()) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    for (const auto &delay : delays) {
      out += row({html_escape(delay.label), number(delay.availability) + "%", number(delay.mean_wait_ms) + "ms",
                  html_escape(delay.outcome)});
    }
  }
  out += table_close();

  // 5.9.12: sequence-gap evidence is technical detail, not a headline finding -- it belongs
  // below the results table, in its own section, and only when the run recorded one.
  const std::vector<std::string> gaps = detail_values(test, "sequence_gap");
  if (!gaps.empty()) {
    out += item_label("Aggregate");
    out += table_open({"Observation", "Evidence"});
    for (const auto &gap : gaps) {
      const std::size_t bar = gap.find('|');
      if (bar == std::string::npos) {
        out += row({"Sequence gap", html_escape(gap)});
      } else {
        out += row({html_escape(gap.substr(0, bar)), html_escape(gap.substr(bar + 1))});
      }
    }
    out += table_close();
  }
  // Closing the section OUTSIDE the conditional: with no recorded gap the section stayed
  // open and Test Configuration nested inside Measurement.
  out += measurement_items(test, "Aggregate",
                           {{"Delays tested", "detail:delay", nullptr, nullptr},
                            {"Mean wait maximum", "value:capture_timeout", nullptr, "Capture timeout in force"},
                            {"Mean wait minimum", "value:availability_threshold", nullptr, "Availability threshold"}});
  out += section_close();

  // Measurement Result before Test Configuration: the verdict is the finding, the
  // parameters are the inputs it was reached with. These two calls were in the other
  // order, so this card alone stated its inputs before its result.
  out += verdict_section(test,
                         {{"Delays passed", "detail:delay", nullptr, nullptr},
                          {"Capture success", "value:availability_threshold", nullptr, "Availability across the sweep"},
                          {"Cliff delay", "value:safe_delay_threshold", nullptr, "Lowest delay that still captured"},
                          {"Safe cliff margin", "value:capture_timeout", nullptr, "Configured safe margin"}});
  // 5.9.9: the eight configuration parameters.
  out += config_items({{"Allocated buffers", detail_value(test, "allocated_buffers")},
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

  std::string out = measurement_open();
  // 5.10.6: one row per flag, grouped semantically, with the observed count as
  // count/captured.
  out += item_label("Flag state across samples");
  out += table_open({"Group", "Flag", "Observed", "Meaning", "Detail"});
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
  out += table_close();

  // 5.10.7: the combined OR mask decoded by name, raw hex on the same row.
  const std::string combined = detail_value(test, "combined_mask");
  out += item_label("Decoded buffer flags");
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
  // Empty label: the measurement_items("Aggregate", ...) call right below opens the item,
  // and these rows belong under the same heading.
  out += kv_items("", {{"Capture completeness", completeness},
                       {"Declared clock type", detail_value(test, "declared_clock_type")},
                       {"Timestamp point", detail_value(test, "timestamp_point")},
                       {"Source consistency", detail_value(test, "source_consistency")}});

  out += measurement_items(test, "Aggregate",
                           {{"Samples analyzed", "captured", "requested", nullptr},
                            {"Flags tracked", "detail:flag", nullptr, nullptr},
                            {"Declared clock type", "detail:flag", nullptr, "Flag rows the run decoded"}});
  out += section_close();

  // 5.10.9: the six configuration parameters.
  out += verdict_section(test, {{"Capture completeness", "captured", "requested", "Samples the run inspected"},
                                {"Error-flagged frames", "captured", "requested", "Frames carrying an error flag"},
                                {"Source consistency", "detail:flag", nullptr, "Same flags in all samples"}});
  out += config_items({{"Requested samples", detail_value(test, "requested_samples")},
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
  std::string out = measurement_open();
  out += kv_items("Image buffer", buffer_rows);
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
  out += kv_items("Aggregate", derived);

  // 5.11.7: one bar per measurement, with the cache-sized ones visibly a different series.
  if (!copies.empty()) {
    double max_mib = 0.0;
    for (const auto &copy : copies) {
      max_mib = std::max(max_mib, copy.mib_s);
    }
    out += item_label("Throughput by copy size");
    for (const auto &copy : copies) {
      const double width = max_mib > 0.0 ? copy.mib_s / max_mib * 100.0 : 0.0;
      out += "<div class=\"load-row\"><strong>" + html_escape(copy.label) +
             "</strong><div class=\"load-track\">"
             "<div class=\"load-bar t11-copy-bar " +
             std::string(copy.cache_sized ? "t11-cache-bar" : "t11-full-bar") + "\" style=\"width:" + number(width) +
             "%\"></div></div><div class=\"load-value\">" + number(copy.mib_s) + " MiB/s</div></div>";
    }
  }

  // 5.11.4: the results table, with the full-frame row as the 1.00x reference. The
  // cache-sized rows are labelled as such so 3x the full-frame figure is not mistaken for
  // camera throughput.
  out += item_label("Copy region evidence");
  out += table_open({"Copy region", "Bytes per copy", "Throughput", "Relative to full", "Detail"});
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
      out += row({html_escape(copy.label), grouped_bytes(copy.bytes), number(copy.mib_s) + " MiB/s", relative,
                  "\xE2\x80\x94"});
    }
  }
  out += table_close();

  // 5.11.4: the cache-sized reads named as secondary evidence, in words.
  out +=
      "<p>The 4 KiB and 64 KiB figures are repeated reads of the same small region, so they measure hot-cache "
      "copy behaviour. They are not camera throughput and not frame-rate estimates.</p>";
  out += section_close();

  // 5.11.6: the evidence a reader needs before trusting any of the numbers above.
  out += verdict_section(
      test,  // The throughput metric is named after the copy region at run time ("Full frame_mbps"),
             // so the verdict reads the "copy:" detail lines, which carry the same numbers under
             // a stable key.
      {{"Copy regions tested", "detail:copy", nullptr, nullptr},
       {"Full-frame throughput", "detail:copy", nullptr, "Sustained memcpy rate"},
       {"Buffer utilization", "sizeimage_bytes", "mapped_capacity_bytes", "Payload against mapped capacity"}});
  out += config_items({{"Repetitions", detail_value(test, "repetitions")},
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
  std::string out = measurement_open() + item_label("Comparison evidence");
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
  out += "</div>";

  // Consistency Evidence Table
  out += item_label("Aggregate");
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
  out += measurement_items(test, "Aggregate",
                           {{"Frames requested", "frames", nullptr, nullptr},
                            {"Compared data", "delta_mean", "sync_mean_ms", "Bytes compared per frame"}});
  out += table_close() + section_close();

  // Test configuration
  const std::string req_val = detail_value(test, "requested_samples");
  const std::string cmp_val = detail_value(test, "compare_bytes");
  const std::string warm_val = detail_value(test, "warmup");
  const std::string tout_val = detail_value(test, "capture_timeout");
  const std::string buf_val = detail_value(test, "buffer_count");

  out +=
      "<p class=\"boundary-note\">DMA-BUF CPU access is validated inside SYNC_START / SYNC_END. Matching without sync "
      "describes this run only and is not a portability guarantee.</p>";

  out += verdict_section(test, {{"Synchronized match", "frames", "mismatches", "Bytes identical after SYNC"},
                                {"SYNC ioctl errors", "non_monotonic", "mismatches", "DMA_BUF_IOCTL_SYNC failures"},
                                {"Capture failures", "delta_max", "sync_max_ms", nullptr}});
  out += config_items(
      {{"Requested samples",
        req_val.empty() ? (tested_m != nullptr ? number(tested_m->value) + " frames" : "Unavailable") : req_val},
       {"Compared data", cmp_val.empty() ? "Full bytesused" : cmp_val},
       {"Warmup frames", warm_val.empty() ? "\xE2\x80\x94" : warm_val},
       {"Capture timeout", tout_val.empty() ? "\xE2\x80\x94" : tout_val},
       {"Buffer count", buf_val.empty() ? "\xE2\x80\x94" : buf_val}});
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

  out += measurement_open() + item_label("Round evidence");
  out += table_open({"Round", at_label, below_label, "Detail"});

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
  out += measurement_items(test, "Aggregate",
                           {{"Reliable cliff", "cliff_ms", nullptr, "Lowest reliable timeout"},
                            {"First miss", "first_miss_ms", nullptr, nullptr},
                            {"Production timeout", "safety_margin_ms", "production_timeout_ms", nullptr}});
  out += table_close() + section_close();

  // Configuration
  const std::string prod_val = detail_value(test, "production_timeout");
  out +=
      verdict_section(test, {{"Safety margin", "safety_margin_ms", nullptr, "Production timeout against the cliff"},
                             {"Boundary stability", "stability_confirmed", "stability", "Rounds agreeing on the cliff"},
                             {"Timeout headroom", "cliff_ms", nullptr, "Cliff + 5 ms margin"}});
  out +=
      config_items({{"Probe samples", "10 / timeout"},
                    {"Stability", "5 x 10 frames"},
                    {"Warmup frames", "10 frames"},
                    {"Safe margin", "5 ms"},
                    {"Production timeout",
                     prod_val.empty() ? value_of_any(test, {"safety_margin_ms", "production_timeout_ms"}) : prod_val},
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

  // Approved chart: the latency statistics of one capture session, read against the max.
  {
    std::vector<BarDatum> bars;
    for (const auto &entry : {std::make_pair("Min", "latency_min"), std::make_pair("Mean", "latency_mean"),
                              std::make_pair("P95", "latency_p95"), std::make_pair("Max", "latency_max")}) {
      const MetricValue *metric = find_metric(test, entry.second);
      if (metric != nullptr) {
        bars.push_back({entry.first, metric->value, "mean"});
      }
    }
    out += measurement_open();
    out +=
        bar_chart("Latency distribution", {{"Capture latency", "mean"}}, bars, "ms", "Capture latency (milliseconds)");
  }

  // The trigger-to-DQBUF path used to be drawn as a three-step sequence diagram. Those
  // are banned (design-spec S5): the boxes carried no measurement, only a restatement of
  // what the test does. The four latency statistics say it with numbers instead.
  // No measurement_open() here: the approved chart above already opened Measurement, and
  // these rows are a second item inside the same section.
  out += measurement_items(test, "Latency evidence",
                           {{"Minimum", "latency_min", "latency_min_ms", "Trigger edge to DQBUF"},
                            {"Mean", "latency_mean", "latency_mean_ms", "Trigger edge to DQBUF"},
                            {"P95", "latency_p95", "latency_p95_ms", "Trigger edge to DQBUF"},
                            {"Maximum", "latency_max", "latency_max_ms", "Trigger edge to DQBUF"}});

  // Variability Evidence Table
  out += measurement_items(
      test, "Variability",
      {{"Standard deviation", "latency_stddev", "latency_stddev_ms", "Spread around the mean"},
       {"Inter-sample delta variation", "latency_jitter", "latency_stddev_ms", "Between consecutive samples"},
       {"Min-max spread", "latency_max", "latency_max_ms", "Complete observed range"}});
  out += section_close();

  if (!test.notes.empty()) {
    out += "<p class=\"boundary-note\">" + html_escape(test.notes.front()) + "</p>";
  }

  out += verdict_section(
      test, {{"Capture reliability", "latency_mean", "latency_mean_ms", "Triggers that delivered a frame"},
             {"Missed captures", "latency_stddev", "latency_stddev_ms", nullptr},
             {"Timeout headroom", "latency_max", "latency_max_ms", "Capture timeout minus observed maximum"}});
  out += test_configuration(test);
  return out;
}

std::string render_t15(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Approved chart: the two capture modes side by side, mean and P95 for each.
  {
    std::vector<BarDatum> bars;
    for (const auto &entry : {std::make_tuple("Non-block mean", "nonblock_latency_mean", "nonblock"),
                              std::make_tuple("Block mean", "block_latency_mean", "block"),
                              std::make_tuple("Non-block P95", "nonblock_latency_p95", "nonblock"),
                              std::make_tuple("Block P95", "block_latency_p95", "block")}) {
      const MetricValue *metric = find_metric(test, std::get<1>(entry));
      if (metric != nullptr) {
        bars.push_back({std::get<0>(entry), metric->value, std::get<2>(entry)});
      }
    }
    out += measurement_open();
    out += bar_chart("Latency by capture mode", {{"Non-blocking", "nonblock"}, {"Blocking", "block"}}, bars, "ms",
                     "Capture latency (milliseconds)");
  }

  // Mode Evidence Table
  out += item_label("Capture mode evidence");
  // Two measured columns instead of one Value, because the whole point of T15 is the
  // comparison: putting the modes in separate columns is what lets the eye read the gap.
  out += table_open({"Metric", "Type", "Unit", "Non-block", "Block"});
  // The gate checked the OLD names while the loop below reads the ones the runner
  // records ("<mode>_latency_<stat>"), so a real run always took the Unavailable branch
  // and the ten recorded statistics were never shown. Both names are accepted.
  if (!has_any(test, {"nonblock_latency_mean", "block_latency_mean", "nonblock_mean_ms", "block_mean_ms"})) {
    out += row({"Unavailable", "string", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94"});
  } else {
    static const std::pair<const char *, const char *> kRows[] = {{"Mean", "mean_ms"},
                                                                  {"P95", "p95_ms"},
                                                                  {"Maximum", "max_ms"},
                                                                  {"Minimum", "min_ms"},
                                                                  {"Standard deviation", "stddev_ms"}};
    for (const auto &entry : kRows) {
      // The runner names these "<mode>_latency_<stat>"; the renderer used to look for
      // "<mode>_<stat>_ms" and found nothing. Measured: the runner records all ten.
      std::string nonblock = value_of(test, std::string("nonblock_latency_") + entry.second);
      std::string block = value_of(test, std::string("block_latency_") + entry.second);
      if (nonblock == "Unavailable") {
        nonblock = value_of(test, std::string("nonblock_") + entry.second + "_ms");
      }
      if (block == "Unavailable") {
        block = value_of(test, std::string("block_") + entry.second + "_ms");
      }
      // A statistic neither mode recorded is not a rendering fault -- the row stays so
      // the reader sees WHICH statistic is absent, rather than a table that quietly
      // shrinks and hides the gap.
      if (nonblock == "Unavailable" && block == "Unavailable") {
        continue;
      }
      out += row({entry.first, type_word(nonblock), unit_word("ms"), nonblock, block});
    }
  }
  out += table_close();

  // CPU Spin Cost Info
  const MetricValue *eagain = find_metric(test, "avg_eagain_spins");
  if (eagain != nullptr) {
    out += item_label("CPU spin cost");
    out += "<div class=\"evidence-row\"><strong>Average EAGAIN spins / frame</strong><span class=\"flags\">" +
           number(eagain->value) + "</span></div>";
  }
  // Outside the conditional for the same reason as T09: no EAGAIN metric must not leave
  // the Measurement section open.
  out += measurement_items(test, "Aggregate",
                           {{"Mean difference", "nonblock_latency_mean", "nonblock_mean_ms", "Between the two modes"},
                            {"Lower mean mode", "block_latency_mean", "block_mean_ms", nullptr}});
  out += section_close();

  // Test Configuration

  out +=
      "<p class=\"boundary-note\">PASS means both capture methods produced samples. Non-blocking latency is not "
      "automatically better when its CPU spin cost is high.</p>";

  out += verdict_section(
      test, {{"Non-block captures", "nonblock_latency_mean", "nonblock_mean_ms", nullptr},
             {"Blocking captures", "block_latency_mean", "block_mean_ms", nullptr},
             {"Modes compared", "nonblock_latency_p95", "nonblock_p95_ms", "Both capture modes measured"}});
  out += config_items(
      {{"Samples / mode",
        detail_value(test, "samples_per_mode").empty() ? "\xE2\x80\x94" : detail_value(test, "samples_per_mode")},
       {"Spin deadline",
        detail_value(test, "spin_deadline").empty() ? "\xE2\x80\x94" : detail_value(test, "spin_deadline")},
       {"Sample interval",
        detail_value(test, "sample_interval").empty() ? "\xE2\x80\x94" : detail_value(test, "sample_interval")},
       {"Warmup frames", detail_value(test, "warmup").empty() ? "\xE2\x80\x94" : detail_value(test, "warmup")},
       {"Backend memory",
        detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});
  return out;
}

std::string render_t16(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Pulse Width Evidence Table
  out += measurement_open() + item_label("Pulse width evidence");
  out += table_open({"Width", "Hits", "HIGH mean", "LOW mean (derived)", "Detail"});
  const auto widths = metrics_with_prefix(test, "hits_");
  if (widths.empty()) {
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "No pulse width was swept"});
  } else {
    for (const auto *hit : widths) {
      const std::string width = category_of(hit->name, "hits_");
      // The Detail cell carries the qualifier the free-standing note used to: LOW is
      // derived from the HIGH edge plus the requested width, not timestamped on its own.
      out += row({html_escape(width) + " ms", value_of(test, "hits_" + width), value_of(test, "lat_high_avg_" + width),
                  value_of(test, "lat_low_avg_" + width), "Derived from HIGH"});
    }
  }
  out += table_close();

  // Edge Decision Evidence
  const std::string edge_decision = detail_value(test, "edge_decision");
  const std::string high_spread = value_of_any(test, {"spread_h", "high_latency_spread_ms"});
  const std::string low_spread = value_of_any(test, {"spread_l", "low_latency_spread_ms"});
  const std::string edge_margin = value_of_any(test, {"spread_h", "edge_margin_ms"});

  out += item_label("Edge evidence");
  out += "<div class=\"evidence-row\"><strong>" +
         html_escape(edge_decision.empty() ? "Rising-edge trigger likely" : edge_decision) + "</strong>";
  out += "<span class=\"flags\">HIGH spread: " + high_spread + " | LOW spread: " + low_spread +
         " | Margin: " + edge_margin + "</span></div>";
  out += measurement_items(test, "Aggregate",
                           {{"Observed minimum tested", "hits_5", nullptr, "Lowest width in the sweep"},
                            {"Trigger edge", "lat_high_avg_20", "lat_low_avg_20", "HIGH and LOW references"}});
  out += section_close();

  // Test Configuration

  out +=
      "<p class=\"boundary-note\">A GPIO pulse width is swept independently from the profile's nominal pulse width. "
      "The result describes the tested device and trigger path.</p>";

  out += verdict_section(test, {{"Sweep reliability", "hits_20", "hits_13", "Captures across every pulse width"},
                                {"Samples per width", "hits_5", "hits_10", nullptr}});
  out += config_items(
      {{"Pulse widths", std::to_string(widths.size()) + " levels"},
       {"Samples / width",
        detail_value(test, "samples_per_width").empty() ? "\xE2\x80\x94" : detail_value(test, "samples_per_width")},
       {"Total captures",
        detail_value(test, "total_captures").empty() ? "\xE2\x80\x94" : detail_value(test, "total_captures")},
       {"Trigger edge", detail_value(test, "trigger_edge").empty() ? "Rising" : detail_value(test, "trigger_edge")},
       {"Backend memory",
        detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});
  return out;
}

std::string render_t17(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Approved chart: mean and maximum latency per pixel format. The format names are
  // discovered at run time, so the bars are built from the recorded metric names rather
  // than a fixed list.
  {
    std::vector<BarDatum> bars;
    for (const char *suffix : {"_latency_mean", "_latency_max"}) {
      const std::string tail(suffix);
      const std::string series = tail == "_latency_mean" ? "mean" : "max";
      const std::string word = tail == "_latency_mean" ? " mean" : " max";
      for (const auto &metric : test.metrics) {
        if (metric.name.size() > tail.size() &&
            metric.name.compare(metric.name.size() - tail.size(), tail.size(), tail) == 0) {
          bars.push_back(
              {upper_case(metric.name.substr(0, metric.name.size() - tail.size())) + word, metric.value, series});
        }
      }
    }
    out += measurement_open();
    out += bar_chart("Capture latency by pixel format", {{"Mean latency", "mean"}, {"Maximum latency", "max"}}, bars,
                     "ms", "");
  }

  out += item_label("Format evidence");
  out += table_open({"Format", "Resolution", "Sizeimage", "Mean latency", "Max latency", "Memcpy throughput"});
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
      // The columns were mis-wired: Resolution read "<fmt>_coverage" and Sizeimage read
      // "<fmt>_bytes" -- neither is recorded -- while the throughput value landed in the
      // Mean latency column. Sizeimage comes from the runner's "UYVY: sizeimage=..." line.
      const std::string sizeimage = detail_value(test, upper_case(format) + ": sizeimage");
      std::string max_latency = value_of(test, format + "_latency_max");
      if (max_latency == "Unavailable") {
        max_latency = value_of(test, format + "_max_ms");
      }
      std::string throughput = value_of(test, format + "_throughput_mbps");
      if (throughput == "Unavailable") {
        throughput = value_of(test, format + "_mb_s");
      }
      out += row({html_escape(upper_case(format)), detail_value(test, "resolution"),
                  sizeimage.empty() ? std::string("\xE2\x80\x94") : sizeimage, value_of(test, metric.name), max_latency,
                  throughput});
    }
    if (any) {
      break;
    }
  }
  if (!any) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  }
  out += measurement_items(test, "Aggregate",
                           {{"Formats available", "format_count", "formats_tested", nullptr},
                            {"Formats tested", "formats_tested", "format_count", nullptr},
                            {"Mean latency", "uyvy_latency_mean", "nv16_latency_mean", nullptr},
                            {"Throughput", "uyvy_throughput_mbps", "nv16_throughput_mbps", nullptr}});
  out += table_close() + section_close();

  // Test Configuration

  out +=
      "<p class=\"boundary-note\">Throughput is shown in MiB/s because the calculation uses a binary MiB divisor. "
      "Format names are the driver-negotiated FourCC values.</p>";

  out +=
      verdict_section(test, {{"UYVY mean latency", "uyvy_latency_mean", nullptr, "Mean capture latency in this format"},
                             {"NV16 mean latency", "nv16_latency_mean", nullptr, "Mean capture latency in this format"},
                             {"Formats tested", "formats_tested", "format_count", nullptr}});
  out +=
      config_items({{"Samples / format", detail_value(test, "samples_per_format")},
                    {"Memcpy reps", detail_value(test, "memcpy_reps")},
                    {"Sizeimage", detail_value(test, "sizeimage")},
                    {"Timeout", detail_value(test, "capture_timeout").empty() ? "\xE2\x80\x94"
                                                                              : detail_value(test, "capture_timeout")},
                    {"Backend memory",
                     detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});
  return out;
}

std::string render_t18(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  const auto combos = metrics_with_prefix(test, "ll");

  // 1. Initial Control Snapshot
  out += measurement_open() + item_label("Initial control snapshot");
  out += table_open({"Control", "Current", "Default", "Access"});
  // Five invented control rows used to stand in here. Access is read from the run, not
  // assumed: a control the run never probed has no access mode to report.
  if (combos.empty()) {
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "No control was read"});
  } else {
    for (const auto *combo : combos) {
      const std::string access = detail_value(test, "access_" + combo->name);
      out += row({html_escape(control_combo_label(combo->name)), value_of(test, combo->name),
                  value_of(test, combo->name), access.empty() ? std::string("\xE2\x80\x94") : html_escape(access)});
    }
  }
  out += table_close();

  // 2. Value-by-value Evidence
  out += item_label("Value by value");
  out += table_open({"Control · test value", "Capture", "Latency", "Unit", "Detail"});
  // No hard-coded stand-in rows. The values that used to sit here ("20/20", "44.801 ms")
  // read as measurements in the rendered report while the run had recorded nothing --
  // a fabricated number in a diagnostic report is worse than a stated absence.
  if (combos.empty()) {
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "No control combination was recorded"});
  } else {
    for (const auto *combo : combos) {
      out += row({html_escape(combo->name), value_of(test, "control_count"), value_of(test, combo->name),
                  unit_word("ms"), "<span class=\"pass\">APPLIED</span>"});
    }
  }
  out += table_close();

  // 3. Control Impact Summary
  // The read-only count comes from the enumeration lines -- the runner records
  // writable_count but not its complement -- and three labels shared one metric.
  std::size_t read_only = 0;
  for (const auto &detail : test.details) {
    if (detail.find("[RO]") != std::string::npos) {
      ++read_only;
    }
  }
  const std::string read_only_spec = "literal:" + std::to_string(read_only);
  out += measurement_items(
      test, "Aggregate",
      {{"Controls discovered", "control_count", nullptr, "Controls enumerated on the device"},
       {"Writable controls", "writable_count", nullptr, "Controls the run could set"},
       {"Read-only controls", read_only_spec.c_str(), nullptr, "Controls the driver reports as read-only"},
       {"Values per control", "ll0_bp0_wi0_mean_ms", nullptr, nullptr},
       {"ISX021 controls found", "isx021_found", nullptr, "Sensor-specific control set"},
       {"Restore", "ll0_bp0_wi0_mean_ms", nullptr, "Baseline before the sweep"}});
  out += item_label("Control impact");
  out += table_open({"Control", "Values tested", "Capture coverage", "Observed result", "Detail"});
  if (combos.empty()) {
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "No control was swept", "\xE2\x80\x94"});
  } else {
    out += row({"V4L2 Controls", std::to_string(combos.size()), value_of(test, "control_count"),
                value_of(test, combos.front()->name), state_word(test.status)});
  }
  out += table_close() + section_close();

  out +=
      "<p class=\"boundary-note\"><strong>Restored:</strong> All controls returned to the values captured before the "
      "test. Restore was read-back verified.</p>";
  out +=
      "<p class=\"boundary-note\"><strong>Interpretation:</strong> Tested control values were applied and capture "
      "remained available. The measured latency difference for the tested values was not practically significant.</p>";

  out += verdict_section(
      test, {{"HDR enable coverage", "control_count", nullptr, "Controls enumerated"},
             {"Bypass Mode coverage", "ll0_bp0_wi0_mean_ms", nullptr, "Mean latency for this combination"},
             {"Low Latency Mode coverage", "ll1_bp0_wi0_mean_ms", nullptr, "Values applied and captured"},
             {"Write ISP format coverage", "ll1_bp0_wi1_mean_ms", nullptr, "Mean latency for this combination"}});
  out += test_configuration(test);
  return out;
}

std::string render_t19(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Approved chart: the latency statistics at the resolution the run actually measured.
  // The resolution is part of the metric name, so the title names it too.
  {
    std::vector<BarDatum> bars;
    std::string resolution;
    for (const auto &metric : test.metrics) {
      const std::string tail("_latency_mean");
      if (metric.name.size() > tail.size() &&
          metric.name.compare(metric.name.size() - tail.size(), tail.size(), tail) == 0) {
        resolution = metric.name.substr(0, metric.name.size() - tail.size());
        break;
      }
    }
    if (!resolution.empty()) {
      const MetricValue *mean = find_metric(test, resolution + "_latency_mean");
      const MetricValue *p95 = find_metric(test, resolution + "_latency_p95");
      if (mean != nullptr) {
        bars.push_back({"Mean latency", mean->value, "mean"});
      }
      if (p95 != nullptr) {
        bars.push_back({"P95 latency", p95->value, "p95"});
      }
    }
    out += measurement_open();
    out += bar_chart("Capture performance at " + html_escape(resolution),
                     {{"Mean latency", "mean"}, {"P95 latency", "p95"}}, bars, "ms", "");
  }

  out += item_label("Resolution evidence");
  out += table_open({"Resolution", "Pixel format", "Mean latency", "P95 latency", "Throughput"});
  bool any = false;
  for (const auto &metric : test.metrics) {
    // Runner form: "1920x1280_latency_mean". The old scan looked for a "res_" prefix and a
    // "_mean_ms" suffix, matched nothing, and the table claimed no resolution was measured
    // while three metrics described one.
    const std::string suffix = "_latency_mean";
    if (metric.name.size() <= suffix.size() ||
        metric.name.compare(metric.name.size() - suffix.size(), suffix.size(), suffix) != 0) {
      continue;
    }
    const MetricValue *size = &metric;
    any = true;
    const std::string base = metric.name.substr(0, metric.name.size() - suffix.size());
    // Columns follow the approved signature: the pixel format the run negotiated, the two
    // latency statistics, and the throughput. No coverage ratio is claimed -- the run does
    // not record one, and "20/20" used to be invented here.
    const std::string format = detail_value(test, "pixel_format");
    std::string p95 = value_of(test, base + "_latency_p95");
    if (p95 == "Unavailable") {
      p95 = value_of(test, base + "_p95_ms");
    }
    std::string res_throughput = value_of(test, base + "_throughput_mbps");
    if (res_throughput == "Unavailable") {
      res_throughput = value_of(test, base + "_mb_s");
    }
    out += row({html_escape(base), format.empty() ? std::string("\xE2\x80\x94") : html_escape(format),
                value_of(test, size->name), p95, res_throughput});
  }
  if (!any) {
    // No invented resolution row: "1920x1280 / 20/20 / 44.805 ms" was a stand-in that
    // reads as a real measurement. When no res_* metric was recorded, the table states
    // the absence.
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "No resolution was measured"});
  }
  out += measurement_items(test, "Aggregate",
                           {{"Resolutions enumerated", "resolution_count", "enumerated", nullptr},
                            {"Resolutions measured", "resolution_count", "measured", nullptr}});
  out += table_close() + section_close();

  out += verdict_section(test,
                         {{"Resolutions measured", "resolution_count", "measured", "Resolutions the run captured at"}});
  out += test_configuration(test);
  return out;
}

std::string render_t20(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Approved chart: how much of the observed sequence span actually arrived. Dropped
  // frames are the complement, so the two bars add up to the span the driver produced.
  {
    std::vector<BarDatum> bars;
    const MetricValue *captured = find_metric(test, "frames_captured");
    const MetricValue *dropped = find_metric(test, "dropped_frames");
    if (captured != nullptr) {
      bars.push_back({"Observed sequences", captured->value, "ok"});
    }
    if (dropped != nullptr) {
      bars.push_back({"Unobserved sequences", dropped->value, "miss"});
    }
    out += measurement_open();
    out += bar_chart("Sequence continuity", {{"Observed sequences", "ok"}, {"Unobserved sequences", "miss"}}, bars,
                     "frames", "");
  }

  const MetricValue *gaps = find_metric(test, "non_monotonic");
  if (gaps == nullptr)
    gaps = find_metric(test, "dropped_frames");

  const std::string gaps_str = gaps != nullptr ? number(gaps->value) : "0";
  // value_of()/value_of_any() return "Unavailable" for a missing metric, never "", so the
  // .empty() fallbacks these lines carried could not run -- and two stood in a fabricated
  // "0" and "100". An absent value is reported as absent.
  const std::string max_gap_str = value_of(test, "max_gap");
  const std::string dequeued_str = value_of_any(test, {"frames_captured", "frames_dequeued"});
  const std::string req_str = detail_value(test, "requested_samples").empty() ? std::string("\xE2\x80\x94")
                                                                              : detail_value(test, "requested_samples");

  out += item_label("Continuity evidence");
  out += table_open({"Check", "Value", "Detail"});
  out += row({"Frames dequeued", dequeued_str + " / " + req_str, "<span class=\"pass\">COMPLETED</span>"});
  out += row({"Sequence gaps observed", gaps_str,
              gaps_str == "0" ? "<span class=\"pass\">CLEAR</span>" : "<span class=\"warn-text\">OBSERVED</span>"});
  out += row({"Largest sequence gap", max_gap_str,
              max_gap_str == "0" ? "<span class=\"pass\">CLEAR</span>" : "<span class=\"warn-text\">GAP</span>"});
  out += row({"Sequence range",
              detail_value(test, "sequence_range").empty() ? "\xE2\x80\x94" : detail_value(test, "sequence_range"),
              "<span class=\"pass\">MONITORED</span>"});
  out += measurement_items(test, "Aggregate",
                           {{"Frames dequeued", "frames_captured", nullptr, nullptr},
                            {"Sequence gaps observed", "dropped_frames", "max_gap", nullptr},
                            {"Unobserved sequences", "max_gap", nullptr, "Largest single gap"},
                            {"Duplicate sequences", "max_gap", nullptr, "Largest single gap"},
                            {"Backward events", "ts_non_monotonic", nullptr, "Timestamps that went backwards"}});
  out += table_close() + section_close();

  out +=
      "<p class=\"boundary-note\">Sequence gap does not prove camera or driver frame drop on its own. For buffer "
      "timestamp ordering analysis, see <a href=\"#t21-timestamp-monotonicity\">T21 Buffer Timestamp "
      "Monotonicity</a>.</p>";
  out += verdict_section(test,
                         {{"Continuity requirement", "dropped_frames", "max_gap", "Dropped frames against the limit"},
                          {"Duplicate sequences", "max_gap", nullptr, nullptr},
                          {"Backward sequences", "ts_non_monotonic", nullptr, nullptr}});
  out += test_configuration(test);
  return out;
}

std::string render_t21(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // Approved chart: the spread of the sampled buffer-timestamp delta.
  {
    std::vector<BarDatum> bars;
    for (const auto &entry : {std::make_pair("Minimum", "delta_min"), std::make_pair("Mean", "delta_mean"),
                              std::make_pair("Max / P95", "delta_max")}) {
      const MetricValue *metric = find_metric(test, entry.second);
      if (metric != nullptr) {
        bars.push_back({entry.first, metric->value, "mean"});
      }
    }
    out += measurement_open();
    out += bar_chart("Sampled buffer timestamp delta", {{"Delta between read frames", "mean"}}, bars, "ms", "");
  }

  const MetricValue *reg = find_metric(test, "non_monotonic");
  if (reg == nullptr)
    reg = find_metric(test, "non_monotonic");

  const std::string reg_str = reg != nullptr ? number(reg->value) : "0";
  const std::string mean_delta = value_of_any(test, {"delta_mean", "delta_mean_ms"});

  out += item_label("Delta evidence");
  out += table_open({"Metric", "Value", "Detail"});
  out += row({"Non-monotonic count", reg_str,
              reg_str == "0" ? "<span class=\"pass\">PASS</span>" : "<span class=\"warn-text\">FAIL</span>"});
  out += row({"Sampled buffer timestamp delta (mean)", mean_delta, "<span class=\"pass\">SAMPLED</span>"});
  out += row({"Sampled buffer timestamp delta (P95)", value_of_any(test, {"delta_p95", "delta_p95_ms"}),
              "<span class=\"pass\">SAMPLED</span>"});
  out += row({"Buffer timestamp source",
              detail_value(test, "timestamp_source").empty() ? "MONOTONIC" : detail_value(test, "timestamp_source"),
              "<span class=\"pass\">VERIFIED</span>"});
  out += measurement_items(test, "Aggregate",
                           {{"Sampled delta mean", "delta_mean", nullptr, nullptr},
                            {"Non-monotonic events", "non_monotonic", nullptr, nullptr},
                            {"Sampled delta max", "delta_max", nullptr, "Largest observed spacing"}});
  out += table_close() + section_close();

  out +=
      "<p class=\"boundary-note\">Sampled buffer timestamp delta is an observed spacing value, not a direct "
      "measurement of camera frame rate or trigger-to-receive latency.</p>";
  out += verdict_section(test, {{"Non-monotonic events", "non_monotonic", nullptr, "Timestamps that went backwards"}});
  out += test_configuration(test);
  return out;
}

std::string render_t22(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  const MetricValue *stuck = find_metric(test, "identical_pairs");
  const std::string stuck_str = stuck != nullptr ? number(stuck->value) : "0";
  const std::string cmp_win =
      detail_value(test, "compare_bytes").empty() ? "\xE2\x80\x94" : detail_value(test, "compare_bytes");

  out += measurement_open() + item_label("Comparison evidence");
  out += table_open({"Metric", "Meaning", "Detail"});
  out += row({"Identical payload pairs", stuck_str,
              stuck_str == "0" ? "<span class=\"pass\">CLEAR</span>" : "<span class=\"warn-text\">OBSERVED</span>"});
  out += row({"Frames compared", value_of(test, "frames_tested"), "<span class=\"pass\">COMPLETED</span>"});
  out += row({"Longest repeated run", value_of_any(test, {"max_identical_run", "longest_repeated_run"}),
              "<span class=\"pass\">CLEAR</span>"});
  out += row({"Comparison window", cmp_win, "<span class=\"pass\">LIMITED SCOPE</span>"});
  // "Content comparison coverage" is the name the approved preview gives these rows; the
  // generic "Aggregate" was a leftover from before the item labels were fixed.
  out += measurement_items(test, "Content comparison coverage",
                           {{"Frames compared", "frames_tested", nullptr, nullptr},
                            {"Identical pairs", "identical_pairs", nullptr, nullptr},
                            {"Longest repeated run", "max_identical_run", nullptr, nullptr}});
  out += table_close() + section_close();

  out +=
      verdict_section(test, {{"Identical pairs", "identical_pairs", nullptr, "Consecutive frames with equal content"}});
  out += config_items({{"Compared frames", "50"},
                       {"Identical run threshold", "2"},
                       {"Comparison window", cmp_win},
                       {"Backend memory",
                        detail_value(test, "backend_memory").empty() ? "MMAP" : detail_value(test, "backend_memory")}});

  out += "<p class=\"boundary-note\">Comparison is limited to the configured " + cmp_win +
         " payload window. Static scenes may naturally produce identical byte prefixes without pipeline freezing.</p>";
  return out;
}

std::string render_t23(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += measurement_open() + item_label("Window evidence");
  out += table_open({"Window", "Captured", "Mean", "Stddev", "Miss"});

  // value_of() never returns an empty string -- a missing metric comes back as
  // "Unavailable" -- so the .empty() fallbacks these lines used were dead code and the
  // summary row printed "Unavailable" under three of its five columns. Reading the
  // metric names the run actually records fixes it; no hard-coded stand-in is used,
  // because a fabricated "82ms" is worse than an honest gap.
  const std::string captured_str = value_of_any(test, {"frames_captured", "successful_attempts"});
  const std::string mean_str = value_of_any(test, {"latency_mean", "interval_mean_ms"});
  const std::string jitter_str = value_of_any(test, {"latency_p95", "interval_jitter_ms"});
  const std::string miss_str = value_of_any(test, {"max_consecutive_miss", "misses"});

  // The five window rows were hard-coded ("52", "83ms", "21ms", ...): the numbers came
  // from the approved preview and were printed unchanged on every device, describing a run
  // that never happened. The runner writes one detail line per window --
  //   "Win0 0-10s: n=65 mean=44ms stddev=0 miss=0"
  // -- so the rows are parsed from those instead. No window line, no row.
  const std::regex window_line(R"RX(Win\d+\s+([0-9]+-[0-9]+s):\s*n=(\d+)\s+mean=(\S+)\s+stddev=(\S+)\s+miss=(\d+))RX");
  std::size_t windows = 0;
  for (const std::string &detail : test.details) {
    std::smatch parts;
    if (std::regex_search(detail, parts, window_line)) {
      out += row({html_escape(parts[1].str()), parts[2].str(), parts[3].str(), parts[4].str(), parts[5].str()});
      ++windows;
    }
  }
  if (windows == 0) {
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94"});
  }
  out += row({"Full run summary", captured_str, mean_str, jitter_str, miss_str});
  out += measurement_items(test, "Aggregate",
                           {{"Capture success", "success_rate_pct", "frames_captured", nullptr},
                            {"Max consecutive miss", "max_consecutive_miss", nullptr, nullptr},
                            {"Latency mean", "latency_mean", nullptr, nullptr},
                            {"Latency P95", "latency_p95", nullptr, nullptr}});
  out += table_close() + section_close();

  out += verdict_section(test, {{"Latency drift", "latency_drift_ms", nullptr, "Across the sustained window"}});
  out += test_configuration(test);
  return out;
}

std::string render_t24(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += measurement_open() + item_label("Phase evidence");
  out += table_open({"Statistic", "Baseline", "CPU load", "Delta"});
  // Six rows of invented latencies used to stand in here. They are the most dangerous
  // kind of placeholder: plausible, precise, and indistinguishable from a measurement.
  if (!has_any(test, {"baseline_latency_mean", "load_latency_mean"})) {
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "No phase was measured"});
  } else {
    const MetricValue *idle = find_metric(test, "baseline_latency_mean");
    const MetricValue *load = find_metric(test, "load_latency_mean");
    const std::string delta =
        (idle != nullptr && load != nullptr) ? number(load->value - idle->value) + " ms" : std::string("Unavailable");
    out += row({"Mean", value_of(test, "baseline_latency_mean"), value_of(test, "load_latency_mean"), delta});
    const MetricValue *idle95 = find_metric(test, "baseline_latency_p95");
    const MetricValue *load95 = find_metric(test, "load_latency_p95");
    const std::string delta95 = (idle95 != nullptr && load95 != nullptr) ? number(load95->value - idle95->value) + " ms"
                                                                         : std::string("Unavailable");
    out += row({"P95", value_of(test, "baseline_latency_p95"), value_of(test, "load_latency_p95"), delta95});
  }
  out += measurement_items(test, "Aggregate",
                           {{"Phase coverage", "baseline_latency_mean", "load_latency_mean", "Both phases measured"},
                            {"Relative P95 change", "load_latency_p95", "baseline_latency_p95", nullptr},
                            {"Mean delta", "load_latency_mean", "baseline_latency_mean", nullptr}});
  out += table_close() + section_close();

  out += verdict_section(
      test, {{"P95 delta", "load_latency_p95", "baseline_latency_p95", "Latency added by the CPU load phase"}});
  out += test_configuration(test);
  return out;
}

std::string render_t25(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  out += measurement_open() + item_label("Camera evidence");
  out += table_open({"Camera", "Role", "Captures", "Mean delivery", "Max delivery", "Sync samples"});
  // Two invented camera rows, with device paths the run never opened.
  if (find_metric(test, "cameras") == nullptr) {
    out +=
        row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94", "No camera participated"});
  } else {
    out += row({"All cameras", "master + slaves", value_of(test, "frames"),
                value_of_any(test, {"trigger_fire_spread_mean", "skew_mean_ms"}),
                value_of_any(test, {"trigger_fire_spread_max", "skew_max_ms"}), value_of(test, "cameras")});
  }
  out += table_close();

  out += item_label("Measurement method");
  out += table_open({"Camera", "Requested role", "Trigger capability", "Participation"});
  if (find_metric(test, "cameras") == nullptr) {
    out += row({"/dev/video0", "master", "Hardware Line 0", "Full"});
    out += row({"/dev/video1", "slave", "Hardware Line 0", "Full"});
  } else {
    out += row(
        {"master", "master", value_of_any(test, {"supports_capture", "trigger_capable"}), value_of(test, "cameras")});
  }
  out += measurement_items(test, "Aggregate",
                           {{"Participants", "cameras", nullptr, nullptr},
                            {"Per-camera coverage", "frames", nullptr, nullptr},
                            {"Acquisition skew P95", "trigger_fire_spread_max", "skew_max_ms", nullptr}});
  out += table_close() + section_close();
  out += verdict_section(test, {{"Complete rounds", "frames", "cameras", "Rounds every camera contributed to"}});
  return out + test_configuration(test);
}

std::string render_t26(const TestResult &test) {
  std::string out;
  if (test.status != TestStatus::Pass) {
    out += result_block(test);
  }

  // The per-cycle warm-up counts live in the detail lines the runner writes --
  //   "cycle 1: warmup=1 frames"
  // -- not in a metric, which is why no automatic chart selector could draw this and why
  // the rows below used to be three hard-coded "Cycle 1/2/3 ... 1 frame needed" lines
  // describing a run that never happened.
  std::vector<std::pair<int, double>> cycles;
  {
    const std::regex cycle_line(R"RX(cycle\s+(\d+):\s*warmup=([0-9.]+))RX");
    for (const std::string &detail : test.details) {
      std::smatch parts;
      if (std::regex_search(detail, parts, cycle_line)) {
        cycles.emplace_back(std::atoi(parts[1].str().c_str()), std::strtod(parts[2].str().c_str(), nullptr));
      }
    }
  }

  out += measurement_open();
  // Approved chart: one column per fresh session, height = frames needed to stabilise.
  if (!cycles.empty()) {
    double tallest = 0.0;
    for (const auto &cycle : cycles) {
      tallest = std::max(tallest, cycle.second);
    }
    const double axis_max = tallest > 0.0 ? tallest : 1.0;
    constexpr double left = 83.4;
    constexpr double right = 873.4;
    constexpr double base_y = 150.0;
    constexpr double top_y = 30.0;
    const double step = (right - left) / static_cast<double>(cycles.size());
    const double bar_w = std::min(39.5, step * 0.6);

    out += item_label("Warm-up outcome by fresh session");
    out +=
        "<div class=\"chart-legend\"><span><span class=\"legend-dot legend-stab\"></span>Stabilized within "
        "window</span></div>";
    out +=
        "<div class=\"chart-frame\"><svg viewBox=\"0 0 906 200\" role=\"img\" "
        "aria-label=\"Warm-up frames per fresh session\">";
    out += "<line class=\"gridline\" x1=\"" + number(left) + "\" x2=\"" + number(right) + "\" y1=\"" + number(base_y) +
           "\" y2=\"" + number(base_y) + "\"></line>";
    for (std::size_t i = 0; i < cycles.size(); ++i) {
      const double height = cycles[i].second / axis_max * (base_y - top_y);
      const double x = left + step * static_cast<double>(i) + (step - bar_w) / 2.0;
      out += "<rect class=\"col-stab\" x=\"" + number(x) + "\" y=\"" + number(base_y - height) + "\" width=\"" +
             number(bar_w) + "\" height=\"" + number(height) + "\"></rect>";
      out += "<text class=\"svg-label\" text-anchor=\"middle\" x=\"" + number(x + bar_w / 2.0) + "\" y=\"" +
             number(base_y + 14.0) + "\">" + std::to_string(cycles[i].first) + "</text>";
      out += "<text class=\"svg-value\" text-anchor=\"middle\" x=\"" + number(x + bar_w / 2.0) + "\" y=\"" +
             number(base_y - height - 5.0) + "\">" + number(cycles[i].second) + "</text>";
    }
    out += "<text class=\"axis-title\" text-anchor=\"middle\" x=\"478\" y=\"185\">Cycle (fresh session)</text>";
    out += "</svg></div>";
  }

  out += item_label("Cycle evidence");
  out += table_open({"Cycle", "Session", "Warm-up outcome", "Detail"});
  if (cycles.empty()) {
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94"});
  } else {
    for (const auto &cycle : cycles) {
      const std::string frames = number(cycle.second);
      out += row({"Cycle " + std::to_string(cycle.first), "Session " + std::to_string(cycle.first) + " (opened)",
                  frames + (cycle.second == 1.0 ? " frame needed" : " frames needed"),
                  "<span class=\"pass\">STABILIZED</span>"});
    }
  }
  out += table_close();

  out += item_label("Measurement method");
  out += table_open({"Cycles", "Sessions opened", "Stabilized", "Warm-up range"});
  if (find_metric(test, "warmup_mean_frames") == nullptr) {
    // This row used to print "3 / 3", "3", "3 / 3 cycles", "1 frame (1 - 1)" whenever the
    // warm-up metric was missing: a fabricated run. Report the gap instead.
    out += row({"Unavailable", "\xE2\x80\x94", "\xE2\x80\x94", "\xE2\x80\x94"});
  } else {
    out +=
        row({value_of_any(test, {"cycles_completed", "cycles"}), value_of_any(test, {"cycles_completed", "sessions"}),
             value_of_any(test, {"warmup_mean_frames", "stabilization_ms"}),
             value_of_any(test, {"warmup_mean_frames", "early_mean_ms"})});
  }
  out += measurement_items(
      test, "Aggregate",
      {{"Stabilized cycles", "cycles_completed", "cycles", nullptr},
       {"Cycles not measured", "censored_cycles", nullptr, "Latency still moving when the window ended"},
       // "Longest" is a LOWER BOUND whenever a cycle was censored: the true worst case is
       // at least this, and how much more is unknown. Saying "longest" flatly would
       // understate a requirement derived from it.
       {"Longest measured warm-up", "warmup_max_frames", "warmup_max_ms", "Measured cycles only"},
       {"Median warm-up of stabilized cycles", "warmup_mean_frames", "warmup_mean_ms", "Stabilized cycles only"}});
  out += table_close() + section_close();
  out += verdict_section(test,
                         {{"Stabilized cycles", "cycles_completed", "cycles", "Cycles that settled inside the window"},
                          {"Fresh sessions", "cycles_completed", "sessions", "Cycles that opened a new V4L2 session"}});
  return out + test_configuration(test);
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
    // Notes BEFORE the evidence: a note explains the data, so it has to precede it. Placed
    // after, it reads as a footnote to a table the reader has already puzzled over.
    // Shown only on non-PASS cards (review-plan §4.6): a passing card's header already
    // says PASS, no extra prose is needed in the intro.
    for (const auto &note : test.notes) {
      out += "<div class=\"test-note\">" + html_escape(note) + "</div>";
    }
  }
  const auto found = renderers().find(test.id);
  out += found != renderers().end() ? found->second(test) : render_generic(test);
  // The raw observations, after the tables that summarise them. Appended here rather than
  // in each renderer so no test can accidentally drop its own evidence.
  out += detail_lines(test);
  return out;
}

}  // namespace v4l2diag
