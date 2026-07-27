// Standalone diagnostic mirroring v4l2-camera-diagnostic-web's /api/dmesg
// handler (dmesg first, journalctl -k -b as fallback), so CAP_SYSLOG /
// kernel.dmesg_restrict issues can be checked without going through the
// browser.
//
// Build:   g++ -std=c++17 -O2 -o check-dmesg-access check-dmesg-access.cpp
// Run:     ./check-dmesg-access

#include <cstdio>
#include <string>

namespace {

int try_cmd(const char *cmd) {
  printf("--- trying: %s ---\n", cmd);
  FILE *pipe = popen(cmd, "r");
  if (!pipe) {
    printf("popen() failed\n");
    return 1;
  }
  std::string output;
  char buf[4096];
  while (fgets(buf, sizeof(buf), pipe) != nullptr) {
    output += buf;
  }
  const int rc = pclose(pipe);
  printf("exit status: %d, output empty: %s, output size: %zu bytes\n", rc, output.empty() ? "yes" : "no",
         output.size());
  if (!output.empty()) {
    const auto nl = output.find('\n');
    printf("first line: %s\n", output.substr(0, nl).c_str());
  }
  return (rc == 0 && !output.empty()) ? 0 : 1;
}

}  // namespace

int main() {
  if (try_cmd("dmesg 2>&1") == 0) {
    printf("\n=> dmesg SUCCEEDED — the web app should be able to Export DMESG.\n");
    return 0;
  }
  if (try_cmd("journalctl -k -b --no-pager 2>&1") == 0) {
    printf("\n=> dmesg failed but journalctl -k -b SUCCEEDED — the web app should still work (it falls back to this).\n");
    return 0;
  }
  printf("\n=> BOTH dmesg and journalctl -k -b FAILED. Export DMESG will show \"Failed to export dmesg\".\n");
  printf("   Fix: sudo setcap cap_syslog+ep <path-to>/v4l2-camera-diagnostic-web\n");
  printf("   (then restart the web server so the new capability takes effect)\n");
  return 1;
}
