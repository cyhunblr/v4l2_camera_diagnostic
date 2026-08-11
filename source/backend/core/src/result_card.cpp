#include "v4l2diag/core/result_card.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#include "v4l2diag/core/duration_format.hpp"

namespace v4l2diag {

namespace {

// A character usable in an HTML id. Anything else becomes '-', so a slug with a space or
// a quote cannot produce an anchor no link can reach.
bool anchor_char(char c) {
  const unsigned char u = static_cast<unsigned char>(c);
  return (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || c == '-' || c == '_';
}

std::string anchor_part(const std::string &raw) {
  std::string out;
  out.reserve(raw.size());
  for (char c : raw) {
    out.push_back(anchor_char(c) ? c : '-');
  }
  return out;
}

}  // namespace

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

bool is_sentinel_metric(const MetricValue &metric) {
  if (std::fabs(metric.value + 500.0) < 0.000001) {
    return true;
  }
  if (std::fabs(metric.value + 1.0) >= 0.000001) {
    return false;
  }
  // -1 is only a sentinel for the metrics that actually use it that way; elsewhere it can
  // be a real value.
  std::string description;
  for (char c : metric.description) {
    description.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return metric.name.find("cliff") != std::string::npos || metric.name.find("safety_margin") != std::string::npos ||
         metric.name.find("min_reliable") != std::string::npos || description.find("n/a") != std::string::npos ||
         description.find("none") != std::string::npos || description.find("no cliff") != std::string::npos ||
         description.find("could not") != std::string::npos;
}

std::string humanize(const std::string &value) {
  std::string out = value;
  std::replace(out.begin(), out.end(), '_', ' ');
  if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') {
    out[0] = static_cast<char>(out[0] - 'a' + 'A');
  }
  return out;
}

std::string control_combo_label(const std::string &name) {
  int led = 0;
  int bypass = 0;
  int window = 0;
  if (std::sscanf(name.c_str(), "ll%d_bp%d_wi%d", &led, &bypass, &window) != 3) {
    return humanize(name);
  }
  std::ostringstream out;
  out << "LED " << led << " \xc2\xb7 BYP " << bypass << " \xc2\xb7 WIN " << window;
  return out.str();
}

std::string upper_case(const std::string &value) {
  std::string out;
  out.reserve(value.size());
  for (char c : value) {
    out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
  }
  return out;
}

std::string test_display_name(const std::string &test_id, const std::string &name) {
  // The leading "tNN" of the slug becomes "TNN". A slug that does not start that way has no
  // number to show, so the readable name stands alone rather than getting an invented one.
  int number = 0;
  if (std::sscanf(test_id.c_str(), "t%d", &number) != 1 || number <= 0) {
    return name;
  }
  char prefix[16];
  std::snprintf(prefix, sizeof(prefix), "T%02d", number);
  // A name that already carries the number keeps it once. Runners and fixtures differ on
  // whether they store "Device Compliance" or "T01 - Device Compliance", and prefixing
  // unconditionally produced "T01 - T01 - Device Compliance".
  if (name.compare(0, std::strlen(prefix), prefix) == 0) {
    return name;
  }
  return std::string(prefix) + " - " + name;
}

std::string card_status_text(TestStatus status) {
  switch (status) {
    case TestStatus::Pass:
      return "PASS";
    case TestStatus::Warn:
      return "WARN";
    case TestStatus::Fail:
      return "FAIL";
    case TestStatus::Skipped:
      // "SKIP" on the card; the wire value stays "skipped".
      return "SKIP";
  }
  return "SKIP";
}

std::string card_status_class(TestStatus status) {
  switch (status) {
    case TestStatus::Pass:
      return "pass";
    case TestStatus::Warn:
      return "warn";
    case TestStatus::Fail:
      return "fail";
    case TestStatus::Skipped:
      return "skip";
  }
  return "skip";
}

std::string result_card_anchor(const std::string &memory_backend, const std::string &test_id) {
  return "result-" + anchor_part(memory_backend) + "-" + anchor_part(test_id);
}

std::string render_backend_band(const std::string &memory_backend, bool first) {
  std::ostringstream out;
  if (!first) {
    // Separates this backend's cards from the previous backend's. The first band opens
    // the section and needs no gap above it.
    out << "<div class=\"backend-gap\"></div>";
  }
  // Upper-case the wire spelling for display: "DMABUF", not "dmabuf" (plan 3.6). The
  // anchor and the JSON keep the lower-case form -- this is a label, not an identifier.
  out << "<div class=\"backend\"><span class=\"backend-label\">Backend</span> <strong>"
      << html_escape(upper_case(memory_backend)) << "</strong>";
  // The DMABUF band names how the buffers were obtained, verbatim from the approved preview:
  // `<span class="backend-note">MMAP buffers exported with VIDIOC_EXPBUF</span>`. This backend
  // is not a native V4L2_MEMORY_DMABUF import, and saying so once per band is what the design
  // does -- an earlier note here claimed the explanation belonged inside T12's card instead, but
  // the approved t12 card carries no such paragraph and its band carries this note. The
  // `backend-note` rule existed in the stylesheet with nothing emitting it.
  if (upper_case(memory_backend) == "DMABUF") {
    out << "<span class=\"backend-note\">MMAP buffers exported with VIDIOC_EXPBUF</span>";
  }
  out << "</div>";
  return out.str();
}

std::string render_result_card_open(const TestResult &test) {
  const std::string status_class = card_status_class(test.status);
  std::ostringstream out;
  out << "<article class=\"test-card " << status_class << "\" id=\""
      << html_escape(result_card_anchor(test.memory_backend, test.id)) << "\">";
  // STATUS | TEST ID + TEST NAME | DURATION. No pill and no dot beside the status: the
  // left border already carries the colour.
  out << "<header class=\"test-header\">";
  out << "<div class=\"status\">" << card_status_text(test.status) << "</div>";
  // review-plan 4.3: the number and the readable name; the technical slug is not repeated
  // here. It stays in the anchor above, where it identifies rather than labels.
  out << "<h2>" << html_escape(test_display_name(test.id, test.name)) << "</h2>";
  out << "<div class=\"duration\">" << html_escape(format_duration_ms(test.duration_ms)) << "</div>";
  out << "</header>";
  // review-plan 6.6 asks for a continuation header on a long card's follow-on page. NOT
  // emitted here: a static element cannot appear "on whatever page the break lands on".
  // Rendering it unconditionally put "| continued" on every card including the ones that
  // never split -- which is worse than omitting it, because it tells the reader a page was
  // cut when it was not. The reliable primitives for this (running headers, @page margin
  // boxes) are ruled out by 6.9. Open question, recorded in fix-plan 3.7.5.
  return out.str();
}

std::string render_result_card_close() {
  return "</article>";
}

bool result_section_visible(TestStatus status) {
  return status != TestStatus::Pass;
}

}  // namespace v4l2diag
