#include "v4l2diag/core/report_writer.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-report-test-XXXXXX";
  char *buffer = new char[pattern.size() + 1];
  std::copy(pattern.begin(), pattern.end(), buffer);
  buffer[pattern.size()] = '\0';
  char *created = mkdtemp(buffer);
  std::string result = created ? created : "/tmp/v4l2diag-report-test";
  delete[] buffer;
  return result;
}

bool exists(const std::string &path) {
  std::ifstream in(path);
  return static_cast<bool>(in);
}

}  // namespace

int main() {
  v4l2diag::RunResult result;
  result.started_at_utc = "2026-01-01T00:00:00Z";
  result.finished_at_utc = "2026-01-01T00:00:01Z";
  result.host_name = "test-host";
  v4l2diag::CameraRunResult camera;
  camera.camera_path = "/dev/video-test";
  camera.profile_id = "test";
  camera.memory_backends.push_back(v4l2diag::MemoryBackend::Mmap);
  v4l2diag::TestResult test;
  test.id = "test";
  test.name = "Test";
  test.category = "unit";
  test.status = v4l2diag::TestStatus::Pass;
  test.summary = "ok";
  camera.tests.push_back(test);
  result.cameras.push_back(camera);

  const std::string dir = make_temp_dir();
  const auto artifacts = v4l2diag::write_reports(result,
                                                 {v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown,
                                                  v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Pdf},
                                                 dir);

  if (artifacts.size() != 4 || !exists(dir + "/diagnostic-report.json") || !exists(dir + "/diagnostic-report.md") ||
      !exists(dir + "/diagnostic-report.html") || !exists(dir + "/diagnostic-report.pdf")) {
    std::cerr << "missing report artifact\n";
    return 1;
  }

  return 0;
}
