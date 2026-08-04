#include "v4l2diag/core/run_config_json.hpp"

#include "v4l2diag/core/run_routing.hpp"

#include <string>

namespace v4l2diag {

RunConfig run_config_from_json(const Json::Value &root, const std::string &report_root,
                               const std::string &config_directory, const std::string &run_id) {
  RunConfig config;
  config.output_directory = report_root + "/web-run-" + run_id;
  config.config_directory = config_directory;

  if (root.isMember("trigger_mode")) {
    parse_trigger_mode(root["trigger_mode"].asString(), &config.trigger_mode);
  }
  if (root.isMember("run_mode")) {
    RunMode mode;
    if (parse_run_mode(root["run_mode"].asString(), &mode)) {
      config.run_mode = mode;
    }
  }
  config.threshold_config_id = root.get("threshold_config_id", "default").asString();
  // Free-run carries no Trigger Profile. Normalised through the shared helper, so
  // a request that says "free-run" and names a profile cannot become a run that
  // reports both.
  config.trigger_profile_id =
      effective_trigger_profile_id(config.trigger_mode, root.get("trigger_profile_id", "").asString());

  // v4: only the path. Per-camera profile_id/trigger_channel_id are gone; the run
  // carries one trigger_profile_id and routing follows from each camera's role.
  auto parse_camera = [](const Json::Value &item) {
    RunConfig::CameraConfig camera;
    camera.path = item.get("path", "").asString();
    return camera;
  };

  if (root.isMember("master")) {
    config.master = parse_camera(root["master"]);
  }
  for (const auto &item : root["slaves"]) {
    config.slaves.push_back(parse_camera(item));
  }

  for (const auto &item : root["memory_backends"]) {
    MemoryBackend backend;
    if (parse_memory_backend(item.asString(), &backend)) {
      config.memory_backends.push_back(backend);
    }
  }
  if (config.memory_backends.empty()) {
    config.memory_backends.push_back(MemoryBackend::Mmap);
  }

  for (const auto &item : root["test_selectors"]) {
    config.test_selectors.push_back(item.asString());
  }
  // No fallback here on purpose. An empty selector list is meaningful:
  // select_tests() resolves it to the "stable" set. The old fallback pushed
  // "implemented", which is not an id, category or tag, so select_tests()
  // matched nothing and the run executed zero tests.

  // "report_formats" is deliberately NOT read (plan 2.10). Every run writes HTML,
  // JSON and Markdown, so a request carrying the old key is simply ignored rather
  // than rejected -- an archived request stays runnable.

  return config;
}

}  // namespace v4l2diag
