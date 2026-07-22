#pragma once

#include "v4l2diag/core/profile_registry.hpp"
#include "v4l2diag/core/types.hpp"

#include <json/json.h>
#include <microhttpd.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct MHD_Daemon;

namespace v4l2diag {

struct WebServerOptions {
  std::string bind_address = "127.0.0.1";
  unsigned short port = 8765;
  unsigned short max_port = 8799;
  std::string web_root;
  std::string report_root;  // empty = resolved to a stable, absolute default in WebServer::start()
  std::string config_directory;
  bool open_browser = true;
};

#if MHD_VERSION >= 0x00097000
using MhdRequestResult = MHD_Result;
#else
using MhdRequestResult = int;
#endif

struct RunLogLine {
  std::size_t offset = 0;
  std::string timestamp_utc;
  std::string severity = "info";
  std::string log_type = "progress";  // section_start, progress, data, summary
  std::string camera;
  std::string test;
  std::string message;
};

class WebServer {
 public:
  explicit WebServer(WebServerOptions options);
  ~WebServer();

  WebServer(const WebServer &) = delete;
  WebServer &operator=(const WebServer &) = delete;

  bool start(std::string *error);
  void stop();
  bool running() const;
  unsigned short port() const {
    return active_port_;
  }
  std::string url() const;

 private:
  struct RunState;

  WebServerOptions options_;
  unsigned short active_port_ = 0;
  MHD_Daemon *daemon_ = nullptr;
  std::atomic<bool> running_{false};
  mutable std::mutex runs_mutex_;
  std::vector<std::shared_ptr<RunState>> runs_;
  mutable std::mutex history_mutex_;
  std::vector<Json::Value>
      history_;  // completed-run summaries, newest first; loaded once at startup, appended on completion

  std::string handle_request(const std::string &method, const std::string &path, const std::string &query,
                             const std::string &body, int *status_code, std::string *content_type);
  std::string handle_api(const std::string &method, const std::string &path, const std::string &query,
                         const std::string &body, int *status_code, std::string *content_type);
  std::string handle_static(const std::string &path, int *status_code, std::string *content_type) const;
  std::string handle_report_file(const std::string &path, int *status_code, std::string *content_type) const;

  std::shared_ptr<RunState> find_run(const std::string &id) const;
  std::shared_ptr<RunState> create_run(const RunConfig &config);
  void execute_run(std::shared_ptr<RunState> run);
  void load_run_history();
  void persist_run_summary(const Json::Value &summary);
  void append_log(const std::shared_ptr<RunState> &run, const std::string &severity, const std::string &message,
                  const std::string &camera = {}, const std::string &test = {},
                  const std::string &log_type = "progress");

  static MhdRequestResult handle_request_static(void *cls, struct MHD_Connection *connection, const char *url,
                                                const char *method, const char *version, const char *upload_data,
                                                size_t *upload_data_size, void **con_cls);
};

std::string default_web_root();
std::string default_report_root();
bool open_url_in_browser(const std::string &url);

}  // namespace v4l2diag
