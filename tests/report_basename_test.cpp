// The canonical artifact basename (plan 3.5).
//
// HTML, JSON, Markdown, the HTML <title>, the Export links and the DMESG filename all
// derive from ONE result, so they cannot drift apart.
//
// The naming inputs are the SOURCE FILE NAMES of the selected Trigger Profile and Test
// Configuration -- not their ids. The two are not the same thing: the threshold loader
// accepts any *.json in its directory and reads the id out of the document, so
// `stress-test.json` may well contain `"id": "default"`. Guessing the filename from the
// id would then put the wrong name on every artifact of that run.
#include "v4l2diag/core/report_naming.hpp"

#include "v4l2diag/core/threshold_registry.hpp"

#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

bool check(bool condition, const std::string &what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

std::string make_temp_dir() {
  std::string pattern = "/tmp/v4l2diag-basename-test-XXXXXX";
  std::vector<char> buffer(pattern.begin(), pattern.end());
  buffer.push_back('\0');
  char *created = mkdtemp(buffer.data());
  return created ? created : "/tmp/v4l2diag-basename-test";
}

void write_file(const std::string &path, const std::string &text) {
  std::ofstream out(path, std::ios::trunc);
  out << text;
}

v4l2diag::ReportNaming naming(v4l2diag::TriggerMode mode, const std::string &profile_file,
                              const std::string &config_file) {
  v4l2diag::ReportNaming out;
  out.started_at_utc = "2026-07-30T12:02:38Z";
  out.trigger_mode = mode;
  out.trigger_profile_file = profile_file;
  out.test_configuration_file = config_file;
  return out;
}

}  // namespace

int main() {
  using v4l2diag::TriggerMode;
  bool ok = true;

  // --- 1. The approved examples -------------------------------------------
  {
    // report-ui-review-plan.md / fix-plan 3.5, verbatim.
    ok &= check(v4l2diag::canonical_report_basename(naming(v4l2diag::TriggerMode::FreeRun, "", "")) ==
                    "2026-07-30_12-02-38_free-run_default_v4l2_camera_diagnostic",
                "the free-run + default example changed: " +
                    v4l2diag::canonical_report_basename(naming(v4l2diag::TriggerMode::FreeRun, "", "")));

    ok &=
        check(v4l2diag::canonical_report_basename(naming(TriggerMode::Hardware, "anvil.json", "stress-test.json")) ==
                  "2026-07-30_12-02-38_hardware-trigger_anvil_stress-test_v4l2_camera_diagnostic",
              "the hardware-trigger example changed: " +
                  v4l2diag::canonical_report_basename(naming(TriggerMode::Hardware, "anvil.json", "stress-test.json")));

    ok &= check(v4l2diag::canonical_report_basename(naming(TriggerMode::Software, "softrig.json", "")) ==
                    "2026-07-30_12-02-38_software-trigger_softrig_default_v4l2_camera_diagnostic",
                "the software-trigger name is wrong: " +
                    v4l2diag::canonical_report_basename(naming(TriggerMode::Software, "softrig.json", "")));
  }

  // --- 2. Free-run omits the Trigger Profile part entirely ----------------
  {
    // Even if a profile file is somehow set: free-run does not route, so naming it
    // would claim a routing the run never used.
    const std::string named = v4l2diag::canonical_report_basename(naming(TriggerMode::FreeRun, "anvil.json", ""));
    ok &= check(named.find("anvil") == std::string::npos, "free-run put a Trigger Profile in the name: " + named);
    ok &= check(named == "2026-07-30_12-02-38_free-run_default_v4l2_camera_diagnostic",
                "the free-run name is wrong when a profile file is present: " + named);
  }

  // --- 3. Extensions and paths never reach the name ------------------------
  {
    struct Case {
      const char *profile_file;
      const char *config_file;
      const char *expect_profile;
      const char *expect_config;
      const char *what;
    };
    const Case cases[] = {
        {"anvil.json", "stress-test.json", "anvil", "stress-test", "plain filenames"},
        {"anvil", "stress-test", "anvil", "stress-test", "already extensionless"},
        {"/etc/v4l2diag/anvil.json", "/tmp/a/b/stress-test.json", "anvil", "stress-test", "absolute paths"},
        {"../anvil.json", "./sub/stress-test.json", "anvil", "stress-test", "relative paths"},
        {"anvil.tar.gz", "stress-test.config.json", "anvil.tar", "stress-test.config", "only the last extension"},
    };
    for (const auto &item : cases) {
      const std::string name =
          v4l2diag::canonical_report_basename(naming(TriggerMode::Hardware, item.profile_file, item.config_file));
      ok &= check(name.find('/') == std::string::npos,
                  std::string("a path separator reached the name (") + item.what + "): " + name);
      ok &= check(name.find(std::string("_") + item.expect_profile + "_") != std::string::npos,
                  std::string("the profile part is wrong (") + item.what + "): " + name);
      ok &= check(name.find(std::string("_") + item.expect_config + "_") != std::string::npos,
                  std::string("the config part is wrong (") + item.what + "): " + name);
    }
  }

  // --- 4. An unusable TEST CONFIGURATION name falls back safely -----------
  {
    // A name that reduces to nothing, or one full of separators, must not produce a broken
    // or dangerous filename. The fallback is "default", and it applies to the Test
    // Configuration ONLY -- section 9 covers the Trigger Profile, which has no fallback.
    //
    // Free-run here on purpose: it omits the profile part, so this exercises the Test
    // Configuration's fallback in isolation.
    for (const char *bad : {"", "   ", "/", "///", ".", "..", ".json", "/.json"}) {
      const std::string name = v4l2diag::canonical_report_basename(naming(TriggerMode::FreeRun, "", bad));
      ok &= check(name.find('/') == std::string::npos,
                  std::string("a separator survived the fallback for \"") + bad + "\": " + name);
      ok &= check(name.find("..") == std::string::npos,
                  std::string("\"..\" survived the fallback for \"") + bad + "\": " + name);
      ok &= check(name.find("_default_v4l2_camera_diagnostic") != std::string::npos,
                  std::string("no safe fallback for \"") + bad + "\": " + name);
    }
  }

  // --- 4b. Hostile filenames are normalised deterministically -------------
  {
    // The same stem lands in three places: a file on disk, the HTML <title>, and a
    // Content-Disposition header. A CR/LF would end the header and let the rest be read
    // as another one, and a quote would close the filename parameter -- so the
    // normalisation has to happen once, here, rather than in each consumer.
    struct Case {
      const char *raw;
      const char *what;
    };
    const Case cases[] = {
        {"anvil\r\nX-Injected: yes", "a CRLF header injection"},
        {"anvil\nX-Injected: yes", "a bare LF"},
        {"anvil\r", "a trailing CR"},
        {"an\"vil", "a double quote"},
        {"an'vil", "a single quote"},
        {"an vil", "an embedded space"},
        {"an\tvil", "a tab"},
        {"an\x01vil", "a control character"},
        {"an;vil", "a semicolon"},
        {"an\\vil", "a backslash"},
        {"an*vil?", "shell globbing characters"},
        {"an|vil", "a pipe"},
        {"an$vil`", "shell expansion characters"},
        {"anvil\x7f", "DEL"},
    };
    for (const auto &item : cases) {
      // Every case here NORMALISES to something usable -- "an vil" becomes "an-vil" -- so
      // a hardware run is nameable and the normalisation is what is under test. A name that
      // reduces to nothing under a triggered mode is section 9's subject, not this one.
      v4l2diag::ReportNaming hostile = naming(TriggerMode::Hardware, item.raw, item.raw);
      const std::string base = v4l2diag::canonical_report_basename(hostile);
      const std::string title = v4l2diag::report_document_title(hostile);
      const std::string dmesg = v4l2diag::dmesg_log_filename(hostile);
      const std::string html = v4l2diag::report_artifact_filename(hostile, v4l2diag::ReportFormat::Html);

      for (const auto &produced : {base, title, dmesg, html}) {
        // Nothing that could break out of a filename, a header or a shell word.
        for (const char *forbidden : {"\r", "\n", "\"", "'", " ", "\t", ";", "\\", "*", "?", "|", "$", "`", "/"}) {
          ok &= check(produced.find(forbidden) == std::string::npos,
                      std::string("a forbidden character survived (") + item.what + "): " + produced);
        }
        for (char c : produced) {
          const unsigned char u = static_cast<unsigned char>(c);
          ok &= check(u >= 0x20 && u != 0x7f,
                      std::string("a control character survived (") + item.what + "): " + produced);
        }
      }
      // Deterministic: the same input always yields the same stem, and every consumer
      // sees that one stem.
      ok &= check(title == base, std::string("the title diverged from the base (") + item.what + ")");
      ok &= check(html == base + ".html", std::string("the artifact name diverged (") + item.what + ")");
      ok &= check(dmesg.rfind("_dmesg.log") != std::string::npos,
                  std::string("the dmesg name lost its suffix (") + item.what + ")");
      // And the run's identity is still recognisable rather than blanked out.
      ok &= check(base.find("anvil") != std::string::npos || base.find("an-vil") != std::string::npos,
                  std::string("the name was destroyed rather than normalised (") + item.what + "): " + base);
    }
  }

  // --- 5. The timestamp shape ---------------------------------------------
  {
    v4l2diag::ReportNaming with_time = naming(v4l2diag::TriggerMode::FreeRun, "", "");
    with_time.started_at_utc = "2026-01-02T03:04:05Z";
    const std::string name = v4l2diag::canonical_report_basename(with_time);
    ok &= check(name.rfind("2026-01-02_03-04-05_", 0) == 0, "the timestamp is not YYYY-MM-DD_HH-mm-ss: " + name);
    // A missing or malformed timestamp must still yield a usable name.
    v4l2diag::ReportNaming no_time = naming(v4l2diag::TriggerMode::FreeRun, "", "");
    no_time.started_at_utc = "";
    const std::string fallback = v4l2diag::canonical_report_basename(no_time);
    ok &= check(!fallback.empty() && fallback.find('/') == std::string::npos,
                "an empty timestamp produced an unusable name: " + fallback);
    ok &= check(fallback.find("v4l2_camera_diagnostic") != std::string::npos,
                "an empty timestamp lost the canonical suffix: " + fallback);
  }

  // --- 6. Every artifact name comes from the one basename -----------------
  {
    const v4l2diag::ReportNaming source = naming(TriggerMode::Hardware, "anvil.json", "stress-test.json");
    const std::string base = v4l2diag::canonical_report_basename(source);
    ok &= check(v4l2diag::report_artifact_filename(source, v4l2diag::ReportFormat::Html) == base + ".html",
                "the HTML artifact name is not the canonical base + .html");
    ok &= check(v4l2diag::report_artifact_filename(source, v4l2diag::ReportFormat::Json) == base + ".json",
                "the JSON artifact name is not the canonical base + .json");
    ok &= check(v4l2diag::report_artifact_filename(source, v4l2diag::ReportFormat::Markdown) == base + ".md",
                "the Markdown artifact name is not the canonical base + .md");
    // The DMESG log replaces the trailing "v4l2_camera_diagnostic" with "dmesg".
    ok &= check(
        v4l2diag::dmesg_log_filename(source) == "2026-07-30_12-02-38_hardware-trigger_anvil_stress-test_dmesg.log",
        "the DMESG name is wrong: " + v4l2diag::dmesg_log_filename(source));
    // The HTML <title> is the extensionless base.
    ok &= check(v4l2diag::report_document_title(source) == base,
                "the document title is not the extensionless canonical base");
  }

  // --- 7. The threshold id does NOT guarantee the filename ---------------
  {
    // Proof that naming from the id would be wrong: the loader accepts any *.json and
    // takes the id from inside the document, so a file called stress-test.json can
    // legitimately hold id "default".
    const std::string dir = make_temp_dir();
    write_file(dir + "/stress-test.json", R"({"id": "default", "name": "Default", "values": {}, "params": {}})");
    v4l2diag::ThresholdRegistry registry(dir);
    const auto configs = registry.list_configs();
    bool found_mismatch = false;
    for (const auto &config : configs) {
      if (config.id == "default") {
        found_mismatch = true;
      }
    }
    ok &= check(found_mismatch,
                "the threshold loader no longer reads the id from inside the file; re-check whether the id can "
                "still differ from the filename");
    // Which is exactly why ReportNaming carries the filename separately: given this
    // config, naming by id would produce "_default_" where the user's file is
    // "stress-test".
    const std::string by_file =
        v4l2diag::canonical_report_basename(naming(TriggerMode::Hardware, "anvil.json", "stress-test.json"));
    const std::string by_id =
        v4l2diag::canonical_report_basename(naming(TriggerMode::Hardware, "anvil.json", "default"));
    ok &= check(by_file != by_id, "naming by filename and by id produced the same result, so the test proves nothing");
    ok &= check(by_file.find("stress-test") != std::string::npos, "the filename-based name lost the real file name");

    unlink((dir + "/stress-test.json").c_str());
    unlink((dir + "/default.json").c_str());
    rmdir(dir.c_str());
  }

  // --- 8. Naming inputs are RESOLVED, never taken from the request --------
  {
    // The run request carries ids. The filename must come from the stored record the
    // registry actually loaded -- a client-supplied string could name any file, or a
    // file that does not exist.
    const std::string dir = make_temp_dir();
    // Same id, different file name: the case that makes id-derived naming wrong.
    write_file(dir + "/stress-test.json", R"({"id": "harsh", "name": "Harsh", "values": {}, "params": {}})");
    v4l2diag::ThresholdRegistry registry(dir);

    std::string resolved;
    for (const auto &config : registry.list_configs()) {
      if (config.id == "harsh") {
        resolved = config.source_file;
      }
    }
    ok &= check(resolved == "stress-test.json",
                "the registry does not report the file a config was loaded from: \"" + resolved + "\"");

    // Resolving by id yields the real file, so the name is the user's file name and not
    // the id.
    v4l2diag::ReportNaming from_registry = naming(TriggerMode::Hardware, "anvil.json", resolved);
    ok &= check(v4l2diag::canonical_report_basename(from_registry).find("stress-test") != std::string::npos,
                "the resolved file name did not reach the artifact name");
    ok &= check(v4l2diag::canonical_report_basename(from_registry).find("_harsh_") == std::string::npos,
                "the id leaked into the artifact name instead of the file name");

    // An id the registry does not know resolves to nothing -- and nothing is NOT
    // guessed at: the name falls back to "default" rather than inventing a file.
    std::string unknown;
    for (const auto &config : registry.list_configs()) {
      if (config.id == "no-such-config") {
        unknown = config.source_file;
      }
    }
    ok &= check(unknown.empty(), "an unknown id resolved to a file name");
    const std::string fallback =
        v4l2diag::canonical_report_basename(naming(TriggerMode::Hardware, "anvil.json", unknown));
    ok &= check(fallback.find("_default_") != std::string::npos,
                "an unresolved config did not fall back to \"default\": " + fallback);

    unlink((dir + "/stress-test.json").c_str());
    unlink((dir + "/default.json").c_str());
    rmdir(dir.c_str());
  }

  // --- 9. A triggered run MUST have a real Trigger Profile file -----------
  {
    // "default" is the Test Configuration's fallback and nothing else's. A hardware or
    // software run routes through a Trigger Profile, so a run whose profile file could not
    // be resolved has no honest name: omitting the part claims a free-run that did route,
    // and substituting "default" names a file the user never chose.
    for (const auto mode : {v4l2diag::TriggerMode::Hardware, v4l2diag::TriggerMode::Software}) {
      bool threw = false;
      try {
        (void)v4l2diag::canonical_report_basename(naming(mode, "", "stress-test.json"));
      } catch (const v4l2diag::ReportNamingError &) {
        threw = true;
      }
      ok &= check(threw, std::string("a triggered run with no Trigger Profile file was named anyway (") +
                             v4l2diag::to_string(mode) + ")");

      // A filename that reduces to nothing usable is the same failure, not a fallback:
      // there is no stem to name the artifact with.
      for (const char *unusable : {".json", "   ", "...", "/", "---"}) {
        bool unusable_threw = false;
        try {
          (void)v4l2diag::canonical_report_basename(naming(mode, unusable, "stress-test.json"));
        } catch (const v4l2diag::ReportNamingError &) {
          unusable_threw = true;
        }
        ok &= check(unusable_threw, std::string("a triggered run accepted an unusable profile filename: \"") +
                                        unusable + "\" (" + v4l2diag::to_string(mode) + ")");
      }

      // The same failure reaches every derived name, not just the basename: an artifact
      // that cannot be named must not be written under any of them.
      for (const auto check_format :
           {v4l2diag::ReportFormat::Html, v4l2diag::ReportFormat::Json, v4l2diag::ReportFormat::Markdown}) {
        bool format_threw = false;
        try {
          (void)v4l2diag::report_artifact_filename(naming(mode, "", "stress-test.json"), check_format);
        } catch (const v4l2diag::ReportNamingError &) {
          format_threw = true;
        }
        ok &= check(format_threw, std::string("report_artifact_filename named a profile-less triggered run (") +
                                      v4l2diag::to_string(check_format) + ")");
      }
      bool dmesg_threw = false;
      try {
        (void)v4l2diag::dmesg_log_filename(naming(mode, "", "stress-test.json"));
      } catch (const v4l2diag::ReportNamingError &) {
        dmesg_threw = true;
      }
      ok &= check(dmesg_threw, "dmesg_log_filename named a profile-less triggered run");

      // Positive control: a resolvable profile still names the run. Without this the
      // checks above could pass because every triggered run throws.
      const std::string named = v4l2diag::canonical_report_basename(naming(mode, "anvil.json", "stress-test.json"));
      ok &= check(named.find("_anvil_") != std::string::npos,
                  std::string("a resolvable triggered run was not named: ") + named);
    }

    // Free-run keeps omitting the part: it routes nothing, so there is no profile to
    // require and naming one would claim a routing the run never used.
    const std::string free_run = v4l2diag::canonical_report_basename(naming(v4l2diag::TriggerMode::FreeRun, "", ""));
    ok &= check(free_run.find("_free-run_default_") != std::string::npos,
                "free-run no longer omits the profile part: " + free_run);
    // A profile filename supplied under free-run is still ignored rather than rejected:
    // the run did not route, whatever was selected beforehand.
    const std::string ignored =
        v4l2diag::canonical_report_basename(naming(v4l2diag::TriggerMode::FreeRun, "anvil.json", "stress-test.json"));
    ok &=
        check(ignored.find("anvil") == std::string::npos, "free-run named a Trigger Profile it never used: " + ignored);
  }

  return ok ? 0 : 1;
}
