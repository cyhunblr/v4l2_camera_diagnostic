#pragma once

#include <string>

namespace v4l2diag {

// Human-readable duration (plan 1.1 / 3.2).
//
//   < 10s      at most two decimals   "1.23s", "6s"
//   10s - <60s at most one decimal    "12.4s", "19s"
//   >= 60s     whole seconds          "1m 27s", "2m"
//   zero       "0s"
//
// Trailing zeros are always stripped: "19.0s" and "1m 0s" never appear.
//
// The frontend has a language-local twin (source/frontend/src/formatDuration.ts) and both
// are tested against ONE shared vector file (tests/data/duration_format_vectors.txt), so
// the same run reads the same in the HTML report and in the web UI.
//
// This is a DISPLAY function. The raw `duration_ms` is what gets stored and served; it is
// never replaced by this string.
std::string format_duration_ms(double milliseconds);

}  // namespace v4l2diag
