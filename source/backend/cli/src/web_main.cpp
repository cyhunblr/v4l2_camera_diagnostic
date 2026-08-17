#include "v4l2diag/cli/web_main.hpp"

#include "v4l2diag/web/web_server.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_running{true};

void handle_signal(int) {
  g_running = false;
}

void print_usage() {
  std::cout
      << "v4l2-camera-diagnostic-web\n\n"
      << "Usage:\n"
      << "  v4l2-camera-diagnostic-web [--host HOST] [--port PORT] [--web-root DIR]\n"
      << "                             [--report-root DIR] [--config-dir DIR] [--no-open]\n\n"
      << "Options:\n"
      << "  --host HOST       Bind address. Default: 127.0.0.1. Use 0.0.0.0 for LAN access.\n"
      << "  --port PORT       Preferred local port. Default: 8765.\n"
      << "  --web-root DIR    Directory containing the built web UI assets.\n"
      << "  --report-root DIR Directory for generated reports. Default: $XDG_DATA_HOME/v4l2-camera-diagnostic/reports\n"
      << "                    (or ~/.local/share/v4l2-camera-diagnostic/reports).\n"
      << "  --config-dir DIR  Profile config directory override.\n"
      << "  --no-open         Do not open the browser automatically.\n";
}

std::string next_arg(int *i, int argc, char **argv) {
  if (*i + 1 >= argc) {
    return {};
  }
  ++(*i);
  return argv[*i];
}

}  // namespace

namespace v4l2diag {

int run_web_main(int argc, char **argv) {
  WebServerOptions options;
  options.web_root = v4l2diag::default_web_root();

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      print_usage();
      return 0;
    }
    if (arg == "--host") {
      options.bind_address = next_arg(&i, argc, argv);
    } else if (arg == "--port") {
      options.port = static_cast<unsigned short>(std::atoi(next_arg(&i, argc, argv).c_str()));
    } else if (arg == "--web-root") {
      options.web_root = next_arg(&i, argc, argv);
    } else if (arg == "--report-root") {
      options.report_root = next_arg(&i, argc, argv);
    } else if (arg == "--config-dir") {
      options.config_directory = next_arg(&i, argc, argv);
    } else if (arg == "--no-open") {
      options.open_browser = false;
    } else {
      std::cerr << "Unknown option: " << arg << "\n";
      print_usage();
      return 2;
    }
  }

  std::signal(SIGINT, handle_signal);
  std::signal(SIGTERM, handle_signal);

  v4l2diag::WebServer server(options);
  std::string error;
  if (!server.start(&error)) {
    std::cerr << "Failed to start web server: " << error << "\n";
    return 1;
  }

  std::cout << "V4L2 Camera Diagnostic web app is running at " << server.url() << "\n";
  if (options.bind_address == "0.0.0.0") {
    std::cout << "LAN access is enabled. Open http://<this-device-lan-ip>:" << server.port()
              << " from another trusted device on the same network.\n";
  }
  std::cout << "Press Ctrl+C to stop.\n";

  std::string browser_url = server.url();
  if (options.bind_address == "0.0.0.0") {
    browser_url = "http://127.0.0.1:" + std::to_string(server.port());
  }

  if (options.open_browser && !v4l2diag::open_url_in_browser(browser_url)) {
    std::cout << "Could not open a browser automatically. Open this URL manually: " << browser_url << "\n";
  }

  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  server.stop();
  std::cout << "Web app stopped.\n";
  return 0;
}

}  // namespace v4l2diag

int main(int argc, char **argv) {
  return v4l2diag::run_web_main(argc, argv);
}
