// Each rendered card must have the STRUCTURE its approved preview shows: the same item
// labels, in the same order, and the same table column signatures.
//
// Every other test in this directory states the contract in C++ and trusts that it still
// matches docs/assets/refactored_previews/. That indirection is how the two drifted:
// measured on the 2026-08-09 device report, 16 of 24 cards carried a different set of
// item labels and 15 carried different column headers, while every existing test was
// green. This one reads the previews themselves, so a card cannot claim to follow a
// preview it no longer resembles.
//
// What it deliberately does NOT check:
//   * cell values -- those come from the run, and a preview's numbers are one example
//   * charts -- their shell is covered by report_card_contract_test
//   * a preview with no card, or a card with no preview: reported, never silently skipped

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/types.hpp"

namespace {

int failures = 0;

void fail(const std::string &message) {
  std::cout << "FAIL: " << message << "\n";
  ++failures;
}

std::string read_file(const std::string &path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

// JSON string escapes, as far as the detail lines use them. Without \uXXXX the arrow in
// "coarse: 150ms \u2192 10/10" reaches the renderer as six literal characters and the
// sweep parser -- which expects a value there -- silently matches nothing.
std::string unescape_json(const std::string &raw) {
  std::string out;
  for (std::size_t i = 0; i < raw.size(); ++i) {
    if (raw[i] != '\\' || i + 1 >= raw.size()) {
      out += raw[i];
      continue;
    }
    const char code = raw[i + 1];
    if (code == 'u' && i + 5 < raw.size()) {
      const long point = std::strtol(raw.substr(i + 2, 4).c_str(), nullptr, 16);
      // UTF-8, enough for the punctuation these lines carry.
      if (point < 0x80) {
        out += static_cast<char>(point);
      } else if (point < 0x800) {
        out += static_cast<char>(0xC0 | (point >> 6));
        out += static_cast<char>(0x80 | (point & 0x3F));
      } else {
        out += static_cast<char>(0xE0 | (point >> 12));
        out += static_cast<char>(0x80 | ((point >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (point & 0x3F));
      }
      i += 5;
      continue;
    }
    out += code == 'n' ? '\n' : code == 't' ? '\t' : code;
    ++i;
  }
  return out;
}

std::string strip_tags(const std::string &html) {
  std::string out;
  bool inside = false;
  for (char c : html) {
    if (c == '<') {
      inside = true;
    } else if (c == '>') {
      inside = false;
    } else if (!inside) {
      out += c;
    }
  }
  // Collapse whitespace and trim.
  std::string flat;
  bool space = false;
  for (char c : out) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      space = true;
      continue;
    }
    if (space && !flat.empty()) {
      flat += ' ';
    }
    space = false;
    flat += c;
  }
  return flat;
}

// The item labels of one card, in document order.
std::vector<std::string> item_labels(const std::string &html) {
  std::vector<std::string> labels;
  for (std::size_t at = html.find("<h4"); at != std::string::npos; at = html.find("<h4", at + 1)) {
    const std::size_t open_end = html.find('>', at);
    const std::size_t close = open_end == std::string::npos ? std::string::npos : html.find("</h4>", open_end);
    if (close == std::string::npos) {
      break;
    }
    labels.push_back(strip_tags(html.substr(open_end + 1, close - open_end - 1)));
    at = close;
  }
  return labels;
}

// Every <article class="test-card ..."> block, taken up to its </article>. A hand scan
// because a regex spanning a whole report backtracks per character and overflows.
std::vector<std::string> cards_in(const std::string &html) {
  std::vector<std::string> cards;
  const std::string open = "<article class=\"test-card";
  for (std::size_t at = html.find(open); at != std::string::npos; at = html.find(open, at + 1)) {
    const std::size_t end = html.find("</article>", at);
    if (end == std::string::npos) {
      break;
    }
    cards.push_back(html.substr(at, end + 10 - at));
    at = end;
  }
  return cards;
}

// The items of the richest card in a preview. A preview holds one card per trigger mode
// and a test that only runs under some of them leaves the others empty -- t16 is
// hardware-only, so two of its three cards carry nothing. Comparing against the
// concatenation would demand those empty cards' (absent) items too, and comparing against
// the first would demand nothing at all.
std::vector<std::string> richest_card_items(const std::string &preview) {
  std::vector<std::string> best;
  for (const std::string &card : cards_in(preview)) {
    const std::vector<std::string> items = item_labels(card);
    if (items.size() > best.size()) {
      best = items;
    }
  }
  return best;
}

// One signature per table: its column headers joined with " | ". Both spellings are
// accepted -- the previews use per-test header classes (".win-header", ".sum-header"),
// production uses the generic ".grid-head"; the CONTRACT is the column names, not the
// class that carries them.
std::vector<std::string> column_signatures(const std::string &html) {
  std::vector<std::string> signatures;
  // The header's cells are read up to its own </div>. This is a hand scan rather than a
  // regex: requiring a <div> to follow swallowed whatever came next when a table had a
  // header and no rows (measured on t15), and the obvious lookahead fix -- "((?!</div>).)*"
  // -- recurses once per character and segfaults on a 200 KB report.
  const std::regex header(R"RX(<div class="(?:grid-head|[a-z0-9]+-header(?:-row)?)[^"]*"[^>]*>)RX");
  for (auto it = std::sregex_iterator(html.begin(), html.end(), header); it != std::sregex_iterator(); ++it) {
    const std::size_t start = static_cast<std::size_t>(it->position(0)) + it->length(0);
    const std::size_t end = html.find("</div>", start);
    const std::string body = html.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string joined;
    for (std::size_t at = body.find("<span"); at != std::string::npos; at = body.find("<span", at + 1)) {
      const std::size_t open_end = body.find('>', at);
      const std::size_t close = open_end == std::string::npos ? std::string::npos : body.find("</span>", open_end);
      if (close == std::string::npos) {
        break;
      }
      if (!joined.empty()) {
        joined += " | ";
      }
      joined += strip_tags(body.substr(open_end + 1, close - open_end - 1));
      at = close;
    }
    if (!joined.empty()) {
      // A header may embed a measured value -- t13 labels its columns with the cliff it
      // found ("At cliff \xC2\xB7 45 ms"), which differs per run and per preview mode.
      // The contract is the column NAMES, so digits are normalised away; a header that
      // differs only in its numbers is the same signature.
      std::string normalised;
      bool in_number = false;
      for (char c : joined) {
        const bool digit = std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.';
        if (digit) {
          if (!in_number) {
            normalised += '#';
          }
          in_number = true;
          continue;
        }
        in_number = false;
        normalised += c;
      }
      signatures.push_back(normalised);
    }
  }
  return signatures;
}

// A preview file repeats the same card once per trigger mode. The contract is one cycle
// of that repetition; anything else means the file itself is inconsistent between modes.
std::vector<std::string> first_cycle(const std::vector<std::string> &all) {
  for (std::size_t n = 1; n <= all.size(); ++n) {
    if (all.size() % n != 0) {
      continue;
    }
    bool repeats = true;
    for (std::size_t i = n; i < all.size() && repeats; ++i) {
      repeats = all[i] == all[i % n];
    }
    if (repeats) {
      return std::vector<std::string>(all.begin(), all.begin() + static_cast<std::ptrdiff_t>(n));
    }
  }
  // No repetition found. A preview holds three mode blocks, so a list that does not
  // divide into equal cycles means the file itself disagrees between modes -- report that
  // rather than comparing a card against three concatenated copies.
  return all;
}

std::string join(const std::vector<std::string> &values) {
  if (values.empty()) {
    return "(none)";
  }
  std::string out;
  for (const std::string &value : values) {
    if (!out.empty()) {
      out += " ; ";
    }
    out += value;
  }
  return out;
}

// Reports the first position at which two lists differ, so a 12-item card does not print
// twelve lines of identical prefix.
void compare(const std::string &slug, const std::string &what, const std::vector<std::string> &want,
             const std::vector<std::string> &got) {
  if (want == got) {
    return;
  }
  std::size_t at = 0;
  while (at < want.size() && at < got.size() && want[at] == got[at]) {
    ++at;
  }
  fail(slug + " " + what + " differ from the approved preview at position " + std::to_string(at + 1) + "\n" +
       "        preview: " + join(want) + "\n" + "        rendered: " + join(got));
}

// A test result carrying every metric name and detail line the renderers look up, so a
// card that renders thin is doing so because of its own structure, not because this
// fixture starved it. Names verified against tests/data/device-run-2026-08-09.json.
v4l2diag::TestResult test_of(const std::string &slug, const std::vector<v4l2diag::MetricValue> &metrics,
                             const std::vector<std::string> &details) {
  v4l2diag::TestResult test;
  test.id = slug;
  test.name = slug;
  test.status = v4l2diag::TestStatus::Pass;
  test.memory_backend = "mmap";
  test.duration_ms = 1500;
  test.summary = slug + " completed.";
  test.category = "capture";
  test.metrics = metrics;
  test.details = details;
  return test;
}

// Loads the recorded device run so each card is rendered from the data it will really
// have. A structural check on a starved fixture reports "missing item" for every chart
// that simply had nothing to draw -- measured: seven cards looked broken that way while
// the real report drew all seven charts.
std::map<std::string, std::pair<std::vector<v4l2diag::MetricValue>, std::vector<std::string>>> recorded_run() {
  std::map<std::string, std::pair<std::vector<v4l2diag::MetricValue>, std::vector<std::string>>> out;
  const std::string json = read_file("tests/data/device-run-2026-08-09.json");
  if (json.empty()) {
    return out;
  }
  // One slice per test object, keyed by its id.
  const std::regex id_re(R"RX("id"\s*:\s*"(t\d\d[a-z0-9-]*)")RX");
  std::vector<std::pair<std::string, std::size_t>> marks;
  for (auto it = std::sregex_iterator(json.begin(), json.end(), id_re); it != std::sregex_iterator(); ++it) {
    marks.emplace_back((*it)[1].str(), static_cast<std::size_t>(it->position(0)));
  }
  // The writer emits object members alphabetically, so "details" comes BEFORE "id".
  // Slicing from one id to the next therefore hands each test the NEXT one's detail
  // lines -- measured: t03 was fed t04's "buffers_requested: 2". The slice has to start
  // at the object that contains the id, not at the id itself.
  for (std::size_t k = 0; k < marks.size(); ++k) {
    std::size_t from = json.rfind('{', marks[k].second);
    if (from == std::string::npos) {
      from = marks[k].second;
    }
    std::size_t to = json.size();
    if (k + 1 < marks.size()) {
      const std::size_t next = json.rfind('{', marks[k + 1].second);
      to = next == std::string::npos ? marks[k + 1].second : next;
    }
    const std::string slice = json.substr(from, to - from);

    std::vector<v4l2diag::MetricValue> metrics;
    const std::size_t m_at = slice.find("\"metrics\"");
    if (m_at != std::string::npos) {
      const std::size_t open_at = slice.find('[', m_at);
      const std::size_t close_at = open_at == std::string::npos ? std::string::npos : slice.find(']', open_at);
      if (open_at != std::string::npos && close_at != std::string::npos) {
        const std::string body = slice.substr(open_at, close_at - open_at);
        const std::regex entry(
            R"RX("name"\s*:\s*"([^"]+)"[\s\S]*?"unit"\s*:\s*"([^"]*)"[\s\S]*?"value"\s*:\s*([-0-9.eE]+))RX");
        for (auto it = std::sregex_iterator(body.begin(), body.end(), entry); it != std::sregex_iterator(); ++it) {
          v4l2diag::MetricValue metric;
          metric.name = (*it)[1].str();
          metric.unit = (*it)[2].str();
          metric.value = std::strtod((*it)[3].str().c_str(), nullptr);
          metrics.push_back(metric);
        }
      }
    }

    std::vector<std::string> details;
    const std::size_t d_at = slice.find("\"details\"");
    if (d_at != std::string::npos) {
      const std::size_t open_at = slice.find('[', d_at);
      const std::size_t close_at = open_at == std::string::npos ? std::string::npos : slice.find(']', open_at);
      if (open_at != std::string::npos && close_at != std::string::npos) {
        const std::string body = slice.substr(open_at, close_at - open_at);
        const std::regex line(R"RX("((?:[^"\\]|\\.)*)")RX");
        for (auto it = std::sregex_iterator(body.begin(), body.end(), line); it != std::sregex_iterator(); ++it) {
          details.push_back(unescape_json((*it)[1].str()));
        }
      }
    }
    out[marks[k].first] = {metrics, details};
  }
  return out;
}

// How a card draws its charts: one entry per chart, in document order, each either
// "svg" or "css-bar".
//
// This is the check whose ABSENCE let the largest defect through. The previous version of
// this file said, in a comment, that charts were "covered by report_card_contract_test" --
// but that test only asserts a chart's SHELL (frame, viewBox width, CSS classes present),
// never which KIND of chart a given card draws. So counting 26 charts read as success
// while, measured on the 2026-08-09 device report, 17 of 24 cards drew a different chart
// type than their approved preview: t13/t14/t16/t23 were SVG in the preview and CSS bars
// in production, t03/t07 the reverse, and t11/t25 had a chart approved but drew none.
std::vector<std::string> chart_kinds(const std::string &raw) {
  std::vector<std::string> kinds;
  // Markup only. A stylesheet DEFINES .bar-track and .budget-track without drawing
  // anything, and counting those rules read a preview's <style> block as a row of charts.
  std::string html = raw;
  for (std::size_t at = html.find("<style"); at != std::string::npos; at = html.find("<style", at)) {
    const std::size_t end = html.find("</style>", at);
    if (end == std::string::npos) {
      html.erase(at);
      break;
    }
    html.erase(at, end + 8 - at);
  }
  // A CSS bar chart is spelled with a per-test track class in the previews
  // (".budget-track" in t13, ".headroom-track" in t14, ".rel-track", ".thr-track") and
  // with the shared ".bar-track" in production. Recognising only ".bar-track" read t13's
  // and t14's second chart as absent, which would have demanded their removal instead of
  // their conversion. The suffix is the contract; the prefix is per-test naming.
  // A run of consecutive track rows is ONE chart, not one per bar: the run is closed by
  // the next <svg> or the next item label (<h4>), which is where a new chart starts.
  const std::regex mark(R"RX(<svg|<h4|class="[a-z0-9]+-track")RX");
  bool in_bar_run = false;
  for (auto it = std::sregex_iterator(html.begin(), html.end(), mark); it != std::sregex_iterator(); ++it) {
    const std::string token = it->str(0);
    if (token == "<svg") {
      kinds.push_back("svg");
      in_bar_run = false;
    } else if (token == "<h4") {
      in_bar_run = false;  // a new item begins; the next track starts a new chart
    } else if (!in_bar_run) {
      kinds.push_back("css-bar");
      in_bar_run = true;
    }
  }
  return kinds;
}

// Every class token used inside a card. Used to prove production invents no visual
// vocabulary the approved previews do not have: a class with no counterpart in any
// preview is content the user never approved, whatever it renders.
std::set<std::string> class_tokens(const std::string &html) {
  std::set<std::string> tokens;
  const std::regex attr(R"RX(class="([^"]*)")RX");
  for (auto it = std::sregex_iterator(html.begin(), html.end(), attr); it != std::sregex_iterator(); ++it) {
    std::istringstream stream((*it)[1].str());
    std::string token;
    while (stream >> token) {
      tokens.insert(token);
    }
  }
  return tokens;
}

std::string make_temp_dir() {
  char pattern[] = "/tmp/v4l2diag-preview-XXXXXX";
  return mkdtemp(pattern);
}

}  // namespace

int main() {
  // Build a run holding every test, so each card is rendered by its own renderer.
  v4l2diag::RunResult run;
  run.started_at_utc = "2026-08-09T01:08:08Z";
  run.trigger_mode = v4l2diag::TriggerMode::Hardware;
  run.trigger_profile_file = "anvil.json";
  run.threshold_config_file = "default.json";
  run.run_id = "preview-structure";

  // The preview files are named t01..t26; the renderers dispatch on the full slug.
  static const char *kTestIds[] = {
      "t01-device-compliance",      "t02-control-inventory", "t03-pipeline-ready",    "t04-no-streamon",
      "t05-pollerr-handling",       "t06-stream-cycles",     "t07-multi-buffer",      "t08-buffer-overwrite",
      "t09-buffer-recycling",       "t10-buffer-flags",      "t11-memory-throughput", "t12-dmabuf-cache-sync",
      "t13-poll-timeout-cliff",     "t14-trigger-latency",   "t15-nonblock-vs-block", "t16-gpio-pulse-width",
      "t17-format-comparison",      "t18-control-sweep",     "t19-resolution-sweep",  "t20-sequence-continuity",
      "t21-timestamp-monotonicity", "t22-stuck-frame",       "t23-sustained-capture", "t24-latency-under-load",
      "t25-multi-camera",           "t26-cold-start"};

  const std::string previews = "docs/assets/refactored_previews";
  std::map<std::string, std::string> preview_of;
  for (const char *full_id : kTestIds) {
    const std::string slug = std::string(full_id).substr(0, 3);
    const std::string path = previews + "/" + slug + "-preview.html";
    const std::string html = read_file(path);
    if (html.empty()) {
      fail(std::string("cannot read ") + path + " (run from the repository root)");
      continue;
    }
    preview_of[full_id] = html;
  }
  if (preview_of.size() < 26) {
    std::cout << "only " << preview_of.size() << " previews were read; the comparison would be vacuous\n";
    return 1;
  }

  // Render every card once, through write_reports() -- the production path.
  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video0";
  camera.role = "master";
  camera.memory_backends.push_back(v4l2diag::MemoryBackend::Mmap);
  const auto recorded = recorded_run();
  if (recorded.size() < 20) {
    std::cout << "FAIL: the recorded device run supplied only " << recorded.size()
              << " tests; a structural check on empty data reports every chart as missing\n";
    return 1;
  }
  for (const auto &entry : preview_of) {
    const auto data = recorded.find(entry.first);
    camera.tests.push_back(data == recorded.end() ? test_of(entry.first, {}, {})
                                                  : test_of(entry.first, data->second.first, data->second.second));
  }
  run.cameras.push_back(camera);

  const std::string directory = make_temp_dir();
  try {
    v4l2diag::write_reports(run, directory);
  } catch (const std::exception &error) {
    std::cout << "FAIL: write_reports threw: " << error.what() << "\n";
    return 1;
  }
  v4l2diag::ReportNaming naming;
  naming.started_at_utc = run.started_at_utc;
  naming.trigger_mode = run.trigger_mode;
  naming.trigger_profile_file = run.trigger_profile_file;
  naming.test_configuration_file = run.threshold_config_file;
  const std::string html =
      read_file(directory + "/" + v4l2diag::report_artifact_filename(naming, v4l2diag::ReportFormat::Html));
  if (html.empty()) {
    std::cout << "FAIL: write_reports produced no HTML\n";
    return 1;
  }

  // Split the report into cards, keyed by the slug in their anchor id.
  std::map<std::string, std::string> card_of;
  for (const std::string &card : cards_in(html)) {
    const std::size_t at = card.find("id=\"result-mmap-");
    if (at == std::string::npos) {
      continue;
    }
    const std::size_t start = at + 16;
    card_of[card.substr(start, card.find('"', start) - start)] = card;
  }

  std::size_t compared = 0;
  for (const auto &entry : preview_of) {
    const std::string slug = card_of.count(entry.first) == 1 ? entry.first : std::string();
    if (slug.empty()) {
      fail(entry.first + " has an approved preview but no rendered card");
      continue;
    }
    // A test the recorded run never executed -- or executed and skipped -- has no data to
    // render from, so its card is legitimately thin and comparing it against a full
    // preview measures the fixture, not the renderer. t09 is FreeRun-only and t12 is
    // device-specific (see trigger_model_test); t25 needs a second camera and skipped on
    // this single-camera device, so its coverage chart has nothing to draw.
    const auto data = recorded.find(entry.first);
    if (data == recorded.end() || (data->second.first.empty() && data->second.second.empty())) {
      std::cout << "  skipped " << entry.first << ": the recorded run has no data for it\n";
      continue;
    }
    ++compared;
    const std::vector<std::string> preview_columns = column_signatures(entry.second);
    compare(slug, "item labels", richest_card_items(entry.second), item_labels(card_of[slug]));
    compare(slug, "table columns", first_cycle(preview_columns), column_signatures(card_of[slug]));

    // Chart kinds, against the richest preview card for the same reason item labels use it.
    const std::string &card = card_of[slug];
    std::vector<std::string> preview_charts;
    for (const std::string &preview_card : cards_in(entry.second)) {
      const std::vector<std::string> kinds = chart_kinds(preview_card);
      if (kinds.size() > preview_charts.size()) {
        preview_charts = kinds;
      }
    }
    compare(slug, "chart kinds", preview_charts, chart_kinds(card));

    // No PROSE production invents on its own.
    //
    // Scoped deliberately to classes that carry explanatory text. A blanket
    // "every class must appear in a preview" check reports 263 differences that are not
    // defects: the previews spell tables with per-test classes (".win-header", ".sum-row")
    // and production spells the same table ".grid-head"/".grid-row cols-5" -- an
    // equivalence this file's column_signatures() already relies on deliberately. Naming
    // those would bury the real finding, which is text the user never approved appearing
    // beside measurements: .boundary-note ("Declared clock type describes buffer
    // metadata..."), .test-note and .chart-axis-caption, in 0 of 26 previews.
    static const std::set<std::string> kProseClasses = {
        "boundary-note", "test-note", "axis-caption", "chart-axis-caption", "chart-note", "note", "callout",
        "hint",          "info-note", "caption",      "explanation",        "advisory"};
    for (const std::string &token : class_tokens(card)) {
      if (kProseClasses.count(token) == 0) {
        continue;
      }
      bool approved = false;
      for (const auto &other : preview_of) {
        if (other.second.find("class=\"" + token + "\"") != std::string::npos) {
          approved = true;
          break;
        }
      }
      if (!approved) {
        fail(slug + ": renders explanatory text in \"" + token + "\", which appears in no approved preview");
      }
    }
  }

  // The raw monospace dump. Production emitted `<div class="detail-list">` after the last
  // section of 12 cards, repeating values the Test Configuration table already showed --
  // in unstyled `key: value` form, outside every <section>. No preview contains it. The
  // existing per-test assertions covered only t01..t11, so every test renumbered to t13+
  // was free to emit it.
  {
    std::size_t dumps = 0;
    std::vector<std::string> where;
    for (const auto &entry : card_of) {
      if (entry.second.find("class=\"detail-list\"") != std::string::npos) {
        ++dumps;
        where.push_back(entry.first.substr(0, 3));
      }
    }
    if (dumps > 0) {
      std::string joined;
      for (const std::string &id : where) {
        joined += (joined.empty() ? "" : ", ") + id;
      }
      fail(std::to_string(dumps) + " card(s) emit the raw detail-list dump, which no preview has: " + joined);
    }
  }

  // Container structure. A card whose preview groups its sections into a multi-column
  // wrapper must group them too: t01's three Device Evidence sections sit side by side in
  // `.three-col`, and production emitted them as three full-width sections stacked
  // vertically -- the same content, a different page.
  for (const auto &entry : preview_of) {
    const std::string slug = entry.first.substr(0, 3);
    if (entry.second.find("class=\"three-col\"") == std::string::npos) {
      continue;
    }
    const auto card = card_of.find(entry.first);
    if (card != card_of.end() && card->second.find("three-col") == std::string::npos) {
      fail(slug + ": preview groups its sections in .three-col, production stacks them vertically");
    }
  }

  // Chart framing. The previews frame a chart with padding alone; production added
  // `overflow-x: auto` together with `min-width: 906px`, which makes a scrollbar appear
  // under every chart whose card is narrower than 906px. The scrollbar is not a rendering
  // detail -- it is a control the approved design does not have.
  {
    const std::size_t at = html.find(".chart-frame, .metric-chart");
    if (at != std::string::npos) {
      const std::size_t end = html.find('}', at);
      const std::string rule = html.substr(at, end == std::string::npos ? 0 : end - at);
      if (rule.find("overflow-x") != std::string::npos && rule.find("auto") != std::string::npos) {
        fail("chart frames set overflow-x:auto, so a scrollbar is drawn under charts; no preview does");
      }
    }
    // Searched inside the svg rule, not across the whole document: the stylesheet ships
    // its own comments, and a comment EXPLAINING why the fixed width was removed contains
    // the very text a document-wide search looks for. Matching prose would make this
    // check fire forever on a file that is already correct.
    const std::size_t svg_at = html.find(".chart-frame svg");
    if (svg_at != std::string::npos) {
      const std::size_t rule_end = html.find('}', svg_at);
      const std::string rule = html.substr(svg_at, rule_end == std::string::npos ? 0 : rule_end - svg_at);
      if (rule.find("min-width") != std::string::npos) {
        fail("chart SVGs are pinned with a min-width, which forces the scrollbar above");
      }
    }
  }

  // Guard the guard: a parse that silently returned nothing would make every comparison
  // trivially equal.
  // 23, not 26: t09 (FreeRun-only) and t12 (device-specific) never ran, and t25 skipped
  // for want of a second camera. The floor exists so a parse that silently produced
  // nothing cannot pass as agreement.
  if (compared < 23) {
    fail("only " + std::to_string(compared) + " of 23 comparable cards were compared");
  }

  if (failures == 0) {
    std::cout << "preview structure: " << compared << " cards match their approved preview\n";
    return 0;
  }
  std::cout << "\n" << failures << " structural difference(s) against docs/assets/refactored_previews/\n";
  return 1;
}
