#include "v4l2diag/core/report_writer.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

namespace v4l2diag {

namespace {

bool ensure_directory(const std::string &path) {
  std::string partial;
  for (char c : path) {
    partial.push_back(c);
    if (c == '/' && partial.size() > 1) {
      mkdir(partial.c_str(), 0755);
    }
  }
  return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

std::string json_escape(const std::string &value) {
  std::ostringstream out;
  for (char c : value) {
    switch (c) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << c;
        break;
    }
  }
  return out.str();
}

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

void write_json(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  out << std::fixed << std::setprecision(3);
  out << "{\n";
  out << "  \"project\": \"" << json_escape(result.project_name) << "\",\n";
  out << "  \"started_at_utc\": \"" << json_escape(result.started_at_utc) << "\",\n";
  out << "  \"finished_at_utc\": \"" << json_escape(result.finished_at_utc) << "\",\n";
  out << "  \"host_name\": \"" << json_escape(result.host_name) << "\",\n";
  out << "  \"run_mode\": \"" << to_string(result.run_mode) << "\",\n";
  out << "  \"cameras\": [\n";
  for (std::size_t ci = 0; ci < result.cameras.size(); ++ci) {
    const auto &camera = result.cameras[ci];
    out << "    {\n";
    out << "      \"path\": \"" << json_escape(camera.camera_path) << "\",\n";
    out << "      \"profile_id\": \"" << json_escape(camera.profile_id) << "\",\n";
    out << "      \"trigger_mode\": \"" << to_string(camera.trigger_mode) << "\",\n";
    out << "      \"trigger_channel_id\": \"" << json_escape(camera.trigger_channel_id) << "\",\n";
    out << "      \"trigger_description\": \"" << json_escape(camera.trigger_description) << "\",\n";
    out << "      \"memory_backends\": [";
    for (std::size_t bi = 0; bi < camera.memory_backends.size(); ++bi) {
      if (bi) {
        out << ", ";
      }
      out << "\"" << to_string(camera.memory_backends[bi]) << "\"";
    }
    out << "],\n";
    out << "      \"tests\": [\n";
    for (std::size_t ti = 0; ti < camera.tests.size(); ++ti) {
      const auto &test = camera.tests[ti];
      out << "        {\n";
      out << "          \"id\": \"" << json_escape(test.id) << "\",\n";
      out << "          \"name\": \"" << json_escape(test.name) << "\",\n";
      out << "          \"category\": \"" << json_escape(test.category) << "\",\n";
      out << "          \"memory_backend\": \"" << json_escape(test.memory_backend) << "\",\n";
      out << "          \"status\": \"" << to_string(test.status) << "\",\n";
      out << "          \"summary\": \"" << json_escape(test.summary) << "\",\n";
      out << "          \"duration_ms\": " << test.duration_ms << ",\n";
      out << "          \"metrics\": [";
      for (std::size_t mi = 0; mi < test.metrics.size(); ++mi) {
        const auto &metric = test.metrics[mi];
        if (mi) {
          out << ", ";
        }
        out << "{\"name\":\"" << json_escape(metric.name) << "\",\"unit\":\"" << json_escape(metric.unit)
            << "\",\"value\":" << metric.value << ",\"description\":\"" << json_escape(metric.description) << "\"}";
      }
      out << "],\n";
      out << "          \"details\": [";
      for (std::size_t di = 0; di < test.details.size(); ++di) {
        if (di) {
          out << ", ";
        }
        out << "\"" << json_escape(test.details[di]) << "\"";
      }
      out << "]\n";
      out << "        }" << (ti + 1 == camera.tests.size() ? "" : ",") << "\n";
    }
    out << "      ]\n";
    out << "    }" << (ci + 1 == result.cameras.size() ? "" : ",") << "\n";
  }
  out << "  ]\n";
  out << "}\n";
}

void write_markdown(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  out << "# V4L2 Camera Diagnostic Report\n\n";
  out << "- Project: `" << result.project_name << "`\n";
  out << "- Started: `" << result.started_at_utc << "`\n";
  out << "- Finished: `" << result.finished_at_utc << "`\n";
  out << "- Host: `" << result.host_name << "`\n";
  out << "- Run mode: `" << to_string(result.run_mode) << "`\n\n";

  for (const auto &camera : result.cameras) {
    out << "## Camera `" << camera.camera_path << "`\n\n";
    out << "- Profile: `" << camera.profile_id << "`\n";
    out << "- Trigger mode: `" << to_string(camera.trigger_mode) << "`\n";
    if (!camera.trigger_channel_id.empty()) {
      out << "- Trigger channel: `" << camera.trigger_channel_id << "`\n";
    }
    if (!camera.trigger_description.empty()) {
      out << "- Trigger detail: `" << camera.trigger_description << "`\n";
    }
    out << "- Backends:";
    for (auto backend : camera.memory_backends) {
      out << " `" << to_string(backend) << "`";
    }
    out << "\n\n";
    out << "| Test | Backend | Category | Status | Duration ms | Summary |\n";
    out << "| --- | --- | --- | --- | ---: | --- |\n";
    for (const auto &test : camera.tests) {
      out << "| `" << test.id << "` | `" << test.memory_backend << "` | " << test.category << " | "
          << to_string(test.status) << " | " << std::fixed << std::setprecision(3) << test.duration_ms << " | "
          << test.summary << " |\n";
    }
    out << "\n";
    for (const auto &test : camera.tests) {
      if (test.metrics.empty() && test.details.empty()) {
        continue;
      }
      out << "### " << test.id << "\n\n";
      if (!test.metrics.empty()) {
        out << "| Metric | Value | Unit | Description |\n";
        out << "| --- | ---: | --- | --- |\n";
        for (const auto &metric : test.metrics) {
          out << "| " << metric.name << " | " << metric.value << " | " << metric.unit << " | " << metric.description
              << " |\n";
        }
        out << "\n";
      }
      for (const auto &detail : test.details) {
        out << "- " << detail << "\n";
      }
      out << "\n";
    }
  }
}

void write_html(const RunResult &result, const std::string &path) {
  std::ofstream out(path);
  out << R"(<!doctype html><html lang="en"><head><meta charset="utf-8">
<title>V4L2 Camera Diagnostic Report</title>
<style>
:root { --pass: #16a34a; --fail: #dc2626; --warn: #d97706; --skip: #64748b; }
* { box-sizing: border-box; }
body { font-family: 'Inter', -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
       margin: 0; padding: 0; background: #f8fafc; color: #1e293b; line-height: 1.5; }
.container { max-width: 1100px; margin: 0 auto; padding: 40px 32px; }
.header { background: linear-gradient(135deg, #0f172a, #1e293b); color: white; padding: 48px 40px; border-radius: 12px; margin-bottom: 32px; }
.header h1 { margin: 0 0 8px; font-size: 28px; font-weight: 800; }
.header .subtitle { color: #94a3b8; font-size: 14px; margin: 0; }
.meta-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 12px; margin-top: 24px; }
.meta-item { background: rgba(255,255,255,0.06); border-radius: 8px; padding: 12px 16px; }
.meta-item .label { color: #94a3b8; font-size: 11px; text-transform: uppercase; letter-spacing: 0.05em; font-weight: 600; }
.meta-item .value { color: #f1f5f9; font-size: 14px; font-weight: 600; margin-top: 4px; }

.summary-bar { display: flex; gap: 12px; flex-wrap: wrap; margin-bottom: 32px; }
.summary-badge { display: flex; align-items: center; gap: 8px; padding: 10px 18px; border-radius: 8px; font-weight: 700; font-size: 14px; }
.summary-badge.pass { background: #f0fdf4; color: var(--pass); border: 1px solid #bbf7d0; }
.summary-badge.fail { background: #fef2f2; color: var(--fail); border: 1px solid #fecaca; }
.summary-badge.warn { background: #fffbeb; color: var(--warn); border: 1px solid #fde68a; }
.summary-badge.skip { background: #f8fafc; color: var(--skip); border: 1px solid #e2e8f0; }
.summary-badge .count { font-size: 22px; font-weight: 800; }

.section { background: white; border: 1px solid #e2e8f0; border-radius: 10px; margin-bottom: 24px; overflow: hidden; }
.section-header { padding: 16px 24px; border-bottom: 1px solid #e2e8f0; display: flex; align-items: center; gap: 12px; }
.section-header h2 { margin: 0; font-size: 18px; }

table.overview { width: 100%; border-collapse: collapse; font-size: 13px; }
table.overview th { background: #f8fafc; padding: 12px 16px; text-align: left; font-weight: 600;
                    color: #64748b; font-size: 11px; text-transform: uppercase; letter-spacing: 0.05em;
                    border-bottom: 1px solid #e2e8f0; }
table.overview td { padding: 12px 16px; border-bottom: 1px solid #f1f5f9; }
table.overview tr:last-child td { border-bottom: none; }
table.overview tr:hover { background: #f8fafc; }
table.overview .test-id { font-family: 'JetBrains Mono', monospace; font-weight: 600; color: #1e293b; }
table.overview .status-cell { font-weight: 700; font-size: 12px; text-transform: uppercase; }
table.overview .status-cell.pass { color: var(--pass); }
table.overview .status-cell.fail { color: var(--fail); }
table.overview .status-cell.warn { color: var(--warn); }
table.overview .status-cell.skipped { color: var(--skip); }
table.overview .duration { color: #64748b; font-family: monospace; }
table.overview .summary-text { color: #475569; }

.test-section { border: 1px solid #e2e8f0; border-radius: 8px; margin: 16px 24px; overflow: hidden; }
.test-section-header { padding: 12px 16px; display: flex; align-items: center; gap: 10px; border-bottom: 1px solid #f1f5f9; }
.test-section-header.pass { background: #f0fdf4; border-left: 4px solid var(--pass); }
.test-section-header.fail { background: #fef2f2; border-left: 4px solid var(--fail); }
.test-section-header.warn { background: #fffbeb; border-left: 4px solid var(--warn); }
.test-section-header.skipped { background: #f8fafc; border-left: 4px solid var(--skip); }
.test-section-header h3 { margin: 0; font-size: 14px; }
.test-section-header .badge { padding: 3px 8px; border-radius: 4px; font-size: 11px; font-weight: 700; text-transform: uppercase; }
.test-section-header .badge.pass { background: #dcfce7; color: var(--pass); }
.test-section-header .badge.fail { background: #fee2e2; color: var(--fail); }
.test-section-header .badge.warn { background: #fef3c7; color: var(--warn); }
.test-section-header .badge.skipped { background: #f1f5f9; color: var(--skip); }
.test-body { padding: 16px; }
.test-body .metrics-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(180px, 1fr)); gap: 8px; margin-bottom: 12px; }
.metric-card { background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; padding: 10px 12px; }
.metric-card .m-name { font-size: 11px; color: #64748b; font-weight: 500; }
.metric-card .m-value { font-size: 16px; font-weight: 700; color: #1e293b; margin-top: 2px; font-family: monospace; }
.metric-card .m-unit { font-size: 11px; color: #94a3b8; }
.detail-list { background: #f8fafc; border-radius: 6px; padding: 12px 16px; font-family: 'JetBrains Mono', monospace;
               font-size: 12px; line-height: 1.8; color: #475569; white-space: pre-wrap; }
.warnings-box { background: #fffbeb; border: 1px solid #fde68a; border-radius: 6px; padding: 10px 14px; margin-top: 8px;
                color: #92400e; font-size: 13px; }
.footer { text-align: center; color: #94a3b8; font-size: 12px; margin-top: 40px; padding-top: 24px; border-top: 1px solid #e2e8f0; }
.export-pdf-btn { position: fixed; top: 20px; right: 20px; display: flex; align-items: center; gap: 8px;
                  background: #0f172a; color: white; border: none; border-radius: 8px; padding: 10px 18px;
                  font-size: 13px; font-weight: 700; font-family: inherit; cursor: pointer; box-shadow: 0 4px 12px rgba(0,0,0,0.2); }
.export-pdf-btn:hover { background: #1e293b; }
@media print { body { background: white; } .container { padding: 20px; } .header { break-inside: avoid; }
               .test-section { break-inside: avoid; } .export-pdf-btn { display: none; }
               @page { margin: 15mm 10mm; size: A4; } }
</style></head><body>
<button class="export-pdf-btn" onclick='window.print()'>Export as PDF</button>
<div class="container">
)";

  // Header
  out << "<div class=\"header\"><h1>V4L2 Camera Diagnostic Report</h1>";
  out << "<p class=\"subtitle\">Automated hardware diagnostic test results</p>";
  out << "<div class=\"meta-grid\">";
  out << "<div class=\"meta-item\"><div class=\"label\">Started</div><div class=\"value\">"
      << html_escape(result.started_at_utc) << "</div></div>";
  out << "<div class=\"meta-item\"><div class=\"label\">Finished</div><div class=\"value\">"
      << html_escape(result.finished_at_utc) << "</div></div>";
  out << "<div class=\"meta-item\"><div class=\"label\">Host</div><div class=\"value\">"
      << html_escape(result.host_name) << "</div></div>";
  if (!result.cameras.empty()) {
    out << "<div class=\"meta-item\"><div class=\"label\">Camera</div><div class=\"value\">"
        << html_escape(result.cameras[0].camera_path) << "</div></div>";
    out << "<div class=\"meta-item\"><div class=\"label\">Profile</div><div class=\"value\">"
        << html_escape(result.cameras[0].profile_id) << "</div></div>";
    out << "<div class=\"meta-item\"><div class=\"label\">Trigger</div><div class=\"value\">"
        << to_string(result.cameras[0].trigger_mode) << "</div></div>";
    if (!result.cameras[0].trigger_description.empty()) {
      out << "<div class=\"meta-item\"><div class=\"label\">Trigger Channel</div><div class=\"value\">"
          << html_escape(result.cameras[0].trigger_description) << "</div></div>";
    }
  }
  out << "</div></div>";

  for (const auto &camera : result.cameras) {
    // Count statuses
    int pass_count = 0, fail_count = 0, warn_count = 0, skip_count = 0;
    for (const auto &t : camera.tests) {
      switch (t.status) {
        case TestStatus::Pass:
          pass_count++;
          break;
        case TestStatus::Fail:
          fail_count++;
          break;
        case TestStatus::Warn:
          warn_count++;
          break;
        case TestStatus::Skipped:
          skip_count++;
          break;
      }
    }

    // Summary badges
    out << "<div class=\"summary-bar\">";
    if (pass_count > 0)
      out << "<div class=\"summary-badge pass\"><span class=\"count\">" << pass_count << "</span> Passed</div>";
    if (fail_count > 0)
      out << "<div class=\"summary-badge fail\"><span class=\"count\">" << fail_count << "</span> Failed</div>";
    if (warn_count > 0)
      out << "<div class=\"summary-badge warn\"><span class=\"count\">" << warn_count << "</span> Warnings</div>";
    if (skip_count > 0)
      out << "<div class=\"summary-badge skip\"><span class=\"count\">" << skip_count << "</span> Skipped</div>";
    out << "</div>";

    // Overview table
    out << "<div class=\"section\"><div class=\"section-header\"><h2>Test Results Overview</h2></div>";
    out << "<table "
           "class=\"overview\"><thead><tr><th>Test</th><th>Backend</th><th>Status</th><th>Duration</th><th>Summary</"
           "th></tr></thead><tbody>";
    for (const auto &test : camera.tests) {
      const std::string status = to_string(test.status);
      out << "<tr><td class=\"test-id\">" << html_escape(test.id) << "</td>";
      out << "<td>" << html_escape(test.memory_backend) << "</td>";
      out << "<td class=\"status-cell " << status << "\">" << status << "</td>";
      out << "<td class=\"duration\">" << std::fixed << std::setprecision(0) << test.duration_ms << "ms</td>";
      out << "<td class=\"summary-text\">" << html_escape(test.summary) << "</td></tr>";
    }
    out << "</tbody></table></div>";

    // Detailed per-test sections
    out << "<div class=\"section\"><div class=\"section-header\"><h2>Detailed Results</h2></div><div "
           "style=\"padding:8px 0\">";
    for (const auto &test : camera.tests) {
      const std::string status = to_string(test.status);
      out << "<div class=\"test-section\">";
      out << "<div class=\"test-section-header " << status << "\">";
      out << "<span class=\"badge " << status << "\">" << status << "</span>";
      out << "<h3>" << html_escape(test.id) << " &mdash; " << html_escape(test.name) << "</h3>";
      out << "<span style=\"margin-left:auto;color:#64748b;font-size:12px\">" << std::fixed << std::setprecision(0)
          << test.duration_ms << "ms</span>";
      out << "</div><div class=\"test-body\">";

      // Metrics grid
      if (!test.metrics.empty()) {
        out << "<div class=\"metrics-grid\">";
        for (const auto &m : test.metrics) {
          out << "<div class=\"metric-card\"><div class=\"m-name\">" << html_escape(m.name) << "</div>";
          out << "<div class=\"m-value\">" << std::fixed << std::setprecision(3) << m.value;
          out << " <span class=\"m-unit\">" << html_escape(m.unit) << "</span></div></div>";
        }
        out << "</div>";
      }

      // Details
      if (!test.details.empty()) {
        out << "<div class=\"detail-list\">";
        for (const auto &d : test.details)
          out << html_escape(d) << "\n";
        out << "</div>";
      }

      // Warnings
      if (!test.warnings.empty()) {
        out << "<div class=\"warnings-box\">";
        for (const auto &w : test.warnings)
          out << "⚠ " << html_escape(w) << "<br>";
        out << "</div>";
      }

      out << "</div></div>";
    }
    out << "</div></div>";
  }

  out << "<div class=\"footer\">Generated by v4l2-camera-diagnostic</div>";
  out << "</div></body></html>\n";
}

void write_pdf(const RunResult &result, const std::string &path) {
  // Strategy: generate the professional HTML report to a temp file, then convert
  // to PDF using wkhtmltopdf. If wkhtmltopdf is unavailable, fall back to writing
  // the HTML directly with .pdf extension (browsers handle it gracefully).
  const std::string html_tmp = path + ".tmp.html";
  write_html(result, html_tmp);

  // Try wkhtmltopdf first (best quality)
  const std::string cmd =
      "wkhtmltopdf --quiet --enable-local-file-access --page-size A4 --margin-top 10mm "
      "--margin-bottom 10mm --margin-left 10mm --margin-right 10mm "
      "\"" +
      html_tmp + "\" \"" + path + "\" 2>/dev/null";
  const int ret = std::system(cmd.c_str());
  if (ret == 0) {
    std::remove(html_tmp.c_str());
    return;
  }

  // Fallback: try weasyprint
  const std::string cmd2 = "weasyprint \"" + html_tmp + "\" \"" + path + "\" 2>/dev/null";
  const int ret2 = std::system(cmd2.c_str());
  if (ret2 == 0) {
    std::remove(html_tmp.c_str());
    return;
  }

  // Last fallback: rename HTML as PDF (browsers will still render it)
  std::rename(html_tmp.c_str(), path.c_str());
}

}  // namespace

std::vector<ReportArtifact> write_reports(const RunResult &result, const std::vector<ReportFormat> &formats,
                                          const std::string &output_directory) {
  ensure_directory(output_directory);
  std::vector<ReportArtifact> artifacts;
  for (ReportFormat format : formats) {
    const std::string path = output_directory + "/diagnostic-report." + to_string(format);
    switch (format) {
      case ReportFormat::Json:
        write_json(result, path);
        break;
      case ReportFormat::Markdown:
        write_markdown(result, output_directory + "/diagnostic-report.md");
        artifacts.push_back({format, output_directory + "/diagnostic-report.md"});
        continue;
      case ReportFormat::Html:
        write_html(result, path);
        break;
      case ReportFormat::Pdf:
        write_pdf(result, path);
        break;
    }
    artifacts.push_back({format, path});
  }
  return artifacts;
}

}  // namespace v4l2diag
