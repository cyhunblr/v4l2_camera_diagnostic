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

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// The shared one, under the name this file's many call sites already use.
std::string humanize_metric_name(const std::string &value) {
  return v4l2diag::humanize(value);
}

std::string format_metric_value(double value) {
  std::ostringstream out;
  const double rounded = std::round(value);
  if (std::fabs(value - rounded) < 0.0005) {
    out << std::fixed << std::setprecision(0) << value;
  } else if (std::fabs(value) >= 100.0) {
    out << std::fixed << std::setprecision(1) << value;
  } else {
    out << std::fixed << std::setprecision(3) << value;
  }
  std::string rendered = out.str();
  if (rendered.find('.') != std::string::npos) {
    while (!rendered.empty() && rendered.back() == '0')
      rendered.pop_back();
    if (!rendered.empty() && rendered.back() == '.')
      rendered.pop_back();
  }
  return rendered;
}

// The shared sentinel rule (plan 3.1): every surface that shows a metric applies it.
using v4l2diag::is_sentinel_metric;

bool is_chartable_metric(const MetricValue &metric) {
  return metric.unit != "bool" && metric.unit != "errno" && std::isfinite(metric.value) && !is_sentinel_metric(metric);
}

std::string statistic_base(const std::string &name) {
  static const std::vector<std::string> suffixes = {
      "_stddev_ms", "_jitter_ms", "_mean_ms", "_p95_ms", "_max_ms", "_min_ms",
      "_stddev",    "_jitter",    "_mean",    "_p95",    "_max",    "_min",
  };
  for (const auto &suffix : suffixes) {
    if (ends_with(name, suffix)) {
      return name.substr(0, name.size() - suffix.size());
    }
  }
  return "";
}

std::string statistic_label(const std::string &name, const std::string &base) {
  std::string label = name;
  if (!base.empty() && starts_with(label, base + "_")) {
    label.erase(0, base.size() + 1);
  }
  if (ends_with(label, "_ms")) {
    label.resize(label.size() - 3);
  }
  return humanize_metric_name(label);
}

bool is_variability_metric(const std::string &name) {
  return ends_with(name, "_stddev_ms") || ends_with(name, "_jitter_ms") || ends_with(name, "_stddev") ||
         ends_with(name, "_jitter");
}

std::string sweep_prefix(const std::string &name) {
  static const std::vector<std::string> suffixes = {
      "_latency_mean",
      "_latency_p95",
      "_latency_max",
      "_throughput_mbps",
  };
  for (const auto &suffix : suffixes) {
    if (ends_with(name, suffix)) {
      return name.substr(0, name.size() - suffix.size());
    }
  }
  return "";
}

std::string sweep_series_label(const std::string &name) {
  if (ends_with(name, "_latency_mean"))
    return "Mean";
  if (ends_with(name, "_latency_p95"))
    return "P95";
  if (ends_with(name, "_latency_max"))
    return "Max";
  if (ends_with(name, "_throughput_mbps"))
    return "Throughput";
  return "";
}

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

AxisTicks nice_ticks(double lo, double hi, int target_count) {
  AxisTicks result;
  if (target_count < 2) {
    target_count = 2;
  }
  if (!(hi > lo)) {
    hi = lo + std::max(1.0, std::fabs(lo) * 0.1);
  }
  const double raw_step = (hi - lo) / (target_count - 1);
  const double magnitude = std::pow(10.0, std::floor(std::log10(raw_step)));
  const double normalized = raw_step / magnitude;
  double step = 10.0;
  if (normalized <= 1.0) {
    step = 1.0;
  } else if (normalized <= 2.0) {
    step = 2.0;
  } else if (normalized <= 5.0) {
    step = 5.0;
  }
  step *= magnitude;
  result.min = std::floor(lo / step) * step;
  result.max = std::ceil(hi / step) * step;
  if (result.max - result.min < step * 0.5) {
    result.max = result.min + step;
  }
  for (double value = result.min; value <= result.max + step * 1e-9; value += step) {
    result.values.push_back(value);
  }
  return result;
}

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

// Monotone in the category index, so colour never cycles back: with the old
// "tone-{index % 4}" the 3rd and 11th pulse width shared a fill.
const char *ramp_color(std::size_t index, std::size_t count) {
  if (count <= 1) {
    return kSequentialRamp[kSequentialRampSize - 1];
  }
  const double position =
      static_cast<double>(index) * static_cast<double>(kSequentialRampSize - 1) / static_cast<double>(count - 1);
  const std::size_t slot = static_cast<std::size_t>(std::llround(position));
  return kSequentialRamp[std::min(slot, kSequentialRampSize - 1)];
}

const char *series_color(std::size_t index) {
  return kOrdinalSeries[std::min(index, kOrdinalSeriesSize - 1)];
}

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

std::string lower_case(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

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

std::string category_label(const MetricValue &metric, const MetricChartGroup &group) {
  const std::string prefix = sweep_prefix(metric.name);
  switch (group.category_kind) {
    case CategoryKind::ControlCombo:
      return control_combo_label(metric.name);
    case CategoryKind::Fourcc:
      return prefix.empty() ? humanize_metric_name(metric.name) : upper_case(prefix);
    case CategoryKind::Resolution:
      return prefix.empty() ? humanize_metric_name(metric.name) : prefix;
    case CategoryKind::PulseWidth: {
      // "hits_30ms" -> "30"; the unit already lives in the axis title.
      std::string label = statistic_label(metric.name, group.label_prefix);
      if (ends_with(label, "ms")) {
        label.resize(label.size() - 2);
      }
      return label;
    }
    case CategoryKind::Statistic:
      break;
  }
  if (!prefix.empty()) {
    const bool looks_like_fourcc =
        prefix.size() <= 5 && prefix.find('x') == std::string::npos && prefix.find('_') == std::string::npos;
    return looks_like_fourcc ? upper_case(prefix) : humanize_metric_name(prefix);
  }
  return statistic_label(metric.name, group.label_prefix);
}

// Ordered category axes sort by the quantity they represent rather than by the
// order the metrics happen to be emitted in.
double category_sort_key(CategoryKind kind, const std::string &label, double value) {
  switch (kind) {
    case CategoryKind::PulseWidth: {
      double ms = 0.0;
      return std::sscanf(label.c_str(), "%lf", &ms) == 1 ? ms : 0.0;
    }
    case CategoryKind::Resolution: {
      int width = 0;
      int height = 0;
      if (std::sscanf(label.c_str(), "%dx%d", &width, &height) == 2) {
        return static_cast<double>(width) * static_cast<double>(height);
      }
      return 0.0;
    }
    case CategoryKind::ControlCombo:
      return value;
    case CategoryKind::Fourcc:
    case CategoryKind::Statistic:
      break;
  }
  return 0.0;
}

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

BarLayout build_bar_layout(const std::vector<MetricValue> &metrics, const MetricChartGroup &group) {
  struct Pending {
    std::size_t metric_index;
    std::string category;
    std::string series;
    double sort_key;
  };
  std::vector<Pending> pending;
  for (std::size_t metric_index : group.indices) {
    const auto &metric = metrics[metric_index];
    const std::string category = category_label(metric, group);
    std::string series = sweep_series_label(metric.name);
    if (series.empty()) {
      series = group.value_axis;
    }
    pending.push_back({metric_index, category, series, category_sort_key(group.category_kind, category, metric.value)});
  }

  std::vector<std::string> ordered;
  for (const auto &entry : pending) {
    if (std::find(ordered.begin(), ordered.end(), entry.category) == ordered.end()) {
      ordered.push_back(entry.category);
    }
  }
  const bool sortable = group.category_kind == CategoryKind::PulseWidth ||
                        group.category_kind == CategoryKind::Resolution ||
                        group.category_kind == CategoryKind::ControlCombo;
  if (sortable) {
    std::map<std::string, double> keys;
    for (const auto &entry : pending) {
      auto it = keys.find(entry.category);
      if (it == keys.end() || entry.sort_key < it->second) {
        keys[entry.category] = entry.sort_key;
      }
    }
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const std::string &lhs, const std::string &rhs) { return keys[lhs] < keys[rhs]; });
  }

  BarLayout layout;
  layout.categories = ordered;
  for (const auto &entry : pending) {
    if (std::find(layout.series.begin(), layout.series.end(), entry.series) == layout.series.end()) {
      layout.series.push_back(entry.series);
    }
  }
  for (const auto &entry : pending) {
    const std::size_t category_index =
        static_cast<std::size_t>(std::find(ordered.begin(), ordered.end(), entry.category) - ordered.begin());
    const std::size_t series_index = static_cast<std::size_t>(
        std::find(layout.series.begin(), layout.series.end(), entry.series) - layout.series.begin());
    layout.items.push_back({entry.metric_index, category_index, series_index});
    layout.max_value = std::max(layout.max_value, std::max(0.0, metrics[entry.metric_index].value));
  }
  return layout;
}

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

std::string prefix_sweep_value_axis(const std::string &unit) {
  if (unit == "MB/s") {
    return "Memcpy throughput";
  }
  if (unit == "ms") {
    return "Capture latency";
  }
  return "Value";
}

std::string axis_with_unit(const std::string &quantity, const std::string &unit) {
  if (unit.empty() || quantity.find('(') != std::string::npos) {
    return quantity;
  }
  return quantity + " (" + unit + ")";
}

void finalize_group(std::vector<MetricChartGroup> &groups, std::vector<bool> &used,
                    const std::vector<MetricValue> &metrics, MetricChartGroup group) {
  if (group.indices.size() < 2) {
    return;
  }
  group.unit = metrics[group.indices.front()].unit;
  group.value_axis = axis_with_unit(group.value_axis, group.unit);

  const BarLayout layout = build_bar_layout(metrics, group);
  if (layout.categories.empty() || layout.series.empty()) {
    return;
  }

  if (layout.series.size() > 1) {
    // Several measured series share one value axis -> identity comes from the
    // series colour, so the category axis cannot also carry a ramp.
    group.color_rule = ColorRule::OrdinalSeries;
  } else if (group.category_kind == CategoryKind::PulseWidth || group.category_kind == CategoryKind::Resolution) {
    group.color_rule = ColorRule::SequentialByCategory;
  } else {
    group.color_rule = ColorRule::SingleHue;
  }

  // Long category labels do not fit under a vertical bar; give them their own
  // row instead of rotating them until they collide.
  std::size_t longest = 0;
  for (const auto &category : layout.categories) {
    longest = std::max(longest, category.size());
  }
  if (group.kind == MetricChartGroup::Kind::VerticalBars && (layout.categories.size() > 12 || longest > 18)) {
    group.kind = MetricChartGroup::Kind::HorizontalBars;
  }
  group.wide = layout.categories.size() > 8 ||
               (group.kind == MetricChartGroup::Kind::HorizontalBars && layout.categories.size() > 5);

  for (std::size_t index : group.indices) {
    used[index] = true;
  }
  groups.push_back(group);
}

std::vector<MetricChartGroup> group_metrics(const std::vector<MetricValue> &metrics, std::vector<bool> &used,
                                            const std::string &test_id) {
  std::vector<MetricChartGroup> groups;
  if (used.size() != metrics.size()) {
    used.assign(metrics.size(), false);
  }

  // 1. Explicitly declared per-test sweeps.
  for (const auto &spec : kSweepChartSpecs) {
    if (test_id != spec.test_id) {
      continue;
    }
    const std::string prefix = spec.metric_prefix;
    std::map<std::string, std::vector<std::size_t>> by_unit;
    for (std::size_t i = 0; i < metrics.size(); ++i) {
      const auto &metric = metrics[i];
      if (used[i] || !is_chartable_metric(metric) || !starts_with(metric.name, prefix)) {
        continue;
      }
      // The ISX021 sweep keys share the "ll" prefix with unrelated names.
      if (prefix == "ll" && (metric.name.find("_bp") == std::string::npos ||
                             metric.name.find("_wi") == std::string::npos || !ends_with(metric.name, "_mean_ms"))) {
        continue;
      }
      by_unit[metric.unit].push_back(i);
    }
    for (const auto &entry : by_unit) {
      MetricChartGroup group;
      group.title = spec.title;
      group.category_axis = spec.category_axis;
      group.value_axis = spec.value_axis;
      group.category_kind = spec.category_kind;
      group.kind = spec.horizontal ? MetricChartGroup::Kind::HorizontalBars : MetricChartGroup::Kind::VerticalBars;
      group.label_prefix = prefix.size() > 1 && prefix.back() == '_' ? prefix.substr(0, prefix.size() - 1) : prefix;
      group.indices = entry.second;
      finalize_group(groups, used, metrics, group);
    }
  }

  // 2. Format / resolution sweeps: two or more categories sharing a latency or
  //    throughput suffix collapse into one chart per unit. Keyed on the category
  //    count, not on how many throughput metrics exist -- the old check made a
  //    latency-only run fall through to one dot chart per category.
  {
    const PrefixSweepSpec *spec = nullptr;
    for (const auto &candidate : kPrefixSweepSpecs) {
      if (test_id == candidate.test_id) {
        spec = &candidate;
        break;
      }
    }
    std::vector<std::string> prefixes;
    for (std::size_t i = 0; i < metrics.size(); ++i) {
      if (used[i] || !is_chartable_metric(metrics[i])) {
        continue;
      }
      const std::string prefix = sweep_prefix(metrics[i].name);
      if (!prefix.empty() && std::find(prefixes.begin(), prefixes.end(), prefix) == prefixes.end()) {
        prefixes.push_back(prefix);
      }
    }
    // A prefix sweep is only a sweep when the test is KNOWN to sweep something, or when
    // every prefix reads as a resolution. Without this, any test whose metric names share
    // two prefixes was charted as a format comparison: measured, t13, t16, t23 and t24
    // each rendered a chart titled "... by format" against a "Format" axis while sweeping
    // no pixel format at all -- baseline_/load_ (t24) and cliff/first_miss (t13) are
    // phases and statistics, not formats.
    bool all_resolution = !prefixes.empty();
    for (const auto &prefix : prefixes) {
      all_resolution = all_resolution && looks_like_resolution_label(prefix);
    }
    if (prefixes.size() >= 2 && (spec != nullptr || all_resolution)) {
      bool resolution_sweep = true;
      for (const auto &prefix : prefixes) {
        resolution_sweep = resolution_sweep && looks_like_resolution_label(prefix);
      }
      std::map<std::string, std::vector<std::size_t>> by_unit;
      for (std::size_t i = 0; i < metrics.size(); ++i) {
        if (!used[i] && is_chartable_metric(metrics[i]) && !sweep_prefix(metrics[i].name).empty()) {
          by_unit[metrics[i].unit].push_back(i);
        }
      }
      for (const auto &entry : by_unit) {
        MetricChartGroup group;
        group.category_axis = spec != nullptr ? spec->category_axis : (resolution_sweep ? "Resolution" : "Format");
        group.category_kind = spec != nullptr ? spec->category_kind
                                              : (resolution_sweep ? CategoryKind::Resolution : CategoryKind::Fourcc);
        group.value_axis = prefix_sweep_value_axis(entry.first);
        group.title = group.value_axis + " by " + lower_case(group.category_axis);
        group.kind = MetricChartGroup::Kind::VerticalBars;
        group.indices = entry.second;
        finalize_group(groups, used, metrics, group);
      }
    }
  }

  // 3. Anything left that shares a statistic base becomes a dot chart.
  struct PendingGroup {
    std::string key;
    std::string base;
    std::vector<std::size_t> indices;
  };
  std::vector<PendingGroup> pending;
  for (std::size_t i = 0; i < metrics.size(); ++i) {
    if (used[i] || !is_chartable_metric(metrics[i]) || is_variability_metric(metrics[i].name)) {
      continue;
    }
    const std::string base = statistic_base(metrics[i].name);
    if (base.empty()) {
      continue;
    }
    const std::string key = base + "\n" + metrics[i].unit;
    auto it = std::find_if(pending.begin(), pending.end(), [&](const PendingGroup &group) { return group.key == key; });
    if (it == pending.end()) {
      pending.push_back({key, base, {i}});
    } else {
      it->indices.push_back(i);
    }
  }
  for (const auto &entry : pending) {
    MetricChartGroup group;
    group.title = humanize_metric_name(entry.base);
    group.label_prefix = entry.base;
    group.category_axis = "Statistic";
    // The base often already carries the unit ("first_frame_ms"); drop it so the
    // axis does not read "First frame ms (ms)".
    std::string quantity = entry.base;
    if (ends_with(quantity, "_ms")) {
      quantity.resize(quantity.size() - 3);
    }
    group.value_axis = humanize_metric_name(quantity);
    group.category_kind = CategoryKind::Statistic;
    group.kind = MetricChartGroup::Kind::StatisticDots;
    group.indices = entry.indices;
    finalize_group(groups, used, metrics, group);
  }

  std::sort(groups.begin(), groups.end(), [](const MetricChartGroup &lhs, const MetricChartGroup &rhs) {
    return lhs.indices.front() < rhs.indices.front();
  });
  return groups;
}

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

void render_axis_line(std::ostream &out, double x1, double y1, double x2, double y2) {
  out << "<line class=\"chart-axis\" x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2 << "\" y2=\"" << y2
      << "\"></line>";
}

void render_axis_title_x(std::ostream &out, double center_x, double y, const std::string &label) {
  out << "<text class=\"axis-caption\" x=\"" << center_x << "\" y=\"" << y << "\" text-anchor=\"middle\">"
      << html_escape(label) << "</text>";
}

void render_axis_title_y(std::ostream &out, double x, double center_y, const std::string &label) {
  out << "<text class=\"axis-caption\" transform=\"translate(" << x << "," << center_y
      << ") rotate(-90)\" text-anchor=\"middle\">" << html_escape(label) << "</text>";
}

// The value-axis margin grows with the widest tick label; a fixed margin clipped
// the rotated axis title as soon as ticks reached four digits (MB/s).
double value_axis_margin(const AxisTicks &ticks) {
  std::size_t widest = 1;
  for (double value : ticks.values) {
    widest = std::max(widest, format_metric_value(value).size());
  }
  return 28.0 + static_cast<double>(widest) * 6.0;
}

void render_statistic_dot_chart(std::ostream &out, const std::vector<MetricValue> &metrics,
                                const MetricChartGroup &group) {
  const std::size_t count = group.indices.size();
  double min_value = metrics[group.indices.front()].value;
  double max_value = min_value;
  for (std::size_t index : group.indices) {
    min_value = std::min(min_value, metrics[index].value);
    max_value = std::max(max_value, metrics[index].value);
  }
  const AxisTicks ticks = nice_ticks(min_value < 0.0 ? min_value : 0.0, max_value, 6);

  std::size_t longest_label = 1;
  for (std::size_t index : group.indices) {
    longest_label = std::max(longest_label, statistic_label(metrics[index].name, group.label_prefix).size());
  }

  const double width = kChartWidth;
  const double left = std::min(180.0, 30.0 + static_cast<double>(longest_label) * 6.2);
  constexpr double right = 48.0;
  constexpr double top = 16.0;
  constexpr double bottom = 44.0;
  constexpr double row_height = 26.0;
  const double plot_height = row_height * static_cast<double>(count);
  const double height = top + plot_height + bottom;
  const double plot_width = width - left - right;
  const double baseline_y = top + plot_height;
  const auto x_for = [&](double value) { return left + (value - ticks.min) * plot_width / (ticks.max - ticks.min); };
  const double guide_start_x = x_for(std::max(ticks.min, 0.0));

  out << "<div class=\"metric-chart metric-dot-chart" << (group.wide ? " metric-chart--wide" : "")
      << "\"><div class=\"metric-chart-title\">" << html_escape(group.title);
  if (!group.unit.empty())
    out << " <span>" << html_escape(group.unit) << "</span>";
  out << "</div><svg viewBox=\"0 0 " << width << " " << height << "\" role=\"img\" aria-label=\""
      << html_escape(group.title) << " by " << html_escape(lower_case(group.category_axis)) << "\">";

  for (double value : ticks.values) {
    const double x = x_for(value);
    out << "<line class=\"chart-grid\" x1=\"" << x << "\" y1=\"" << top << "\" x2=\"" << x << "\" y2=\"" << baseline_y
        << "\"></line>";
    out << "<text class=\"axis-value\" x=\"" << x << "\" y=\"" << (baseline_y + 15.0) << "\" text-anchor=\"middle\">"
        << html_escape(format_metric_value(value)) << "</text>";
  }
  if (ticks.min < 0.0) {
    const double zero_x = x_for(0.0);
    out << "<line class=\"zero-baseline\" x1=\"" << zero_x << "\" y1=\"" << top << "\" x2=\"" << zero_x << "\" y2=\""
        << baseline_y << "\"></line>";
  }
  render_axis_line(out, left, baseline_y, width - right, baseline_y);
  render_axis_line(out, left, baseline_y, left, top);
  render_axis_title_x(out, left + plot_width / 2.0, height - 6.0, group.value_axis);
  render_axis_title_y(out, 11.0, top + plot_height / 2.0, group.category_axis);

  for (std::size_t i = 0; i < count; ++i) {
    const std::size_t index = group.indices[i];
    const auto &metric = metrics[index];
    const double x = x_for(metric.value);
    const double y = top + (static_cast<double>(i) + 0.5) * row_height;
    const std::string label = statistic_label(metric.name, group.label_prefix);
    const bool label_to_left = x > width - right - 40.0;
    out << "<line class=\"dot-row-line\" x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << (width - right)
        << "\" y2=\"" << y << "\"></line>";
    out << "<line class=\"guide-line\" x1=\"" << guide_start_x << "\" y1=\"" << y << "\" x2=\"" << x << "\" y2=\"" << y
        << "\"></line>";
    out << "<text class=\"dot-label\" x=\"" << (left - 9.0) << "\" y=\"" << (y + 3.4) << "\" text-anchor=\"end\">"
        << html_escape(label) << "</text>";
    out << "<circle class=\"metric-point\" data-metric=\"" << html_escape(metric.name) << "\" cx=\"" << x << "\" cy=\""
        << y << "\" r=\"5\"><title>"
        << html_escape(label + ": " + format_metric_value(metric.value) +
                       (metric.unit.empty() ? "" : " " + metric.unit))
        << "</title></circle>";
    out << "<text class=\"point-value\" x=\"" << (label_to_left ? x - 9.0 : x + 9.0) << "\" y=\"" << (y + 3.4)
        << "\" text-anchor=\"" << (label_to_left ? "end" : "start") << "\">"
        << html_escape(format_metric_value(metric.value)) << "</text>";
  }
  out << "</svg></div>";
}

const char *bar_color(const MetricChartGroup &group, const BarLayout &layout, const BarItem &item) {
  switch (group.color_rule) {
    case ColorRule::SequentialByCategory:
      return ramp_color(item.category_index, layout.categories.size());
    case ColorRule::OrdinalSeries:
      return series_color(item.series_index);
    case ColorRule::SingleHue:
      break;
  }
  return kSingleHue;
}

std::string bar_tooltip(const MetricValue &metric, const BarLayout &layout, const BarItem &item) {
  std::string text = layout.categories[item.category_index];
  if (layout.series.size() > 1) {
    text += " " + layout.series[item.series_index];
  }
  text += ": " + format_metric_value(metric.value);
  if (!metric.unit.empty()) {
    text += " " + metric.unit;
  }
  return text;
}

// A compact ramp key, so a colour that encodes the category axis can be read.
void render_ramp_legend(std::ostream &out, const BarLayout &layout) {
  if (layout.categories.size() < 2) {
    return;
  }
  out << "<div class=\"chart-ramp-key\"><span>" << html_escape(layout.categories.front()) << "</span><i>";
  for (std::size_t i = 0; i < kSequentialRampSize; ++i) {
    out << "<b style=\"background:" << kSequentialRamp[i] << "\"></b>";
  }
  out << "</i><span>" << html_escape(layout.categories.back()) << "</span></div>";
}

void render_series_legend(std::ostream &out, const BarLayout &layout) {
  if (layout.series.size() < 2) {
    return;
  }
  out << "<div class=\"chart-series-legend\">";
  for (std::size_t i = 0; i < layout.series.size(); ++i) {
    out << "<span><i style=\"background:" << series_color(i) << "\"></i>" << html_escape(layout.series[i]) << "</span>";
  }
  out << "</div>";
}

void render_vertical_bar_chart(std::ostream &out, const std::vector<MetricValue> &metrics,
                               const MetricChartGroup &group) {
  const BarLayout layout = build_bar_layout(metrics, group);
  if (layout.categories.empty() || layout.series.empty()) {
    return;
  }
  const std::size_t category_count = layout.categories.size();
  const AxisTicks ticks = nice_ticks(0.0, layout.max_value, 6);

  std::size_t longest_category = 1;
  for (const auto &category : layout.categories) {
    longest_category = std::max(longest_category, category.size());
  }
  // Rotate only when a horizontal label would not fit its slot; rotated labels
  // need a deeper bottom margin or the axis title lands on top of them.
  // A wide chart takes the whole row, so it needs a wider coordinate system too:
  // stretching the 520-unit box across ~950px scaled every label up by 1.8x.
  const double width = kChartWidth;
  const double left = value_axis_margin(ticks);
  constexpr double right = 14.0;
  constexpr double top = 16.0;
  constexpr double plot_height = 190.0;
  const double slot = (width - left - right) / static_cast<double>(category_count);
  const bool rotate_labels = static_cast<double>(longest_category) * 6.2 > slot - 4.0;
  const double bottom = rotate_labels ? 68.0 : 40.0;
  const double height = top + plot_height + bottom;
  const double baseline_y = top + plot_height;
  const double plot_width = width - left - right;
  constexpr double bar_gap = 2.0;
  const double series_count = static_cast<double>(layout.series.size());
  const double bar_width = std::max(3.0, std::min(30.0, (slot * 0.66 - bar_gap * (series_count - 1.0)) / series_count));
  const double occupied = bar_width * series_count + bar_gap * (series_count - 1.0);
  const auto y_for = [&](double value) { return baseline_y - (std::max(0.0, value) / ticks.max) * plot_height; };

  out << "<div class=\"metric-chart metric-vertical-bars" << (group.wide ? " metric-chart--wide" : "")
      << "\"><div class=\"metric-chart-title\">" << html_escape(group.title);
  if (!group.unit.empty())
    out << " <span>" << html_escape(group.unit) << "</span>";
  out << "</div><svg viewBox=\"0 0 " << width << " " << height << "\" role=\"img\" aria-label=\""
      << html_escape(group.value_axis) << " by " << html_escape(lower_case(group.category_axis)) << "\">";

  for (double value : ticks.values) {
    const double y = y_for(value);
    out << "<line class=\"chart-grid\" x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << (width - right) << "\" y2=\""
        << y << "\"></line>";
    out << "<text class=\"axis-value\" x=\"" << (left - 7.0) << "\" y=\"" << (y + 3.4) << "\" text-anchor=\"end\">"
        << html_escape(format_metric_value(value)) << "</text>";
  }
  render_axis_line(out, left, baseline_y, width - right, baseline_y);
  render_axis_title_y(out, 11.0, top + plot_height / 2.0, group.value_axis);
  render_axis_title_x(out, left + plot_width / 2.0, height - 5.0, group.category_axis);

  // With many bars a number over every one is unreadable, so label the extremes
  // plus every other bar -- printed reports have no tooltips to fall back on.
  double series_min = 0.0;
  double series_max = 0.0;
  bool have_extremes = false;
  for (const auto &item : layout.items) {
    const double value = metrics[item.metric_index].value;
    if (!have_extremes) {
      series_min = value;
      series_max = value;
      have_extremes = true;
    }
    series_min = std::min(series_min, value);
    series_max = std::max(series_max, value);
  }
  std::size_t widest_value = 1;
  for (const auto &item : layout.items) {
    widest_value = std::max(widest_value, format_metric_value(metrics[item.metric_index].value).size());
  }
  const bool label_all = slot >= static_cast<double>(widest_value) * 6.2 + 4.0;

  for (const auto &item : layout.items) {
    const auto &metric = metrics[item.metric_index];
    const double center_x = left + slot * (static_cast<double>(item.category_index) + 0.5);
    const double x = center_x - occupied / 2.0 + static_cast<double>(item.series_index) * (bar_width + bar_gap);
    const double y = y_for(metric.value);
    out << "<rect class=\"vertical-bar\" data-metric=\"" << html_escape(metric.name) << "\" x=\"" << x << "\" y=\"" << y
        << "\" width=\"" << bar_width << "\" height=\"" << std::max(0.8, baseline_y - y) << "\" rx=\"2\" fill=\""
        << bar_color(group, layout, item) << "\"><title>" << html_escape(bar_tooltip(metric, layout, item))
        << "</title></rect>";
    const bool is_extreme = metric.value == series_min || metric.value == series_max;
    if (layout.series.size() == 1 && (label_all || is_extreme || item.category_index % 2 == 0)) {
      out << "<text class=\"bar-point-value\" x=\"" << (x + bar_width / 2.0) << "\" y=\"" << (y - 4.0)
          << "\" text-anchor=\"middle\">" << html_escape(format_metric_value(metric.value)) << "</text>";
    }
  }

  for (std::size_t i = 0; i < category_count; ++i) {
    const double x = left + slot * (static_cast<double>(i) + 0.5);
    if (rotate_labels) {
      out << "<text class=\"axis-label\" transform=\"translate(" << (x + 3.0) << "," << (baseline_y + 13.0)
          << ") rotate(-40)\" text-anchor=\"end\">" << html_escape(layout.categories[i]) << "</text>";
    } else {
      out << "<text class=\"axis-label\" x=\"" << x << "\" y=\"" << (baseline_y + 14.0) << "\" text-anchor=\"middle\">"
          << html_escape(layout.categories[i]) << "</text>";
    }
  }
  out << "</svg>";
  if (group.color_rule == ColorRule::SequentialByCategory) {
    render_ramp_legend(out, layout);
  }
  render_series_legend(out, layout);
  out << "</div>";
}

// Horizontal bars are SVG like every other chart and carry a real value axis with
// ticks. The previous CSS-div version normalised every bar against the largest,
// so the longest bar was always full width and no scale existed at all.
void render_horizontal_chart(std::ostream &out, const std::vector<MetricValue> &metrics,
                             const MetricChartGroup &group) {
  const BarLayout layout = build_bar_layout(metrics, group);
  if (layout.categories.empty() || layout.series.empty()) {
    return;
  }
  const std::size_t category_count = layout.categories.size();
  const AxisTicks ticks = nice_ticks(0.0, layout.max_value, 6);

  std::size_t longest_category = 1;
  for (const auto &category : layout.categories) {
    longest_category = std::max(longest_category, category.size());
  }

  const double width = kChartWidth;
  const double left = std::min(240.0, 22.0 + static_cast<double>(longest_category) * 6.2);
  constexpr double right = 44.0;
  constexpr double top = 26.0;
  constexpr double bottom = 34.0;
  const double row_height = layout.series.size() > 1 ? 12.0 * static_cast<double>(layout.series.size()) + 8.0 : 22.0;
  const double plot_height = row_height * static_cast<double>(category_count);
  const double height = top + plot_height + bottom;
  const double plot_width = width - left - right;
  const auto x_for = [&](double value) { return left + (std::max(0.0, value) / ticks.max) * plot_width; };

  out << "<div class=\"metric-chart metric-bars" << (group.wide ? " metric-chart--wide" : "")
      << "\"><div class=\"metric-chart-title\">" << html_escape(group.title);
  if (!group.unit.empty())
    out << " <span>" << html_escape(group.unit) << "</span>";
  out << "</div><svg viewBox=\"0 0 " << width << " " << height << "\" role=\"img\" aria-label=\""
      << html_escape(group.value_axis) << " by " << html_escape(lower_case(group.category_axis)) << "\">";

  for (double value : ticks.values) {
    const double x = x_for(value);
    out << "<line class=\"chart-grid\" x1=\"" << x << "\" y1=\"" << top << "\" x2=\"" << x << "\" y2=\""
        << (top + plot_height) << "\"></line>";
    out << "<text class=\"axis-value\" x=\"" << x << "\" y=\"" << (top + plot_height + 14.0)
        << "\" text-anchor=\"middle\">" << html_escape(format_metric_value(value)) << "</text>";
  }
  render_axis_line(out, left, top, left, top + plot_height);
  render_axis_title_x(out, left + plot_width / 2.0, height - 4.0, group.value_axis);
  // The category-axis title sits above its label column rather than rotated
  // alongside it, where it overlapped the labels themselves.
  out << "<text class=\"axis-caption\" x=\"0\" y=\"12\" text-anchor=\"start\">" << html_escape(group.category_axis)
      << "</text>";

  const double bar_height = layout.series.size() > 1 ? 10.0 : row_height - 8.0;
  for (const auto &item : layout.items) {
    const auto &metric = metrics[item.metric_index];
    const double row_top = top + row_height * static_cast<double>(item.category_index);
    const double y = layout.series.size() > 1
                         ? row_top + 4.0 + static_cast<double>(item.series_index) * (bar_height + 2.0)
                         : row_top + 4.0;
    out << "<rect class=\"horizontal-bar\" data-metric=\"" << html_escape(metric.name) << "\" x=\"" << left << "\" y=\""
        << y << "\" width=\"" << std::max(0.8, x_for(metric.value) - left) << "\" height=\"" << bar_height
        << "\" rx=\"2\" fill=\"" << bar_color(group, layout, item) << "\"><title>"
        << html_escape(bar_tooltip(metric, layout, item)) << "</title></rect>";
    if (layout.series.size() == 1) {
      out << "<text class=\"bar-point-value\" x=\"" << (x_for(metric.value) + 5.0) << "\" y=\""
          << (y + bar_height - 1.5) << "\" text-anchor=\"start\">" << html_escape(format_metric_value(metric.value))
          << "</text>";
    }
  }

  for (std::size_t i = 0; i < category_count; ++i) {
    const double row_top = top + row_height * static_cast<double>(i);
    out << "<text class=\"axis-label\" x=\"" << (left - 8.0) << "\" y=\"" << (row_top + row_height / 2.0 + 3.4)
        << "\" text-anchor=\"end\">" << html_escape(layout.categories[i]) << "</text>";
  }
  out << "</svg>";
  if (group.color_rule == ColorRule::SequentialByCategory) {
    render_ramp_legend(out, layout);
  }
  render_series_legend(out, layout);
  out << "</div>";
}

struct TimeoutProbePoint {
  int timeout_ms = 0;
  int hits = 0;
  int total = 0;
};

bool parse_timeout_hit_pair(const char *text, TimeoutProbePoint *point) {
  int timeout = 0;
  int hits = 0;
  int total = 0;
  if (std::sscanf(text, "%dms%*[^0-9]%d/%d", &timeout, &hits, &total) == 3 && total > 0) {
    point->timeout_ms = timeout;
    point->hits = hits;
    point->total = total;
    return true;
  }
  return false;
}

std::vector<TimeoutProbePoint> parse_t13_probe_points(const std::vector<std::string> &details) {
  std::map<int, std::pair<int, int>> totals_by_timeout;
  for (const auto &detail : details) {
    if (starts_with(detail, "coarse:") || starts_with(detail, "bsearch:")) {
      const std::size_t colon = detail.find(':');
      TimeoutProbePoint point;
      if (colon != std::string::npos && parse_timeout_hit_pair(detail.c_str() + colon + 1, &point)) {
        totals_by_timeout[point.timeout_ms].first += point.hits;
        totals_by_timeout[point.timeout_ms].second += point.total;
      }
      continue;
    }

    if (!starts_with(detail, "stability round")) {
      continue;
    }
    std::size_t at = detail.find('@');
    while (at != std::string::npos) {
      TimeoutProbePoint point;
      if (parse_timeout_hit_pair(detail.c_str() + at + 1, &point)) {
        totals_by_timeout[point.timeout_ms].first += point.hits;
        totals_by_timeout[point.timeout_ms].second += point.total;
      }
      at = detail.find('@', at + 1);
    }
  }

  std::vector<TimeoutProbePoint> points;
  for (const auto &entry : totals_by_timeout) {
    points.push_back({entry.first, entry.second.first, entry.second.second});
  }
  return points;
}

int metric_index_by_name(const std::vector<MetricValue> &metrics, const std::string &name) {
  for (std::size_t i = 0; i < metrics.size(); ++i) {
    if (metrics[i].name == name) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void render_t13_distribution_chart(std::ostream &out, const std::vector<TimeoutProbePoint> &points) {
  if (points.size() < 2) {
    return;
  }
  constexpr double width = kChartWidth;
  constexpr double height = 250.0;
  constexpr double left = 44.0;
  constexpr double right = 18.0;
  constexpr double top = 16.0;
  constexpr double bottom = 62.0;
  constexpr double point_inset = 18.0;
  const double plot_width = width - left - right - (point_inset * 2.0);
  const double plot_height = height - top - bottom;
  const auto x_for = [&](std::size_t index) { return left + point_inset + plot_width * index / (points.size() - 1); };
  const auto y_for = [&](double ratio) { return top + 8.0 + (100.0 - ratio) * (plot_height - 16.0) / 100.0; };
  const double baseline_y = top + plot_height;

  out << "<div class=\"metric-chart metric-xy-chart t13-distribution-chart metric-chart--wide\">"
         "<div class=\"metric-chart-title\">Timeout hit distribution <span>%</span></div>"
         "<svg viewBox=\"0 0 906 250\" role=\"img\" aria-label=\"Hit ratio by poll timeout\">";
  out << "<rect class=\"hit-zone hit-zone-high\" x=\"" << left << "\" y=\"" << top << "\" width=\""
      << (width - left - right) << "\" height=\"" << (plot_height * 0.25) << "\"></rect>";
  out << "<rect class=\"hit-zone hit-zone-mid\" x=\"" << left << "\" y=\"" << (top + plot_height * 0.25)
      << "\" width=\"" << (width - left - right) << "\" height=\"" << (plot_height * 0.5) << "\"></rect>";
  out << "<rect class=\"hit-zone hit-zone-low\" x=\"" << left << "\" y=\"" << (top + plot_height * 0.75)
      << "\" width=\"" << (width - left - right) << "\" height=\"" << (plot_height * 0.25) << "\"></rect>";
  for (int tick = 0; tick <= 4; ++tick) {
    const double value = 100.0 - 25.0 * tick;
    const double y = y_for(value);
    out << "<line class=\"chart-grid\" x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << (width - right) << "\" y2=\""
        << y << "\"></line>";
    out << "<text class=\"axis-value\" x=\"" << (left - 7.0) << "\" y=\"" << (y + 3.4) << "\" text-anchor=\"end\">"
        << html_escape(format_metric_value(value)) << "</text>";
  }
  render_axis_line(out, left, baseline_y, width - right, baseline_y);
  render_axis_title_x(out, left + (width - left - right) / 2.0, height - 5.0, "Probed timeout (ms)");
  render_axis_title_y(out, 11.0, top + plot_height / 2.0, "Hit ratio (%)");

  std::ostringstream path;
  std::ostringstream area;
  area << x_for(0) << "," << baseline_y << " ";
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto &point = points[i];
    const double ratio = 100.0 * point.hits / point.total;
    const double x = x_for(i);
    const double y = y_for(ratio);
    if (i)
      path << " ";
    path << x << "," << y;
    area << x << "," << y << " ";
    out << "<line class=\"guide-line\" x1=\"" << x << "\" y1=\"" << y << "\" x2=\"" << x << "\" y2=\"" << baseline_y
        << "\"></line>";
  }
  area << x_for(points.size() - 1) << "," << baseline_y;
  out << "<polygon class=\"distribution-area\" points=\"" << area.str() << "\"></polygon>";
  out << "<polyline class=\"metric-line distribution-line\" points=\"" << path.str() << "\"></polyline>";

  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto &point = points[i];
    const double ratio = 100.0 * point.hits / point.total;
    const double x = x_for(i);
    const double y = y_for(ratio);
    const double value_y = y < top + 16.0 ? y + 16.0 : y - 8.0;
    out << "<circle class=\"metric-point t13-probe-point\" data-timeout-ms=\"" << point.timeout_ms << "\" cx=\"" << x
        << "\" cy=\"" << y << "\" r=\"4.5\"><title>" << point.timeout_ms << "ms: " << point.hits << "/" << point.total
        << " hits</title></circle>";
    out << "<text class=\"point-value\" x=\"" << x << "\" y=\"" << value_y << "\" text-anchor=\"middle\">" << point.hits
        << "/" << point.total << "</text>";
    out << "<text class=\"axis-label\" transform=\"translate(" << (x + 3.0) << "," << (baseline_y + 13.0)
        << ") rotate(-40)\" text-anchor=\"end\">" << point.timeout_ms << "</text>";
  }
  out << "</svg></div>";
}

bool render_t13_threshold_chart(std::ostream &out, const std::vector<MetricValue> &metrics, std::vector<bool> &used) {
  const int cliff_index = metric_index_by_name(metrics, "cliff_ms");
  const int miss_index = metric_index_by_name(metrics, "first_miss_ms");
  const int safety_index = metric_index_by_name(metrics, "safety_margin_ms");
  if (cliff_index < 0 || !is_chartable_metric(metrics[cliff_index])) {
    return false;
  }

  struct ThresholdMarker {
    std::string label;
    std::string metric_name;
    double value = 0.0;
  };
  std::vector<ThresholdMarker> markers;
  if (miss_index >= 0 && is_chartable_metric(metrics[miss_index])) {
    markers.push_back({"First miss", metrics[miss_index].name, metrics[miss_index].value});
    used[miss_index] = true;
  }
  markers.push_back({"Cliff", metrics[cliff_index].name, metrics[cliff_index].value});
  used[cliff_index] = true;
  if (safety_index >= 0 && is_chartable_metric(metrics[safety_index])) {
    markers.push_back(
        {"Production", "production_timeout_ms", metrics[cliff_index].value + metrics[safety_index].value});
    used[safety_index] = true;
  }
  if (markers.size() < 2) {
    return false;
  }

  double min_value = markers.front().value;
  double max_value = markers.front().value;
  for (const auto &marker : markers) {
    min_value = std::min(min_value, marker.value);
    max_value = std::max(max_value, marker.value);
  }
  const AxisTicks ticks = nice_ticks(0.0, max_value, 5);
  min_value = ticks.min;
  const double span = std::max(1.0, ticks.max - ticks.min);

  constexpr double width = kChartWidth;
  constexpr double left = 120.0;
  constexpr double right = 24.0;
  constexpr double top = 16.0;
  constexpr double bottom = 40.0;
  const double row_height = 26.0;
  const double plot_height = row_height * static_cast<double>(markers.size());
  const double height = top + plot_height + bottom;
  const double baseline_y = top + plot_height;
  const auto x_for = [&](double value) { return left + (width - left - right) * (value - min_value) / span; };
  const double cliff_x = x_for(metrics[cliff_index].value);
  double production_x = width - right;
  const auto production = std::find_if(markers.begin(), markers.end(), [](const ThresholdMarker &marker) {
    return marker.metric_name == "production_timeout_ms";
  });
  if (production != markers.end()) {
    production_x = x_for(production->value);
  }

  out << "<div class=\"metric-chart t13-threshold-chart metric-chart--wide\"><div class=\"metric-chart-title\">Timeout "
         "thresholds <span>ms</span></div><svg viewBox=\"0 0 "
      << width << " " << height << "\" role=\"img\" aria-label=\"Poll timeout by threshold type\">";
  out << "<rect class=\"threshold-zone threshold-risk\" x=\"" << left << "\" y=\"" << top << "\" width=\""
      << std::max(0.0, cliff_x - left) << "\" height=\"" << plot_height << "\"></rect>";
  out << "<rect class=\"threshold-zone threshold-margin\" x=\"" << cliff_x << "\" y=\"" << top << "\" width=\""
      << std::max(0.0, production_x - cliff_x) << "\" height=\"" << plot_height << "\"></rect>";
  out << "<rect class=\"threshold-zone threshold-safe\" x=\"" << production_x << "\" y=\"" << top << "\" width=\""
      << std::max(0.0, width - right - production_x) << "\" height=\"" << plot_height << "\"></rect>";
  for (double value : ticks.values) {
    const double x = x_for(value);
    out << "<line class=\"chart-grid\" x1=\"" << x << "\" y1=\"" << top << "\" x2=\"" << x << "\" y2=\"" << baseline_y
        << "\"></line>";
    out << "<text class=\"axis-value\" x=\"" << x << "\" y=\"" << (baseline_y + 15.0) << "\" text-anchor=\"middle\">"
        << html_escape(format_metric_value(value)) << "</text>";
  }
  render_axis_line(out, left, baseline_y, width - right, baseline_y);
  render_axis_line(out, left, baseline_y, left, top);
  render_axis_title_x(out, left + (width - left - right) / 2.0, height - 5.0, "Poll timeout (ms)");
  render_axis_title_y(out, 11.0, top + plot_height / 2.0, "Threshold");
  for (std::size_t i = 0; i < markers.size(); ++i) {
    const auto &marker = markers[i];
    const double x = x_for(marker.value);
    const double y = top + (static_cast<double>(i) + 0.5) * row_height;
    out << "<line class=\"threshold-row\" x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << (width - right)
        << "\" y2=\"" << y << "\"></line>";
    out << "<line class=\"guide-line\" x1=\"" << x << "\" y1=\"" << y << "\" x2=\"" << x << "\" y2=\"" << baseline_y
        << "\"></line>";
    out << "<text class=\"threshold-label\" x=\"" << (left - 9.0) << "\" y=\"" << (y + 3.4) << "\" text-anchor=\"end\">"
        << html_escape(marker.label) << "</text>";
    out << "<circle class=\"threshold-point marker-" << (i % 4) << "\" data-metric=\""
        << html_escape(marker.metric_name) << "\" cx=\"" << x << "\" cy=\"" << y << "\" r=\"5\"><title>"
        << html_escape(marker.label + ": " + format_metric_value(marker.value) + " ms") << "</title></circle>";
    const bool value_to_left = x > width - right - 40.0;
    out << "<text class=\"point-value\" x=\"" << (value_to_left ? x - 9.0 : x + 9.0) << "\" y=\"" << (y + 3.4)
        << "\" text-anchor=\"" << (value_to_left ? "end" : "start") << "\">"
        << html_escape(format_metric_value(marker.value)) << "</text>";
  }
  out << "</svg></div>";
  return true;
}

bool render_t13_visuals(std::ostream &out, const TestResult &test, std::vector<bool> &used) {
  bool opened = false;
  const auto points = parse_t13_probe_points(test.details);
  if (points.size() >= 2) {
    out << "<div class=\"metric-visuals t13-visuals\">";
    opened = true;
    render_t13_distribution_chart(out, points);
  }
  std::ostringstream threshold_html;
  if (render_t13_threshold_chart(threshold_html, test.metrics, used)) {
    if (!opened) {
      out << "<div class=\"metric-visuals t13-visuals\">";
      opened = true;
    }
    out << threshold_html.str();
  }
  if (opened) {
    out << "</div>";
  }
  return opened;
}

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

void render_test_metrics(std::ostream &out, const TestResult &test) {
  const auto &metrics = test.metrics;
  std::vector<bool> used;
  used.assign(metrics.size(), false);
  bool has_visuals = false;
  if (test.id == "t13-poll-timeout-cliff") {
    has_visuals = render_t13_visuals(out, test, used);
  }
  const auto groups = group_metrics(metrics, used, test.id);
  if (!groups.empty())
    out << "<div class=\"metric-visuals\">";
  for (const auto &group : groups) {
    if (group.kind == MetricChartGroup::Kind::HorizontalBars) {
      render_horizontal_chart(out, metrics, group);
    } else if (group.kind == MetricChartGroup::Kind::VerticalBars) {
      render_vertical_bar_chart(out, metrics, group);
    } else {
      render_statistic_dot_chart(out, metrics, group);
    }
  }
  if (!groups.empty())
    out << "</div>";
  // The un-charted metrics used to be dumped here as a generic key/value list. The test's
  // own content renderer presents them now (plan 3.1, review round 2), so emitting the
  // list too would print the same number twice under two different labels.
  (void)has_visuals;
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
.chart-frame { padding: 0; }
.section .section { padding: 0; border-bottom: 0; }
.metric-chart { padding: 14px 0; }
/* The SVG renders at its viewBox width and is NOT stretched: with width:100% the box is
   whatever the container happens to be (measured: 852, 870, 886, 934, 938) and every
   declared font-size is multiplied by that ratio. Fixing the rendered width to the
   viewBox width makes one SVG unit exactly one CSS pixel, so an 8px label is 8px.
   A narrow viewport still scales the whole chart down together via max-width. */
.chart-frame svg, .metric-chart svg { display: block; width: 906px; min-width: 906px; height: auto; overflow: visible; }
/* Below ~1000px the fixed-width chart no longer fits. It SCROLLS inside its own box
   rather than being squeezed: shrinking it would scale every label with it, which is the
   defect the fixed width exists to prevent. The page itself never scrolls sideways. */
.chart-frame, .metric-chart { overflow-x: auto; max-width: 100%; }
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

      // The charts come first: they are this test's visual evidence, and several of them
      // consume metrics that the table below would otherwise repeat.
      // Charts only where an approved preview has one (plan 3.1, review round 3): the
      // selector picks statistic families automatically, which grew a dot chart on eight
      // tests whose previews show none. An unapproved chart in a customer-facing report is
      // not a bonus.
      if (!test.metrics.empty() && test_charts_approved(test.id)) {
        render_test_metrics(out, test);
      }
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
