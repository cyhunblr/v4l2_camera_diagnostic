// A renderer may not stand in a concrete-looking value for one it does not have.
//
// This is a SOURCE-level check on test_content.cpp. The pattern it forbids looked like
//   value_of(test, "x").empty() ? "82ms" : value_of(test, "x")
//   detail_value(test, "warmup").empty() ? "5 frames" : ...
//   row({"0-10s", "52", "83ms", "21ms", "0"})            // five hard-coded window rows
// and it is worse than an empty cell: a reader cannot tell the stand-in from a
// measurement, so the report describes a run that never happened. Measured on the
// 2026-08-08 device report: T23 printed five window rows copied from the approved
// preview, T26 a "3 / 3" cycle row, T22 "0" and "50 / 50".
//
// Two further facts this locks in, both measured:
//   * value_of() and value_of_any() never return "" -- a missing metric comes back as
//     "Unavailable" -- so `value_of(...).empty() ? fallback : value_of(...)` is dead code
//     whose fallback can never run. Writing one is always a mistake.
//   * detail_value() DOES return "" for an absent key, so an .empty() test on it is
//     legitimate; only the substituted text is constrained.

#include <algorithm>
#include <cctype>
#include <iterator>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

std::string read_file(const std::string &path) {
  std::ifstream input(path);
  if (!input) {
    std::cout << "FAIL: cannot read " << path << "\n";
    ++failures;
    return std::string();
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// Text that reads as a measurement: a number, a duration, a ratio, a byte size. The em
// dash, "Unavailable" and prose ("Not triggered") state an absence instead and are fine.
bool looks_measured(const std::string &text) {
  if (text.empty()) {
    return false;
  }
  const bool has_digit =
      std::any_of(text.begin(), text.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
  if (!has_digit) {
    return false;
  }
  // A unit word or a bare number both qualify; "4 KiB", "82ms", "3 / 3", "52".
  return true;
}

}  // namespace

int main() {
  const std::string src = read_file("source/backend/core/src/test_content.cpp");
  if (src.empty()) {
    std::cout << "run this from the repository root\n";
    return 1;
  }

  // 1. No dead .empty() test on a helper that never returns an empty string.
  {
    const std::regex dead(R"RX((value_of|value_of_any)\((?:[^;]{0,120}?)\)\s*\.empty\(\))RX");
    std::vector<std::string> hits;
    for (auto it = std::sregex_iterator(src.begin(), src.end(), dead); it != std::sregex_iterator(); ++it) {
      hits.push_back(it->str(0));
    }
    if (!hits.empty()) {
      std::cout << "FAIL: " << hits.size() << " .empty() test(s) on value_of/value_of_any, which return "
                << "\"Unavailable\" and never \"\" -- the fallback is unreachable:\n";
      for (const std::string &hit : hits) {
        std::cout << "    " << hit << "\n";
      }
      ++failures;
    }
  }

  // 2. No measurement-looking stand-in behind an .empty() test.
  {
    const std::regex stand_in(R"RX(\.empty\(\)\s*\?\s*"([^"]*)")RX");
    std::vector<std::string> hits;
    for (auto it = std::sregex_iterator(src.begin(), src.end(), stand_in); it != std::sregex_iterator(); ++it) {
      const std::string text = (*it)[1].str();
      // "\xE2\x80\x94" (em dash) arrives here as an escape sequence, not a digit.
      if (text.find("\\x") != std::string::npos) {
        continue;
      }
      // A number chosen for SVG GEOMETRY is not a stand-in for a measurement: it is the
      // drawing, not the datum. The literal that provoked this is a circle radius --
      // `(point.flag.empty() ? "4" : "5")` picks a marker 1px larger for a flagged point.
      // The exemption is deliberately narrow: the match must be immediately followed by
      // an SVG attribute close, so a fabricated table value can never take this path.
      const std::size_t after = static_cast<std::size_t>(it->position(0)) + it->length(0);
      const std::string tail = src.substr(after, 40);
      if (tail.find("/>") != std::string::npos || tail.find("\\\" ") != std::string::npos) {
        continue;
      }
      if (looks_measured(text)) {
        hits.push_back(text);
      }
    }
    if (!hits.empty()) {
      std::cout << "FAIL: " << hits.size() << " fabricated stand-in value(s) a reader cannot tell from a "
                << "measurement:\n";
      for (const std::string &hit : hits) {
        std::cout << "    \"" << hit << "\"\n";
      }
      ++failures;
    }
  }

  // 3. No table row built entirely from literals. A row of quoted strings with numbers in
  //    them is a hard-coded measurement; real rows read metrics or parsed detail fields.
  {
    const std::regex literal_row(
        R"RX(row\(\{"[^"]*",\s*"([^"]*[0-9][^"]*)",\s*"([^"]*[0-9][^"]*)",\s*"([^"]*[0-9][^"]*)")RX");
    std::vector<std::string> hits;
    for (auto it = std::sregex_iterator(src.begin(), src.end(), literal_row); it != std::sregex_iterator(); ++it) {
      // "\xE2\x80\x94" is the em dash escape; its digits are not a measurement.
      const bool all_escapes = (*it)[1].str().find("\\x") != std::string::npos &&
                               (*it)[2].str().find("\\x") != std::string::npos &&
                               (*it)[3].str().find("\\x") != std::string::npos;
      if (!all_escapes) {
        hits.push_back(it->str(0));
      }
    }
    if (!hits.empty()) {
      std::cout << "FAIL: " << hits.size() << " table row(s) built from hard-coded numbers:\n";
      for (const std::string &hit : hits) {
        std::cout << "    " << hit.substr(0, 100) << "\n";
      }
      ++failures;
    }
  }

  // Guard the guard: if the file stopped being read, or the patterns stopped matching the
  // shape of this source, the three checks above pass vacuously.
  {
    const std::regex any_row(R"RX(row\(\{)RX");
    const std::size_t rows = static_cast<std::size_t>(
        std::distance(std::sregex_iterator(src.begin(), src.end(), any_row), std::sregex_iterator()));
    // 89 row() call sites at the time of writing; the floor only has to prove the file
    // was read and parsed, not track the exact count.
    if (rows < 60) {
      std::cout << "FAIL: only " << rows << " row() call sites seen; the scan is not reading test_content.cpp\n";
      ++failures;
    }
    const std::regex any_empty(R"RX(\.empty\(\)\s*\?)RX");
    const std::size_t empties = static_cast<std::size_t>(
        std::distance(std::sregex_iterator(src.begin(), src.end(), any_empty), std::sregex_iterator()));
    if (empties < 5) {
      std::cout << "FAIL: only " << empties << " .empty() ternaries seen; check 2 cannot be exercising anything\n";
      ++failures;
    }
  }

  if (failures == 0) {
    std::cout << "no fabricated values: no dead .empty() chains, no measurement-looking stand-ins, "
              << "no hard-coded rows\n";
    return 0;
  }
  return 1;
}
