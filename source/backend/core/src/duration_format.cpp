#include "v4l2diag/core/duration_format.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace v4l2diag {

namespace {

// "1.50" -> "1.5", "6.00" -> "6". Applied to a fixed-precision rendering, so there is
// always a '.' to find and the integer part is never touched.
std::string strip_trailing_zeros(std::string text) {
  const std::size_t dot = text.find('.');
  if (dot == std::string::npos) {
    return text;
  }
  std::size_t last = text.find_last_not_of('0');
  if (last == dot) {
    // Every decimal was a zero, so the point goes too.
    last = dot - 1;
  }
  return text.substr(0, last + 1);
}

// Seconds at a fixed precision, zeros stripped, with the unit attached.
//
// The rounding is done here rather than left to "%.*f", which rounds half-to-even
// (0.125 -> "0.12"). The frontend twin cannot reproduce that tie-breaking, and the two
// must agree exactly, so both round half-up explicitly and only then print.
std::string seconds_text(double seconds, int precision) {
  double scale = 1.0;
  for (int i = 0; i < precision; ++i) {
    scale *= 10.0;
  }
  const double rounded = std::floor(seconds * scale + 0.5) / scale;

  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%.*f", precision, rounded);
  return strip_trailing_zeros(std::string(buffer)) + "s";
}

}  // namespace

std::string format_duration_ms(double milliseconds) {
  // NaN compares false against everything, so it lands here rather than falling through
  // to snprintf and rendering as "nan" in a report. A negative duration is not a real
  // measurement either; it is clamped instead of shown with a minus sign.
  if (!(milliseconds > 0.0)) {
    return "0s";
  }
  if (std::isinf(milliseconds)) {
    // No sensible rendering exists, but "inf" in a report looks like corruption.
    return "0s";
  }

  const double seconds = milliseconds / 1000.0;

  if (seconds < 10.0) {
    const std::string text = seconds_text(seconds, 2);
    // Two decimals of a very small duration round to "0s", which reads as "no time at
    // all" for something that was actually measured. Floor it at the smallest value the
    // band can express instead.
    if (text == "0s") {
      return "0.01s";
    }
    // 9.999s rounds to "10s", which belongs to the next band -- and formatting it there
    // gives the same "10s", so the value is simply re-dispatched rather than special-cased.
    if (text == "10s") {
      return seconds_text(10.0, 1);
    }
    return text;
  }

  if (seconds < 60.0) {
    const std::string text = seconds_text(seconds, 1);
    // 59.96s rounds to 60.0s, which is a whole minute: it must read "1m", not "60s".
    if (text != "60s") {
      return text;
    }
  }

  // Whole seconds from here: a minute-scale duration does not need tenths, and rounding
  // first keeps "1m 59.6s" from appearing as "1m 60s".
  const long long total_seconds = static_cast<long long>(std::llround(seconds));
  const long long minutes = total_seconds / 60;
  const long long remainder = total_seconds % 60;
  if (remainder == 0) {
    // "1m 0s" says nothing that "1m" does not.
    return std::to_string(minutes) + "m";
  }
  return std::to_string(minutes) + "m " + std::to_string(remainder) + "s";
}

}  // namespace v4l2diag
