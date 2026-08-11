// Detailed Result Card Template (plan 3.1).
//
// The OUTER shell only. The 24 test previews' content -- charts, tables, metric
// definitions, per-test prose -- is approved test-specific material and is NOT
// consolidated here; this locks the shell those cards sit in: the backend band, the card
// element and its status modifier, the three-column header, the anchor, and the RESULT
// section's visibility rule.
//
// Locked against the approved preview sources (docs/assets/previews/*.html and
// detailed-result-card.css), because the live HTML report used to emit a completely
// different shell: no backend band, no anchor, a two-part header with a coloured pill,
// and "skipped" where the design says "SKIP".

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/result_card.hpp"
#include "v4l2diag/core/test_content.hpp"
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

v4l2diag::TestResult test_of(v4l2diag::TestStatus status, const std::string &id = "t10-buffer-flags",
                             const std::string &name = "Buffer Flags") {
  v4l2diag::TestResult test;
  test.id = id;
  test.name = name;
  test.status = status;
  test.memory_backend = "mmap";
  test.duration_ms = 12420.0;
  test.summary = "All dequeued buffers carried a plausible flag set.";
  return test;
}

// A private directory under /tmp; the caller removes it.
std::string make_temp_dir() {
  char pattern[] = "/tmp/v4l2diag-card-XXXXXX";
  const char *path = mkdtemp(pattern);
  return path == nullptr ? std::string() : std::string(path);
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

std::size_t count_of(const std::string &haystack, const std::string &needle) {
  std::size_t count = 0;
  for (std::size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
    ++count;
  }
  return count;
}

// The canonical name a result's artifacts get (plan 3.5). Computed the same way the
// writer computes it, from the same inputs.
v4l2diag::ReportNaming naming_of(const v4l2diag::RunResult &run) {
  v4l2diag::ReportNaming naming;
  naming.started_at_utc = run.started_at_utc;
  naming.trigger_mode = run.trigger_mode;
  naming.trigger_profile_file = run.trigger_profile_file;
  naming.test_configuration_file = run.threshold_config_file;
  return naming;
}

void remove_report_directory(const std::string &directory, const v4l2diag::RunResult &run) {
  const v4l2diag::ReportNaming naming = naming_of(run);
  for (const auto format :
       {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown}) {
    unlink((directory + "/" + v4l2diag::report_artifact_filename(naming, format)).c_str());
  }
  rmdir(directory.c_str());
}

}  // namespace

int main() {
  bool ok = true;

  // --- 1. The card shell matches the approved template --------------------
  {
    const std::string html = v4l2diag::render_result_card_open(test_of(v4l2diag::TestStatus::Pass));

    // The element and class names the approved CSS selects on. A renamed wrapper would
    // silently drop every rule in detailed-result-card.css.
    ok &= check(contains(html, "<article class=\"test-card pass\""), "no article.test-card.pass: " + html);
    ok &= check(contains(html, "<header class=\"test-header\">"), "no header.test-header: " + html);

    // STATUS | TEST ID + TEST NAME | DURATION, in that order. The old header put a
    // coloured pill first and the duration in an inline-styled span.
    const std::size_t status_at = html.find("class=\"status\"");
    const std::size_t heading_at = html.find("<h2>");
    const std::size_t duration_at = html.find("class=\"duration\"");
    ok &= check(status_at != std::string::npos && heading_at != std::string::npos && duration_at != std::string::npos,
                "the header is missing one of status/h2/duration: " + html);
    ok &= check(status_at < heading_at && heading_at < duration_at, "the header parts are out of order: " + html);

    // review-plan 4.3: the number and the readable name. The technical slug is NOT
    // repeated in the visible heading -- it stays in the anchor, where it is an identifier
    // rather than a label.
    ok &= check(contains(html, "T10 - Buffer Flags"), "the heading does not read \"Txx - Readable name\": " + html);
    ok &= check(!contains(html, ">t10-buffer-flags"), "the technical slug is repeated in the heading: " + html);
    ok &= check(contains(html, "id=\"result-mmap-t10-buffer-flags\""), "the slug left the anchor as well");

    // The number appears once. Runners and fixtures differ on whether the stored name
    // already carries it, and prefixing unconditionally produced "T10 - T10 - ...".
    v4l2diag::TestResult prefixed = test_of(v4l2diag::TestStatus::Pass, "t10-buffer-flags", "T10 - Buffer Flags");
    const std::string prefixed_html = v4l2diag::render_result_card_open(prefixed);
    ok &= check(contains(prefixed_html, "<h2>T10 - Buffer Flags</h2>"),
                "an already-numbered name was renumbered: " + prefixed_html);
    ok &= check(!contains(prefixed_html, "T10 - T10"), "the test number was prefixed twice");
    // A slug with no number keeps the readable name alone rather than getting an invented one.
    ok &= check(contains(v4l2diag::render_result_card_open(test_of(v4l2diag::TestStatus::Pass, "all-tests", "Suite")),
                         "<h2>Suite</h2>"),
                "a numberless slug produced an invented test number");

    // The shared duration formatter (plan 3.2), not a private rendering.
    ok &= check(contains(html, "12.4s"), "the duration is not the shared format: " + html);

    // No dot or icon next to the status text -- an explicit design decision.
    ok &= check(!contains(html, "badge") && !contains(html, "&bull;") && !contains(html, "●"),
                "a badge or dot survived next to the status: " + html);

    // No inline style in the shell: every rule belongs to the shared stylesheet.
    ok &= check(!contains(html, "style=\""), "the shell carries an inline style: " + html);
  }

  // --- 2. The status modifier, and SKIP's visible text --------------------
  {
    ok &= check(contains(v4l2diag::render_result_card_open(test_of(v4l2diag::TestStatus::Warn)), "test-card warn"),
                "WARN did not set the card modifier");
    ok &= check(contains(v4l2diag::render_result_card_open(test_of(v4l2diag::TestStatus::Fail)), "test-card fail"),
                "FAIL did not set the card modifier");

    // The wire value is "skipped"; the card shows "SKIP" and its class is "skip", which
    // is what the approved CSS selects on.
    const std::string skipped = v4l2diag::render_result_card_open(test_of(v4l2diag::TestStatus::Skipped));
    ok &= check(contains(skipped, "test-card skip"), "SKIP did not set the card modifier: " + skipped);
    ok &= check(contains(skipped, ">SKIP<"), "the visible status text is not \"SKIP\": " + skipped);
    ok &= check(!contains(skipped, "SKIPPED") && !contains(skipped, "skipped"),
                "\"skipped\" survived as visible text: " + skipped);

    // Every status is upper-case in the card, matching the previews.
    for (const auto status : {v4l2diag::TestStatus::Pass, v4l2diag::TestStatus::Warn, v4l2diag::TestStatus::Fail,
                              v4l2diag::TestStatus::Skipped}) {
      const std::string html = v4l2diag::render_result_card_open(test_of(status));
      ok &= check(
          contains(html, ">PASS<") || contains(html, ">WARN<") || contains(html, ">FAIL<") || contains(html, ">SKIP<"),
          "a status was not rendered in the card's upper-case form: " + html);
    }
  }

  // --- 3. The anchor is result-<backend>-<test_id>, with no camera -------
  {
    v4l2diag::TestResult test = test_of(v4l2diag::TestStatus::Pass);
    test.memory_backend = "dmabuf";
    const std::string html = v4l2diag::render_result_card_open(test);
    ok &= check(contains(html, "id=\"result-dmabuf-t10-buffer-flags\""), "wrong anchor: " + html);

    // The full slug, not a truncated or prettified form.
    v4l2diag::TestResult long_id = test_of(v4l2diag::TestStatus::Pass, "t22-t07-multi-buffer-shell");
    ok &= check(contains(v4l2diag::render_result_card_open(long_id), "id=\"result-mmap-t22-t07-multi-buffer-shell\""),
                "the anchor did not use the full test slug: " + v4l2diag::render_result_card_open(long_id));

    // A camera path must not reach the anchor: the same test on two cameras would
    // otherwise produce two different anchors for one design element.
    ok &= check(!contains(html, "video") && !contains(html, "dev"), "a camera path leaked into the anchor: " + html);

    // An anchor is an HTML id: a slug with a space or a quote would break it outright.
    v4l2diag::TestResult hostile = test_of(v4l2diag::TestStatus::Pass, "t9 \"odd\"/id");
    const std::string hostile_html = v4l2diag::render_result_card_open(hostile);
    const std::size_t id_at = hostile_html.find("id=\"");
    ok &= check(id_at != std::string::npos, "no anchor at all for an odd slug");
    const std::string anchor = hostile_html.substr(id_at + 4, hostile_html.find('"', id_at + 4) - id_at - 4);
    ok &= check(anchor.find(' ') == std::string::npos && anchor.find('"') == std::string::npos &&
                    anchor.find('/') == std::string::npos,
                "an unusable character survived in the anchor: " + anchor);
  }

  // --- 4. The backend band appears once per backend ----------------------
  {
    // Three tests, two backends: two bands, and the second must not repeat the first.
    std::vector<v4l2diag::TestResult> tests = {test_of(v4l2diag::TestStatus::Pass, "t01-a"),
                                               test_of(v4l2diag::TestStatus::Pass, "t02-b"),
                                               test_of(v4l2diag::TestStatus::Pass, "t03-c")};
    tests[2].memory_backend = "dmabuf";

    std::string html;
    std::string current_backend;
    for (const auto &test : tests) {
      if (test.memory_backend != current_backend) {
        html += v4l2diag::render_backend_band(test.memory_backend, current_backend.empty());
        current_backend = test.memory_backend;
      }
      html += v4l2diag::render_result_card_open(test);
    }

    std::size_t bands = 0;
    for (std::size_t at = html.find("class=\"backend\""); at != std::string::npos;
         at = html.find("class=\"backend\"", at + 1)) {
      ++bands;
    }
    ok &= check(bands == 2, "expected one band per backend, got " + std::to_string(bands));
    ok &= check(contains(html, "MMAP") || contains(html, "mmap"), "the band does not name the backend: " + html);
    ok &= check(contains(html, "dmabuf") || contains(html, "DMABUF"), "the second backend has no band: " + html);

    // The first band needs no leading gap; a later one does, so two backends' cards do
    // not run together.
    ok &= check(!contains(v4l2diag::render_backend_band("mmap", true), "backend-gap"),
                "the first band emitted a leading gap");
    ok &= check(contains(v4l2diag::render_backend_band("dmabuf", false), "backend-gap"),
                "a following band emitted no leading gap");
  }

  // --- 5. RESULT appears only on WARN, FAIL and SKIP ---------------------
  {
    // A PASS card stating "RESULT: passed" is noise; the header already says PASS.
    ok &= check(!v4l2diag::result_section_visible(v4l2diag::TestStatus::Pass), "PASS showed a RESULT section");
    ok &= check(v4l2diag::result_section_visible(v4l2diag::TestStatus::Warn), "WARN hid the RESULT section");
    ok &= check(v4l2diag::result_section_visible(v4l2diag::TestStatus::Fail), "FAIL hid the RESULT section");
    ok &= check(v4l2diag::result_section_visible(v4l2diag::TestStatus::Skipped), "SKIP hid the RESULT section");
  }

  // --- 6. The shell escapes what it interpolates -------------------------
  {
    v4l2diag::TestResult test = test_of(v4l2diag::TestStatus::Fail, "t01", "Name <script>alert(1)</script>");
    const std::string html = v4l2diag::render_result_card_open(test);
    ok &= check(!contains(html, "<script>"), "a script tag survived the card header: " + html);
    ok &= check(contains(html, "&lt;script&gt;"), "the name was not escaped: " + html);
  }

  // --- 7. The rendered report actually uses the shared shell ---------------
  {
    // Sections 1-6 test the renderer. This runs the real writer, because the renderer
    // being correct is worth nothing if write_html_report() keeps emitting its own markup
    // -- which is exactly what it did before plan 3.1.
    v4l2diag::RunResult run;
    run.started_at_utc = "2026-08-04T12:02:38Z";
    run.finished_at_utc = "2026-08-04T12:04:00Z";
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Pass, "t01-a", "Alpha"));
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Warn, "t10-buffer-flags", "Buffer Flags"));
    v4l2diag::TestResult on_dmabuf = test_of(v4l2diag::TestStatus::Fail, "t12-dmabuf", "DMABUF Sync");
    on_dmabuf.memory_backend = "dmabuf";
    // A minute-scale duration, so the report is checked against the "Xm Ys" band and not
    // only the sub-minute one every other test in this fixture uses.
    on_dmabuf.duration_ms = 87200.0;
    camera.tests.push_back(on_dmabuf);
    v4l2diag::TestResult skipped = test_of(v4l2diag::TestStatus::Skipped, "t19-skip", "Skipped One");
    skipped.memory_backend = "dmabuf";
    camera.tests.push_back(skipped);
    run.cameras.push_back(camera);

    const std::string directory = make_temp_dir();
    bool wrote = true;
    try {
      v4l2diag::write_reports(run, directory);
    } catch (const std::exception &error) {
      wrote = check(false, std::string("write_reports threw: ") + error.what());
    }
    ok &= wrote;

    if (wrote) {
      const std::string html =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
      ok &= check(!html.empty(), "the report is empty");

      // One band per backend, scoped to Detailed Results: the Overview bands its own groups
      // too (review-plan 3.2), so a whole-document count would see four.
      const std::string detailed = html.substr(html.find("<h2>Detailed Results</h2>"));
      ok &= check(count_of(detailed, "class=\"backend\"") == 2,
                  "expected 2 backend bands in Detailed Results, got " +
                      std::to_string(count_of(detailed, "class=\"backend\"")));
      ok &= check(count_of(detailed, "class=\"backend-gap\"") == 1,
                  "expected exactly one leading gap between the two bands");
      ok &= check(count_of(html, "<article class=\"test-card") == 4, "expected 4 cards");

      // The anchors carry the backend and the full slug, and no camera path.
      for (const char *anchor : {"id=\"result-mmap-t01-a\"", "id=\"result-mmap-t10-buffer-flags\"",
                                 "id=\"result-dmabuf-t12-dmabuf\"", "id=\"result-dmabuf-t19-skip\""}) {
        ok &= check(contains(html, anchor), std::string("missing anchor ") + anchor);
      }
      ok &= check(!contains(html, "result-dev-video0") && !contains(html, "video0-t01"),
                  "a camera path reached an anchor in the report");

      // The four visible statuses, and no "SKIPPED" anywhere in the document.
      for (const char *text :
           {"class=\"status\">PASS<", "class=\"status\">WARN<", "class=\"status\">FAIL<", "class=\"status\">SKIP<"}) {
        ok &= check(contains(html, text), std::string("missing status text ") + text);
      }
      ok &= check(!contains(html, "SKIPPED"), "\"SKIPPED\" survived in the rendered report");

      // The shared duration formatter reached the report.
      ok &= check(contains(html, ">1m 27s<"), "the shared duration format is not in the report");

      // The result line on WARN, FAIL and SKIP -- three of the four cards; PASS shows
      // none. It is no longer a section: it sits above them and carries the card's own
      // status class, so one generic class can never colour a WARN card neutral grey.
      const std::size_t result_lines = count_of(html, "class=\"result-warn\"") +
                                       count_of(html, "class=\"result-fail\"") +
                                       count_of(html, "class=\"result-skip\"");
      ok &= check(result_lines == 3, "expected 3 result lines, got " + std::to_string(result_lines));
      ok &= check(count_of(html, "section-label\">Result<") == 0, "the Result section survived as a section");

      // The old shell must be gone, not merely unused: leaving it would let a later
      // change quietly render the pre-3.1 markup again.
      ok &= check(!contains(html, "test-section"), "the old .test-section wrapper survived");
      ok &= check(!contains(html, "class=\"badge"), "the old status pill survived");

      // The transferred stylesheet has to be present, or every card renders unstyled.
      ok &= check(contains(html, ".test-card") && contains(html, ".test-header") && contains(html, ".backend "),
                  "the shared card stylesheet is missing from the report");

      remove_report_directory(directory, run);
    }
  }

  // --- 8. In-report corrections (plan 3.6) --------------------------------
  {
    // Free-run: the Trigger Profile row must SAY that none is required rather than
    // vanish. A missing row reads as missing information; the run genuinely needed no
    // profile.
    v4l2diag::RunResult free_run;
    free_run.started_at_utc = "2026-08-04T12:02:38Z";
    free_run.finished_at_utc = "2026-08-04T12:04:00Z";
    free_run.trigger_mode = v4l2diag::TriggerMode::FreeRun;
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Pass, "t01-a", "Alpha"));
    free_run.cameras.push_back(camera);

    const std::string free_dir = make_temp_dir();
    bool wrote_free = true;
    try {
      v4l2diag::write_reports(free_run, free_dir);
    } catch (const std::exception &error) {
      wrote_free = check(false, std::string("write_reports threw for the free-run case: ") + error.what());
    }
    if (wrote_free) {
      const std::string html = read_file(
          free_dir + "/" + v4l2diag::report_artifact_filename(naming_of(free_run), v4l2diag::ReportFormat::Html));
      ok &= check(contains(html, "Trigger Profile"), "free-run dropped the Trigger Profile row entirely");
      ok &= check(contains(html, "Not required (free-run)"),
                  "free-run does not state that no Trigger Profile is required");
      remove_report_directory(free_dir, free_run);
    }

    // A hardware run still names its profile, so the row is not simply hard-coded.
    v4l2diag::RunResult hardware = free_run;
    hardware.trigger_mode = v4l2diag::TriggerMode::Hardware;
    hardware.trigger_profile_id = "anvil";
    // A triggered run must carry the profile's source FILE: artifacts are named from it
    // and there is no default for a run that actually routed (plan 3.5.2).
    hardware.trigger_profile_file = "anvil.json";
    const std::string hw_dir = make_temp_dir();
    bool wrote_hw = true;
    try {
      v4l2diag::write_reports(hardware, hw_dir);
    } catch (const std::exception &error) {
      wrote_hw = check(false, std::string("write_reports threw for the hardware case: ") + error.what());
    }
    if (wrote_hw) {
      const std::string html = read_file(
          hw_dir + "/" + v4l2diag::report_artifact_filename(naming_of(hardware), v4l2diag::ReportFormat::Html));
      ok &= check(contains(html, "anvil"), "a hardware run does not name its Trigger Profile");
      ok &= check(!contains(html, "Not required (free-run)"), "a hardware run claims no Trigger Profile is required");
      remove_report_directory(hw_dir, hardware);
    }
  }

  {
    // The DMABUF band names the backend as DMABUF and explains the method. The JSON key
    // stays "dmabuf": this is a VISIBLE-label change only.
    v4l2diag::TestResult on_dmabuf = test_of(v4l2diag::TestStatus::Pass);
    on_dmabuf.memory_backend = "dmabuf";
    const std::string band = v4l2diag::render_backend_band(on_dmabuf.memory_backend, true);
    ok &= check(contains(band, "DMABUF"), "the DMABUF band does not name the backend in the visible form: " + band);
    // The method note lives in the BAND, not inside T12's card. Observed on the approved
    // preview: `<div class="backend">Backend <strong>DMABUF</strong><span
    // class="backend-note">MMAP buffers exported with VIDIOC_EXPBUF</span></div>`.
    //
    // This assertion previously required the opposite, citing review-plan 5.12.2 -- and T12's
    // card carried a paragraph plus a DQBUF/SYNC/QBUF diagram drawn with `protocol`/`step`
    // classes that no CSS rule defined. The approved t12 card holds two items and no prose, so
    // the artifact decides (project rule 6) and the note moved to the band, where the
    // `backend-note` rule had been sitting unused.
    ok &= check(contains(band, "VIDIOC_EXPBUF"), "the DMABUF band does not state the export method: " + band);
    ok &= check(contains(band, "backend-note"), "the DMABUF band's method note is not in a backend-note span");
    ok &= check(contains(band, "backend-label\">Backend<"), "the band has no visible BACKEND label: " + band);

    const std::string mmap_band = v4l2diag::render_backend_band("mmap", true);
    ok &= check(contains(mmap_band, "MMAP"), "the MMAP band does not name the backend: " + mmap_band);
    // Only DMABUF carries it: MMAP buffers are not exported, so the note would be false there.
    ok &= check(!contains(mmap_band, "VIDIOC_EXPBUF"), "the MMAP band carries the DMABUF method note");
    ok &= check(!contains(mmap_band, "backend-note"), "the MMAP band carries an empty method note span");

    // ...and T12's card no longer repeats it, nor the unstyled protocol diagram.
    v4l2diag::TestResult t12 = test_of(v4l2diag::TestStatus::Pass);
    t12.id = "t12-dmabuf-cache-sync";
    t12.memory_backend = "dmabuf";
    const std::string t12_html = v4l2diag::render_test_content(t12);
    ok &= check(!contains(t12_html, "class=\"protocol\""), "T12's card still draws the unstyled protocol strip");
    ok &= check(!contains(t12_html, "<p>"), "T12's card still opens with prose; the approved card has none");

    // The anchor keeps the lower-case wire spelling: it is an id, not a label, and
    // changing it would break every link into an existing report.
    ok &= check(contains(v4l2diag::render_result_card_open(on_dmabuf), "id=\"result-dmabuf-"),
                "the visible relabelling leaked into the anchor");
  }

  {
    // The distribution count reads "Warned", matching the shared terminology. "Warnings"
    // named a list; this is a count of tests.
    v4l2diag::RunResult run;
    run.started_at_utc = "2026-08-04T12:02:38Z";
    run.finished_at_utc = "2026-08-04T12:04:00Z";
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Warn, "t01-a", "Alpha"));
    run.cameras.push_back(camera);

    const std::string directory = make_temp_dir();
    bool wrote = true;
    try {
      v4l2diag::write_reports(run, directory);
    } catch (const std::exception &error) {
      wrote = check(false, std::string("write_reports threw: ") + error.what());
    }
    if (wrote) {
      const std::string html =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
      ok &= check(contains(html, "<span>Warned</span>"), "the distribution count is not labelled \"Warned\"");
      ok &= check(!contains(html, "<span>Warnings</span>"), "the old \"Warnings\" count label survived");
      // The per-test warnings BOX is a list and keeps its own wording; only the count
      // label changed.
      const std::string json =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Json));
      ok &= check(contains(json, "\"warn_count\"") || contains(json, "warn"),
                  "the JSON key changed with the visible label");
      remove_report_directory(directory, run);
    }
  }

  // --- 9. The Overview table uses the same visible terminology -----------
  {
    // The Overview row rendered to_string(status), so "skipped" reached the DOM and
    // text-transform: uppercase showed the reader "SKIPPED" -- the exact string plan 3.6
    // removes. A test that only searched for upper-case source text could not see it,
    // because the upper-casing happens in CSS.
    v4l2diag::RunResult run;
    run.started_at_utc = "2026-08-04T12:02:38Z";
    run.finished_at_utc = "2026-08-04T12:04:00Z";
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Pass, "t01-a", "Alpha"));
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Warn, "t02-b", "Beta"));
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Fail, "t03-c", "Gamma"));
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Skipped, "t04-d", "Delta"));
    run.cameras.push_back(camera);

    const std::string directory = make_temp_dir();
    bool wrote = true;
    try {
      v4l2diag::write_reports(run, directory);
    } catch (const std::exception &error) {
      wrote = check(false, std::string("write_reports threw: ") + error.what());
    }
    if (wrote) {
      const std::string html =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));

      // Each of the four statuses, as the Overview cell actually spells it. Asserted per
      // status rather than in aggregate, so a single wrong one cannot hide behind three
      // right ones.
      // The Overview cell now also carries a data-label for the narrow layout (3.9), so the
      // class and the status word are no longer adjacent in the markup.
      for (const char *cls : {"status-cell pass\"", "status-cell warn\"", "status-cell fail\"", "status-cell skip\""}) {
        ok &= check(contains(html, cls), std::string("the Overview cell is missing the class ") + cls);
      }
      for (const char *word : {">PASS<", ">WARN<", ">FAIL<", ">SKIP<"}) {
        ok &= check(contains(html, word), std::string("the Overview table is missing ") + word);
      }

      // The lower-case wire spelling must not reach the DOM as visible text at all: with
      // text-transform: uppercase, ">skipped<" IS "SKIPPED" on screen.
      for (const char *stale : {">skipped<", ">pass<", ">warn<", ">fail<", ">SKIPPED<"}) {
        ok &= check(!contains(html, stale), std::string("the Overview table still renders ") + stale);
      }
      // The class too: the stylesheet selects .skip now, so a .skipped cell would be
      // uncoloured as well as mislabelled.
      ok &= check(!contains(html, "status-cell skipped"), "the Overview cell still uses the .skipped class");

      // The JSON keeps the wire value. This is a visible-label change only.
      const std::string json =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Json));
      ok &= check(contains(json, "\"skipped\""), "the JSON lost the \"skipped\" wire value");
      ok &= check(!contains(json, "\"SKIP\""), "the visible label leaked into the JSON");

      remove_report_directory(directory, run);
    }
  }

  // --- 10. The RENDERED report carries no double presentation -------------
  {
    // Section 8 of test_content_registry_test checks the renderer. This checks the report,
    // because the generic key/value list came from the CHART code, not from the content
    // renderer -- the registry could be perfect and the report still print every metric
    // twice: once in the test's own table and once in a "Supporting values" strip.
    v4l2diag::RunResult run;
    run.started_at_utc = "2026-08-04T12:02:38Z";
    run.finished_at_utc = "2026-08-04T12:04:00Z";
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";
    v4l2diag::TestResult test = test_of(v4l2diag::TestStatus::Pass, "t23-sustained-capture", "Sustained Capture");
    v4l2diag::MetricValue frames;
    frames.name = "frames";
    frames.value = 18000;
    test.metrics.push_back(frames);
    v4l2diag::MetricValue mean;
    mean.name = "interval_mean_ms";
    mean.value = 33.3;
    mean.unit = "ms";
    test.metrics.push_back(mean);
    camera.tests.push_back(test);
    run.cameras.push_back(camera);

    const std::string directory = make_temp_dir();
    bool wrote = true;
    try {
      v4l2diag::write_reports(run, directory);
    } catch (const std::exception &error) {
      wrote = check(false, std::string("write_reports threw: ") + error.what());
    }
    if (wrote) {
      const std::string html =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
      // The test's own approved table is present...
      ok &= check(contains(html, "<span>Window</span>"),
                  "the per-test table is missing from the rendered report (columns are grid cells now)");
      // ...and the generic strip that duplicated it is not. Checked as MARKUP, not as a
      // CSS class name, because the stylesheet legitimately still defines the rule.
      ok &= check(!contains(html, "<dl class=\"metric-kv-list\">"),
                  "the rendered report still emits the generic metric-kv-list");
      ok &= check(!contains(html, "supporting-title\">Supporting values<"),
                  "the rendered report still emits the Supporting values strip");
      remove_report_directory(directory, run);
    }
  }

  // --- 11. The RENDERED report charts only approved tests -----------------
  {
    // Section 9 of test_content_registry_test locks the allow-list. This locks the WIRING:
    // the list could be right while write_html_report() still called the chart renderer
    // unconditionally, which is exactly how eight unapproved charts got into the report.
    v4l2diag::RunResult run;
    run.started_at_utc = "2026-08-04T12:02:38Z";
    run.finished_at_utc = "2026-08-04T12:04:00Z";
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";

    // Both cards are given the metrics their own renderer reads. The generic statistic
    // family this used to supply ("interval_mean_ms") drew nothing once the automatic
    // selector was turned off -- every approved chart is now the test's own.
    for (const char *id : {"t14-trigger-latency", "t23-sustained-capture"}) {
      v4l2diag::TestResult test = test_of(v4l2diag::TestStatus::Pass, id, id);
      for (const auto &entry : {std::make_pair("latency_min", 44.7), std::make_pair("latency_mean", 44.8),
                                std::make_pair("latency_p95", 44.9), std::make_pair("latency_max", 45.0)}) {
        v4l2diag::MetricValue metric;
        metric.name = entry.first;
        metric.value = entry.second;
        metric.unit = "ms";
        metric.description = "A latency statistic.";
        test.metrics.push_back(metric);
      }
      test.details.push_back("capture_timeout: 100ms");
      test.details.push_back("Win0 0-10s: n=65 mean=44ms stddev=0 miss=0");
      camera.tests.push_back(test);
    }
    run.cameras.push_back(camera);

    const std::string directory = make_temp_dir();
    bool wrote = true;
    try {
      v4l2diag::write_reports(run, directory);
    } catch (const std::exception &error) {
      wrote = check(false, std::string("write_reports threw: ") + error.what());
    }
    if (wrote) {
      const std::string html =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
      // Both previews approve a chart, so both cards must draw one. This case used to
      // prove the opposite -- that an allow-list kept t14 chart-free -- but t14's preview
      // does show "Latency distribution", and the list is gone: a chart now exists exactly
      // when the test's own renderer draws it.
      const std::size_t t14_at = html.find("id=\"result-mmap-t14-trigger-latency\"");
      const std::size_t t23_at = html.find("id=\"result-mmap-t23-sustained-capture\"");
      ok &= check(t14_at != std::string::npos && t23_at != std::string::npos, "a card is missing from the report");
      if (t14_at != std::string::npos && t23_at != std::string::npos) {
        const std::string t14_card = html.substr(t14_at, t23_at - t14_at);
        const std::string t23_card = html.substr(t23_at);
        // Both previews approve a chart now -- t14's "Latency distribution" and "Capture
        // timeout headroom", t23's "Mean capture latency by window". They are drawn by the
        // tests' own renderers inside .chart-frame; the generic .metric-chart selector is
        // off, so checking that class alone reported a chart as lost while it was there.
        ok &= check(contains(t14_card, "Latency distribution"), "t14 lost the chart its preview approves");
        ok &= check(contains(t23_card, "chart-frame") || contains(t23_card, "class=\"metric-chart"),
                    "t23 lost the chart its preview approves");
      }
      remove_report_directory(directory, run);
    }
  }

  // --- 12. Metadata cards, per review-plan 1.5-1.8 ------------------------
  {
    // Four cards with a fixed set of rows each. The rows are the contract: a reader looking
    // for the run's id, the camera's backend or the trigger channel has one place to find
    // each, and a missing row reads as missing information rather than as "not applicable".
    v4l2diag::RunResult run;
    run.started_at_utc = "2026-08-04T12:02:38Z";
    run.finished_at_utc = "2026-08-04T12:04:00Z";
    run.host_name = "diag-host";
    run.kernel_release = "5.15.0-139-generic";
    run.kernel_version = "#1 SMP";
    run.run_id = "web-run-7";
    run.trigger_mode = v4l2diag::TriggerMode::Hardware;
    run.trigger_profile_id = "anvil";
    run.trigger_profile_file = "anvil.json";
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";
    camera.role = "master";
    camera.trigger_description = "GPIO line 17";
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Pass, "t01-a", "Alpha"));
    run.cameras.push_back(camera);

    const std::string directory = make_temp_dir();
    bool wrote = true;
    try {
      v4l2diag::write_reports(run, directory);
    } catch (const std::exception &error) {
      wrote = check(false, std::string("write_reports threw: ") + error.what());
    }
    if (wrote) {
      const std::string html =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));

      // 1.5 RUN: Started, Finished, Duration, Run ID.
      for (const char *key : {"Started", "Finished", "Duration", "Run ID"}) {
        ok &= check(contains(html, std::string("class=\"k\">") + key + "<"),
                    std::string("the RUN card is missing the \"") + key + "\" row");
      }
      ok &= check(contains(html, "web-run-7"), "the RUN card does not show the run id");

      // 1.6 SYSTEM: Host, Kernel release, Kernel version.
      for (const char *key : {"Host", "Kernel release", "Kernel version"}) {
        ok &= check(contains(html, std::string("class=\"k\">") + key + "<"),
                    std::string("the SYSTEM card is missing the \"") + key + "\" row");
      }

      // 1.7 CAMERA: Device, Trigger Profile, Backend. The Trigger Profile row is always
      // shown -- under free-run its value is "Not required (free-run)", never hidden.
      for (const char *key : {"Device", "Trigger Profile", "Backend"}) {
        ok &= check(contains(html, std::string("class=\"k\">") + key + "<"),
                    std::string("the CAMERA card is missing the \"") + key + "\" row");
      }

      // 1.8 TRIGGER: Mode, Channel, and under a triggered mode the rate and width too.
      for (const char *key : {"Mode", "Channel", "Nominal pulse rate", "Pulse width"}) {
        ok &= check(contains(html, std::string("class=\"k\">") + key + "<"),
                    std::string("the TRIGGER card is missing the \"") + key + "\" row");
      }
      ok &= check(contains(html, "GPIO line 17"), "the TRIGGER card does not show the channel");

      // Each backend named once, in run order. Deduplicating by substring against the
      // accumulated string matched "MMAP" inside "MMAP, ..." and appended it again for
      // every test, producing a 26-entry list on a single-backend run.
      ok &= check(count_of(html, "MMAP") >= 1, "the CAMERA card does not name the backend");
      ok &= check(!contains(html, "MMAP, MMAP"), "the CAMERA card repeats a backend");

      // 1.8: no CONFIGURATION NOTE or any field serving the same purpose.
      ok &= check(!contains(html, "meta-note"), "the metadata cards still carry an explanatory note field");
      ok &= check(!contains(html, "is the profile"), "the pulse-rate explanation paragraph survived; 1.8 forbids it");

      remove_report_directory(directory, run);
    }

    // Two backends: both named, in run order, each exactly once.
    v4l2diag::RunResult two = run;
    two.cameras[0].tests.clear();
    two.cameras[0].tests.push_back(test_of(v4l2diag::TestStatus::Pass, "t01-a", "Alpha"));
    v4l2diag::TestResult on_dmabuf = test_of(v4l2diag::TestStatus::Pass, "t02-b", "Beta");
    on_dmabuf.memory_backend = "dmabuf";
    two.cameras[0].tests.push_back(on_dmabuf);
    two.cameras[0].tests.push_back(test_of(v4l2diag::TestStatus::Pass, "t03-c", "Gamma"));

    const std::string two_dir = make_temp_dir();
    bool wrote_two = true;
    try {
      v4l2diag::write_reports(two, two_dir);
    } catch (const std::exception &error) {
      wrote_two = check(false, std::string("write_reports threw: ") + error.what());
    }
    if (wrote_two) {
      const std::string html =
          read_file(two_dir + "/" + v4l2diag::report_artifact_filename(naming_of(two), v4l2diag::ReportFormat::Html));
      const std::size_t at = html.find("class=\"k\">Backend</span>");
      ok &= check(at != std::string::npos, "the CAMERA card lost its Backend row");
      if (at != std::string::npos) {
        const std::string value = html.substr(at, 90);
        ok &= check(contains(value, "MMAP, DMABUF"),
                    "the Backend row does not name both backends in run order: " + value.substr(0, 70));
      }
      remove_report_directory(two_dir, two);
    }
  }

  // --- 13. Test Results Overview, per review-plan 3.1-3.10 ----------------
  {
    // The Overview was a single flat table with Test/Backend/Status/Duration/Summary. The
    // approved design groups it by backend, drops the Backend and Summary columns, and puts
    // a link to each test's detailed card in a DETAILS column.
    v4l2diag::RunResult run;
    run.started_at_utc = "2026-08-04T12:02:38Z";
    run.finished_at_utc = "2026-08-04T12:04:00Z";
    v4l2diag::CameraRunResult camera;
    camera.camera_path = "/dev/video0";
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Pass, "t01-device-compliance", "V4L2 Device Compliance"));
    camera.tests.push_back(test_of(v4l2diag::TestStatus::Skipped, "t04-no-streamon", "Frame Capture without STREAMON"));
    v4l2diag::TestResult on_dmabuf =
        test_of(v4l2diag::TestStatus::Warn, "t12-dmabuf-cache-sync", "DMABUF CPU Read Synchronization");
    on_dmabuf.memory_backend = "dmabuf";
    camera.tests.push_back(on_dmabuf);
    run.cameras.push_back(camera);

    const std::string directory = make_temp_dir();
    bool wrote = true;
    try {
      v4l2diag::write_reports(run, directory);
    } catch (const std::exception &error) {
      wrote = check(false, std::string("write_reports threw: ") + error.what());
    }
    if (wrote) {
      const std::string html =
          read_file(directory + "/" + v4l2diag::report_artifact_filename(naming_of(run), v4l2diag::ReportFormat::Html));
      const std::size_t ov_at = html.find("Test Results Overview");
      const std::size_t det_at = html.find("<h2>Detailed Results</h2>");
      ok &= check(ov_at != std::string::npos && det_at != std::string::npos && ov_at < det_at,
                  "the Overview section is missing or out of order");
      const std::string overview = html.substr(ov_at, det_at - ov_at);

      // 3.1/3.2/3.12: grouped by backend, one full-width band per backend.
      ok &= check(count_of(overview, "class=\"backend\"") == 2,
                  "the Overview does not carry one backend band per backend, got " +
                      std::to_string(count_of(overview, "class=\"backend\"")));
      // 3.1/3.12: one table per backend group, not one table for everything.
      ok &= check(count_of(overview, "<table class=\"overview\"") == 2,
                  "the Overview does not carry one table per backend group");

      // 3.4: exactly these four columns. The Backend column moved into the band; the
      // Summary column became DETAILS.
      const std::vector<std::string> want = {"Test", "Status", "Duration", "Details"};
      // Search for "<th>" exactly: "<th" also matches "<thead", which made the first
      // column come back as "<tr><th>Test".
      std::vector<std::string> got;
      for (std::size_t at = overview.find("<th>"); at != std::string::npos; at = overview.find("<th>", at + 1)) {
        const std::size_t close = overview.find("</th>", at);
        if (close == std::string::npos) {
          break;
        }
        const std::string text = overview.substr(at + 4, close - at - 4);
        if (!text.empty() && std::find(got.begin(), got.end(), text) == got.end()) {
          got.push_back(text);
        }
      }
      ok &= check(got == want, "the Overview columns are wrong");
      ok &= check(!contains(overview, "<th>Backend</th>"), "the Backend column survived");
      ok &= check(!contains(overview, "<th>Summary</th>"), "the Summary column survived");

      // 3.5: "T01 - V4L2 Device Compliance", not the technical slug.
      ok &= check(contains(overview, "T01 - V4L2 Device Compliance"),
                  "the Test column does not show the number and readable name");
      ok &= check(!contains(overview, ">t01-device-compliance<"), "the Test column still shows the technical slug");

      // 3.8: one link per row, pointing at that test's card anchor.
      ok &= check(count_of(overview, "View detailed result") == 3,
                  "expected one \"View detailed result\" link per test row, got " +
                      std::to_string(count_of(overview, "View detailed result")));
      for (const char *anchor : {"#result-mmap-t01-device-compliance", "#result-mmap-t04-no-streamon",
                                 "#result-dmabuf-t12-dmabuf-cache-sync"}) {
        ok &= check(contains(overview, std::string("href=\"") + anchor + "\""),
                    std::string("the Overview does not link ") + anchor);
      }

      // 3.6: the visible status words, and no dot or icon beside them.
      for (const char *cls : {"status-cell pass\"", "status-cell warn\"", "status-cell skip\""}) {
        ok &= check(contains(overview, cls), std::string("the Overview is missing the class ") + cls);
      }
      for (const char *word : {">PASS<", ">WARN<", ">SKIP<"}) {
        ok &= check(contains(overview, word), std::string("the Overview is missing ") + word);
      }
      ok &= check(!contains(overview, ">skipped<"), "the wire spelling reached the Overview");

      // 3.9: on a narrow screen each cell names its field.
      ok &= check(contains(html, "data-label=\"Status\"") || contains(html, "content: \"Status: \""),
                  "the mobile Overview layout does not label its fields");

      // 3.10: the table head repeats on continuation pages and a row is never split.
      ok &= check(contains(html, "display: table-header-group"),
                  "the Overview table head does not repeat on print continuation pages");
      ok &=
          check(contains(html, "table.overview tr { break-inside: avoid"), "an Overview row may be split across pages");

      remove_report_directory(directory, run);
    }
  }

  if (ok) {
    std::cout << "result_card_template_test: all checks passed\n";
  }
  return ok ? 0 : 1;
}
