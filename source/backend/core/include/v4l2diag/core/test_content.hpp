#pragma once

#include <string>
#include <vector>

#include "v4l2diag/core/types.hpp"

namespace v4l2diag {

// Per-test card content (plan 3.1, review round 2 item 6).
//
// The OUTER shell is shared (see result_card.hpp). What goes inside is NOT: the approved
// previews give each test its own table with its own column schema -- 30 distinct header
// sets across 22 previews -- because each test measures something different. "Resolution |
// Coverage | Mean | P95 | State" is T19's evidence; it means nothing for T10, whose
// evidence is "Group | Flag | Observed | Meaning | State".
//
// A renderer reads ONLY the structured metrics and details it is given. When a value is
// absent it says so; it never prints a plausible number. That is the difference between a
// report and a mock-up.

// Whether a dedicated renderer exists for this technical test id.
//
// Keyed on the test id ("t13-poll-timeout-cliff"), never on a preview filename
// ("t13-unified") -- a registry keyed on the latter would never match a real run.
bool has_test_content_renderer(const std::string &test_id);

// Every registered test id, for tests and for auditing coverage.
std::vector<std::string> registered_test_content_ids();

// The card body for this test: its tables, its metric definitions, its conditional
// sections. Falls back to an honest generic rendering for an unregistered test rather than
// dropping the evidence.
std::string render_test_content(const TestResult &test);

// Whether this test may render metric charts.
//
// An allow-list drawn from the approved previews: a test qualifies iff one of its preview
// cards carries a chart. The chart selector otherwise picks up any statistic family
// (_mean_ms / _p95_ms / _max_ms) automatically, which gave eight tests a dot chart their

// Whether this test's card shows a "Result" block. Only on non-PASS cards: a passing
// card's header already states the verdict.
bool test_content_shows_result(const std::string &test_id, TestStatus status);

// The verdict line alone -- `<div class="result-warn">Warned: ...</div>` and friends --
// with no measurement content after it.
//
// A skipped test measured nothing, so its card is this line and nothing else: that is what
// all seven SKIP cards in the approved previews show. The caller uses this instead of
// render_test_content() rather than in addition to it; render_test_content() emits the same
// block itself for a non-PASS card.
std::string render_test_result_block(const TestResult &test);

}  // namespace v4l2diag
