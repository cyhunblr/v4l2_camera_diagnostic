// PDF is no longer a report format (report-ui-review-plan.md §6.2): the product
// generates HTML and the user prints it. These checks lock the removal and that
// legacy *web run requests* still holding "pdf" degrade gracefully instead of
// breaking a run.
//
// Scope note: this covers run_config_from_json() and write_reports(). Stored
// profile configs are covered by report_formats_removal_test.cpp, which locks the
// v4 -> v5 migration -- the field is dropped from the profile schema entirely, so
// the old "parses to an empty list" concern no longer exists.
#include "v4l2diag/core/report_naming.hpp"
#include "v4l2diag/core/report_writer.hpp"
#include "v4l2diag/core/run_config_json.hpp"
#include "v4l2diag/core/types.hpp"

#include <json/json.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

bool check(bool condition, const char *what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-formats-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : "/tmp/v4l2diag-formats-test";
}

v4l2diag::RunConfig from(const std::string &text) {
  Json::CharReaderBuilder builder;
  Json::Value root;
  std::string errors;
  std::istringstream in(text);
  Json::parseFromStream(builder, in, &root, &errors);
  return v4l2diag::run_config_from_json(root, "/tmp/reports", "/tmp/config", "run-1");
}

// Artifact format identity is preserved by v5; only the user preference went.
bool has_artifact(const std::vector<v4l2diag::ReportArtifact> &artifacts, v4l2diag::ReportFormat wanted) {
  return std::any_of(artifacts.begin(), artifacts.end(),
                     [&](const v4l2diag::ReportArtifact &a) { return a.format == wanted; });
}

std::string read_file(const std::string &path) {
  std::ifstream in(path);
  if (!in.good()) {
    return std::string();
  }
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

}  // namespace

int main() {
  using v4l2diag::ReportFormat;
  bool ok = true;

  // "pdf" is not a format any more: parsing it fails instead of yielding an
  // enumerator, and the surviving three still parse.
  {
    ReportFormat format = ReportFormat::Json;
    ok &= check(!v4l2diag::parse_report_format("pdf", &format), "\"pdf\" is still accepted as a report format");
    ok &= check(!v4l2diag::parse_report_format("PDF", &format), "\"PDF\" is still accepted as a report format");
    ok &= check(format == ReportFormat::Json, "a rejected format overwrote the caller's value");

    for (const char *name : {"json", "md", "markdown", "html"}) {
      ok &= check(v4l2diag::parse_report_format(name, &format), "a supported format stopped parsing");
    }
  }

  // No enumerator may serialise to "pdf" any more.
  {
    for (ReportFormat format : {ReportFormat::Json, ReportFormat::Markdown, ReportFormat::Html}) {
      ok &= check(std::string(v4l2diag::to_string(format)) != "pdf", "a format still serialises to \"pdf\"");
    }
  }

  // --- Legacy request resilience ------------------------------------------
  //
  // v5 removed the field (plan 2.10): a run request no longer selects formats. An
  // archived request that still carries the key must stay runnable -- the key is
  // ignored, not rejected. Nothing about the run's artifacts depends on it.
  {
    for (const char *body : {R"({"report_formats":["json","pdf","html"]})", R"({"report_formats":["pdf"]})",
                             R"({"report_formats":["PDF","xps","json"]})", R"({"report_formats":[]})"}) {
      const auto config = from(body);
      // The parser accepted the document and produced a usable run config; the only
      // observable proof left is that nothing else was disturbed.
      if (!check(config.output_directory.find("/tmp/reports") == 0,
                 "a legacy report_formats key broke the rest of the request")) {
        std::cerr << "  body was: " << body << "\n";
        ok = false;
      }
      // The runtime model has no such field to be influenced -- RunConfig dropped it in
      // v5 -- so the key can only ever be inert. The artifact set is fixed instead:
      // whatever the request asked for, a run writes all three (checked below).
    }
  }

  // --- Every run writes all three artifacts -------------------------------
  {
    const std::string dir = make_temp_dir();
    v4l2diag::RunResult result;
    result.started_at_utc = "2026-08-03T00:00:00Z";
    result.finished_at_utc = "2026-08-03T00:00:01Z";

    // The no-format overload is what a run uses: HTML, JSON and Markdown, always.
    const auto artifacts = v4l2diag::write_reports(result, dir);
    ok &= check(artifacts.size() == 3, "a run did not produce exactly three artifacts");
    // Artifact format identity survives (plan 2.10): each file says what it is.
    ok &= check(has_artifact(artifacts, ReportFormat::Html), "no HTML artifact was produced");
    ok &= check(has_artifact(artifacts, ReportFormat::Json), "no JSON artifact was produced");
    ok &= check(has_artifact(artifacts, ReportFormat::Markdown), "no Markdown artifact was produced");
    for (const auto &artifact : artifacts) {
      ok &= check(artifact.path.size() < 4 || artifact.path.substr(artifact.path.size() - 4) != ".pdf",
                  "write_reports produced a .pdf artifact");
    }

    // The HTML report keeps its Export button, and it must be a plain
    // browser-print trigger -- same behaviour as Ctrl+P.
    //
    // The label is now the final one: "Export PDF" (report-ui-review-plan.md 1.3).
    // Plan item 3.3 renamed the source and this assertion together; the previous
    // "Export as PDF" was a leftover from 2.2 and is asserted gone below.
    v4l2diag::ReportNaming naming;
    naming.started_at_utc = result.started_at_utc;
    naming.trigger_mode = result.trigger_mode;
    naming.trigger_profile_file = result.trigger_profile_file;
    naming.test_configuration_file = result.threshold_config_file;

    const std::string html =
        read_file(dir + "/" + v4l2diag::report_artifact_filename(naming, v4l2diag::ReportFormat::Html));
    ok &= check(!html.empty(), "the HTML report was not written");
    ok &= check(html.find("window.print()") != std::string::npos,
                "the HTML report lost its window.print() export trigger");
    ok &= check(html.find(">Export PDF<") != std::string::npos, "the HTML report lost its Export PDF button");
    ok &= check(html.find("Export as PDF") == std::string::npos, "the pre-3.3 label \"Export as PDF\" survived");
    // Nothing may point at a generated PDF artifact -- neither the old fixed name nor a
    // canonically named one.
    ok &= check(html.find("diagnostic-report.pdf") == std::string::npos,
                "the HTML report still references a generated PDF artifact");
    ok &= check(html.find(".pdf") == std::string::npos, "the HTML report references a .pdf file");

    for (const auto format :
         {v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown, v4l2diag::ReportFormat::Html}) {
      unlink((dir + "/" + v4l2diag::report_artifact_filename(naming, format)).c_str());
    }
    rmdir(dir.c_str());
  }

  return ok ? 0 : 1;
}
