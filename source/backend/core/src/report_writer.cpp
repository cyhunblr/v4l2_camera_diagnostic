#include "v4l2diag/core/report_writer.hpp"

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

std::string json_escape(const std::string &value) {
  std::ostringstream out;
  for (char c : value) {
    switch (c) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << c;
        break;
    }
  }
  return out.str();
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

std::string html_escape(const std::string &value) {
  std::ostringstream out;
  for (char c : value) {
    switch (c) {
      case '&':
        out << "&amp;";
        break;
      case '<':
        out << "&lt;";
        break;
      case '>':
        out << "&gt;";
        break;
      case '"':
        out << "&quot;";
        break;
      default:
        out << c;
        break;
    }
  }
  return out.str();
}

bool starts_with(const std::string &value, const std::string &prefix) {
  return value.size() >= prefix.size() && value.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &value, const std::string &suffix) {
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string humanize_metric_name(std::string value) {
  std::replace(value.begin(), value.end(), '_', ' ');
  if (!value.empty() && value[0] >= 'a' && value[0] <= 'z') {
    value[0] = static_cast<char>(value[0] - 'a' + 'A');
  }
  return value;
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

bool is_sentinel_metric(const MetricValue &metric) {
  if (std::fabs(metric.value + 500.0) < 0.000001) {
    return true;
  }
  if (std::fabs(metric.value + 1.0) >= 0.000001) {
    return false;
  }
  std::string description = metric.description;
  std::transform(description.begin(), description.end(), description.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return metric.name.find("cliff") != std::string::npos || metric.name.find("safety_margin") != std::string::npos ||
         metric.name.find("min_reliable") != std::string::npos || description.find("n/a") != std::string::npos ||
         description.find("none") != std::string::npos || description.find("no cliff") != std::string::npos ||
         description.find("could not") != std::string::npos;
}

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
std::string control_combo_label(const std::string &name) {
  int led = 0;
  int bypass = 0;
  int window = 0;
  if (std::sscanf(name.c_str(), "ll%d_bp%d_wi%d", &led, &bypass, &window) != 3) {
    return humanize_metric_name(name);
  }
  std::ostringstream out;
  out << "LED " << led << " \xc2\xb7 BYP " << bypass << " \xc2\xb7 WIN " << window;
  return out.str();
}

std::string upper_case(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
  return value;
}

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
    if (prefixes.size() >= 2) {
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
      {"Warnings", "warn", warn_count},
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

  const double width = group.wide ? 780.0 : 460.0;
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
  const double width = group.wide ? 780.0 : 460.0;
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

  const double width = group.wide ? 780.0 : 460.0;
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
  constexpr double width = 780.0;
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
         "<svg viewBox=\"0 0 780 250\" role=\"img\" aria-label=\"Hit ratio by poll timeout\">";
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

  constexpr double width = 780.0;
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

void render_plain_metrics(std::ostream &out, const std::vector<MetricValue> &metrics, const std::vector<bool> &used,
                          bool has_visuals) {
  bool has_plain_metrics = false;
  for (std::size_t i = 0; i < metrics.size(); ++i)
    has_plain_metrics = has_plain_metrics || !used[i];
  if (!has_plain_metrics)
    return;

  if (has_visuals) {
    out << "<div class=\"supporting-values\"><div class=\"supporting-title\">Supporting values</div>";
  }
  out << "<dl class=\"metric-kv-list\">";
  for (std::size_t i = 0; i < metrics.size(); ++i) {
    if (used[i])
      continue;
    const auto &metric = metrics[i];
    out << "<div class=\"metric-kv-row\"><dt>" << html_escape(humanize_metric_name(metric.name)) << "</dt><dd>";
    if (is_sentinel_metric(metric)) {
      out << "N/A";
    } else if (metric.unit == "bool") {
      out << (metric.value != 0.0 ? "true" : "false");
    } else {
      out << html_escape(format_metric_value(metric.value));
      if (!metric.unit.empty())
        out << " <span>" << html_escape(metric.unit) << "</span>";
    }
    out << "</dd></div>";
  }
  out << "</dl>";
  if (has_visuals) {
    out << "</div>";
  }
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

  // One category appears on every chart of the sweep (latency and throughput), so
  // it must only be counted once.
  std::vector<std::string> charted;
  for (const auto &group : groups) {
    if (group.category_kind != spec->category_kind) {
      continue;
    }
    const BarLayout layout = build_bar_layout(metrics, group);
    for (const auto &category : layout.categories) {
      if (std::find(charted.begin(), charted.end(), category) == charted.end()) {
        charted.push_back(category);
      }
    }
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
  render_chart_omissions(out, test, metrics, groups);
  has_visuals = has_visuals || !groups.empty();
  render_plain_metrics(out, metrics, used, has_visuals);
}

void write_json(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  out << std::fixed << std::setprecision(3);
  out << "{\n";
  out << "  \"project\": \"" << json_escape(result.project_name) << "\",\n";
  out << "  \"started_at_utc\": \"" << json_escape(result.started_at_utc) << "\",\n";
  out << "  \"finished_at_utc\": \"" << json_escape(result.finished_at_utc) << "\",\n";
  out << "  \"host_name\": \"" << json_escape(result.host_name) << "\",\n";
  out << "  \"kernel_release\": \"" << json_escape(result.kernel_release) << "\",\n";
  out << "  \"kernel_version\": \"" << json_escape(result.kernel_version) << "\",\n";
  out << "  \"run_mode\": \"" << to_string(result.run_mode) << "\",\n";
  out << "  \"cameras\": [\n";
  for (std::size_t ci = 0; ci < result.cameras.size(); ++ci) {
    const auto &camera = result.cameras[ci];
    out << "    {\n";
    out << "      \"path\": \"" << json_escape(camera.camera_path) << "\",\n";
    out << "      \"profile_id\": \"" << json_escape(camera.profile_id) << "\",\n";
    out << "      \"trigger_mode\": \"" << to_string(camera.trigger_mode) << "\",\n";
    out << "      \"trigger_channel_id\": \"" << json_escape(camera.trigger_channel_id) << "\",\n";
    out << "      \"trigger_description\": \"" << json_escape(camera.trigger_description) << "\",\n";
    out << "      \"trigger_rate_hz\": " << camera.trigger_rate_hz << ",\n";
    out << "      \"pulse_width_ms\": " << camera.pulse_width_ms << ",\n";
    out << "      \"memory_backends\": [";
    for (std::size_t bi = 0; bi < camera.memory_backends.size(); ++bi) {
      if (bi) {
        out << ", ";
      }
      out << "\"" << to_string(camera.memory_backends[bi]) << "\"";
    }
    out << "],\n";
    out << "      \"tests\": [\n";
    for (std::size_t ti = 0; ti < camera.tests.size(); ++ti) {
      const auto &test = camera.tests[ti];
      out << "        {\n";
      out << "          \"id\": \"" << json_escape(test.id) << "\",\n";
      out << "          \"name\": \"" << json_escape(test.name) << "\",\n";
      out << "          \"category\": \"" << json_escape(test.category) << "\",\n";
      out << "          \"memory_backend\": \"" << json_escape(test.memory_backend) << "\",\n";
      out << "          \"status\": \"" << to_string(test.status) << "\",\n";
      out << "          \"summary\": \"" << json_escape(test.summary) << "\",\n";
      out << "          \"duration_ms\": " << test.duration_ms << ",\n";
      out << "          \"metrics\": [";
      for (std::size_t mi = 0; mi < test.metrics.size(); ++mi) {
        const auto &metric = test.metrics[mi];
        if (mi) {
          out << ", ";
        }
        out << "{\"name\":\"" << json_escape(metric.name) << "\",\"unit\":\"" << json_escape(metric.unit)
            << "\",\"value\":" << metric.value << ",\"description\":\"" << json_escape(metric.description) << "\"}";
      }
      out << "],\n";
      out << "          \"details\": [";
      for (std::size_t di = 0; di < test.details.size(); ++di) {
        if (di) {
          out << ", ";
        }
        out << "\"" << json_escape(test.details[di]) << "\"";
      }
      out << "]\n";
      out << "        }" << (ti + 1 == camera.tests.size() ? "" : ",") << "\n";
    }
    out << "      ]\n";
    out << "    }" << (ci + 1 == result.cameras.size() ? "" : ",") << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

void write_markdown(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  out << "# V4L2 Camera Diagnostic Report\n\n";
  out << "- Project: `" << result.project_name << "`\n";
  out << "- Started: `" << result.started_at_utc << "`\n";
  out << "- Finished: `" << result.finished_at_utc << "`\n";
  out << "- Host: `" << result.host_name << "`\n";
  out << "- Kernel: `" << result.kernel_release << "` (`" << result.kernel_version << "`)\n";
  out << "- Run mode: `" << to_string(result.run_mode) << "`\n\n";

  for (const auto &camera : result.cameras) {
    out << "## Camera `" << camera.camera_path << "`\n\n";
    out << "- Profile: `" << camera.profile_id << "`\n";
    out << "- Trigger mode: `" << to_string(camera.trigger_mode) << "`\n";
    if (!camera.trigger_channel_id.empty()) {
      out << "- Trigger channel: `" << camera.trigger_channel_id << "`\n";
    }
    if (!camera.trigger_description.empty()) {
      out << "- Trigger detail: `" << camera.trigger_description << "`\n";
    }
    if (camera.trigger_mode != TriggerMode::FreeRun) {
      out << "- Trigger rate: `" << std::fixed << std::setprecision(2) << camera.trigger_rate_hz << " Hz`\n";
      out << "- Pulse width: `" << std::fixed << std::setprecision(2) << camera.pulse_width_ms << " ms`\n";
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
      if (test.metrics.empty() && test.details.empty()) {
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
      for (const auto &detail : test.details) {
        out << "- " << detail << "\n";
      }
      out << "\n";
    }
  }
}

void write_html(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  out << R"(<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>V4L2 Camera Diagnostic Report</title>
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
.meta-note { color: #94a3b8; font-size: 11px; font-style: italic; margin-top: 8px; padding-top: 8px; border-top: 1px solid rgba(255,255,255,0.06); }

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
table.overview .status-cell.pass { color: var(--pass); }
table.overview .status-cell.fail { color: var(--fail); }
table.overview .status-cell.warn { color: var(--warn); }
table.overview .status-cell.skipped { color: var(--skip); }
table.overview .duration { color: #64748b; font-family: monospace; }
table.overview .summary-text { color: #475569; }

.test-section { border: 1px solid #e2e8f0; border-radius: 8px; margin: 16px 24px; overflow: hidden; }
.test-section-header { padding: 12px 16px; display: flex; align-items: center; gap: 10px; border-bottom: 1px solid #f1f5f9; }
.test-section-header.pass { background: #f0fdf4; border-left: 4px solid var(--pass); }
.test-section-header.fail { background: #fef2f2; border-left: 4px solid var(--fail); }
.test-section-header.warn { background: #fffbeb; border-left: 4px solid var(--warn); }
.test-section-header.skipped { background: #f8fafc; border-left: 4px solid var(--skip); }
.test-section-header h3 { margin: 0; font-size: 14px; }
.test-section-header .badge { padding: 3px 8px; border-radius: 4px; font-size: 11px; font-weight: 700; text-transform: uppercase; }
.test-section-header .badge.pass { background: #dcfce7; color: var(--pass); }
.test-section-header .badge.fail { background: #fee2e2; color: var(--fail); }
.test-section-header .badge.warn { background: #fef3c7; color: var(--warn); }
.test-section-header .badge.skipped { background: #f1f5f9; color: var(--skip); }
.test-body { padding: 16px; }
/* Charts use a ~520-unit viewBox so one SVG unit renders at roughly one CSS
   pixel: with the old 760-unit box inside a 360px column every label was scaled
   down to about 7px. Wide charts (many categories, or horizontal rows) take the
   full row instead of being squeezed into a column. */
.metric-visuals { display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 420px), 1fr)); gap: 12px; margin-bottom: 12px; }
.metric-chart { border: 1px solid #dbe4ee; border-radius: 6px; background: #fff; padding: 14px; box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04); }
.metric-chart--wide { grid-column: 1 / -1; }
.metric-chart-title { color: #1e293b; font-size: 13px; font-weight: 750; margin-bottom: 8px; }
.metric-chart-title span { color: #94a3b8; font-size: 10px; font-weight: 600; margin-left: 4px; }
.metric-chart svg { display: block; width: 100%; max-width: 100%; height: auto; overflow: visible; }
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
.export-actions { position: fixed; top: 20px; right: 20px; display: flex; flex-direction: column; gap: 10px; }
.export-pdf-btn { display: flex; align-items: center; gap: 8px; text-decoration: none;
                  background: #0f172a; color: white; border: none; border-radius: 8px; padding: 10px 18px;
                  font-size: 13px; font-weight: 700; font-family: inherit; cursor: pointer; box-shadow: 0 4px 12px rgba(0,0,0,0.2); }
.export-pdf-btn:hover { background: #1e293b; }
@media (max-width: 700px) {
  .container { padding: 20px 12px; }
  .header { padding: 28px 20px; border-radius: 8px; }
  .test-section { margin: 12px; }
  .test-section-header { align-items: flex-start; flex-wrap: wrap; }
  table.overview thead { display: none; }
  table.overview tbody { display: block; }
  table.overview tr { display: grid; grid-template-columns: minmax(0, 1fr) auto auto; gap: 6px 12px; padding: 12px 14px; }
  table.overview td { padding: 0; border: none; }
  table.overview tr + tr { border-top: 1px solid #e2e8f0; }
  table.overview td:nth-child(1) { grid-column: 1 / 4; grid-row: 1; overflow-wrap: anywhere; }
  table.overview td:nth-child(2) { grid-column: 1; grid-row: 2; color: #64748b; }
  table.overview td:nth-child(3) { grid-column: 2; grid-row: 2; }
  table.overview td:nth-child(4) { grid-column: 3; grid-row: 2; }
  table.overview td:nth-child(5) { grid-column: 1 / 4; grid-row: 3; }
  /* The chart viewBox now matches its rendered width, so the label sizes no
     longer need to be scaled up to compensate for a 760-unit box. */
  .export-actions { position: static; flex-direction: row; padding: 12px; background: #f8fafc; }
}
@media print { body { background: white; } .container { padding: 20px; } .header { break-inside: avoid; }
               .test-section, .metric-chart, .result-distribution { break-inside: avoid; }
               .export-actions { display: none; }
               /* A printed column is narrower than a screen row, so a narrow chart
                  would be blown up while a wide one shrank. Cap the narrow ones and
                  let the wide ones use the full text width. */
               .metric-chart:not(.metric-chart--wide) svg { max-width: 500px; }
               @page { margin: 15mm 10mm; size: A4; } }
</style></head><body>
<div class="export-actions">
<button class="export-pdf-btn" onclick='window.print()'>Export as PDF</button>
<a class="export-pdf-btn" href="/api/dmesg?download=1" download="dmesg.txt">Export DMESG</a>
</div>
<div class="container">
)";

  // Header
  out << "<div class=\"header\"><h1>V4L2 Camera Diagnostic Report</h1>";
  out << "<p class=\"subtitle\">Automated hardware diagnostic test results</p>";
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
    out << "<div class=\"meta-row\"><span class=\"k\">Profile</span><span class=\"v\">"
        << html_escape(result.cameras[0].profile_id) << "</span></div>";
    out << "</div>";

    out << "<div class=\"meta-group\"><div class=\"group-title\">Trigger</div>";
    out << "<div class=\"meta-row\"><span class=\"k\">Mode</span><span class=\"v\">"
        << to_string(result.cameras[0].trigger_mode) << "</span></div>";
    if (!result.cameras[0].trigger_description.empty()) {
      out << "<div class=\"meta-row\"><span class=\"k\">Channel</span><span class=\"v\">"
          << html_escape(result.cameras[0].trigger_description) << "</span></div>";
    }
    if (result.cameras[0].trigger_mode != TriggerMode::FreeRun) {
      std::ostringstream rate_ss, pulse_ss;
      rate_ss << std::fixed << std::setprecision(2) << result.cameras[0].trigger_rate_hz;
      pulse_ss << std::fixed << std::setprecision(2) << result.cameras[0].pulse_width_ms;
      out << "<div class=\"meta-row\"><span class=\"k\">Nominal pulse rate</span><span class=\"v\">" << rate_ss.str()
          << " Hz</span></div>";
      out << "<div class=\"meta-row\"><span class=\"k\">Pulse width</span><span class=\"v\">" << pulse_ss.str()
          << " ms</span></div>";
      out << "<div class=\"meta-note\">\"Nominal pulse rate\" is the profile's configured GPIO trigger "
             "frequency; most tests pace their own capture loop independently and do not follow this "
             "rate (see each test's own sample-interval parameters).</div>";
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

    // Overview table
    out << "<div class=\"section\"><div class=\"section-header\"><h2>Test Results Overview</h2></div>";
    out << "<table "
           "class=\"overview\"><thead><tr><th>Test</th><th>Backend</th><th>Status</th><th>Duration</th><th>Summary</"
           "th></tr></thead><tbody>";
    for (const auto &test : camera.tests) {
      const std::string status = to_string(test.status);
      out << "<tr><td class=\"test-id\">" << html_escape(test.id) << "</td>";
      out << "<td>" << html_escape(test.memory_backend) << "</td>";
      out << "<td class=\"status-cell " << status << "\">" << status << "</td>";
      out << "<td class=\"duration\">" << std::fixed << std::setprecision(0) << test.duration_ms << "ms</td>";
      out << "<td class=\"summary-text\">" << html_escape(test.summary) << "</td></tr>";
    }
    out << "</tbody></table></div>";

    // Detailed per-test sections
    out << "<div class=\"section\"><div class=\"section-header\"><h2>Detailed Results</h2></div><div "
           "style=\"padding:8px 0\">";
    for (const auto &test : camera.tests) {
      const std::string status = to_string(test.status);
      out << "<div class=\"test-section\">";
      out << "<div class=\"test-section-header " << status << "\">";
      out << "<span class=\"badge " << status << "\">" << status << "</span>";
      out << "<h3>" << html_escape(test.id) << " &mdash; " << html_escape(test.name) << "</h3>";
      out << "<span style=\"margin-left:auto;color:#64748b;font-size:12px\">" << std::fixed << std::setprecision(0)
          << test.duration_ms << "ms</span>";
      out << "</div><div class=\"test-body\">";

      if (!test.metrics.empty()) {
        render_test_metrics(out, test);
      }

      // Details
      if (!test.details.empty()) {
        out << "<div class=\"detail-list\">";
        for (const auto &d : test.details)
          out << html_escape(d) << "\n";
        out << "</div>";
      }

      // Warnings
      if (!test.warnings.empty()) {
        out << "<div class=\"warnings-box\">";
        for (const auto &w : test.warnings)
          out << "⚠ " << html_escape(w) << "<br>";
        out << "</div>";
      }

      out << "</div></div>";
    }
    out << "</div></div>";
  }

  out << "<div class=\"footer\">Generated by v4l2-camera-diagnostic</div>";
  out << "</div></body></html>\n";
}

void write_pdf(const RunResult &result, const std::string &path) {
  // Strategy: generate the professional HTML report to a temp file, then convert
  // to PDF using wkhtmltopdf. If wkhtmltopdf is unavailable, fall back to writing
  // the HTML directly with .pdf extension (browsers handle it gracefully).
  const std::string html_tmp = path + ".tmp.html";
  write_html(result, html_tmp);

  // Try wkhtmltopdf first (best quality)
  const std::string cmd =
      "wkhtmltopdf --quiet --enable-local-file-access --page-size A4 --margin-top 10mm "
      "--margin-bottom 10mm --margin-left 10mm --margin-right 10mm "
      "\"" +
      html_tmp + "\" \"" + path + "\" 2>/dev/null";
  const int ret = std::system(cmd.c_str());
  if (ret == 0) {
    std::remove(html_tmp.c_str());
    return;
  }

  // Fallback: try weasyprint
  const std::string cmd2 = "weasyprint \"" + html_tmp + "\" \"" + path + "\" 2>/dev/null";
  const int ret2 = std::system(cmd2.c_str());
  if (ret2 == 0) {
    std::remove(html_tmp.c_str());
    return;
  }

  // Last fallback: rename HTML as PDF (browsers will still render it)
  std::rename(html_tmp.c_str(), path.c_str());
}

}  // namespace

std::vector<ReportArtifact> write_reports(const RunResult &result, const std::vector<ReportFormat> &formats,
                                          const std::string &output_directory) {
  ensure_directory(output_directory);
  std::vector<ReportArtifact> artifacts;
  for (ReportFormat format : formats) {
    const std::string path = output_directory + "/diagnostic-report." + to_string(format);
    switch (format) {
      case ReportFormat::Json:
        write_json(result, path);
        break;
      case ReportFormat::Markdown:
        write_markdown(result, output_directory + "/diagnostic-report.md");
        artifacts.push_back({format, output_directory + "/diagnostic-report.md"});
        continue;
      case ReportFormat::Html:
        write_html(result, path);
        break;
      case ReportFormat::Pdf:
        write_pdf(result, path);
        break;
    }
    artifacts.push_back({format, path});
  }
  return artifacts;
}

}  // namespace v4l2diag
