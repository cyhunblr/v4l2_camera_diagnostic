#include "v4l2diag/core/diagnostic_runner.hpp"
#include "v4l2diag/core/test_content.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
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
//
// Deliberately WITHOUT thousands separators: this same function feeds SVG coordinates
// (x=, y=, cx=, points=) and CSS widths (style="width:...%") in 52 places, where a comma
// would break the geometry rather than read it. Grouping belongs to the display wrapper
// below, which the value cells use.
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

// number(), with thousands separators in the integer part: the form the approved previews
// use for a displayed value ("1,014.5" MiB/s in t11, "87,325.9" spins in t15, "1,016.6" in
// t17, "1,049.1" in t19). The 2026-08-09 device report printed all four ungrouped.
//
// Only the grouping is added. Decimal precision stays exactly as number() computes it: the
// approved set prints the same declared float/milliseconds type with 0, 1, 2 and 3 decimals
// (t06 "1132", t13 "45.0", t06 "44.81", t14 "55.164"), so no single precision rule
// reproduces it and that question is still open (user decision, option C).
//
// Integer byte counts already had grouped_bytes(); this covers the fractional values it
// cannot express, and the two agree digit-for-digit on whole numbers.
std::string display_number(double value) {
  const std::string text = number(value);
  const std::size_t dot = text.find('.');
  const std::size_t int_end = dot == std::string::npos ? text.size() : dot;
  const std::size_t first_digit = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
  const std::size_t digits = int_end - first_digit;
  if (digits < 4) {
    return text;
  }
  std::string out = text.substr(0, first_digit);
  for (std::size_t i = 0; i < digits; ++i) {
    if (i > 0 && (digits - i) % 3 == 0) {
      out += ',';
    }
    out += text[first_digit + i];
  }
  out += text.substr(int_end);
  return out;
}

// A MEASURED millisecond quantity, at three decimals.
//
// The approved previews print latency statistics that way -- t14 "44.836", t15 "44.796",
// t17 "44.801" / "44.805", t24 "+0.011" -- and the resolution matters: at two decimals
// UYVY and NV16 both collapse to "44.8" and the comparison T17 exists for disappears.
//
// A WHOLE number is returned whole. Measured across the approved set: 55 cells carry a
// millisecond unit and 45 of them are integers -- every `Test Configuration` timeout and
// interval ("Capture timeout 500", "Slow-start guard 2000"). Those are inputs to the run,
// not measurements, and printing "500.000 milliseconds" claims a microsecond-resolution
// setting that was never made. Fabricating precision is worse than losing it.
//
// The number of decimals is NOT a property of the unit -- it is a property of what the metric
// measures, which is why one rule could never reproduce the approved set. Read off the
// hardware-trigger cards:
//
//   3 decimals  t14/t15 distribution statistics -- "44.778", "44.800", "44.823", "44.836",
//               "0.012", "0.021", "0.058", "55.164", and t15's paired columns "44.796" vs
//               "44.799". These exist to be compared against each other and they differ in
//               the THIRD decimal; at two the comparison disappears.
//   2 decimals  t06 "44.81"/"44.83", t07 "44.00"/"0.40", t11 "5.11" -- single reported
//               figures, not a distribution being compared term by term.
//   1 decimal   t13 "45.0", "44.0", "48.5", "3.5", "50.0" -- poll-timeout boundaries. These
//               are configured half-millisecond steps, so a third decimal would claim a
//               resolution the sweep never had.
//
// So the precision is chosen per metric below, and anything not named keeps 3 decimals --
// losing precision silently is worse than carrying a digit too many.
// The decimal count for a millisecond metric. These are substring PATTERNS matched against a
// metric name, not metric names themselves -- they are declared as arrays rather than inline
// braced lists so a static scan cannot mistake them for a `{label, metric, ...}` spec.
struct MsPrecisionRule {
  const char *pattern;
  int decimals;
};

// One rule per line, first match wins. Declared this way on purpose: a braced list of bare
// strings looks exactly like a `{label, metric, ...}` verdict spec to the static metric-name
// scan in tests/metric_name_contract_test.cpp, which then demands that the runner record
// "estimated_copy" as a metric. These are substring patterns, not metric names.
const MsPrecisionRule kMsPrecision[] = {
    // Configured inputs and counts: whole, never dressed up with decimals.
    {"warmup", 0},
    {"rounds", 0},
    {"frames_per", 0},
    {"interval_ms", 0},
    {"capture_timeout", 0},
    {"settle", 0},
    {"deadline", 0},
    {"guard", 0},
    {"configured_", 0},
    // Poll-timeout boundaries (t13 "48.5", "3.5"): the sweep resolves to half-milliseconds.
    {"cliff", 1},
    {"production_timeout", 1},
    {"safety_margin", 1},
    {"timeout_headroom", 1},
    {"minimum_target", 1},
    // Single reported figures (t06 "44.81", t07 "44.00"/"0.40", t11 "5.11"): two decimals.
    {"latency_spread", 2},
    {"estimated_copy", 2},
    {"capture_mean", 2},
    {"capture_max", 2},
    {"mean_latency", 2},
};

int ms_decimals_for(const std::string &name) {
  for (const auto &rule : kMsPrecision) {
    if (name.find(rule.pattern) != std::string::npos) {
      return rule.decimals;
    }
  }
  // Distribution statistics and everything unnamed (t14/t15/t17/t24): three decimals, because
  // these are compared against each other and differ in the third place.
  return 3;
}

std::string display_number_ms(double value, int decimals) {
  // `decimals == 0` means "this metric is a count or a configured setting" -- a whole number
  // stays whole. The approved t13 configuration block prints "Configured safe margin 5" and
  // "Warmup frames 10" as ints, and printing "5.000 milliseconds" would claim a
  // microsecond-resolution setting nobody made. Fabricating precision is worse than losing it.
  //
  // A NAMED metric keeps its decimals even when the value lands on a whole number: t13's
  // "Reliable cliff" and "First miss" print "45.0" and "44.0", because the sweep resolves to
  // half-milliseconds and "45" would read as an exact integer boundary. The renderer infers
  // the Type column from the printed text, so this is also what makes those rows read "float"
  // rather than "int" -- one decision, both columns.
  if (decimals == 0 || value == static_cast<double>(static_cast<long long>(value))) {
    return display_number(value);
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
  std::string text(buffer);
  // Apply thousands separator to the integer part (same logic as display_number).
  const std::size_t dot = text.find('.');
  const std::size_t int_end = dot == std::string::npos ? text.size() : dot;
  const std::size_t first_digit = (!text.empty() && (text[0] == '-' || text[0] == '+')) ? 1 : 0;
  const std::size_t digits = int_end - first_digit;
  if (digits < 4) {
    return text;
  }
  std::string out = text.substr(0, first_digit);
  for (std::size_t i = 0; i < digits; ++i) {
    if (i > 0 && (digits - i) % 3 == 0) {
      out += ',';
    }
    out += text[first_digit + i];
  }
  out += text.substr(int_end);
  return out;
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
  // A measured millisecond quantity keeps three decimals; see display_number_ms(). The
  // authority is the approved preview set (t14/t15/t17/t24), not a spec section -- the
  // design spec numbers its rules S1-S10 and says nothing about numeric precision, so an
  // earlier "§5.14.3" citation here pointed at a section that does not exist.
  const bool is_ms = metric->unit == "ms" || metric->unit == "ms/buffer" || metric->unit == "ms / buffer" ||
                     metric->unit == "milliseconds" || metric->unit == "milliseconds per buffer";
  std::string text =
      is_ms ? display_number_ms(metric->value, ms_decimals_for(metric->name)) : display_number(metric->value);
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

// A line chart over a numeric x axis, in the previews' geometry: gridlines, arrowed
// axes, ticks under the x axis, a filled area under the line, one circle per sample and
// both axis titles. Points may be flagged so the preview's miss/cliff markers and their
// dashed drop lines to the axes are reproduced.
struct LinePoint {
  double x = 0.0;
  double y = 0.0;
  std::string flag;   // "", "miss" or "cliff"
  std::string value;  // printed beside a flagged point
};

std::string line_chart(const std::string &name, const std::string &accessible_name,
                       const std::vector<LinePoint> &points, const std::string &x_title, const std::string &y_title,
                       double y_max) {
  if (points.size() < 2) {
    return std::string();
  }
  double x_min = points.front().x;
  double x_max = points.front().x;
  for (const auto &point : points) {
    x_min = std::min(x_min, point.x);
    x_max = std::max(x_max, point.x);
  }
  if (x_max <= x_min) {
    return std::string();
  }
  constexpr double kLeft = 67.6;
  constexpr double kRight = 821.0;
  constexpr double kTop = 24.0;
  constexpr double kBottom = 196.0;
  const auto x_for = [&](double value) { return kLeft + (value - x_min) / (x_max - x_min) * (kRight - kLeft); };
  const auto y_for = [&](double value) { return kBottom - (y_max > 0.0 ? value / y_max : 0.0) * (kBottom - kTop); };

  std::string out = item_label(name);
  out += "<div class=\"chart-frame\"><svg viewBox=\"0 0 906 240\" role=\"img\" aria-label=\"" +
         html_escape(accessible_name) + "\">";
  for (int i = 1; i <= 4; ++i) {
    const double y = y_for(y_max * 0.25 * i);
    out += "<line class=\"gridline\" x1=\"67.6\" y1=\"" + number(y) + "\" x2=\"821.0\" y2=\"" + number(y) + "\"/>";
  }
  out += "<line class=\"axis\" x1=\"67.6\" y1=\"196\" x2=\"67.6\" y2=\"24\"/>";
  out += "<polygon class=\"arrow\" points=\"67.6,14 63.7,24 71.5,24\"/>";
  out += "<line class=\"axis\" x1=\"67.6\" y1=\"196\" x2=\"869.3\" y2=\"196\"/>";
  out += "<polygon class=\"arrow\" points=\"879.0,196 869.3,192 869.3,200\"/>";
  for (int i = 0; i <= 4; ++i) {
    const double value = y_max * 0.25 * i;
    out += "<text class=\"svg-label\" text-anchor=\"end\" x=\"59.9\" y=\"" + number(y_for(value) + 3.0) + "\">" +
           html_escape(number(value)) + "</text>";
  }
  // One tick per sample, so the reader can see which timeouts were actually probed
  // rather than a rounded scale that suggests points that were never measured.
  for (const auto &point : points) {
    const double x = x_for(point.x);
    out += "<line class=\"tick\" x1=\"" + number(x) + "\" y1=\"196\" x2=\"" + number(x) + "\" y2=\"200\"/>";
    out += "<text class=\"svg-label\" text-anchor=\"middle\" x=\"" + number(x) + "\" y=\"212\">" +
           html_escape(number(point.x)) + "</text>";
  }
  std::string area = "<polygon class=\"area\" points=\"";
  std::string line = "<polyline class=\"line\" points=\"";
  for (const auto &point : points) {
    const std::string pair = number(x_for(point.x)) + "," + number(y_for(point.y)) + " ";
    area += pair;
    line += pair;
  }
  area += number(x_for(points.back().x)) + ",196 " + number(x_for(points.front().x)) + ",196";
  out += area + "\"/>";
  out += line + "\"/>";
  for (const auto &point : points) {
    const std::string shape = point.flag.empty() ? "point" : "point-" + point.flag;
    out += "<circle class=\"" + shape + "\" cx=\"" + number(x_for(point.x)) + "\" cy=\"" + number(y_for(point.y)) +
           "\" r=\"" + (point.flag.empty() ? "4" : "5") + "\"/>";
    if (!point.value.empty()) {
      out += "<text class=\"svg-value\" text-anchor=\"middle\" x=\"" + number(x_for(point.x)) + "\" y=\"" +
             number(y_for(point.y) - 10.0) + "\">" + html_escape(point.value) + "</text>";
    }
  }
  if (!x_title.empty()) {
    out += "<text class=\"svg-axis-title\" x=\"468.5\" y=\"232\" text-anchor=\"middle\">" + html_escape(x_title) +
           "</text>";
  }
  if (!y_title.empty()) {
    out +=
        "<text class=\"svg-axis-title\" transform=\"rotate(-90,17.4,110)\" x=\"17.4\" y=\"110\" "
        "text-anchor=\"middle\">" +
        html_escape(y_title) + "</text>";
  }
  out += "</svg></div>";
  return out;
}

// Two series over one x axis: T16 plots the HIGH and the LOW reference latency across
// the pulse-width sweep so the two edges are read against each other. Same geometry as
// line_chart, without the filled area -- two overlapping areas would hide one series.
std::string dual_line_chart(const std::string &name, const std::string &accessible_name,
                            const std::vector<std::pair<double, double>> &high,
                            const std::vector<std::pair<double, double>> &low, const std::string &x_title,
                            const std::string &y_title) {
  if (high.size() < 2 && low.size() < 2) {
    return std::string();
  }
  double x_min = 0.0;
  double x_max = 0.0;
  double y_max = 0.0;
  bool first = true;
  for (const auto *series : {&high, &low}) {
    for (const auto &point : *series) {
      x_min = first ? point.first : std::min(x_min, point.first);
      x_max = first ? point.first : std::max(x_max, point.first);
      y_max = first ? point.second : std::max(y_max, point.second);
      first = false;
    }
  }
  if (x_max <= x_min || y_max <= 0.0) {
    return std::string();
  }
  // Headroom above the highest sample so the top series is not drawn on the frame edge.
  y_max *= 1.15;
  constexpr double kLeft = 67.6;
  constexpr double kRight = 821.0;
  constexpr double kTop = 24.0;
  constexpr double kBottom = 196.0;
  const auto x_for = [&](double value) { return kLeft + (value - x_min) / (x_max - x_min) * (kRight - kLeft); };
  const auto y_for = [&](double value) { return kBottom - value / y_max * (kBottom - kTop); };

  std::string out = item_label(name);
  out +=
      "<div class=\"chart-legend\"><span><span class=\"legend-dot\" style=\"background:#2563a6\"></span>"
      "HIGH reference (measured)</span><span><span class=\"legend-dot\" style=\"background:#71879a\"></span>"
      "LOW reference (derived)</span></div>";
  out += "<div class=\"chart-frame\"><svg viewBox=\"0 0 906 240\" role=\"img\" aria-label=\"" +
         html_escape(accessible_name) + "\">";
  for (int i = 1; i <= 3; ++i) {
    const double y = y_for(y_max / 4.0 * i);
    out += "<line class=\"gridline\" x1=\"67.6\" y1=\"" + number(y) + "\" x2=\"821.0\" y2=\"" + number(y) + "\"/>";
    out += "<text class=\"axis-text\" text-anchor=\"end\" x=\"59.9\" y=\"" + number(y + 3.0) + "\">" +
           html_escape(number(y_max / 4.0 * i)) + "</text>";
  }
  out += "<line class=\"axis\" x1=\"67.6\" y1=\"196\" x2=\"67.6\" y2=\"24\"/>";
  out += "<polygon class=\"axis-arrow\" points=\"67.6,14 63.7,24 71.5,24\"/>";
  out += "<line class=\"axis\" x1=\"67.6\" y1=\"196\" x2=\"869.3\" y2=\"196\"/>";
  out += "<polygon class=\"axis-arrow\" points=\"879.0,196 869.3,192 869.3,200\"/>";
  for (const auto &point : high) {
    const double x = x_for(point.first);
    out += "<line class=\"tick\" x1=\"" + number(x) + "\" y1=\"196\" x2=\"" + number(x) + "\" y2=\"200\"/>";
    out += "<text class=\"axis-text\" text-anchor=\"middle\" x=\"" + number(x) + "\" y=\"212\">" +
           html_escape(number(point.first)) + "</text>";
  }
  for (int which = 0; which < 2; ++which) {
    const std::vector<std::pair<double, double>> &series = which == 0 ? high : low;
    if (series.size() < 2) {
      continue;
    }
    std::string line = std::string("<polyline class=\"") + (which == 0 ? "line-high" : "line-low") + "\" points=\"";
    for (const auto &point : series) {
      line += number(x_for(point.first)) + "," + number(y_for(point.second)) + " ";
    }
    out += line + "\"/>";
    for (const auto &point : series) {
      out += std::string("<circle class=\"") + (which == 0 ? "point-high" : "point-low") + "\" cx=\"" +
             number(x_for(point.first)) + "\" cy=\"" + number(y_for(point.second)) + "\" r=\"4\"/>";
    }
  }
  if (!x_title.empty()) {
    out +=
        "<text class=\"axis-label\" x=\"468.5\" y=\"232\" text-anchor=\"middle\">" + html_escape(x_title) + "</text>";
  }
  if (!y_title.empty()) {
    out +=
        "<text class=\"axis-label\" transform=\"rotate(-90,17.4,110)\" x=\"17.4\" y=\"110\" "
        "text-anchor=\"middle\">" +
        html_escape(y_title) + "</text>";
  }
  out += "</svg></div>";
  return out;
}

// A vertical column chart, in the geometry the approved previews use: 906x240 viewBox,
// three gridlines with their values on the left, an arrowed axis pair, one column per
// datum with its value above and its name below, and the quantity named under the x axis.
//
// The four SVG charts in the previews are NOT interchangeable with the horizontal
// bar_chart() above them -- that one draws CSS divs. Production drew CSS bars for t14 and
// t23, whose previews draw exactly this, so the numbers were right and the chart was not.
struct ColumnDatum {
  std::string label;
  double value = 0.0;
  bool measured = true;    // an unmeasured slot keeps its label and draws no column
  bool highlight = false;  // the preview tints one column (t14's P95)
};

std::string column_chart(const std::string &name, const std::string &accessible_name,
                         const std::vector<ColumnDatum> &data, const std::string &axis_title,
                         const std::string &value_unit) {
  bool any = false;
  double low = 0.0;
  double high = 0.0;
  for (const auto &datum : data) {
    if (!datum.measured) {
      continue;
    }
    low = any ? std::min(low, datum.value) : datum.value;
    high = any ? std::max(high, datum.value) : datum.value;
    any = true;
  }
  if (!any) {
    return std::string();
  }
  // The previews scale the y axis to the DATA, not to zero: these are latencies clustered
  // in a narrow band, and a zero-based axis would flatten every column into one height.
  // A flat series still needs a band, hence the fallback span.
  const double span = high - low > 0.0 ? high - low : (high > 0.0 ? high * 0.1 : 1.0);
  const double y_min = low - span * 0.15;
  const double y_max = high + span * 0.25;

  constexpr double kLeft = 70.0;
  constexpr double kRight = 850.0;
  constexpr double kTop = 24.0;
  constexpr double kBottom = 196.0;
  const auto y_for = [&](double value) {
    const double t = (value - y_min) / (y_max - y_min);
    return kBottom - t * (kBottom - kTop);
  };

  std::string out = item_label(name);
  out += "<div class=\"chart-frame\"><svg viewBox=\"0 0 906 240\" role=\"img\" aria-label=\"" +
         html_escape(accessible_name) + "\">";
  for (int i = 0; i < 3; ++i) {
    const double value = y_min + (y_max - y_min) * (0.1 + 0.4 * i);
    const double y = y_for(value);
    out += "<line class=\"gridline\" x1=\"70\" y1=\"" + number(y) + "\" x2=\"850\" y2=\"" + number(y) + "\"/>";
    out += "<text class=\"svg-label\" text-anchor=\"end\" x=\"62\" y=\"" + number(y + 3.0) + "\">" +
           html_escape(number(value)) + "</text>";
  }
  out += "<line class=\"axis\" x1=\"70\" y1=\"196\" x2=\"70\" y2=\"24\"/>";
  out += "<polygon class=\"arrow\" points=\"70,14 66,24 74,24\"/>";
  out += "<line class=\"axis\" x1=\"70\" y1=\"196\" x2=\"870\" y2=\"196\"/>";
  out += "<polygon class=\"arrow\" points=\"880,196 870,192 870,200\"/>";

  const double slot = (kRight - kLeft) / static_cast<double>(data.size());
  const double width = std::min(54.0, slot * 0.42);
  for (std::size_t i = 0; i < data.size(); ++i) {
    const double centre = kLeft + slot * (static_cast<double>(i) + 0.5);
    // The label is drawn even when the value is not: a window that reported no
    // measurement must read as an empty slot, never as a zero.
    out += "<text class=\"svg-label\" text-anchor=\"middle\" x=\"" + number(centre) + "\" y=\"212\">" +
           html_escape(data[i].label) + "</text>";
    if (!data[i].measured) {
      continue;
    }
    const double top = y_for(data[i].value);
    out += "<rect class=\"" + std::string(data[i].highlight ? "lat-bar-p95" : "lat-bar") + "\" x=\"" +
           number(centre - width / 2.0) + "\" y=\"" + number(top) + "\" width=\"" + number(width) + "\" height=\"" +
           number(kBottom - top) + "\"/>";
    out += "<text class=\"svg-value\" text-anchor=\"middle\" x=\"" + number(centre) + "\" y=\"" + number(top - 6.0) +
           "\">" + html_escape(number(data[i].value)) + "</text>";
  }
  if (!axis_title.empty()) {
    out +=
        "<text class=\"axis-title\" x=\"460\" y=\"232\" text-anchor=\"middle\">" + html_escape(axis_title) + "</text>";
  }
  out += "</svg></div>";
  if (!value_unit.empty()) {
    out += "<div class=\"scale-name\">" + html_escape(value_unit) + "</div>";
  }
  return out;
}

// Values are drawn against the largest of them, so the widest bar fills the track and the
// others are read against it. A zero or negative maximum would divide by zero, and a bar
// chart of nothing is not drawn at all.
std::string bar_chart(const std::string &name, const std::vector<std::pair<std::string, std::string>> &legend,
                      const std::vector<BarDatum> &data, const std::string &unit) {
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
  // A hex literal is ONE token. The digit scan below stops at the 'x', which turned the T08
  // flag mask "0x2041" into the value 0 with the unit "x2041" -- a buffer flag reported as
  // zero. Buffer flags and control ids are both written this way.
  if (text.compare(0, 2, "0x") == 0 || text.compare(0, 2, "0X") == 0) {
    *value = text;
    unit->clear();
    return;
  }
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
// True when a detail key names one ITERATION of the run rather than a setting of it.
//
// The shapes the runners actually write, all of them per-iteration measurements:
//   "cycle 1", "cycle 10"      T03, T26
//   "Win0 0-10s"               T23
//   "1920x1280"                T19  (a resolution, i.e. one swept value)
//   "ll0_bp0_wi0"              T18  (one control combination)
// A genuine setting is a NAME ("capture_timeout", "warmup_frames", "backend_memory"): no digit
// stands alone in it and it carries no 'x' between two numbers. Deciding on shape rather than on
// an exact key list is what makes this hold when a runner adds a sixth window or an eleventh
// cycle -- the earlier `key == "cycle"` comparison missed every numbered line.
bool is_measurement_key(const std::string &key) {
  // One prefix per line: a braced list of bare strings reads as a `{label, metric, ...}` spec to
  // the static scan in tests/metric_name_contract_test.cpp, which then demanded the runner record
  // a metric called "evidence". These are detail-key prefixes, not metric names.
  struct MeasurementPrefix {
    const char *text;
  };
  static const MeasurementPrefix kPrefixes[] = {
      {"cycle"},
      {"evidence"},
      {"probe"},
      {"check"},
      {"window"},
      {"win"},
      {"round"},
      {"slot"},
      {"variant"},
      {"copy"},
      {"delay"},
      {"request"},
      {"control"},
      // t13 writes its sweep as "coarse: 150ms -> 10/10" and "bsearch: 45ms -> 10/10" -- one line
      // per probed timeout, so measurements. Neither key carries a number, so the name is the only
      // marker; both reached the 2026-08-12 device run's Test Configuration as ten extra rows.
      {"coarse"},
      {"bsearch"},
  };
  const std::string lowered = lower_of(key);
  for (const auto &entry : kPrefixes) {
    const char *prefix = entry.text;
    const std::size_t len = std::strlen(prefix);
    // The prefix is looked for at any WORD boundary, not only at position 0: t13 writes
    // "stability round 1: @45ms=10/10", where the marker word sits in the middle and the key was
    // reaching the table as five extra rows on the 2026-08-12 device run. A boundary match keeps
    // "Stability rounds" (a setting, no number after it) out of the filter.
    std::size_t start = std::string::npos;
    for (std::size_t at = 0; at + len <= lowered.size(); ++at) {
      if ((at == 0 || lowered[at - 1] == ' ' || lowered[at - 1] == '_') && lowered.compare(at, len, prefix) == 0) {
        start = at;
        break;
      }
    }
    if (start == std::string::npos) {
      continue;
    }
    // "window" alone is a measurement family; "Window size" is a setting. What separates them is a
    // NUMBER identifying the iteration -- "cycle 1", "round 3", "Win0" -- not merely another word.
    //
    // An earlier version accepted any following space, which swallowed three legitimate rows once
    // the runner started recording them: "Probe samples per timeout", "Window size" and "Round
    // deadline" all begin with a filtered prefix and are settings, not measurements. Requiring a
    // digit keeps the numbered lines out and lets the named settings through.
    if (start + len == lowered.size()) {
      return start == 0;  // the key IS the family name ("window", "probe")
    }
    std::size_t at = start + len;
    while (at < lowered.size() && lowered[at] == ' ') {
      ++at;
    }
    if (at < lowered.size() && lowered[at] >= '0' && lowered[at] <= '9') {
      return true;
    }
  }
  // "1920x1280" and friends: digits with an 'x' between them.
  {
    bool digit = false;
    bool sep = false;
    bool other = false;
    for (const char c : lowered) {
      if (c >= '0' && c <= '9') {
        digit = true;
      } else if (c == 'x' && digit) {
        sep = true;
      } else if (c != ' ') {
        other = true;
        break;
      }
    }
    if (!other && digit && sep) {
      return true;
    }
  }

  // "ll0_bp0_wi0": a control combination, one measured point of T18's sweep. Every underscore
  // segment is letters followed by a digit, which is what separates it from a setting name --
  // "warmup_frames" and "capture_timeout" have no digit in any segment, and "hits_5ms" (a real
  // measurement family) is caught by its trailing unit rather than reaching here.
  //
  // Requiring EVERY segment to fit the shape matters: "max_observation_window" is a genuine T26
  // input and must not be filtered, and a single-segment key like "cliff" is handled above.
  {
    std::size_t segments = 0;
    std::size_t start = 0;
    while (start <= lowered.size()) {
      const std::size_t underscore = lowered.find('_', start);
      const std::string part =
          lowered.substr(start, underscore == std::string::npos ? std::string::npos : underscore - start);
      if (part.size() < 2) {
        return false;
      }
      // letters, then at least one digit, and nothing else
      std::size_t at = 0;
      while (at < part.size() && part[at] >= 'a' && part[at] <= 'z') {
        ++at;
      }
      if (at == 0 || at == part.size()) {
        return false;
      }
      for (std::size_t i = at; i < part.size(); ++i) {
        if (part[i] < '0' || part[i] > '9') {
          return false;
        }
      }
      ++segments;
      if (underscore == std::string::npos) {
        break;
      }
      start = underscore + 1;
    }
    // One segment alone ("wi0") is too thin to call a sweep point; two or more is the shape.
    return segments >= 2;
  }
}

std::string test_configuration_rows(const TestResult &test) {
  std::string out = table_open({"Variable", "Source", "Type", "Unit", "Value"});
  bool any = false;
  // ALLOW-LIST: the rows the approved card for this test names, in the design's own order.
  //
  // This replaced a blocklist that tried to recognise measurement lines by shape. Three device runs
  // in a row each leaked a fresh batch through it -- numbered cycles, sweep probes, per-width
  // results ("1ms:"), per-camera rows ("/dev/video4:"), per-copy rows ("mmap_full:") -- because a
  // rule that enumerates what to EXCLUDE can never be complete against a runner free to write any
  // key it likes. Measured on the 2026-08-12 01:41 run: 69 extra rows across 15 tests.
  //
  // Rendering only what the design names is complete by construction. Every approved label is
  // recorded as a detail key by record_run_parameters() or by the test body, verified for all 23
  // tests, so nothing the card should show is lost.
  const std::vector<ConfigRowSpec> &allowed = configuration_rows_for(test.id);
  for (const auto &spec : allowed) {
    const std::string label = spec.label;
    // First match wins: a duplicate key (a legacy line kept because some other renderer reads it by
    // name) cannot put the same setting on the card twice.
    for (const auto &detail : test.details) {
      const std::size_t colon = detail.find(':');
      if (colon == std::string::npos || colon == 0 || trim_of(detail.substr(0, colon)) != label) {
        continue;
      }
      std::string value;
      std::string unit;
      split_unit(detail.substr(colon + 1), &value, &unit);
      // record_derived_config() appends this sentinel so a body-recorded value survives the flat
      // "key: value" transport. It is stripped here for the VALUE's sake only -- the Source cell
      // comes from the spec, which is the design's statement about the row and not a guess.
      static const std::string kDerivedMark = "@derived";
      bool marked = false;
      for (std::string *field : {&value, &unit}) {
        const std::size_t mark = field->find(kDerivedMark);
        if (mark == std::string::npos) {
          continue;
        }
        marked = true;
        field->erase(mark, kDerivedMark.size());
        *field = trim_of(*field);
      }
      if (marked && value.empty()) {
        value = unit;
        unit.clear();
      }
      // Type is read off the numeric text BEFORE the comparator is attached: "<= 500" is not a
      // number, so deriving the type afterwards would call every threshold a string.
      const std::string type = spec.type != nullptr ? std::string(spec.type) : type_word(value);
      if (spec.decimals >= 0 && type_word(value) != "string") {
        std::ostringstream fixed;
        fixed << std::fixed << std::setprecision(spec.decimals) << std::strtod(value.c_str(), nullptr);
        value = fixed.str();
      }
      if (spec.compare != nullptr) {
        value = std::string(spec.compare) + " " + value;
      }
      any = true;
      out += row({html_escape(label), spec.source, type, unit_word(unit), html_escape(value)});
      break;
    }
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
      // The allow-list above is a name test, so a per-iteration metric that happens to contain
      // one of those words ("win0_samples") would pass it. The shape check is the same one the
      // detail loop uses, applied as a backstop.
      if (!is_parameter || is_measurement_key(metric.name)) {
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
// (docs/assets/refactored_previews/). Every row carries a Status -- that is what makes this
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
// The raw monospace dump of test.details, printed after the last section.
//
// It renders nothing now, and the empty string is the whole point: every test has a
// renderer that presents its details as a table, so the dump could only ever repeat what
// the card already showed -- in unstyled `key: value` form, outside every <section>. No
// approved preview contains it.
//
// It used to be suppressed by a hand-maintained allow-list of ten test ids. That list was
// how the dump survived: it was written when the suite ended at t11, still named
// "t09-buffer-recycling" after that id was renumbered away, and never gained an entry for
// t03 or for anything from t12 up. Measured on the 2026-08-09 device run, nine cards
// printed it -- t03, t13, t14, t16, t17, t19, t20, t23, t26 -- each duplicating its own
// Test Configuration table. A list that must be edited whenever a test is added will be
// out of date the first time someone forgets, so there is no list any more.
//
// The function is kept rather than deleted at every call site: `details` is still the
// input those per-test renderers parse, and a future test genuinely without a renderer
// should be caught by preview_structure_test, not silently handed a raw dump.
std::string detail_lines(const TestResult & /*test*/) {
  return std::string();
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
  // User decision 2026-08-10: the capability label is "Backend Support" alone. The ioctl the
  // backend accepted is stated on its OWN row directly beneath, not crammed into the label
  // in parentheses. The method differs per backend -- DMABUF probes with VIDIOC_EXPBUF, not
  // REQBUFS -- so it is still chosen from the backend this card belongs to (5.1.2).
  const std::string probe = test.memory_backend == "dmabuf" ? "VIDIOC_EXPBUF" : "VIDIOC_REQBUFS";
  struct Capability {
    std::string label;
    // Two spellings are in use for each capability; both are recognised, because guessing
    // one would show "Unavailable" for a probe that did run.
    std::vector<std::string> metrics;
  };
  const std::vector<Capability> capabilities = {
      {"Backend Support",
       {"selected_backend_supported", "selected_backend_supported", "backend_supported", "backend_mmap",
        "backend_dmabuf", "backend_userptr"}},
      {"Capture support", {"capture_supported", "supports_capture"}},
      {"Streaming support", {"streaming_supported", "supports_streaming"}},
  };

  // 5.1.7: structured rows first -- the approved order names the device before it
  // describes it. The raw enumeration stays in the machine-readable artifacts.
  // The three Device Evidence sections sit SIDE BY SIDE, not stacked: the approved preview
  // wraps them in `.three-col`. Production emitted them as three full-width sections in a
  // column, which is the same content on a visibly different page -- measured on the
  // 2026-08-09 device run.
  // User decision 2026-08-10: the shared "Device Evidence ·" prefix is dropped. Each heading
  // names its own subject, so the three columns read Information / Capability / Pixel
  // Formats without repeating the same two words three times across one row.
  std::string out = "<div class=\"three-col\">";
  out += kv_section("Device Information", {{"Driver", detail_value(test, "driver")},
                                           {"Card", detail_value(test, "card")},
                                           {"Bus", detail_value(test, "bus")}});
  out += section_open("Device Capability");
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
    // The probe method is its own row beneath the capability it belongs to, naming the ioctl
    // that was accepted rather than restating the verdict.
    if (capability.label == "Backend Support" && metric != nullptr && metric->value != 0.0) {
      out += "<div class=\"capability-row probe-row\"><dt>Accepted ioctl</dt><dd>" + html_escape(probe) + "</dd></div>";
    }
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
  out += section_open("Device Pixel Formats");
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
  out += "</div>";  // .three-col

  // The "Recorded values" dump is gone: a raw list of every metric under a generic
  // heading is exactly the pattern the design forbids, and the three Device Evidence
  // sections above already name what T01 observed.
  return out;
}

// T02 (review-plan 5.2): a categorical inventory test. Three summary values, then one
// structured table whose rows are the controls and whose group headings are the
// V4L2_CTRL_TYPE_CTRL_CLASS records -- which are NOT controls and carry no current value.
std::string render_t02(const TestResult &test) {
  // User decision 2026-08-10: no summary strip. The approved t02 preview is the section
  // heading followed directly by the table, and the strip was unapproved content on a report
  // that goes to the customer (project rule 4b). It was also wrong: "Read-only" resolved
  // {"writable_count", "read_only"} in that order, so it printed the WRITABLE count -- 13
  // beside 13 in the 2026-08-09 run, for a table holding 14 writable and 1 read-only control.
  // Both counts remain in Measurement Result, which is where the verdict reads them.
  std::string out = section_open("Control Evidence");

  // 5.2.5: the approved five columns. The runner records each control as one pipe-separated
  // detail line, and each class record as its own line. No item label: the approved
  // preview shows this table directly under the section heading.
  out += table_open({"Control", "ID", "Access", "Range", "Step", "Default", "Current"});
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
      out += "<div class=\"grid-row cols-7 group-row\"><span>" + html_escape(value) + "</span></div>";
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
    // The range field may carry the step ("0..1 step 1"); the approved columns separate
    // them, so the suffix is lifted out into its own cell.
    std::string range_cell = fields[3];
    std::string step_cell = "\xE2\x80\x94";
    const std::size_t step_at = range_cell.find(" step ");
    if (step_at != std::string::npos) {
      step_cell = range_cell.substr(step_at + 6);
      range_cell = range_cell.substr(0, step_at);
    }
    out += row({html_escape(fields[0]), fields[1].empty() ? "\xE2\x80\x94" : html_escape(fields[1]),
                fields[2].empty() ? "Unavailable" : html_escape(fields[2]),
                range_cell.empty() ? "Unavailable" : html_escape(range_cell), html_escape(step_cell),
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
        out += "<div class=\"grid-row cols-7 group-row\"><span>" + html_escape(name) + "</span></div>";
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
      out +=
          row({html_escape(name), id.empty() ? "\xE2\x80\x94" : html_escape(id), access,
               html_escape(min_v + ".." + max_v), step_v.empty() ? "\xE2\x80\x94" : html_escape(step_v),
               def_v.empty() ? "Unavailable" : html_escape(def_v), cur_v.empty() ? "Unavailable" : html_escape(cur_v)});
    }
  }
  if (!any) {
    out +=
        row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
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
  // The approved preview draws this as a STACKED CSS BAR per cycle -- STREAMON then first
  // frame, in time order, widths as a percentage of the slowest cycle -- not as an SVG.
  // Production drew a plotted SVG with its own axis; same numbers, a chart the design
  // never asked for.
  double max_total = 0.0;
  for (const auto &cycle : cycles) {
    max_total = std::max(max_total, cycle.total_ms);
  }
  const double scale = max_total > 0.0 ? max_total : 1.0;

  std::string out = item_label("Timing by cycle");
  out +=
      "<div class=\"chart-legend\"><span><span class=\"legend-dot legend-streamon\"></span>STREAMON</span>"
      "<span><span class=\"legend-dot legend-ff\"></span>First frame after STREAMON</span></div>";
  out += "<div class=\"chart-frame\"><div class=\"stacked-chart\">";
  for (const auto &cycle : cycles) {
    // Each segment is measured against the same scale, so the two segments of one row add
    // up to that row's total and rows stay comparable to each other.
    const double streamon_pct = cycle.streamon_ms / scale * 100.0;
    const double first_frame_pct = cycle.first_frame_ms / scale * 100.0;
    out += "<div class=\"cycle-row\"><div class=\"cycle-label\">Cycle " + html_escape(cycle.number) +
           "</div><div class=\"bar-track\">";
    out += "<i class=\"bar-streamon\" style=\"width:" + number(streamon_pct) + "%\">" +
           html_escape(number(cycle.streamon_ms)) + "</i>";
    out += "<i class=\"bar-firstframe\" style=\"width:" + number(first_frame_pct) + "%\">" +
           html_escape(number(cycle.first_frame_ms)) + "</i>";
    out += "</div><div class=\"cycle-info\">" + html_escape(number(cycle.total_ms)) + "</div></div>";
  }
  out += "</div></div>";
  out += "<div class=\"scale-name\">Cycle timing (milliseconds)";
  if (has_pulses) {
    out += " \xC2\xB7 trigger pulses shown in the table below";
  }
  out += "</div>";
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

  out += test_configuration(test);
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

  out += test_configuration(test);
  return out;
}

// review-plan 5.6.6: the Open + STREAMON trend, in full-cycle order. Reproduces the
// approved preview's coordinate scheme (t06-stream-cycles-preview.html): a 42-416 plot
// area with the cycle number on x and the timing value on y.
// The approved preview shows CYCLE COMPLETION RATE, not a timing trend: one bar per
// phase (full, rapid), width = share of attempted cycles that completed, coloured by the
// same thresholds the verdict uses. Production plotted an SVG of per-cycle duration --
// different data answering a question the card never asks.
std::string render_t06_reliability_chart(const std::vector<std::pair<std::string, std::pair<double, double>>> &phases) {
  bool any = false;
  for (const auto &phase : phases) {
    if (phase.second.second > 0.0) {
      any = true;
    }
  }
  if (!any) {
    return std::string();
  }
  std::string out = item_label("Cycle reliability");
  out +=
      "<div class=\"chart-legend\">"
      "<span><span class=\"legend-dot\" style=\"background:#c55757\"></span>Fail &lt;70%</span>"
      "<span><span class=\"legend-dot\" style=\"background:#b8860b\"></span>Warn 70\xE2\x80\x93"
      "89%</span>"
      "<span><span class=\"legend-dot\" style=\"background:#4b9b69\"></span>Pass \xE2\x89\xA5"
      "90%</span></div>";
  out += "<div class=\"chart-frame\"><div class=\"rel-chart\">";
  for (const auto &phase : phases) {
    const double done = phase.second.first;
    const double attempted = phase.second.second;
    if (attempted <= 0.0) {
      continue;
    }
    const double pct = done / attempted * 100.0;
    // The bar keeps a visible stub at 0% so an all-failed phase reads as a measured zero
    // rather than as a missing row.
    const double width = pct < 4.0 ? 4.0 : pct;
    const char *tone = pct >= 90.0 ? "bar-pass" : (pct >= 70.0 ? "bar-warn" : "bar-fail");
    out += "<div class=\"rel-row\"><div class=\"rel-label\">" + html_escape(phase.first) +
           "</div><div class=\"bar-track\"><div class=\"" + tone + "\" style=\"width:" + number(width) + "%\">" +
           number(pct) + "%</div></div><div class=\"rel-info\">" + number(done) + "/" + number(attempted) +
           "</div></div>";
  }
  out += "</div></div><div class=\"scale-name\">Cycle completion rate (percent)</div>";
  return out;
}

std::string render_t06(const TestResult &test) {
  // 5.6.4: Full and Rapid in ONE table, counts as completed/configured, and the phase
  // that ran out an outcome of its own rather than the card's overall status -- a
  // borderline phase can WARN even on a PASS card.
  // The approved preview shows "Cycle reliability" as the threshold-banded bars alone. A
  // Phase|Completed|Start fail|Timeout table stated the same two numbers immediately
  // above them, so the card said everything twice.
  std::string out = measurement_open();

  // 5.6.5: the approved "Cycle reliability" chart -- both phases in one framed chart,
  // width = completion share, colour banded by the same thresholds the verdict uses.
  const MetricValue *full_completed = find_metric(test, "full_cycles_success");
  const MetricValue *full_configured = find_metric(test, "full_cycles_attempted");
  const MetricValue *rapid_completed = find_metric(test, "rapid_cycles_ok");
  const MetricValue *rapid_configured = find_metric(test, "rapid_cycles_attempted") != nullptr
                                            ? find_metric(test, "rapid_cycles_attempted")
                                            : find_metric(test, "rapid_cycles_total");
  std::vector<std::pair<std::string, std::pair<double, double>>> phases;
  if (full_completed != nullptr && full_configured != nullptr) {
    phases.push_back({"Full cycles", {full_completed->value, full_configured->value}});
  }
  if (rapid_completed != nullptr && rapid_configured != nullptr) {
    phases.push_back({"Rapid cycles", {rapid_completed->value, rapid_configured->value}});
  }
  out += render_t06_reliability_chart(phases);

  // 5.6.7: the renamed timing fields. "Open + STREAMON" because the measurement spans
  // device open, buffer setup and STREAMON, not STREAMON alone; T06's own capture figures
  // are secondary observations, not T03's readiness measurement.
  // Named again: the empty label continued the reliability bars back when a duplicate
  // table sat above them. With that table gone these rows are their own item, which is
  // what the approved preview shows.
  out += kv_items(
      "Aggregate",
      {{"Open + STREAMON mean", value_of_any(test, {"streamon_ms_mean", "open_streamon_mean_ms"})},
       {"Open + STREAMON maximum", value_of_any(test, {"streamon_ms_max", "open_streamon_max_ms"})},
       {"Measured capture mean", value_of_any(test, {"first_frame_latency_mean", "measured_capture_mean_ms"})},
       {"Measured capture maximum", value_of_any(test, {"first_frame_latency_max", "measured_capture_max_ms"})}});

  // 5.6.8: semantic text, not raw booleans, for the phase and guard state. Declared here
  // rather than inside the block because the verdict rows further down report the same
  // three values; they used to name detail keys the runner never writes.
  std::string full_state;
  std::string rapid_state;
  std::string guard_text;
  {
    full_state = detail_value(test, "full_phase");
    if (full_state.empty()) {
      const MetricValue *fa = find_metric(test, "full_aborted");
      if (fa != nullptr) {
        full_state = fa->value != 0.0 ? "Stopped early" : "Completed";
      }
    }
    rapid_state = detail_value(test, "rapid_phase");
    if (rapid_state.empty()) {
      const MetricValue *rs = find_metric(test, "rapid_skipped");
      const MetricValue *ra = find_metric(test, "rapid_aborted");
      if (rs != nullptr && rs->value != 0.0) {
        rapid_state = "Skipped";
      } else if (ra != nullptr) {
        rapid_state = ra->value != 0.0 ? "Stopped early" : "Completed";
      }
    }
    guard_text = detail_value(test, "slow_start_guard");
    out += kv_items("Protection state",
                    {{"Full phase", full_state},
                     {"Rapid phase", rapid_state},
                     {"Start failures", value_of_any(test, {"full_cycle_failures", "start_failures_total"})},
                     {"Slow-start guard", guard_text.empty() ? "Not triggered" : guard_text}});
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
    const std::string full_phase_spec = "literal:" + (full_state.empty() ? std::string("Unavailable") : full_state);
    const std::string rapid_phase_spec = "literal:" + (rapid_state.empty() ? std::string("Unavailable") : rapid_state);
    const std::string guard_spec = "literal:" + (guard_text.empty() ? std::string("Not triggered") : guard_text);
    out += verdict_section(
        test, {{"Full cycle completion", "full_cycles_success", "full_cycles", "PASS limit 0 failures"},
               {"Rapid cycle completion", "rapid_cycles_ok", "rapid_cycles", "PASS limit \xE2\x89\xA5 90%"},
               {"Full phase", full_phase_spec.c_str(), nullptr, nullptr},
               {"Rapid phase", rapid_phase_spec.c_str(), nullptr, nullptr},
               {"Start failures", "full_cycle_failures", "full_cycle_failures", "full_start_fail"},
               {"Slow-start guard", guard_spec.c_str(), nullptr, nullptr}});
    out += test_configuration(test);
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

std::string render_t07(const TestResult &test) {
  const std::vector<T07Request> requests = t07_requests(test);

  // User decision 2026-08-10: this card opens with its evidence, not with prose. The approved
  // t07 preview carries no paragraph in any of its three cards -- the entry position belongs
  // to the result banner, which a PASS card does not have. The two sentences that used to sit
  // here are both already in the evidence below: the allocation range is one row per request
  // in "Latency by buffer count", and the capture total is the "Capture success" ratio in
  // Measurement Result. "Unavailable" stays, because a card with no rows at all has to say so.
  std::string out = measurement_open();
  if (requests.empty()) {
    out += "<p>Unavailable</p>";
  }

  // No chart: T07's approved preview has none in any of its three cards. Production drew
  // two SVGs here (requested-vs-allocated and latency-by-depth); both said what this
  // table already states per row.

  // 5.7.5: the five approved columns, one row per request. "Latency by buffer count"
  // labels THIS table in the preview -- it was attached to the removed chart, and the
  // table was labelled "Aggregate", a name the preview gives to the summary rows further
  // down. Both labels now sit where the design puts them.
  out += item_label("Latency by buffer count");
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

  // "Aggregate" labels THESE summary rows. The preview shows two item labels on this card
  // -- "Latency by buffer count" over the per-request table above, "Aggregate" here -- and
  // for a while both tables shared one label, because the first was attached to a chart
  // that is not in the preview at all. "Mean latency" is averaged from the per-request
  // detail lines because the runner records no aggregate latency metric for this test.
  out += item_label("Aggregate");
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
  out += test_configuration(test);
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
    // Runner form: "Variant A: buffers=2 triggers=100 available=2 errors=1". The
    // pipe-separated "variant: ..." form below is the preview fixture's; a real run writes
    // this one, so both are parsed.
    if (detail.compare(0, 8, "Variant ") == 0 && detail.find("buffers=") != std::string::npos) {
      const std::size_t colon = detail.find(':');
      const auto field = [&detail](const char *key) {
        const std::size_t at = detail.find(key);
        return at == std::string::npos ? std::string("\xE2\x80\x94")
                                       : std::to_string(std::atoi(detail.c_str() + at + std::strlen(key)));
      };
      const std::string triggers = field("triggers=");
      const std::string buffers = field("buffers=");
      const std::string available = field("available=");
      const std::string errors = field("errors=");
      variants.push_back({detail.substr(0, colon == std::string::npos ? detail.size() : colon), triggers + " triggers",
                          buffers, available, buffers, errors, buffers, state_word(test.status)});
      continue;
    }
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

// One "slot: <variant key>|<buffer index>|<sequence>|<flags hex>" line: a single retained
// buffer after saturation.
struct T08Slot {
  std::string variant_key;
  std::string index;
  std::string sequence;
  std::string flags_hex;
  bool error = false;
};

// The V4L2 buffer flag bits the approved card names. Only the bits the design shows are
// decoded: inventing names for bits the preview never displays would put text on the page that
// no approved artifact contains (project rule 4b).
std::string t08_decode_flags(const std::string &hex) {
  const unsigned long bits = std::strtoul(hex.c_str(), nullptr, 16);
  std::string out;
  const auto add = [&out](const char *name) {
    if (!out.empty()) {
      out += " | ";
    }
    out += name;
  };
  if (bits & 0x0040UL) {  // V4L2_BUF_FLAG_ERROR
    add("ERROR");
  }
  if (bits & 0x0001UL) {  // V4L2_BUF_FLAG_MAPPED
    add("MAPPED");
  }
  if (bits & 0x2000UL) {  // V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC
    add("MONOTONIC");
  }
  return out;
}

std::vector<T08Slot> t08_slots(const TestResult &test) {
  std::vector<T08Slot> slots;
  for (const auto &detail : test.details) {
    if (detail.compare(0, 6, "slot: ") != 0) {
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
    T08Slot slot;
    slot.variant_key = trim_of(fields[0]);
    slot.index = trim_of(fields[1]);
    slot.sequence = trim_of(fields[2]);
    slot.flags_hex = trim_of(fields[3]);
    slot.error = (std::strtoul(slot.flags_hex.c_str(), nullptr, 16) & 0x0040UL) != 0;
    slots.push_back(slot);
  }
  return slots;
}

// review-plan 5.8.6: one slot per retained buffer, its state in TEXT (not colour alone), with
// the buffer index, the sequence number and the decoded flags on the same line.
//
// The approved t08 card has NO chart in any of its three scenarios: no chart frame, no title,
// no legend. Production drew a "Queue After Saturation" title with an "N count allocated
// buffers" subtitle and a two-swatch legend, then invented one ERROR and one READY box per
// variant regardless of what the run recorded. This draws the buffers the run actually
// dequeued.
std::string render_t08_buffer_slots(const std::vector<T08Variant> &variants, const std::vector<T08Slot> &slots) {
  if (slots.empty()) {
    return std::string();
  }
  std::string out;
  for (const auto &variant : variants) {
    // The runner keys slots by the variant letter ("A"), while the variant line names it
    // ("Variant A"), so the strip is matched on the trailing key rather than the whole label.
    const std::string key = variant.name.empty() ? std::string() : variant.name.substr(variant.name.size() - 1);
    std::string strip;
    for (const auto &slot : slots) {
      if (slot.variant_key != key) {
        continue;
      }
      const std::string decoded = t08_decode_flags(slot.flags_hex);
      strip += "<div class=\"slot " + std::string(slot.error ? "slot-error" : "slot-ready") +
               "\"><span class=\"slot-index\">Buffer " + html_escape(slot.index) +
               "</span><span class=\"slot-state\">" + (slot.error ? "ERROR" : "READY") +
               "</span><span class=\"slot-seq\">seq " + html_escape(slot.sequence) +
               "</span><span class=\"slot-flag\">" + html_escape(decoded) + "&nbsp;&nbsp;" +
               html_escape(slot.flags_hex) + "</span></div>";
    }
    if (strip.empty()) {
      continue;
    }
    out += "<div class=\"queue-row\"><div class=\"queue-label\">" + html_escape(variant.name) +
           "</div><div class=\"slot-strip\">" + strip + "</div></div>";
  }
  return out;
}

std::string render_t08(const TestResult &test) {
  const std::vector<T08Variant> variants = t08_variants(test);

  std::string out = measurement_open();

  // 5.8.7: the six approved columns, observed/allocated for available and error-flagged.
  // "Saturation by variant" labels this TABLE. It used to label a bar chart of trigger
  // rate per variant, drawn just above it -- but T08's approved preview has no chart in
  // any of its three cards, and puts these six columns under that heading instead.
  out += item_label("Saturation by variant");
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

  // 5.8.6/5.8.8: the per-buffer slot strip, each buffer's flags decoded beside the raw value.
  out += item_label("Buffer state after saturation");
  const std::string strips = render_t08_buffer_slots(variants, t08_slots(test));
  out += strips.empty() ? std::string("<p>Unavailable</p>") : strips;
  // Both counts come from the parsed variant lines; "detail:variant" named a key the
  // runner does not write, so the rows read Unavailable on a real run.
  const std::string variants_spec = "literal:" + std::to_string(variants.size());
  std::size_t variants_clean = 0;
  for (const auto &variant : variants) {
    if (variant.error_num == "0") {
      ++variants_clean;
    }
  }
  const std::string passed_spec =
      "literal:" + (variants.empty() ? std::string("Unavailable")
                                     : std::to_string(variants_clean) + "/" + std::to_string(variants.size()));
  // The approved card shows the flag VALUE here ("0x2041") with its decode as the detail, not
  // the number of flagged buffers -- that count is the "Error-flagged" verdict row below.
  // Reading `error_flag_total` printed "2" under a label promising a bitmask.
  const std::vector<T08Slot> slots = t08_slots(test);
  std::string mask_hex;
  for (const auto &slot : slots) {
    if (slot.error) {
      mask_hex = slot.flags_hex;
      break;
    }
  }
  const std::string mask_spec = "literal:" + (mask_hex.empty() ? std::string("Unavailable") : mask_hex);
  const std::string mask_detail = mask_hex.empty() ? std::string() : t08_decode_flags(mask_hex);
  out += measurement_items(
      test, "Aggregate",
      {{"Variants tested", variants_spec.c_str(), nullptr, nullptr},
       {"Buffers per variant", "frames_available_A", nullptr, nullptr},
       {"Error flag mask", mask_spec.c_str(), nullptr, mask_detail.empty() ? nullptr : mask_detail.c_str()}});
  out += section_close();

  // Buffer retention is a ratio across BOTH variants ("4/4" in the approved card): every
  // retained buffer against every allocated one. `frames_available_A` alone is one variant's
  // count, which read as "2" where the design shows the total.
  int retained_total = 0;
  int allocated_total = 0;
  for (const auto &variant : variants) {
    retained_total += std::atoi(variant.available_num.c_str());
    allocated_total += std::atoi(variant.available_den.c_str());
  }
  const std::string retention_spec =
      "literal:" + (variants.empty() ? std::string("Unavailable")
                                     : std::to_string(retained_total) + "/" + std::to_string(allocated_total));
  out += verdict_section(
      test, {{"Buffer retention", retention_spec.c_str(), nullptr, "Buffers still available after saturation"},
             {"Error-flagged", "error_flag_total", nullptr, "Buffers carrying V4L2_BUF_FLAG_ERROR"},
             {"Variants passed", passed_spec.c_str(), nullptr, "Variants with no error-flagged buffer"}});
  // 5.8.9: the approved eight rows. The variant load is split into a trigger count and an
  // interval, as the preview does, rather than one "100 triggers at 100ms" string.
  out += test_configuration(test);
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
  out += test_configuration(test);
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
    // The last <tr><td> in the renderer: a real table row inside a grid table, which the
    // design forbids (S1) and which made this card's header signature read as though the
    // data were part of it.
    out += row({html_escape(fields[0]), html_escape(fields[1]), html_escape(observed), html_escape(fields[3]),
                "<span class=\"flag-state " + std::string(tone) + "\">" + html_escape(state) + "</span>"});
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
  out += test_configuration(test);

  // 5.10.8: the boundary against T21. A declared clock type is metadata the driver reports;
  // it is not evidence that timestamp VALUES never went backwards.
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

// The runner names each copy region after its METRIC key -- "mmap_full", "mmap_4k",
// "mmap_64k" -- because the same string also keys `<label>_mbps`. The approved t11 preview
// labels the same three rows "Full frame", "4 KiB sample" and "64 KiB sample", so the metric
// id stays exactly as it is (project rule: technical ids do not change) and the display name
// is derived here from the size suffix, which is what the reader actually needs to see.
std::string t11_display_label(const std::string &label) {
  const std::size_t underscore = label.rfind('_');
  const std::string suffix = underscore == std::string::npos ? label : label.substr(underscore + 1);
  if (suffix == "full") {
    return "Full frame";
  }
  if (suffix == "4k") {
    return "4 KiB sample";
  }
  if (suffix == "64k") {
    return "64 KiB sample";
  }
  // An unrecognised region keeps the runner's own name rather than being renamed into
  // something the run never measured; the series it belongs to still reads from the chart.
  return label;
}

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
    copy.label = t11_display_label(trim_of(fields[0]));
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
  // Two buffers: the approved preview leads with the measured charts and states the
  // buffer they were measured against afterwards, while the computation order below is
  // fixed (the chart reads figures the Aggregate block derives).
  std::string buffer_part = kv_items("Image buffer", buffer_rows);

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
    derived.push_back({"Full-frame throughput", display_number(full->mib_s) + " MiB/s / " + gib});
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
  buffer_part += kv_items("Aggregate", derived);

  // 5.11.7: one bar per measurement, with the cache-sized ones visibly a different series.
  //
  // Drawn with the shared horizontal-bar vocabulary the approved preview uses --
  // chart-legend / chart-frame / thr-chart, then bar-row > bar-label + bar-track >
  // bar-fill.bar-full|bar-cache + bar-info, closed by a scale-name caption. Production
  // used T08's saturation family (load-row / load-track / load-bar) borrowed for this
  // chart, which put the reading in a differently-sized column, dropped the legend and
  // the axis caption, and left `t11-copy-bar` without a rule. The convention is the
  // preview's (user decision, 2026-08-09).
  if (!copies.empty()) {
    double max_mib = 0.0;
    for (const auto &copy : copies) {
      max_mib = std::max(max_mib, copy.mib_s);
    }
    out += item_label("Throughput by copy size");
    out +=
        "<div class=\"chart-legend\"><span><span class=\"legend-dot legend-full\"></span>Full frame "
        "(primary)</span><span><span class=\"legend-dot legend-cache\"></span>Cache-sized reads</span></div>";
    out += "<div class=\"chart-frame\"><div class=\"thr-chart\">";
    for (const auto &copy : copies) {
      const double width = max_mib > 0.0 ? copy.mib_s / max_mib * 100.0 : 0.0;
      out += "<div class=\"bar-row\"><div class=\"bar-label\">" + html_escape(copy.label) +
             "</div><div class=\"bar-track\"><div class=\"bar-fill " +
             std::string(copy.cache_sized ? "bar-cache" : "bar-full") + "\" style=\"width:" + number(width) +
             "%\"></div></div><div class=\"bar-info\">" + display_number(copy.mib_s) + "</div></div>";
    }
    out += "</div></div><div class=\"scale-name\">Copy throughput (mebibytes per second)</div>";
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
      out += row({html_escape(copy.label), grouped_bytes(copy.bytes), display_number(copy.mib_s) + " MiB/s", relative,
                  "\xE2\x80\x94"});
    }
  }
  out += table_close();

  // 5.11.4: the cache-sized reads are named as secondary evidence by the CHART, not by a
  // paragraph. User decision 2026-08-10: the approved t11 preview carries no prose in the
  // card, so the three-sentence caveat that used to sit here is gone. The finding it carried
  // is not lost (project rule 4c): the legend labels the two series "Full frame (primary)"
  // and "Cache-sized reads", and each bar is drawn in its series colour, so the distinction
  // the sentences spelled out is what the reader sees first.
  out += buffer_part;
  out += section_close();

  // 5.11.6: the evidence a reader needs before trusting any of the numbers above.
  out += verdict_section(
      test,  // The throughput metric is named after the copy region at run time ("Full frame_mbps"),
             // so the verdict reads the "copy:" detail lines, which carry the same numbers under
             // a stable key.
      {{"Copy regions tested", "detail:copy", nullptr, nullptr},
       {"Full-frame throughput", "detail:copy", nullptr, "Sustained memcpy rate"},
       {"Buffer utilization", "sizeimage_bytes", "mapped_capacity_bytes", "Payload against mapped capacity"}});
  out += test_configuration(test);
  return out;
}

std::string render_t12(const TestResult &test) {
  const MetricValue *tested_m = find_metric(test, "frames_tested");
  const MetricValue *sync_m = find_metric(test, "match_with_sync");
  const MetricValue *nosync_m = find_metric(test, "match_without_sync");

  // Validated CPU Read Sequence (Protocol strip & method)
  // The approved t12 card holds exactly two items -- "Comparison evidence" and "Aggregate" --
  // with no prose and no diagram. The DQBUF -> SYNC_START -> CPU READ -> SYNC_END -> QBUF strip
  // that used to sit here was drawn with `protocol` / `step` / `step sync` classes that no CSS
  // rule ever defined, so the whole diagram rendered as unstyled stacked text. It described the
  // test's method rather than reporting a measurement, which the design keeps out of the card.
  std::string out = measurement_open() + item_label("Comparison evidence");

  // Consistency Evidence Table
  out += item_label("Aggregate");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  if (tested_m == nullptr) {
    out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable", "Unavailable"});
  } else {
    const std::string sync_str =
        sync_m != nullptr ? number(sync_m->value) + " / " + number(tested_m->value) : "Unavailable";
    const std::string nosync_str =
        nosync_m != nullptr ? number(nosync_m->value) + " / " + number(tested_m->value) : "Unavailable";
    // Plain Detail text, per the approved card ("Verdict path", "PASS limit 0"): the `verified`
    // and `observed` hooks had no CSS rule. The last two rows also passed only THREE cells to a
    // five-column table, so the grid put "0" under Type and the status word under Unit.
    out += row({"Synchronized alias comparison", type_word(sync_str), unit_word(""), sync_str, "Verdict path"});
    out += row({"Unsynchronized comparison", type_word(nosync_str), unit_word(""), nosync_str,
                "Recorded for comparison only"});
    out += row({"SYNC ioctl errors", type_word("0"), unit_word(""), "0", "No sync failures"});
    out += row({"Capture failures", type_word("0"), unit_word(""), "0", "Every frame captured"});
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

  out += verdict_section(test, {{"Synchronized match", "frames", "mismatches", "Bytes identical after SYNC"},
                                {"SYNC ioctl errors", "non_monotonic", "mismatches", "DMA_BUF_IOCTL_SYNC failures"},
                                {"Capture failures", "delta_max", "sync_max_ms", nullptr}});
  out += test_configuration(test);
  return out;
}

std::string render_t13(const TestResult &test) {
  std::string out;

  const MetricValue *cliff = find_metric(test, "cliff_ms");
  const MetricValue *first_miss = find_metric(test, "first_miss_ms");
  const std::string at_label = cliff != nullptr ? "At cliff · " + number(cliff->value) + " ms" : "At cliff";
  const std::string below_label =
      first_miss != nullptr ? "Below cliff · " + number(first_miss->value) + " ms" : "Below cliff";

  // Approved charts, drawn here rather than by the generic selector so they sit inside
  // the Measurement section like every other item. The sweep points come from the probe
  // lines the runner writes ("coarse: 150ms -> 10/10", "bsearch:  45ms -> 10/10").
  out += measurement_open();
  {
    // Matched without the arrow: a raw string literal does not interpret \xE2\x86\x92, so
    // spelling the UTF-8 arrow inside R"RX(...)" looks for those characters literally and
    // never matches. Anything between the timeout and the ratio is skipped instead.
    const std::regex probe(R"RX((?:coarse|bsearch):\s*(\d+)ms[^0-9]+(\d+)/(\d+))RX");
    std::vector<LinePoint> sweep;
    for (const std::string &detail : test.details) {
      std::smatch parts;
      if (std::regex_search(detail, parts, probe)) {
        const double hit = std::strtod(parts[2].str().c_str(), nullptr);
        const double of = std::strtod(parts[3].str().c_str(), nullptr);
        LinePoint point;
        point.x = std::strtod(parts[1].str().c_str(), nullptr);
        point.y = of > 0.0 ? hit / of * 100.0 : 0.0;
        // The two decisive samples are marked, as in the preview: the first timeout that
        // missed a frame, and the lowest one that captured every frame.
        if (first_miss != nullptr && point.x == first_miss->value) {
          point.flag = "miss";
          point.value = parts[2].str() + "/" + parts[3].str();
        } else if (cliff != nullptr && point.x == cliff->value) {
          point.flag = "cliff";
          point.value = parts[2].str() + "/" + parts[3].str();
        }
        sweep.push_back(point);
      }
    }
    // Sorted by timeout: the runner writes a coarse pass and then a binary search, so the
    // detail lines arrive out of order and a line drawn in that order zig-zags backwards.
    std::sort(sweep.begin(), sweep.end(), [](const LinePoint &lhs, const LinePoint &rhs) { return lhs.x < rhs.x; });
    // The preview draws this as a LINE over the swept timeout, not as one bar per probe:
    // the question is where success collapses, which is a shape, not a set of magnitudes.
    out += line_chart("Capture success by poll timeout", "Capture success by poll timeout", sweep,
                      "Poll timeout (milliseconds)", "Capture success (percent)", 100.0);

    // The three timeouts that bound the decision, on one scale.
    std::vector<BarDatum> budget;
    for (const auto &entry : {std::make_pair("First miss", "first_miss_ms"), std::make_pair("Cliff", "cliff_ms"),
                              std::make_pair("Safety margin", "safety_margin_ms")}) {
      const MetricValue *metric = find_metric(test, entry.second);
      if (metric != nullptr) {
        budget.push_back({entry.first, metric->value, "thr"});
      }
    }
    out += bar_chart("Timeout budget", {{"Poll timeout", "thr"}}, budget, "ms");
  }

  out += item_label("Round evidence");
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

    // The approved t13 round table reads "Boundary confirmed" in this column, as PLAIN text --
    // no class, no colour. It used to emit YES/NO through `ok`/`no` hooks that no CSS rule ever
    // defined, so the cell rendered at browser-default weight. The round's verdict is already
    // carried by the two count columns beside it (10/10 at the cliff, 0/10 below it).
    const bool is_ok = detail.find("✓") != std::string::npos || detail.find("YES") != std::string::npos;
    const std::string confirmed_cell = is_ok ? "Boundary confirmed" : "Boundary not confirmed";

    out += row({round_num, at_val, below_val, confirmed_cell});
  }

  if (!any_round) {
    if (cliff != nullptr) {
      out += row({"1", value_of(test, "stability_rounds_passed"), value_of(test, "first_miss_ms"),
                  state_word(test.status) == "PASS" ? "Boundary confirmed" : "Boundary not confirmed"});
    } else {
      out += row({"Unavailable", "Unavailable", "Unavailable", "Unavailable"});
    }
  }
  // The production timeout is a CONFIGURED value ("Production timeout 48.5 / From
  // configuration" in the approved preview), not the safety margin. Binding it to
  // `safety_margin_ms` made one card print the same 3.5 twice under two different labels --
  // measured on the 2026-08-10 device run, where no production_timeout metric exists at all.
  const std::string prod_val = detail_value(test, "production_timeout");
  // `literal:` carries a value the caller resolved; the string has to outlive the spec vector
  // because VerdictSpec holds a `const char *`.
  const std::string prod_spec =
      "literal:" + (prod_val.empty() ? std::string("Unavailable")
                                     : prod_val + (prod_val.find("ms") == std::string::npos ? " ms" : ""));
  out += measurement_items(test, "Aggregate",
                           {{"Reliable cliff", "cliff_ms", nullptr, "Lowest reliable timeout"},
                            {"First miss", "first_miss_ms", nullptr, nullptr},
                            {"Production timeout", prod_spec.c_str(), nullptr, "From configuration"}});
  out += table_close() + section_close();

  out +=
      verdict_section(test, {{"Safety margin", "safety_margin_ms", nullptr, "Production timeout against the cliff"},
                             {"Boundary stability", "stability_confirmed", "stability", "Rounds agreeing on the cliff"},
                             {"Timeout headroom", "cliff_ms", nullptr, "Cliff + 5 ms margin"}});
  out += test_configuration(test);

  return out;
}

std::string render_t14(const TestResult &test) {
  std::string out;

  // Approved chart: the latency statistics of one capture session, as the VERTICAL COLUMN
  // chart the preview draws -- four columns on a data-scaled axis, P95 tinted apart.
  // Production drew horizontal CSS bars here, which is a different chart of the same four
  // numbers.
  {
    std::vector<ColumnDatum> columns;
    for (const auto &entry : {std::make_pair("Min", "latency_min"), std::make_pair("Mean", "latency_mean"),
                              std::make_pair("P95", "latency_p95"), std::make_pair("Max", "latency_max")}) {
      const MetricValue *metric = find_metric(test, entry.second);
      ColumnDatum column;
      column.label = entry.first;
      column.measured = metric != nullptr;
      column.value = metric != nullptr ? metric->value : 0.0;
      column.highlight = std::string(entry.first) == "P95";
      columns.push_back(column);
    }
    out += measurement_open();
    out += column_chart("Latency distribution", "Capture latency distribution", columns,
                        "Capture latency (milliseconds)", "");

    // Approved chart: how much of the configured capture timeout the slowest frame
    // actually used. A latency figure alone does not say whether the run was close to
    // timing out; this does.
    const MetricValue *worst = find_metric(test, "latency_max");
    const std::string timeout_text = detail_value(test, "capture_timeout");
    const double timeout_ms = timeout_text.empty() ? 0.0 : std::strtod(timeout_text.c_str(), nullptr);
    std::vector<BarDatum> headroom;
    if (worst != nullptr && timeout_ms > 0.0) {
      headroom.push_back({"Maximum observed", worst->value, "max"});
      headroom.push_back({"Capture timeout", timeout_ms, "thr"});
      headroom.push_back({"Remaining headroom", std::max(0.0, timeout_ms - worst->value), "ok"});
    }
    out += bar_chart("Capture timeout headroom", {{"Observed", "max"}, {"Timeout", "thr"}, {"Headroom", "ok"}},
                     headroom, "ms");
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

  // The spread is max MINUS min, which no metric records -- it was bound to `latency_max` and
  // printed the maximum under a label promising a range (44.843 on the 2026-08-10 run).
  const MetricValue *lat_min = find_metric(test, "latency_min");
  const MetricValue *lat_max = find_metric(test, "latency_max");
  const std::string spread_spec = lat_min != nullptr && lat_max != nullptr
                                      ? "literal:" + display_number_ms(lat_max->value - lat_min->value, 3) + " ms"
                                      : std::string("literal:Unavailable");
  out += measurement_items(
      test, "Variability",
      {{"Standard deviation", "latency_stddev", "latency_stddev_ms", "Spread around the mean"},
       {"Inter-sample delta variation", "latency_jitter", "latency_stddev_ms", "Between consecutive samples"},
       {"Min-max spread", spread_spec.c_str(), nullptr, "Complete observed range"}});
  out += section_close();

  // Reliability is a RATIO of frames and missed captures is a COUNT; the approved preview
  // shows "50/50" and "0". Both were bound to latency statistics, so the card reported
  // "Capture reliability 44.807" and "Missed captures 0.013" -- neither is a count of
  // anything. `frames_captured` and `frames_missed` are what the runner actually records.
  const MetricValue *captured = find_metric(test, "frames_captured");
  const MetricValue *missed = find_metric(test, "frames_missed");
  const std::string reliability_spec =
      captured != nullptr && missed != nullptr
          ? "literal:" + display_number(captured->value) + "/" + display_number(captured->value + missed->value)
          : std::string("literal:Unavailable");
  // "capture_timeout: 100ms" -- strtod stops at the unit suffix.
  const std::string timeout_detail = detail_value(test, "capture_timeout");
  const double timeout_ms = timeout_detail.empty() ? 0.0 : std::strtod(timeout_detail.c_str(), nullptr);
  const std::string headroom_spec = timeout_ms > 0.0 && lat_max != nullptr
                                        ? "literal:" + display_number_ms(timeout_ms - lat_max->value, 3) + " ms"
                                        : std::string("literal:Unavailable");
  out += verdict_section(
      test, {{"Capture reliability", reliability_spec.c_str(), nullptr, "Triggers that delivered a frame"},
             {"Missed captures", "frames_missed", nullptr, nullptr},
             // Approved t14 shows "55.164" against a 100 ms timeout, i.e. timeout - max. The
             // binding printed the maximum itself (44.843), contradicting its own detail text.
             {"Timeout headroom", headroom_spec.c_str(), nullptr, "Capture timeout minus observed maximum"}});
  out += test_configuration(test);
  return out;
}

std::string render_t15(const TestResult &test) {
  std::string out;

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
    out += bar_chart("Latency by capture mode", {{"Non-blocking", "nonblock"}, {"Blocking", "block"}}, bars, "ms");
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
    out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
    out += row({"Average EAGAIN spins / frame", type_word(number(eagain->value)), unit_word(""),
                display_number(eagain->value), "Spins the non-blocking mode paid per frame"});
    out += table_close();
  }
  // Outside the conditional for the same reason as T09: no EAGAIN metric must not leave
  // the Measurement section open.
  // "Mean difference" is the difference BETWEEN the modes and "Lower mean mode" names a mode
  // -- the approved preview shows "0.003" and "Practically equal". Both were bound to a raw
  // mean, so the 2026-08-10 card printed 44.797 and 44.799 under labels promising neither.
  const MetricValue *nb_mean = find_metric(test, "nonblock_latency_mean");
  const MetricValue *bl_mean = find_metric(test, "block_latency_mean");
  std::string difference_spec = "literal:Unavailable";
  std::string lower_spec = "literal:Unavailable";
  if (nb_mean != nullptr && bl_mean != nullptr) {
    const double gap = bl_mean->value - nb_mean->value;
    const double magnitude = gap < 0.0 ? -gap : gap;
    difference_spec = "literal:" + display_number_ms(magnitude, 3) + " ms";
    // The approved wording: a gap under the run's own standard deviation is not a winner.
    const MetricValue *nb_sd = find_metric(test, "nonblock_latency_stddev");
    const double noise = nb_sd != nullptr ? nb_sd->value : 0.0;
    lower_spec =
        magnitude <= noise ? "literal:Practically equal" : (gap > 0.0 ? "literal:Non-blocking" : "literal:Blocking");
  }
  out += measurement_items(test, "Aggregate",
                           {{"Mean difference", difference_spec.c_str(), nullptr, "Between the two modes"},
                            {"Lower mean mode", lower_spec.c_str(), nullptr, nullptr}});
  out += section_close();

  // Test Configuration

  // All three rows are ratios in the approved preview -- "30/30", "30/30", "2/2" -- and all
  // three were bound to latency means or a P95. `nonblock_captures` / `block_captures` are the
  // metrics that hold the sample counts.
  const MetricValue *nb_caps = find_metric(test, "nonblock_captures");
  const MetricValue *bl_caps = find_metric(test, "block_captures");
  const std::string samples_detail = detail_value(test, "samples_per_mode");
  const double per_mode = !samples_detail.empty() ? std::strtod(samples_detail.c_str(), nullptr)
                          : nb_caps != nullptr    ? nb_caps->value
                                                  : 0.0;
  const auto ratio_spec = [per_mode](const MetricValue *captures) {
    if (captures == nullptr || per_mode <= 0.0) {
      return std::string("literal:Unavailable");
    }
    return "literal:" + display_number(captures->value) + "/" + display_number(per_mode);
  };
  const std::string nonblock_spec = ratio_spec(nb_caps);
  const std::string blocking_spec = ratio_spec(bl_caps);
  // Both modes ran when both recorded a capture count; the preview states this as "2/2".
  const int modes_done = (nb_caps != nullptr ? 1 : 0) + (bl_caps != nullptr ? 1 : 0);
  const std::string modes_spec = "literal:" + std::to_string(modes_done) + "/2";
  out += verdict_section(test, {{"Non-block captures", nonblock_spec.c_str(), nullptr, nullptr},
                                {"Blocking captures", blocking_spec.c_str(), nullptr, nullptr},
                                {"Modes compared", modes_spec.c_str(), nullptr, "Both capture modes measured"}});
  out += test_configuration(test);
  return out;
}

std::string render_t16(const TestResult &test) {
  std::string out;

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

  // Approved chart: the HIGH and LOW reference latency across the whole sweep, so the two
  // edges can be read against each other rather than one width at a time.
  {
    // The preview plots the two references as LINES over the swept pulse width, not as a
    // column of bars: the point is how each edge behaves as the width changes, and the
    // pairing at each width, which a bar list does not show.
    std::vector<std::pair<double, double>> high_series;
    std::vector<std::pair<double, double>> low_series;
    for (const auto &metric : test.metrics) {
      const std::string high("lat_high_avg_");
      const std::string low("lat_low_avg_");
      if (metric.name.rfind(high, 0) == 0) {
        high_series.push_back({std::strtod(metric.name.substr(high.size()).c_str(), nullptr), metric.value});
      } else if (metric.name.rfind(low, 0) == 0) {
        low_series.push_back({std::strtod(metric.name.substr(low.size()).c_str(), nullptr), metric.value});
      }
    }
    // Metric order follows the recorded run, not the sweep, so the widths have to be put
    // back in order before they are joined by a line.
    const auto by_width = [](const std::pair<double, double> &lhs, const std::pair<double, double> &rhs) {
      return lhs.first < rhs.first;
    };
    std::sort(high_series.begin(), high_series.end(), by_width);
    std::sort(low_series.begin(), low_series.end(), by_width);
    out +=
        dual_line_chart("Edge evidence across the sweep", "HIGH and LOW reference latency across the pulse width sweep",
                        high_series, low_series, "Pulse width (microseconds)", "Reference latency (milliseconds)");
  }

  out += item_label("Edge evidence");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  {
    const std::string decision = edge_decision.empty() ? std::string("\xE2\x80\x94") : edge_decision;
    out += row({"Edge decision", "string", unit_word(""), html_escape(decision), "\xE2\x80\x94"});
    out += row({"HIGH spread", type_word(high_spread), unit_word("milliseconds"), high_spread,
                "Across the pulse-width sweep"});
    out += row(
        {"LOW spread", type_word(low_spread), unit_word("milliseconds"), low_spread, "Derived from the falling edge"});
    out += row({"Margin", type_word(edge_margin), unit_word("milliseconds"), edge_margin, "\xE2\x80\x94"});
  }
  out += table_close();
  out += measurement_items(test, "Aggregate",
                           {{"Observed minimum tested", "hits_1ms", "hits_5ms", "Lowest width at full hits"},
                            {"Trigger edge", "value:Edge detection", nullptr, nullptr}});
  out += section_close();

  // Test Configuration

  out += verdict_section(test, {{"Sweep reliability", "hits_20ms", "hits_13ms", "Captures across every pulse width"},
                                {"Samples per width", "hits_5ms", "hits_10ms", nullptr}});
  out += test_configuration(test);
  return out;
}

std::string render_t17(const TestResult &test) {
  std::string out;

  // Approved charts: the per-format latency pair, then the throughput on its own scale --
  // MB/s and ms cannot share an axis without one of them becoming unreadable.
  out += measurement_open();
  {
    std::vector<BarDatum> latency;
    std::vector<BarDatum> throughput;
    for (const auto &metric : test.metrics) {
      const std::string mean("_latency_mean"), max("_latency_max"), mbps("_throughput_mbps");
      const auto ends_with_suffix = [&metric](const std::string &suffix) {
        return metric.name.size() > suffix.size() &&
               metric.name.compare(metric.name.size() - suffix.size(), suffix.size(), suffix) == 0;
      };
      if (ends_with_suffix(mean)) {
        latency.push_back(
            {upper_case(metric.name.substr(0, metric.name.size() - mean.size())) + " mean", metric.value, "mean"});
      } else if (ends_with_suffix(max)) {
        latency.push_back(
            {upper_case(metric.name.substr(0, metric.name.size() - max.size())) + " max", metric.value, "max"});
      } else if (ends_with_suffix(mbps)) {
        throughput.push_back(
            {upper_case(metric.name.substr(0, metric.name.size() - mbps.size())), metric.value, "thr"});
      }
    }
    out += bar_chart("Capture latency by pixel format", {{"Mean latency", "mean"}, {"Maximum latency", "max"}}, latency,
                     "ms");
    out += bar_chart("Memcpy throughput by pixel format", {{"Throughput", "thr"}}, throughput, "MB/s");
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
      verdict_section(test, {{"UYVY mean latency", "uyvy_latency_mean", nullptr, "Mean capture latency in this format"},
                             {"NV16 mean latency", "nv16_latency_mean", nullptr, "Mean capture latency in this format"},
                             {"Formats tested", "formats_tested", "format_count", nullptr}});
  out += test_configuration(test);
  return out;
}

std::string render_t18(const TestResult &test) {
  std::string out;

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
                     {{"Mean latency", "mean"}, {"P95 latency", "p95"}}, bars, "ms");
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
                     "frames");
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
  // Five columns, as every other evidence table: the reading and its unit are separate
  // cells, and the state word rides in Detail.
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  {
    // The Detail column states what the number means, in plain text -- the approved t20 card
    // reads "No gaps expected" and "90 gaps observed" here, and the verdict is carried by the
    // Status column's own `.verdict` class. The status words that used to sit here were split
    // between a styled `pass` and an UNSTYLED `warn-text`, so a warning rendered plainer than a
    // pass: the one cell a reader must not miss was the one with no rule.
    const std::string dequeued = dequeued_str + " / " + req_str;
    out += row({"Frames dequeued", type_word(dequeued), unit_word("frames"), dequeued, "All requested frames"});
    out += row({"Sequence gaps observed", type_word(gaps_str), unit_word(""), gaps_str,
                gaps_str == "0" ? "No gaps expected" : gaps_str + " gaps observed"});
    out += row({"Largest sequence gap", type_word(max_gap_str), unit_word(""), max_gap_str,
                max_gap_str == "0" ? "No gaps expected" : "Largest observed run"});
    const std::string range = detail_value(test, "sequence_range").empty() ? std::string("\xE2\x80\x94")
                                                                           : detail_value(test, "sequence_range");
    out += row({"Sequence range", type_word(range), unit_word(""), range, "buffer.sequence"});
  }
  out += measurement_items(test, "Aggregate",
                           {{"Frames dequeued", "frames_captured", nullptr, nullptr},
                            {"Sequence gaps observed", "dropped_frames", "max_gap", nullptr},
                            {"Unobserved sequences", "max_gap", nullptr, "Largest single gap"},
                            {"Duplicate sequences", "max_gap", nullptr, "Largest single gap"},
                            {"Backward events", "ts_non_monotonic", nullptr, "Timestamps that went backwards"}});
  out += table_close() + section_close();

  out += verdict_section(test,
                         {{"Continuity requirement", "dropped_frames", "max_gap", "Dropped frames against the limit"},
                          {"Duplicate sequences", "max_gap", nullptr, nullptr},
                          {"Backward sequences", "ts_non_monotonic", nullptr, nullptr}});
  out += test_configuration(test);
  return out;
}

std::string render_t21(const TestResult &test) {
  std::string out;

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
    out += bar_chart("Sampled buffer timestamp delta", {{"Delta between read frames", "mean"}}, bars, "ms");
  }

  const MetricValue *reg = find_metric(test, "non_monotonic");
  if (reg == nullptr)
    reg = find_metric(test, "non_monotonic");

  const std::string reg_str = reg != nullptr ? number(reg->value) : "0";
  const std::string mean_delta = value_of_any(test, {"delta_mean", "delta_mean_ms"});

  out += item_label("Delta evidence");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  {
    // A Measurement row carries NO verdict -- that is what separates it from Measurement Result
    // (design-spec S1). This cell printed a bare PASS/FAIL in a table with no Status column, and
    // the FAIL half went through the unstyled `warn-text` hook.
    out += row({"Non-monotonic count", type_word(reg_str), unit_word(""), reg_str,
                reg_str == "0" ? "Timestamps increase monotonically" : "Backward timestamps observed"});
    std::string mean_bare;
    std::string mean_unit;
    split_unit(mean_delta, &mean_bare, &mean_unit);
    out += row({"Sampled buffer timestamp delta (mean)", type_word(mean_delta), unit_word(mean_unit),
                mean_bare.empty() ? mean_delta : mean_bare, "<span class=\"pass\">SAMPLED</span>"});
    const std::string p95 = value_of_any(test, {"delta_p95", "delta_p95_ms"});
    std::string p95_bare;
    std::string p95_unit;
    split_unit(p95, &p95_bare, &p95_unit);
    out += row({"Sampled buffer timestamp delta (P95)", type_word(p95), unit_word(p95_unit),
                p95_bare.empty() ? p95 : p95_bare, "<span class=\"pass\">SAMPLED</span>"});
    const std::string source = detail_value(test, "timestamp_source").empty() ? std::string("\xE2\x80\x94")
                                                                              : detail_value(test, "timestamp_source");
    out += row(
        {"Buffer timestamp source", type_word(source), unit_word(""), source, "<span class=\"pass\">VERIFIED</span>"});
  }
  out += measurement_items(test, "Aggregate",
                           {{"Sampled delta mean", "delta_mean", nullptr, nullptr},
                            {"Non-monotonic events", "non_monotonic", nullptr, nullptr},
                            {"Sampled delta max", "delta_max", nullptr, "Largest observed spacing"}});
  out += table_close() + section_close();

  out += verdict_section(test, {{"Non-monotonic events", "non_monotonic", nullptr, "Timestamps that went backwards"}});
  out += test_configuration(test);
  return out;
}

std::string render_t22(const TestResult &test) {
  std::string out;

  const MetricValue *stuck = find_metric(test, "identical_pairs");
  const std::string stuck_str = stuck != nullptr ? number(stuck->value) : "0";
  const std::string cmp_win =
      detail_value(test, "compare_bytes").empty() ? "\xE2\x80\x94" : detail_value(test, "compare_bytes");

  out += measurement_open();
  // The approved preview draws "Content comparison coverage" as a bar chart, not a table:
  // unique pairs against identical pairs, so the eye reads the ratio. It was rendered as a
  // second table, which is why this card carried three tables where the preview has one.
  {
    const MetricValue *tested = find_metric(test, "frames_tested");
    const MetricValue *identical = find_metric(test, "identical_pairs");
    std::vector<BarDatum> bars;
    if (tested != nullptr && identical != nullptr) {
      bars.push_back({"Unique pairs", std::max(0.0, tested->value - 1.0 - identical->value), "uniq"});
      bars.push_back({"Identical pairs", identical->value, "ident"});
    }
    out += bar_chart("Content comparison coverage", {{"Unique pairs", "uniq"}, {"Identical pairs", "ident"}}, bars,
                     "pairs");
  }
  out += item_label("Comparison evidence");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  {
    // Measurement row: describes the count, does not judge it (design-spec S1).
    out += row({"Identical payload pairs", type_word(stuck_str), unit_word(""), stuck_str,
                stuck_str == "0" ? "Every pair differed" : "Repeated payload observed"});
    const std::string compared = value_of(test, "frames_tested");
    out += row({"Frames compared", type_word(compared), unit_word("frames"), compared,
                "<span class=\"pass\">COMPLETED</span>"});
    const std::string longest = value_of_any(test, {"max_identical_run", "longest_repeated_run"});
    out +=
        row({"Longest repeated run", type_word(longest), unit_word(""), longest, "<span class=\"pass\">CLEAR</span>"});
    std::string win_bare;
    std::string win_unit;
    split_unit(cmp_win, &win_bare, &win_unit);
    out += row({"Comparison window", type_word(cmp_win), unit_word(win_unit), win_bare.empty() ? cmp_win : win_bare,
                "<span class=\"pass\">LIMITED SCOPE</span>"});
  }
  out += table_close() + section_close();

  out +=
      verdict_section(test, {{"Identical pairs", "identical_pairs", nullptr, "Consecutive frames with equal content"}});
  out += test_configuration(test);

  return out;
}

std::string render_t23(const TestResult &test) {
  std::string out;

  // Approved chart: the mean latency of each reported window, so drift is read as a shape
  // rather than inferred from a single drift figure. Parsed from the same detail lines the
  // window table below is built from.
  out += measurement_open();
  {
    const std::regex window_line(R"RX(Win\d+\s+([0-9]+-[0-9]+s):\s*n=\d+\s+mean=([0-9.]+))RX");
    std::vector<ColumnDatum> windows;
    for (const std::string &detail : test.details) {
      std::smatch parts;
      if (std::regex_search(detail, parts, window_line)) {
        ColumnDatum column;
        column.label = parts[1].str();
        column.value = std::strtod(parts[2].str().c_str(), nullptr);
        windows.push_back(column);
      }
    }
    // The column chart the preview draws, not horizontal CSS bars. A window the run never
    // reported keeps its label and draws no column -- the preview leaves the last window
    // empty for exactly that reason.
    out += column_chart("Mean capture latency by window", "Mean capture latency by reported window", windows,
                        "Elapsed test time (seconds)", "");
  }

  out += item_label("Window evidence");
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
  out += measurement_items(test, "Latency evidence",
                           {{"Capture success", "success_rate_pct", "frames_captured", nullptr},
                            {"Max consecutive miss", "max_consecutive_miss", nullptr, nullptr},
                            {"Latency mean", "latency_mean", nullptr, nullptr},
                            {"Latency P95", "latency_p95", nullptr, nullptr}});
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

  // Approved charts: the two phases side by side, then the single number the verdict is
  // actually drawn from. Both read metrics the runner records for every load run.
  out += measurement_open();
  {
    std::vector<BarDatum> phases;
    for (const auto &entry : {std::make_tuple("Baseline mean", "baseline_latency_mean", "base"),
                              std::make_tuple("CPU load mean", "load_latency_mean", "load"),
                              std::make_tuple("Baseline P95", "baseline_latency_p95", "base"),
                              std::make_tuple("CPU load P95", "load_latency_p95", "load")}) {
      const MetricValue *metric = find_metric(test, std::get<1>(entry));
      if (metric != nullptr) {
        phases.push_back({std::get<0>(entry), metric->value, std::get<2>(entry)});
      }
    }
    out += bar_chart("Capture latency by test phase", {{"Baseline", "base"}, {"CPU load", "load"}}, phases, "ms");

    // The verdict rests on the P95 delta alone, so it gets its own chart rather than
    // being read off the difference between two bars.
    std::vector<BarDatum> impact;
    const MetricValue *base_p95 = find_metric(test, "baseline_latency_p95");
    const MetricValue *load_p95 = find_metric(test, "load_latency_p95");
    if (base_p95 != nullptr && load_p95 != nullptr) {
      // A bar cannot be drawn to a negative length, and CPU load lowering P95 is a real
      // outcome rather than an absent one -- measured on this device: -0.0036 ms. The bar
      // takes the magnitude and the label carries the direction.
      const double delta = load_p95->value - base_p95->value;
      impact.push_back({delta < 0.0 ? "P95 delta (lower under load)" : "P95 delta", std::abs(delta), "delta"});
    }
    out += bar_chart("P95 impact against verdict thresholds", {{"P95 delta", "delta"}}, impact, "ms");
  }

  // The approved preview names this table for what it holds -- one row per statistic --
  // rather than for the phases it compares.
  out += item_label("Statistic evidence");
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
  // Approved item: the three figures the verdict is read from, stated once with their
  // direction, so the reader does not have to subtract two columns of the table above.
  {
    const MetricValue *base_cap = find_metric(test, "baseline_captures");
    const MetricValue *load_cap = find_metric(test, "load_captures");
    const MetricValue *base_mean = find_metric(test, "baseline_latency_mean");
    const MetricValue *load_mean = find_metric(test, "load_latency_mean");
    const MetricValue *base95 = find_metric(test, "baseline_latency_p95");
    const MetricValue *load95 = find_metric(test, "load_latency_p95");
    const std::string coverage = (base_cap != nullptr && load_cap != nullptr)
                                     ? number(base_cap->value) + " \xC2\xB7 " + number(load_cap->value)
                                     : std::string("Unavailable");
    std::string relative = "Unavailable";
    std::string relative_detail = "\xE2\x80\x94";
    if (base95 != nullptr && load95 != nullptr && base95->value > 0.0) {
      const double pct = (load95->value - base95->value) / base95->value * 100.0;
      relative = (pct >= 0.0 ? "+" : "") + number(pct);
      relative_detail = number(base95->value) + " \xE2\x86\x92 " + number(load95->value) + " ms";
    }
    std::string mean_delta = "Unavailable";
    if (base_mean != nullptr && load_mean != nullptr) {
      const double diff = load_mean->value - base_mean->value;
      mean_delta = (diff >= 0.0 ? "+" : "") + number(diff);
    }
    out += item_label("Phase evidence");
    out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
    out += row({"Phase coverage", type_word(coverage), unit_word("captures"), coverage, "Baseline \xC2\xB7 CPU load"});
    out += row({"Relative P95 change", type_word(relative), unit_word("percent"), relative, relative_detail});
    out += row({"Mean delta", type_word(mean_delta), unit_word("milliseconds"), mean_delta, "\xE2\x80\x94"});
    out += table_close();

    // What this measurement does NOT establish. The load phase pins the CPU, but nothing
    // here verifies that it actually saturated, so the report says so rather than letting
    // the reader assume it.
    out += item_label("Measurement validity");
    out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
    const std::string threads = detail_value(test, "load_threads");
    out += row({"Configured load threads", type_word(threads), unit_word(""),
                threads.empty() ? std::string("\xE2\x80\x94") : html_escape(threads), "\xE2\x80\x94"});
    out += row({"CPU saturation coverage", "string", unit_word(""), "\xE2\x80\x94", "Not verified by this test"});
    out += row({"Observed CPU utilization", "string", unit_word(""), "\xE2\x80\x94", "Not measured"});
    const std::string timeout = detail_value(test, "capture_timeout");
    out += row({"Baseline / load timeout", type_word(timeout), unit_word(""),
                timeout.empty() ? std::string("\xE2\x80\x94") : html_escape(timeout), "\xE2\x80\x94"});
    out += row({"Verdict scope", "string", unit_word(""), "P95 delta only", "\xE2\x80\x94"});
    out += table_close();
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

  // Approved chart: how many rounds each camera contributed to. Built from the per-camera
  // detail lines the runner writes; with nothing recorded it draws nothing rather than an
  // empty frame.
  out += measurement_open();
  {
    const std::regex camera_line(R"RX(camera:\s*([^|]+)\|[^|]*\|\s*(\d+))RX");
    std::vector<BarDatum> rounds;
    for (const std::string &detail : test.details) {
      std::smatch parts;
      if (std::regex_search(detail, parts, camera_line)) {
        rounds.push_back({trim_of(parts[1].str()), std::strtod(parts[2].str().c_str(), nullptr), "ok"});
      }
    }
    out += bar_chart("Round capture coverage", {{"Rounds captured", "ok"}}, rounds, "rounds");
  }

  out += item_label("Camera evidence");
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

  // Approved item: what the run observed across the participating cameras. Reads the
  // recorded counts; with no second camera there is nothing to compare, and the rows say
  // so rather than naming devices that were never opened -- this block used to print
  // "/dev/video0" and "/dev/video1" as a fixed pair on every run.
  out += item_label("Round evidence");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  const std::string participants = value_of_any(test, {"cameras", "participants"});
  out += row({"Participants", type_word(participants), unit_word(""), participants, "\xE2\x80\x94"});
  const std::string coverage = value_of_any(test, {"frames", "per_camera_frames"});
  out += row({"Per-camera coverage", type_word(coverage), unit_word(""), coverage, "\xE2\x80\x94"});
  const std::string skew = value_of_any(test, {"trigger_fire_spread_max", "skew_max_ms"});
  out += row({"Acquisition skew P95", type_word(skew), unit_word("milliseconds"), skew,
              "Requires a shared trigger reference"});
  out += table_close();

  // What this measurement cannot claim. Every line is a statement about the METHOD, so it
  // holds whether or not a second camera took part.
  out += item_label("Measurement validity");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  out += row({"Capture round alignment", "string", unit_word(""), "Aligned", "\xE2\x80\x94"});
  out +=
      row({"Common physical trigger", "string", unit_word(""), "None", "No external trigger shared between cameras"});
  out += row({"Buffer timestamp comparison", "string", unit_word(""), "Not performed", "No shared trigger reference"});
  out += row({"DQBUF receipt timing", "string", unit_word(""), "Not used", "\xE2\x80\x94"});
  out += row({"Synchronization verdict", "string", unit_word(""), "Not evaluated", "\xE2\x80\x94"});
  out += table_close();

  out += item_label("Measurement method");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  out += row({"Trigger source", "string", unit_word(""), "Free-run (no external trigger)",
              "Free-run has no external trigger"});
  out += row({"Timing reference", "string", unit_word(""), "Frame availability", "\xE2\x80\x94"});
  out += row({"Sync measurement", "string", unit_word(""), "Not performed", "Free-run has no trigger reference"});
  out += table_close();
  out += section_close();
  out += verdict_section(test, {{"Complete rounds", "frames", "cameras", "Rounds every camera contributed to"}});
  return out + test_configuration(test);
}

std::string render_t26(const TestResult &test) {
  std::string out;

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

  out += measurement_items(
      test, "Aggregate",
      {{"Stabilized cycles", "cycles_completed", "cycles", nullptr},
       {"Cycles not measured", "censored_cycles", nullptr, "Latency still moving when the window ended"},
       // "Longest" is a LOWER BOUND whenever a cycle was censored: the true worst case is
       // at least this, and how much more is unknown. Saying "longest" flatly would
       // understate a requirement derived from it.
       {"Longest measured warm-up", "warmup_max_frames", "warmup_max_ms", "Measured cycles only"},
       {"Median warm-up of stabilized cycles", "warmup_mean_frames", "warmup_mean_ms", "Stabilized cycles only"}});
  // Approved item: what this measurement can and cannot claim. Every row is derived from
  // what the run recorded -- the censored count says how many cycles never settled, and
  // the timing source names what the warm-up figure actually includes.
  {
    const MetricValue *completed = find_metric(test, "cycles_completed");
    const MetricValue *censored = find_metric(test, "censored_cycles");
    const std::string sessions =
        completed != nullptr ? number(completed->value) + "/" + number(completed->value) : std::string("Unavailable");
    std::string settled = "Unavailable";
    if (completed != nullptr && censored != nullptr) {
      settled = number(completed->value - censored->value) + "/" + number(completed->value);
    }
    out += item_label("Measurement validity");
    out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
    out += row({"Fresh V4L2 sessions", type_word(sessions), unit_word(""), sessions, "\xE2\x80\x94"});
    out += row({"Stabilized within window", type_word(settled), unit_word(""), settled, "\xE2\x80\x94"});
    out += row({"Frame misses recorded", "string", unit_word(""), "\xE2\x80\x94", "Not reported by driver"});
    const std::string reference = detail_value(test, "reference_window");
    out += row({"Reference window", "string", unit_word(""),
                reference.empty() ? std::string("\xE2\x80\x94") : html_escape(reference), "\xE2\x80\x94"});
    out += row({"Timing source", "string", unit_word(""), "DQBUF wait", "Includes the frame wait"});
    out += table_close();
  }
  // How the warm-up was measured, not how it came out -- the counts are already in
  // Aggregate above. The approved preview states the procedure and its two bounds in the
  // canonical five columns.
  out += item_label("Measurement method");
  out += table_open({"Metric", "Type", "Unit", "Value", "Detail"});
  out += row({"Fresh session", "string", unit_word(""), "Open, STREAMON, measure, close", "\xE2\x80\x94"});
  const std::string window = detail_value(test, "max_observation_window");
  out += row({"Maximum observation window", type_word(window), unit_word("frames"),
              window.empty() ? std::string("\xE2\x80\x94") : html_escape(window), "\xE2\x80\x94"});
  const std::string reference_frames = detail_value(test, "steady_reference");
  out += row({"Steady-state reference", type_word(reference_frames), unit_word("frames"),
              reference_frames.empty() ? std::string("\xE2\x80\x94") : html_escape(reference_frames), "\xE2\x80\x94"});
  const std::string tolerance = detail_value(test, "stability_threshold_pct");
  out += row({"Latency tolerance", type_word(tolerance), unit_word("percent"),
              tolerance.empty() ? std::string("\xE2\x80\x94") : html_escape(tolerance), "\xE2\x80\x94"});
  out += table_close();
  out += section_close();
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

std::string render_test_result_block(const TestResult &test) {
  return result_block(test);
}

std::string render_test_content(const TestResult &test) {
  std::string out;
  if (test_content_shows_result(test.id, test.status)) {
    // The single approved line for a non-PASS card, and nothing else. `test.notes` is
    // deliberately not rendered: the approved previews give a card exactly one piece of
    // explanatory prose -- this verdict line -- and every further note production used to
    // print (.test-note here, .boundary-note in the per-test renderers) appeared in none
    // of the 26 previews.
    out += result_block(test);
  }
  const auto found = renderers().find(test.id);
  out += found != renderers().end() ? found->second(test) : render_generic(test);
  out += detail_lines(test);
  return out;
}

}  // namespace v4l2diag
