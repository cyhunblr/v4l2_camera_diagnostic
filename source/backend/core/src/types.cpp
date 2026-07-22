#include "v4l2diag/core/types.hpp"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace v4l2diag {

namespace {

std::string lower_copy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

const char *to_string(MemoryBackend backend) {
  switch (backend) {
    case MemoryBackend::Mmap:
      return "mmap";
    case MemoryBackend::Dmabuf:
      return "dmabuf";
    case MemoryBackend::UserPtr:
      return "userptr";
  }
  return "unknown";
}

const char *to_string(ReportFormat format) {
  switch (format) {
    case ReportFormat::Json:
      return "json";
    case ReportFormat::Markdown:
      return "markdown";
    case ReportFormat::Html:
      return "html";
    case ReportFormat::Pdf:
      return "pdf";
  }
  return "unknown";
}

const char *to_string(RunMode mode) {
  switch (mode) {
    case RunMode::Sequential:
      return "sequential";
    case RunMode::Parallel:
      return "parallel";
  }
  return "unknown";
}

const char *to_string(TriggerMode mode) {
  switch (mode) {
    case TriggerMode::Hardware:
      return "hardware";
    case TriggerMode::Software:
      return "software";
    case TriggerMode::FreeRun:
      return "free-run";
  }
  return "free-run";
}

const char *to_string(TestStatus status) {
  switch (status) {
    case TestStatus::Pass:
      return "pass";
    case TestStatus::Fail:
      return "fail";
    case TestStatus::Warn:
      return "warn";
    case TestStatus::Skipped:
      return "skipped";
    case TestStatus::Error:
      return "error";
  }
  return "unknown";
}

bool parse_memory_backend(const std::string &value, MemoryBackend *backend) {
  const std::string lowered = lower_copy(trim(value));
  if (lowered == "mmap") {
    *backend = MemoryBackend::Mmap;
    return true;
  }
  if (lowered == "dmabuf" || lowered == "dma-buf") {
    *backend = MemoryBackend::Dmabuf;
    return true;
  }
  if (lowered == "userptr") {
    *backend = MemoryBackend::UserPtr;
    return true;
  }
  return false;
}

bool parse_report_format(const std::string &value, ReportFormat *format) {
  const std::string lowered = lower_copy(trim(value));
  if (lowered == "json") {
    *format = ReportFormat::Json;
    return true;
  }
  if (lowered == "md" || lowered == "markdown") {
    *format = ReportFormat::Markdown;
    return true;
  }
  if (lowered == "html") {
    *format = ReportFormat::Html;
    return true;
  }
  if (lowered == "pdf") {
    *format = ReportFormat::Pdf;
    return true;
  }
  return false;
}

bool parse_run_mode(const std::string &value, RunMode *mode) {
  const std::string lowered = lower_copy(trim(value));
  if (lowered == "sequential") {
    *mode = RunMode::Sequential;
    return true;
  }
  if (lowered == "parallel" || lowered == "async" || lowered == "asynchronous") {
    *mode = RunMode::Parallel;
    return true;
  }
  return false;
}

bool parse_trigger_mode(const std::string &value, TriggerMode *mode) {
  if (value == "hardware") {
    *mode = TriggerMode::Hardware;
    return true;
  }
  if (value == "software") {
    *mode = TriggerMode::Software;
    return true;
  }
  if (value == "free-run" || value == "free_run" || value == "freerun") {
    *mode = TriggerMode::FreeRun;
    return true;
  }
  return false;
}

std::vector<std::string> split_csv(const std::string &value) {
  std::vector<std::string> out;
  std::stringstream ss(value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    item = trim(item);
    if (!item.empty()) {
      out.push_back(item);
    }
  }
  return out;
}

std::string trim(const std::string &value) {
  auto begin = value.begin();
  while (begin != value.end() && std::isspace(static_cast<unsigned char>(*begin))) {
    ++begin;
  }
  auto end = value.end();
  while (end != begin && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
    --end;
  }
  return std::string(begin, end);
}

std::string utc_timestamp() {
  std::time_t now = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&now, &tm);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

std::string host_name() {
  char buffer[256];
  if (gethostname(buffer, sizeof(buffer)) == 0) {
    buffer[sizeof(buffer) - 1] = '\0';
    return buffer;
  }
  return "unknown";
}

}  // namespace v4l2diag
