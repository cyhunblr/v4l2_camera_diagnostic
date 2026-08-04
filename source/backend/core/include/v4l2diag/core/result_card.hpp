#pragma once

#include <string>

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

// Detailed Result Card Template (plan 3.1) -- the OUTER shell of a result card.
//
// Deliberately just the shell. The 24 test previews' charts, tables, metric definitions
// and per-test prose are approved test-specific content and stay where they are; what is
// shared is the frame they sit in, because that is what was drifting: the live HTML report
// emitted no backend band, no anchor, a different header order and "skipped" where the
// design says "SKIP".

// `&`, `<`, `>` and `"` as entities. Exposed because the card shell interpolates test
// names and ids, and every caller that builds report HTML needs the same escaping.
std::string html_escape(const std::string &value);

// "interval_mean_ms" -> "Interval mean ms". A metric's key made readable.
//
// Shared because both the card shell and the per-test content renderers label metrics, and
// two copies would drift into two spellings of the same key.
std::string humanize(const std::string &value);

// "ll1_bp0_wi1_mean_ms" -> "LED 1 · BYP 0 · WIN 1".
//
// T18's control combinations. The metric KEYS never change -- docs, the threshold registry
// and the tests reference them -- so the spelling-out happens only at the point of display.
// Shared because both the chart axis and T18's own table label the same combinations.
// Docs: docs/backend/tests/t18-control-sweep.md
std::string control_combo_label(const std::string &name);

// Upper-cased, for the visible backend label and format names.
std::string upper_case(const std::string &value);

// Whether a metric's value is a SENTINEL rather than a measurement.
//
// The runner encodes "could not be measured" as -500, and -1 for a handful of named
// metrics (cliff, safety margin, min reliable) or when the description says so. Rendering
// one as a number puts "-500 ms" in a report, which reads as a real negative measurement.
// Every surface that shows a metric has to apply this, so it lives here.
bool is_sentinel_metric(const MetricValue &metric);

// "t03-pipeline-ready" + "Pipeline Readiness after STREAMON" ->
// "T03 - Pipeline Readiness after STREAMON".
//
// The visible name of a test (review-plan 3.5 / 4.3): its number and readable name, with
// the technical slug left out. The slug stays in the anchor and in the JSON -- it is an
// identifier, not a label. Shared because the Overview row and the card header must spell
// the same test the same way.
std::string test_display_name(const std::string &test_id, const std::string &name);

// The visible status text: PASS / WARN / FAIL / SKIP.
//
// NOT the wire value -- `to_string(TestStatus::Skipped)` is "skipped", which the design
// shortens to "SKIP" on the card. Keeping the two separate is the point: the JSON artifact
// must keep saying "skipped".
std::string card_status_text(TestStatus status);

// The CSS modifier: pass / warn / fail / skip. Matches the approved stylesheet, which
// selects `.test-card.skip` and not `.test-card.skipped`.
std::string card_status_class(TestStatus status);

// `result-<backend>-<test_id>`, with the full test slug and no camera.
//
// No camera on purpose: the same test on two cameras would otherwise produce two anchors
// for one design element, and the report's own links could not name either one. Characters
// that cannot appear in an HTML id are replaced.
std::string result_card_anchor(const std::string &memory_backend, const std::string &test_id);

// The band that opens a run of tests sharing a memory backend -- once per backend, not
// once per test. `first` suppresses the leading gap, which only separates one backend's
// cards from the previous backend's.
std::string render_backend_band(const std::string &memory_backend, bool first);

// The card's opening markup: `<article class="test-card <status>" id="result-...">` plus
// the `STATUS | TEST ID + TEST NAME | DURATION` header. The caller appends the
// test-specific body and then `render_result_card_close()`.
//
// The duration comes from the shared formatter (plan 3.2), so a card reads the same as the
// web UI.
std::string render_result_card_open(const TestResult &test);

// Closes what `render_result_card_open()` opened.
std::string render_result_card_close();

// Whether a card shows a RESULT section.
//
// Only WARN, FAIL and SKIP: on a passing card the header already says PASS, so a "RESULT:
// passed" row underneath it is noise.
bool result_section_visible(TestStatus status);

}  // namespace v4l2diag
