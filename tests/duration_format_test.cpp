// Duration formatting, C++ side (plan 3.2).
//
// The vectors are NOT written out here: they live in tests/data/duration_format_vectors.txt
// and the frontend test reads the same file. A duration must read identically in the HTML
// report and in the web UI, so two hand-maintained copies of the expectations would be
// exactly the wrong shape -- one could be "fixed" while the other kept the old value and
// both test suites would stay green.

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "v4l2diag/core/duration_format.hpp"

namespace {

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cout << "FAIL: " << message << "\n";
  }
  return condition;
}

struct Vector {
  std::string raw_ms;
  double ms = 0.0;
  std::string expected;
  int line = 0;
};

// The vector file, located relative to this source file so the test runs from any
// directory. V4L2DIAG_TEST_DATA_DIR is defined by the build.
std::string vector_path() {
  return std::string(V4L2DIAG_TEST_DATA_DIR) + "/duration_format_vectors.txt";
}

std::vector<Vector> load_vectors(bool *ok) {
  std::vector<Vector> vectors;
  std::ifstream in(vector_path());
  if (!in) {
    *ok = check(false, "cannot open the shared vector file: " + vector_path());
    return vectors;
  }
  std::string line;
  int number = 0;
  while (std::getline(in, line)) {
    ++number;
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::size_t tab = line.find('\t');
    if (tab == std::string::npos) {
      *ok = check(false, "vector line " + std::to_string(number) + " has no tab separator: " + line);
      continue;
    }
    Vector vector;
    vector.raw_ms = line.substr(0, tab);
    vector.ms = std::strtod(vector.raw_ms.c_str(), nullptr);
    vector.expected = line.substr(tab + 1);
    vector.line = number;
    vectors.push_back(vector);
  }
  return vectors;
}

}  // namespace

int main() {
  bool ok = true;

  // --- 1. Every shared vector ---------------------------------------------
  const std::vector<Vector> vectors = load_vectors(&ok);
  // A silently empty vector file would make this whole test vacuous.
  ok &= check(vectors.size() >= 40, "the shared vector file yielded too few cases: " + std::to_string(vectors.size()));

  for (const Vector &vector : vectors) {
    const std::string actual = v4l2diag::format_duration_ms(vector.ms);
    ok &= check(actual == vector.expected, "vector line " + std::to_string(vector.line) + ": " + vector.raw_ms +
                                               "ms formatted as \"" + actual + "\", expected \"" + vector.expected +
                                               "\"");
  }

  // --- 2. The band boundaries are exactly where the contract puts them ----
  {
    // Just under 10s keeps two decimals; 10s itself moves to one.
    ok &= check(v4l2diag::format_duration_ms(9990.0) == "9.99s", "9990ms: " + v4l2diag::format_duration_ms(9990.0));
    ok &= check(v4l2diag::format_duration_ms(10000.0) == "10s", "10000ms: " + v4l2diag::format_duration_ms(10000.0));
    // Just under 60s keeps a decimal; 60s itself becomes minutes.
    ok &= check(v4l2diag::format_duration_ms(59500.0) == "59.5s", "59500ms: " + v4l2diag::format_duration_ms(59500.0));
    ok &= check(v4l2diag::format_duration_ms(60000.0) == "1m", "60000ms: " + v4l2diag::format_duration_ms(60000.0));
  }

  // --- 3. No trailing zero ever survives ----------------------------------
  {
    // The bug this contract exists to kill: "19.0s", "20.0s", "1m 0s".
    for (double ms = 0.0; ms <= 600000.0; ms += 137.0) {
      const std::string actual = v4l2diag::format_duration_ms(ms);
      const bool has_trailing_zero = actual.find(".0s") != std::string::npos ||
                                     actual.find(".00s") != std::string::npos ||
                                     actual.find(".10s") != std::string::npos;
      ok &= check(!has_trailing_zero, "a trailing zero survived at " + std::to_string(ms) + "ms: " + actual);
      // "1m 0s" says nothing that "1m" does not.
      ok &= check(actual.find(" 0s") == std::string::npos, "a zero seconds part survived: " + actual);
      // A minus sign in a duration reads as a formatter bug.
      ok &= check(actual.find('-') == std::string::npos, "a sign survived: " + actual);
    }
  }

  // --- 4. Non-finite input does not produce garbage -----------------------
  {
    // NaN through a printf-family "%.2f" would render as "nan" or "-nan" in a report.
    const double nan_value = std::strtod("nan", nullptr);
    const double inf_value = std::strtod("inf", nullptr);
    ok &= check(v4l2diag::format_duration_ms(nan_value) == "0s",
                "NaN formatted as \"" + v4l2diag::format_duration_ms(nan_value) + "\"");
    const std::string infinite = v4l2diag::format_duration_ms(inf_value);
    ok &= check(infinite.find("inf") == std::string::npos && infinite.find("nan") == std::string::npos,
                "infinity formatted as \"" + infinite + "\"");
  }

  if (ok) {
    std::cout << "duration_format_test: all checks passed\n";
  }
  return ok ? 0 : 1;
}
