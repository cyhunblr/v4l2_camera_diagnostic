// The four-button export toolbar in the rendered report (plan 3.3), and the canonical
// artifact names it links to (plan 3.5).
//
// Transfers the toolbar approved in plan 1.7 (TASARIM ONAYLANDI, 2026-08-04) into the
// source renderer. The design is not revisited here: the labels, the order and the
// element kinds come from the approved previews.
//
// The links are the reason this is a test and not a review note: they are plain relative
// hrefs, so if the writer names a file one way and the toolbar links another, an archived
// report opened over file:// has two dead buttons and nothing says so.

#include <unistd.h>

#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/types.hpp"

namespace {

bool check(bool condition, const std::string &message) {
  if (!condition) {
    std::cout << "FAIL: " << message << "\n";
  }
  return condition;
}

bool contains(const std::string &haystack, const std::string &needle) {
  return haystack.find(needle) != std::string::npos;
}

std::size_t count_of(const std::string &haystack, const std::string &needle) {
  std::size_t count = 0;
  for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

std::string make_temp_dir() {
  char pattern[] = "/tmp/v4l2diag-toolbar-XXXXXX";
  const char *path = mkdtemp(pattern);
  return path == nullptr ? std::string() : std::string(path);
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

v4l2diag::RunResult run_of(v4l2diag::TriggerMode mode, const std::string &profile_file,
                           const std::string &config_file) {
  v4l2diag::RunResult run;
  run.started_at_utc = "2026-07-30T12:02:38Z";
  run.finished_at_utc = "2026-07-30T12:09:11Z";
  run.trigger_mode = mode;
  run.trigger_profile_file = profile_file;
  run.threshold_config_file = config_file;

  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video0";
  v4l2diag::TestResult test;
  test.id = "t10-buffer-flags";
  test.name = "Buffer Flags";
  test.status = v4l2diag::TestStatus::Pass;
  test.memory_backend = "mmap";
  test.duration_ms = 12420.0;
  test.summary = "Fine.";
  camera.tests.push_back(test);
  run.cameras.push_back(camera);
  return run;
}

// Writes the run and returns its HTML, leaving the directory in place for the caller to
// inspect and remove.
std::string write_and_read(const v4l2diag::RunResult &run, const std::string &directory, bool *ok) {
  try {
    v4l2diag::write_reports(run, directory);
  } catch (const std::exception &error) {
    *ok = check(false, std::string("write_reports threw: ") + error.what());
    return std::string();
  }
  v4l2diag::ReportNaming naming;
  naming.started_at_utc = run.started_at_utc;
  naming.trigger_mode = run.trigger_mode;
  naming.trigger_profile_file = run.trigger_profile_file;
  naming.test_configuration_file = run.threshold_config_file;
  return read_file(directory + "/" + v4l2diag::report_artifact_filename(naming, v4l2diag::ReportFormat::Html));
}

void remove_artifacts(const std::string &directory, const v4l2diag::RunResult &run) {
  v4l2diag::ReportNaming naming;
  naming.started_at_utc = run.started_at_utc;
  naming.trigger_mode = run.trigger_mode;
  naming.trigger_profile_file = run.trigger_profile_file;
  naming.test_configuration_file = run.threshold_config_file;
  for (const auto format :
       {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown}) {
    unlink((directory + "/" + v4l2diag::report_artifact_filename(naming, format)).c_str());
  }
  rmdir(directory.c_str());
}

}  // namespace

int main() {
  bool ok = true;

  // --- 1. Four actions, the approved labels, the approved order -----------
  {
    const v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::Hardware, "anvil.json", "stress-test.json");
    const std::string directory = make_temp_dir();
    const std::string html = write_and_read(run, directory, &ok);

    // The final label is "Export PDF". "Export as PDF" was a leftover from plan 2.2 and
    // this item renames the source and its assertion together.
    ok &= check(contains(html, ">Export PDF<"), "the PDF action does not use the approved label");
    ok &= check(!contains(html, "Export as PDF"), "the pre-3.3 label \"Export as PDF\" survived");

    for (const char *label : {">Export PDF<", ">Export JSON<", ">Export Markdown<", ">Export DMESG<"}) {
      ok &= check(count_of(html, label) == 1,
                  std::string("expected exactly one ") + label + ", got " + std::to_string(count_of(html, label)));
    }

    // PDF, JSON, Markdown, DMESG -- the order the approved preview uses.
    const std::size_t pdf = html.find(">Export PDF<");
    const std::size_t json = html.find(">Export JSON<");
    const std::size_t markdown = html.find(">Export Markdown<");
    const std::size_t dmesg = html.find(">Export DMESG<");
    ok &= check(pdf < json && json < markdown && markdown < dmesg, "the export actions are out of order");

    // One row, inside the header, below the title -- the layout approved in plan 1.7
    // after an earlier attempt put the toolbar above the H1.
    const std::size_t heading = html.find("<h1");
    const std::size_t row = html.find("class=\"export-row\"");
    ok &= check(row != std::string::npos, "there is no export row");
    ok &= check(heading < row, "the export row is above the H1; the approved layout puts it below the title");

    remove_artifacts(directory, run);
  }

  // --- 2. PDF prints; it does not build a file ---------------------------
  {
    const v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::FreeRun, "", "");
    const std::string directory = make_temp_dir();
    const std::string html = write_and_read(run, directory, &ok);

    ok &= check(contains(html, "window.print()"), "the PDF action does not call window.print()");
    // The browser owns the filename, the .pdf extension and every print option, so the
    // report brings no PDF machinery of its own.
    ok &= check(!contains(html, "jspdf") && !contains(html, "html2pdf") && !contains(html, "print.js"),
                "a client-side PDF library reached the report");

    // The <title> is the canonical base, because the print dialog offers it as the PDF
    // name. The visible H1 stays the human title.
    v4l2diag::ReportNaming naming;
    naming.started_at_utc = run.started_at_utc;
    naming.trigger_mode = run.trigger_mode;
    naming.test_configuration_file = run.threshold_config_file;
    ok &= check(contains(html, "<title>" + v4l2diag::report_document_title(naming) + "</title>"),
                "the <title> is not the canonical artifact base");
    ok &= check(contains(html, "V4L2 Camera Diagnostic Report"), "the visible H1 changed");

    remove_artifacts(directory, run);
  }

  // --- 3. JSON and Markdown link to the files on disk -------------------
  {
    // The whole point: no Blob, no client-side serialisation, and no name derived a
    // second time. These must be the files the writer actually produced.
    const v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::Software, "pulse.json", "harsh-limits.json");
    const std::string directory = make_temp_dir();
    const std::string html = write_and_read(run, directory, &ok);

    v4l2diag::ReportNaming naming;
    naming.started_at_utc = run.started_at_utc;
    naming.trigger_mode = run.trigger_mode;
    naming.trigger_profile_file = run.trigger_profile_file;
    naming.test_configuration_file = run.threshold_config_file;
    const std::string json_name = v4l2diag::report_artifact_filename(naming, v4l2diag::ReportFormat::Json);
    const std::string markdown_name = v4l2diag::report_artifact_filename(naming, v4l2diag::ReportFormat::Markdown);

    ok &= check(contains(html, "href=\"" + json_name + "\""), "the JSON action does not link the written file");
    ok &= check(contains(html, "href=\"" + markdown_name + "\""), "the Markdown action does not link the written file");

    // Relative, so an archived folder opened over file:// still works. An absolute path
    // or an /api/ URL would break exactly there.
    ok &= check(!contains(html, "href=\"/" + json_name) && !contains(html, "href=\"/api/reports"),
                "an export link is not relative");
    ok &= check(!contains(html, "Blob(") && !contains(html, "createObjectURL"),
                "the report serialises an artifact in the browser");

    // The linked files exist. A link to a name nobody wrote is a dead button.
    std::ifstream json_file(directory + "/" + json_name);
    std::ifstream markdown_file(directory + "/" + markdown_name);
    ok &= check(json_file.good(), "the linked JSON file does not exist: " + json_name);
    ok &= check(markdown_file.good(), "the linked Markdown file does not exist: " + markdown_name);

    remove_artifacts(directory, run);
  }

  // --- 4. The artifacts carry the canonical name, not the old fixed one --
  {
    const v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::Hardware, "anvil.json", "stress-test.json");
    const std::string directory = make_temp_dir();
    ok &= !write_and_read(run, directory, &ok).empty();

    // The exact name from plan 3.5's locked examples.
    const std::string base = "2026-07-30_12-02-38_hardware-trigger_anvil_stress-test_v4l2_camera_diagnostic";
    for (const char *extension : {".html", ".json", ".md"}) {
      std::ifstream file(directory + "/" + base + extension);
      ok &= check(file.good(), std::string("missing canonically named artifact: ") + base + extension);
    }
    // The pre-3.5 fixed name must be gone, not merely accompanied.
    for (const char *stale : {"diagnostic-report.html", "diagnostic-report.json", "diagnostic-report.md"}) {
      std::ifstream file(directory + "/" + stale);
      ok &= check(!file.good(), std::string("the fixed pre-3.5 name is still written: ") + stale);
    }

    remove_artifacts(directory, run);
  }

  // --- 5. The archived DMESG state: disabled button + the approved note ---
  {
    const v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::Hardware, "anvil.json", "stress-test.json");
    const std::string directory = make_temp_dir();
    const std::string html = write_and_read(run, directory, &ok);

    // The approved wording, verbatim (report-ui-review-plan 1.3). A shortened or
    // reworded version is a different approved design.
    ok &= check(contains(html, "DMESG export requires the diagnostic server."),
                "the approved archived-DMESG note is missing from the report");
    ok &= check(contains(html, "class=\"export-note\""), "the note does not use the approved element");

    // A REAL disabled control: only the disabled attribute makes a button unfocusable and
    // unclickable. An ARIA-only placeholder still takes focus and still fires clicks.
    ok &= check(contains(html, "<button type=\"button\" class=\"export-pdf-btn\" id=\"export-dmesg\" disabled"),
                "the archived DMESG control is not a real disabled button");
    ok &= check(!contains(html, "aria-disabled=\"true\""), "an aria-disabled placeholder survived");

    // Disabled and the note visible are the DEFAULT state, so a report opened over
    // file:// -- or with scripting off -- shows the honest answer rather than a link that
    // cannot work.
    // The note is plain markup, not something a script adds: no script runs over file://
    // with scripting off, and the explanation still has to be there.
    ok &= check(contains(html, "<p class=\"export-note\" id=\"export-dmesg-note\">"),
                "the note is not part of the default markup");
    ok &= check(!contains(html, "class=\"export-note\" hidden") &&
                    !contains(html, "class=\"export-note\" style=\"display:none"),
                "the note starts hidden, so the archived case shows nothing");

    // A CLI run has no server to ask, so there is no state to resolve: no script at all,
    // and the button stays disabled with the note up.
    ok &= check(run.run_id.empty(), "this fixture is meant to be a CLI run");
    ok &= check(!contains(html, "<script>"), "a CLI report carries a state-resolving script it cannot use");

    remove_artifacts(directory, run);
  }

  // --- 5b. A web run resolves the state at load time ---------------------
  {
    v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::Hardware, "anvil.json", "stress-test.json");
    // Only a web run can offer the download, because only it has a run the server can
    // resolve (plan 3.4).
    run.run_id = "web-run-7";
    const std::string directory = make_temp_dir();
    const std::string html = write_and_read(run, directory, &ok);

    // Still disabled and still explained by default: the same file may be archived and
    // reopened over file:// later, and it must degrade to the honest state then.
    ok &= check(contains(html, "id=\"export-dmesg\" disabled"), "a web run's button does not start disabled");
    ok &= check(contains(html, "DMESG export requires the diagnostic server."),
                "a web run's report has no archived-state note");

    // Served: the script enables the button AND hides the note. Leaving the note up next
    // to a working button would contradict it.
    ok &= check(contains(html, "b.disabled=false"), "the script does not enable the button when served");
    ok &= check(contains(html, "n.hidden=true"), "the script does not hide the note when served");
    ok &= check(contains(html, "location.protocol==='file:'"), "the script does not distinguish the file:// case");

    // The run id travels in the URL; the filename does not. The server derives the name.
    ok &= check(contains(html, "data-run-id=\"web-run-7\""), "the run id is not on the control");
    ok &= check(contains(html, "encodeURIComponent(b.dataset.runId)"),
                "the run id is interpolated into the URL without encoding");
    ok &= check(!contains(html, "download=\""), "the report sets a download filename on the DMESG control");

    // Print hides both. A printed page has no clickable control and no server.
    const std::size_t print_at = html.find("@media print");
    ok &= check(print_at != std::string::npos, "there is no print stylesheet");
    // The whole print block, not a fixed-size window: the block grows as print rules are
    // added, and a 900-character slice silently stopped covering this rule.
    const std::string print_block = html.substr(print_at);
    ok &= check(print_block.find(".export-row, .export-note { display: none") != std::string::npos,
                "print does not hide the export row and the note");

    remove_artifacts(directory, run);
  }

  // --- 6. Print page geometry, per review-plan 6.8 ------------------------
  {
    const v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::FreeRun, "", "");
    const std::string directory = make_temp_dir();
    const std::string html = write_and_read(run, directory, &ok);

    // review-plan 6.8: page geometry's SINGLE source is the print CSS, and the approved
    // rule is an @page block. 6.2 says the application does not FORCE the user's print
    // options -- and it does not: @page margins stay overridable from the print dialog, and
    // 6.8 says so explicitly. An earlier review round read 6.2 alone and deleted the rule,
    // which removed approved CSS.
    ok &= check(contains(html, "@page"), "the approved @page rule is missing");
    ok &= check(contains(html, "size: A4"), "the approved paper size is missing");
    ok &= check(contains(html, "margin: 12mm 10mm 14mm"), "the approved page margins are missing");

    const std::size_t print_at = html.find("@media print");
    ok &= check(print_at != std::string::npos, "the report lost its print stylesheet entirely");
    // 6.8 rule 1-2: margins are defined ONLY inside @page, and print adds no extra
    // horizontal padding. A second source would fight the user's dialog choice.
    if (print_at != std::string::npos) {
      const std::string print_block = html.substr(print_at);
      ok &= check(!contains(print_block, ".container { padding: 20px; }"),
                  "print still adds container padding, so the margin has two sources");
    }

    // 6.8 rule 3-4: status colours survive regardless of the background-graphics setting.
    ok &= check(contains(html, "print-color-adjust: exact"), "print-color-adjust is missing");
    ok &= check(contains(html, "-webkit-print-color-adjust: exact"),
                "the -webkit- prefixed print-color-adjust is missing");

    // 6.9: the application still writes no running footer -- page numbers and date stamps
    // belong to the browser's print options.
    ok &= check(!contains(html, "@bottom-center") && !contains(html, "@top-center"),
                "the report uses @page margin boxes, whose browser support is unreliable");
    ok &= check(!contains(html, "Page X"), "the application writes its own page numbering");

    remove_artifacts(directory, run);
  }

  // --- 7. Print pagination, per review-plan 6.5-6.7 -----------------------
  {
    const v4l2diag::RunResult run = run_of(v4l2diag::TriggerMode::FreeRun, "", "");
    const std::string directory = make_temp_dir();
    const std::string html = write_and_read(run, directory, &ok);
    const std::size_t print_at = html.find("@media print");
    ok &= check(print_at != std::string::npos, "there is no print stylesheet");
    const std::string print_block = print_at == std::string::npos ? std::string() : html.substr(print_at);

    // 6.5: Detailed Results always starts on a fresh page, and a backend band is never
    // orphaned from the first test header under it.
    ok &= check(contains(print_block, "break-before: page"), "Detailed Results does not start on a new page in print");
    ok &= check(contains(print_block, ".backend { break-after: avoid"),
                "a backend band may be left alone at the foot of a page");

    // 6.6: a card is kept whole when it fits, and splits only at approved block
    // boundaries. A single chart, table row or configuration item never splits.
    ok &= check(contains(print_block, ".test-card { break-inside: auto"),
                "a long card cannot split at its block boundaries");
    ok &= check(contains(print_block, ".test-card .section { break-inside: avoid"),
                "a content block may be split mid-section");
    ok &= check(contains(print_block, "table.evidence tr { break-inside: avoid"),
                "an evidence table row may be split across pages");
    ok &= check(contains(print_block, "table.evidence thead { display: table-header-group"),
                "an evidence table head does not repeat on continuation pages");

    // 6.6 continuation header: NOT asserted, because it is not implemented. A static
    // element cannot be placed on whatever page a break lands on, and emitting it on every
    // card told the reader a page was cut when it was not. See fix-plan 3.7.5 -- this is an
    // open question for the user, not a silently dropped requirement.
    ok &= check(!contains(html, "| continued"),
                "a continuation header is emitted on every card, including ones that never split");

    // 6.7: charts print as vectors, never split, and carry meaning beyond colour.
    ok &= check(contains(print_block, ".metric-chart { break-inside: avoid"), "a chart may be split across pages");
    ok &= check(!contains(html, "<canvas"), "a chart is rendered as a bitmap rather than as SVG");
    ok &= check(!contains(html, "animation:") && !contains(html, "@keyframes"),
                "the report carries animation, which print cannot represent");

    remove_artifacts(directory, run);
  }

  if (ok) {
    std::cout << "export_toolbar_test: all checks passed\n";
  }
  return ok ? 0 : 1;
}
