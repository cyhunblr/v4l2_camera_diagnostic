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

struct MetricChartGroup {
  std::string title;
  std::string unit;
  std::string label_prefix;
  std::vector<std::size_t> indices;
  bool horizontal = false;
};

void append_metric_group(std::vector<MetricChartGroup> &groups, std::vector<bool> &used,
                         const std::vector<MetricValue> &metrics, const std::vector<std::size_t> &indices,
                         const std::string &title, const std::string &label_prefix, bool force_horizontal) {
  if (indices.size() < 2) {
    return;
  }
  MetricChartGroup group;
  group.title = title;
  group.unit = metrics[indices.front()].unit;
  group.label_prefix = label_prefix;
  group.indices = indices;
  group.horizontal = force_horizontal || indices.size() > 6;
  if (!group.horizontal) {
    for (std::size_t index : indices) {
      if (statistic_label(metrics[index].name, label_prefix).size() > 18) {
        group.horizontal = true;
        break;
      }
    }
  }
  for (std::size_t index : indices)
    used[index] = true;
  groups.push_back(group);
}

std::vector<MetricChartGroup> group_metrics(const std::vector<MetricValue> &metrics, std::vector<bool> &used) {
  std::vector<MetricChartGroup> groups;
  if (used.size() != metrics.size()) {
    used.assign(metrics.size(), false);
  }

  const struct {
    const char *prefix;
    const char *title;
  } sweep_patterns[] = {
      {"hits_", "Pulse Width Hits"},
      {"lat_high_avg_", "HIGH Edge Latency"},
      {"lat_low_avg_", "LOW Edge Latency"},
      {"ll", "Control Sweep Latency"},
  };

  for (const auto &pattern : sweep_patterns) {
    std::map<std::string, std::vector<std::size_t>> by_unit;
    for (std::size_t i = 0; i < metrics.size(); ++i) {
      const auto &metric = metrics[i];
      bool matches = starts_with(metric.name, pattern.prefix);
      if (std::string(pattern.prefix) == "ll") {
        matches = matches && metric.name.find("_bp") != std::string::npos &&
                  metric.name.find("_wi") != std::string::npos && ends_with(metric.name, "_mean_ms");
      }
      if (!used[i] && matches && is_chartable_metric(metric)) {
        by_unit[metric.unit].push_back(i);
      }
    }
    for (const auto &entry : by_unit) {
      append_metric_group(groups, used, metrics, entry.second, pattern.title, "", true);
    }
  }

  // A repeated latency/throughput prefix identifies format or resolution sweeps
  // without relying on the test ID.
  std::vector<std::string> throughput_prefixes;
  for (const auto &metric : metrics) {
    if (ends_with(metric.name, "_throughput_mbps")) {
      throughput_prefixes.push_back(sweep_prefix(metric.name));
    }
  }
  std::sort(throughput_prefixes.begin(), throughput_prefixes.end());
  throughput_prefixes.erase(std::unique(throughput_prefixes.begin(), throughput_prefixes.end()),
                            throughput_prefixes.end());
  if (throughput_prefixes.size() >= 2) {
    std::map<std::string, std::vector<std::size_t>> by_unit;
    for (std::size_t i = 0; i < metrics.size(); ++i) {
      const std::string prefix = sweep_prefix(metrics[i].name);
      if (!used[i] && !prefix.empty() && is_chartable_metric(metrics[i]) &&
          std::find(throughput_prefixes.begin(), throughput_prefixes.end(), prefix) != throughput_prefixes.end()) {
        by_unit[metrics[i].unit].push_back(i);
      }
    }
    for (const auto &entry : by_unit) {
      const std::string title = entry.first == "MB/s" ? "Throughput Sweep" : "Latency Sweep";
      append_metric_group(groups, used, metrics, entry.second, title, "", true);
    }
  }

  struct PendingGroup {
    std::string key;
    std::string base;
    std::string unit;
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
      pending.push_back({key, base, metrics[i].unit, {i}});
    } else {
      it->indices.push_back(i);
    }
  }
  for (const auto &entry : pending) {
    append_metric_group(groups, used, metrics, entry.indices, humanize_metric_name(entry.base), entry.base, false);
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

void render_statistic_dot_chart(std::ostream &out, const std::vector<MetricValue> &metrics,
                                const MetricChartGroup &group) {
  constexpr double width = 760.0;
  const double height = std::max(190.0, 82.0 + 48.0 * group.indices.size());
  constexpr double left = 118.0;
  constexpr double right = 58.0;
  constexpr double top = 38.0;
  constexpr double bottom = 48.0;
  constexpr double point_inset = 16.0;
  const double plot_width = width - left - right - (point_inset * 2.0);
  const double plot_height = height - top - bottom;

  double min_value = metrics[group.indices.front()].value;
  double max_value = metrics[group.indices.front()].value;
  for (std::size_t index : group.indices) {
    min_value = std::min(min_value, metrics[index].value);
    max_value = std::max(max_value, metrics[index].value);
  }
  const bool has_negative = min_value < 0.0;
  if (has_negative) {
    max_value = std::max(max_value, 0.0);
    min_value = std::min(min_value, 0.0);
  }
  const double raw_span = max_value - min_value;
  const double magnitude = std::max(std::fabs(max_value), std::fabs(min_value));
  const double padding = std::max(raw_span * 0.16, std::max(magnitude * 0.03, 0.001));
  min_value -= padding;
  max_value += padding;
  if (!has_negative && min_value < 0.0) {
    min_value = 0.0;
  }
  if (std::fabs(max_value - min_value) < 0.000001) {
    const double equal_padding = std::max(std::fabs(max_value) * 0.08, 1.0);
    min_value -= equal_padding;
    max_value += equal_padding;
    if (!has_negative && min_value < 0.0) {
      min_value = 0.0;
    }
  }
  const auto x_for = [&](double value) {
    return left + point_inset + (value - min_value) * plot_width / (max_value - min_value);
  };
  const double guide_start_x = has_negative ? x_for(0.0) : left;

  out << "<div class=\"metric-chart metric-dot-chart\"><div class=\"metric-chart-title\">" << html_escape(group.title);
  if (!group.unit.empty())
    out << " <span>" << html_escape(group.unit) << "</span>";
  out << "</div><svg viewBox=\"0 0 760 " << height << "\" role=\"img\" aria-label=\"" << html_escape(group.title)
      << " statistic dot chart\">";

  for (int tick = 0; tick <= 4; ++tick) {
    const double value = min_value + (max_value - min_value) * tick / 4.0;
    const double x = x_for(value);
    out << "<line class=\"chart-grid\" x1=\"" << x << "\" y1=\"" << top << "\" x2=\"" << x << "\" y2=\""
        << (height - bottom) << "\"></line>";
    out << "<text class=\"axis-value\" x=\"" << x << "\" y=\"" << (height - 17.0) << "\" text-anchor=\"middle\">"
        << html_escape(format_metric_value(value)) << "</text>";
  }
  if (has_negative) {
    const double zero_x = x_for(0.0);
    out << "<line class=\"zero-baseline\" x1=\"" << zero_x << "\" y1=\"" << top << "\" x2=\"" << zero_x << "\" y2=\""
        << (height - bottom) << "\"></line>";
  }
  out << "<line class=\"chart-axis\" x1=\"" << left << "\" y1=\"" << (height - bottom) << "\" x2=\"" << (width - right)
      << "\" y2=\"" << (height - bottom) << "\"></line>";

  for (std::size_t i = 0; i < group.indices.size(); ++i) {
    const std::size_t index = group.indices[i];
    const auto &metric = metrics[index];
    const double x = x_for(metric.value);
    const double y = top + (i + 0.5) * plot_height / group.indices.size();
    const std::string label = statistic_label(metric.name, group.label_prefix);
    const bool label_to_left = x > width - right - 72.0;
    const double value_x = label_to_left ? x - 12.0 : x + 12.0;
    const char *text_anchor = label_to_left ? "end" : "start";
    out << "<line class=\"dot-row-line\" x1=\"" << left << "\" y1=\"" << y << "\" x2=\"" << (width - right)
        << "\" y2=\"" << y << "\"></line>";
    out << "<line class=\"guide-line\" x1=\"" << guide_start_x << "\" y1=\"" << y << "\" x2=\"" << x << "\" y2=\"" << y
        << "\"></line>";
    out << "<text class=\"dot-label\" x=\"" << (left - 14.0) << "\" y=\"" << (y + 4.0) << "\" text-anchor=\"end\">"
        << html_escape(label) << "</text>";
    out << "<circle class=\"metric-point\" data-metric=\"" << html_escape(metric.name) << "\" cx=\"" << x << "\" cy=\""
        << y << "\" r=\"7\"></circle>";
    out << "<text class=\"point-value\" x=\"" << value_x << "\" y=\"" << (y + 4.0) << "\" text-anchor=\"" << text_anchor
        << "\">" << html_escape(format_metric_value(metric.value)) << "</text>";
  }
  out << "</svg></div>";
}

void render_horizontal_chart(std::ostream &out, const std::vector<MetricValue> &metrics,
                             const MetricChartGroup &group) {
  double max_magnitude = 0.0;
  for (std::size_t index : group.indices)
    max_magnitude = std::max(max_magnitude, std::fabs(metrics[index].value));
  if (max_magnitude < 0.000001)
    max_magnitude = 1.0;

  out << "<div class=\"metric-chart metric-bars\"><div class=\"metric-chart-title\">" << html_escape(group.title);
  if (!group.unit.empty())
    out << " <span>" << html_escape(group.unit) << "</span>";
  out << "</div><div class=\"bar-list\">";
  for (std::size_t item = 0; item < group.indices.size(); ++item) {
    const std::size_t index = group.indices[item];
    const auto &metric = metrics[index];
    const double bar_width = 100.0 * std::fabs(metric.value) / max_magnitude;
    out << "<div class=\"bar-row\" data-metric=\"" << html_escape(metric.name) << "\"><div class=\"bar-label\">"
        << html_escape(humanize_metric_name(metric.name)) << "</div><div class=\"bar-track\"><span class=\"bar-fill"
        << (metric.value < 0.0 ? " negative" : "") << " tone-" << (item % 4) << "\" style=\"width:" << std::fixed
        << std::setprecision(3) << bar_width << "%\"><i></i></span></div><div class=\"bar-value\">"
        << html_escape(format_metric_value(metric.value));
    if (!metric.unit.empty())
      out << " " << html_escape(metric.unit);
    out << "</div></div>";
  }
  out << "</div></div>";
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
  constexpr double width = 760.0;
  constexpr double height = 300.0;
  constexpr double left = 64.0;
  constexpr double right = 28.0;
  constexpr double top = 34.0;
  constexpr double bottom = 92.0;
  constexpr double point_inset = 28.0;
  const double plot_width = width - left - right - (point_inset * 2.0);
  const double plot_height = height - top - bottom;
  const auto x_for = [&](std::size_t index) { return left + point_inset + plot_width * index / (points.size() - 1); };
  const auto y_for = [&](double ratio) { return top + 10.0 + (100.0 - ratio) * (plot_height - 20.0) / 100.0; };
  const double baseline_y = top + plot_height;

  out << "<div class=\"metric-chart metric-xy-chart t13-distribution-chart\"><div class=\"metric-chart-title\">Timeout "
         "Hit Distribution <span>%</span></div><svg viewBox=\"0 0 760 300\" role=\"img\" aria-label=\"t13 timeout hit "
         "distribution\">";
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
    out << "<text class=\"axis-value\" x=\"" << (left - 9.0) << "\" y=\"" << (y + 4.0) << "\" text-anchor=\"end\">"
        << html_escape(format_metric_value(value)) << "</text>";
  }
  out << "<line class=\"chart-axis\" x1=\"" << left << "\" y1=\"" << baseline_y << "\" x2=\"" << (width - right)
      << "\" y2=\"" << baseline_y << "\"></line>";

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
    const double value_y = y < top + 24.0 ? y + 24.0 : y - 11.0;
    out << "<circle class=\"metric-point t13-probe-point\" data-timeout-ms=\"" << point.timeout_ms << "\" cx=\"" << x
        << "\" cy=\"" << y << "\" r=\"6\"></circle>";
    out << "<text class=\"point-value\" x=\"" << x << "\" y=\"" << value_y << "\" text-anchor=\"middle\">" << point.hits
        << "/" << point.total << "</text>";
    out << "<text class=\"axis-label\" transform=\"translate(" << (x + 2.0) << "," << (baseline_y + 25.0)
        << ") rotate(-45)\" text-anchor=\"end\">" << point.timeout_ms << "ms</text>";
  }
  out << "</svg></div>";
}

bool render_t13_threshold_chart(std::ostream &out, const std::vector<MetricValue> &metrics, std::vector<bool> &used) {
  const int cliff_index = metric_index_by_name(metrics, "cliff_ms");
  const int total_index = metric_index_by_name(metrics, "cliff_total_ms");
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
  if (total_index >= 0 && is_chartable_metric(metrics[total_index])) {
    markers.push_back({"Total cliff", metrics[total_index].name, metrics[total_index].value});
    used[total_index] = true;
  }
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
  const double padding = std::max((max_value - min_value) * 0.12, 1.0);
  min_value = std::max(0.0, min_value - padding);
  max_value += padding;
  const double span = std::max(1.0, max_value - min_value);

  constexpr double width = 760.0;
  constexpr double left = 68.0;
  constexpr double right = 34.0;
  constexpr double band_y = 42.0;
  constexpr double band_height = 28.0;
  constexpr double marker_y = band_y + band_height / 2.0;
  const auto x_for = [&](double value) { return left + (width - left - right) * (value - min_value) / span; };
  const double cliff_x = x_for(metrics[cliff_index].value);
  double production_x = width - right;
  const auto production = std::find_if(markers.begin(), markers.end(), [](const ThresholdMarker &marker) {
    return marker.metric_name == "production_timeout_ms";
  });
  if (production != markers.end()) {
    production_x = x_for(production->value);
  }

  out << "<div class=\"metric-chart t13-threshold-chart\"><div class=\"metric-chart-title\">Timeout Thresholds "
         "<span>ms</span></div><svg viewBox=\"0 0 760 108\" role=\"img\" aria-label=\"t13 timeout thresholds\">";
  out << "<rect class=\"threshold-zone threshold-risk\" x=\"" << left << "\" y=\"" << band_y << "\" width=\""
      << std::max(0.0, cliff_x - left) << "\" height=\"" << band_height << "\"></rect>";
  out << "<rect class=\"threshold-zone threshold-margin\" x=\"" << cliff_x << "\" y=\"" << band_y << "\" width=\""
      << std::max(0.0, production_x - cliff_x) << "\" height=\"" << band_height << "\"></rect>";
  out << "<rect class=\"threshold-zone threshold-safe\" x=\"" << production_x << "\" y=\"" << band_y << "\" width=\""
      << std::max(0.0, width - right - production_x) << "\" height=\"" << band_height << "\"></rect>";
  out << "<text class=\"axis-value\" x=\"" << left << "\" y=\"94\" text-anchor=\"start\">"
      << html_escape(format_metric_value(min_value)) << "ms</text>";
  out << "<text class=\"axis-value\" x=\"" << (width - right) << "\" y=\"94\" text-anchor=\"end\">"
      << html_escape(format_metric_value(max_value)) << "ms</text>";
  for (std::size_t i = 0; i < markers.size(); ++i) {
    const auto &marker = markers[i];
    const double x = x_for(marker.value);
    out << "<line class=\"threshold-marker marker-" << (i % 4) << "\" data-metric=\"" << html_escape(marker.metric_name)
        << "\" x1=\"" << x << "\" y1=\"28\" x2=\"" << x << "\" y2=\"82\"></line>";
    out << "<circle class=\"threshold-point marker-" << (i % 4) << "\" cx=\"" << x << "\" cy=\"" << marker_y
        << "\" r=\"6\"></circle>";
  }
  out << "</svg><div class=\"threshold-legend\">";
  for (std::size_t i = 0; i < markers.size(); ++i) {
    const auto &marker = markers[i];
    out << "<span class=\"threshold-legend-item marker-" << (i % 4) << "\"><i></i><span>" << html_escape(marker.label)
        << "</span><strong>" << html_escape(format_metric_value(marker.value)) << "ms</strong></span>";
  }
  out << "</div></div>";
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

void render_test_metrics(std::ostream &out, const TestResult &test) {
  const auto &metrics = test.metrics;
  std::vector<bool> used;
  used.assign(metrics.size(), false);
  bool has_visuals = false;
  if (test.id == "t13-poll-timeout-cliff") {
    has_visuals = render_t13_visuals(out, test, used);
  }
  const auto groups = group_metrics(metrics, used);
  if (!groups.empty())
    out << "<div class=\"metric-visuals\">";
  for (const auto &group : groups) {
    if (group.horizontal)
      render_horizontal_chart(out, metrics, group);
    else
      render_statistic_dot_chart(out, metrics, group);
  }
  if (!groups.empty())
    out << "</div>";
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
.metric-visuals { display: grid; grid-template-columns: repeat(auto-fit, minmax(min(100%, 360px), 1fr)); gap: 12px; margin-bottom: 12px; }
.metric-chart { border: 1px solid #dbe4ee; border-radius: 6px; background: #fff; padding: 14px; overflow: hidden; box-shadow: 0 1px 2px rgba(15, 23, 42, 0.04); }
.metric-chart-title { color: #1e293b; font-size: 13px; font-weight: 750; margin-bottom: 6px; }
.metric-chart-title span { color: #94a3b8; font-size: 10px; font-weight: 600; margin-left: 4px; }
.metric-chart svg { display: block; width: 100%; max-width: 100%; height: auto; overflow: hidden; }
.metric-xy-chart svg { min-width: 0; }
.chart-grid { stroke: #e2e8f0; stroke-width: 1; vector-effect: non-scaling-stroke; }
.chart-axis { stroke: #94a3b8; stroke-width: 1.5; vector-effect: non-scaling-stroke; }
.guide-line { stroke: #94a3b8; stroke-width: 1; stroke-dasharray: 4 5; vector-effect: non-scaling-stroke; }
.dot-row-line { stroke: #f1f5f9; stroke-width: 10; stroke-linecap: round; vector-effect: non-scaling-stroke; }
.zero-baseline { stroke: #475569; stroke-width: 1.5; stroke-dasharray: 3 4; vector-effect: non-scaling-stroke; }
.metric-line { fill: none; stroke: #2563eb; stroke-width: 2.5; stroke-linejoin: round; stroke-linecap: round; vector-effect: non-scaling-stroke; }
.metric-point { fill: #0f8aa6; stroke: #fff; stroke-width: 2.5; vector-effect: non-scaling-stroke; }
.axis-value, .axis-label, .point-value, .dot-label { font-family: 'JetBrains Mono', monospace; fill: #64748b; font-size: 13px; }
.dot-label { fill: #475569; font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; font-weight: 700; }
.point-value { fill: #1e293b; font-weight: 700; }
.hit-zone { opacity: 0.48; }
.hit-zone-high { fill: #dcfce7; }
.hit-zone-mid { fill: #fef3c7; }
.hit-zone-low { fill: #fee2e2; }
.distribution-area { fill: #bae6fd; opacity: 0.58; }
.distribution-line { stroke: #087f9b; stroke-width: 3; }
.t13-probe-point { fill: #087f9b; }
.threshold-zone { rx: 5; ry: 5; }
.threshold-risk { fill: #fecaca; }
.threshold-margin { fill: #fde68a; }
.threshold-safe { fill: #bbf7d0; }
.threshold-marker { stroke-width: 3; stroke-dasharray: 3 3; vector-effect: non-scaling-stroke; }
.threshold-marker.marker-0, .threshold-point.marker-0 { stroke: #dc2626; fill: #dc2626; }
.threshold-marker.marker-1, .threshold-point.marker-1 { stroke: #d97706; fill: #d97706; }
.threshold-marker.marker-2, .threshold-point.marker-2 { stroke: #0f8aa6; fill: #0f8aa6; }
.threshold-marker.marker-3, .threshold-point.marker-3 { stroke: #15803d; fill: #15803d; }
.threshold-point { stroke: #fff !important; stroke-width: 2.5; vector-effect: non-scaling-stroke; }
.threshold-legend { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 7px 12px; margin-top: 2px; }
.threshold-legend-item { display: grid; grid-template-columns: 8px minmax(0, 1fr) auto; align-items: center; gap: 6px; min-width: 0; color: #64748b; font-size: 10px; }
.threshold-legend-item i { width: 7px; height: 7px; border-radius: 50%; background: currentColor; }
.threshold-legend-item strong { color: #1e293b; font-family: monospace; font-size: 11px; white-space: nowrap; }
.threshold-legend-item.marker-0 { color: #dc2626; }
.threshold-legend-item.marker-1 { color: #d97706; }
.threshold-legend-item.marker-2 { color: #0f8aa6; }
.threshold-legend-item.marker-3 { color: #15803d; }
.bar-list { display: grid; gap: 9px; }
.bar-row { display: grid; grid-template-columns: minmax(0, 1.2fr) minmax(80px, 2fr) minmax(54px, auto); align-items: center; gap: 10px; }
.bar-label { color: #475569; font-size: 11px; overflow-wrap: anywhere; }
.bar-track { height: 5px; border-radius: 3px; background: #e2e8f0; }
.bar-fill { position: relative; display: block; height: 100%; min-width: 2px; border-radius: 3px; background: #2563eb; }
.bar-fill i { position: absolute; right: -4px; top: 50%; width: 9px; height: 9px; border: 2px solid #fff; border-radius: 50%; background: inherit; transform: translateY(-50%); box-shadow: 0 0 0 1px currentColor; }
.bar-fill.tone-0 { background: #2563eb; color: #2563eb; }
.bar-fill.tone-1 { background: #0f8aa6; color: #0f8aa6; }
.bar-fill.tone-2 { background: #d97706; color: #d97706; }
.bar-fill.tone-3 { background: #e2553d; color: #e2553d; }
.bar-fill.negative { background: #dc2626; color: #dc2626; }
.bar-value { color: #1e293b; font-family: monospace; font-size: 11px; font-weight: 700; text-align: right; white-space: nowrap; }
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
  .metric-xy-chart .axis-value, .metric-xy-chart .axis-label, .metric-dot-chart .axis-value,
  .metric-dot-chart .dot-label, .t13-threshold-chart .axis-value { font-size: 23px; }
  .metric-xy-chart .point-value, .metric-dot-chart .point-value { font-size: 24px; }
  .metric-point { stroke-width: 3; }
  .bar-row { grid-template-columns: minmax(100px, 1fr) minmax(90px, 1.4fr); }
  .bar-value { grid-column: 2; }
  .export-actions { position: static; flex-direction: row; padding: 12px; background: #f8fafc; }
}
@media print { body { background: white; } .container { padding: 20px; } .header { break-inside: avoid; }
               .test-section, .metric-chart, .result-distribution { break-inside: avoid; }
               .export-actions { display: none; }
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
