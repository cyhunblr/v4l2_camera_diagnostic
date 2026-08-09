// Pixel-format enumeration de-duplicates on (fourcc, buffer type).
//
// The 2026-08-09 device run exposed a report that answered one question two ways: T01 said
// "3 formats" and printed UYVY twice, while T17 measured 2 formats on the same camera. The
// driver (tegra isx021) advertises UYVY at VIDIOC_ENUM_FMT index 0 AND index 2, both
// single-plane; T17 already skipped repeats when building its own list, T01's discovery path
// did not.
//
// enumerate_formats() itself needs a real ioctl, so the decision it makes per entry lives in
// append_distinct_format() and is exercised here directly -- the loop needs hardware, the
// rule does not.

#include <iostream>
#include <string>
#include <vector>

#include "v4l2diag/hw/device_discovery.hpp"

namespace {

int failures = 0;

void check(bool condition, const std::string &message) {
  if (!condition) {
    std::cout << "FAIL: " << message << "\n";
    ++failures;
  }
}

v4l2diag::V4L2FormatInfo fmt(const std::string &fourcc, const std::string &description,
                             const std::string &buffer_type) {
  v4l2diag::V4L2FormatInfo info;
  info.fourcc = fourcc;
  info.description = description;
  info.buffer_type = buffer_type;
  return info;
}

}  // namespace

int main() {
  // --- the exact sequence the device reported --------------------------------
  // Replaying the real enumeration: UYVY, NV16, UYVY -- all single-plane.
  {
    std::vector<v4l2diag::V4L2FormatInfo> formats;
    const bool first = v4l2diag::append_distinct_format(fmt("UYVY", "UYVY 4:2:2", "single-plane"), &formats);
    const bool second = v4l2diag::append_distinct_format(fmt("NV16", "Y/CbCr 4:2:2", "single-plane"), &formats);
    const bool repeat = v4l2diag::append_distinct_format(fmt("UYVY", "UYVY 4:2:2", "single-plane"), &formats);

    check(first, "the first format was not appended");
    check(second, "a different fourcc was rejected");
    check(!repeat, "the driver's repeated UYVY was appended a second time");
    check(formats.size() == 2, "expected 2 distinct formats, got " + std::to_string(formats.size()));
    if (formats.size() == 2) {
      check(formats[0].fourcc == "UYVY", "the first entry is not UYVY");
      check(formats[1].fourcc == "NV16", "the second entry is not NV16");
    }
  }

  // --- the same fourcc under a DIFFERENT buffer type is a distinct entry ------
  // query_device() enumerates single-plane and multi-plane into one vector. The same fourcc
  // under both is two real capture configurations, so keying on fourcc alone would silently
  // drop a supported mode -- the opposite defect from the one above.
  {
    std::vector<v4l2diag::V4L2FormatInfo> formats;
    v4l2diag::append_distinct_format(fmt("UYVY", "UYVY 4:2:2", "single-plane"), &formats);
    const bool multi = v4l2diag::append_distinct_format(fmt("UYVY", "UYVY 4:2:2", "multi-plane"), &formats);

    check(multi, "the same fourcc under multi-plane was dropped as a duplicate");
    check(formats.size() == 2, "single-plane and multi-plane UYVY collapsed into one entry");
  }

  // --- the description is not part of the key --------------------------------
  // Two indices for one fourcc may carry slightly different description strings; the format
  // is still the same format. Keying on the description would let the duplicate back in.
  {
    std::vector<v4l2diag::V4L2FormatInfo> formats;
    v4l2diag::append_distinct_format(fmt("UYVY", "UYVY 4:2:2", "single-plane"), &formats);
    const bool relabelled =
        v4l2diag::append_distinct_format(fmt("UYVY", "UYVY 4:2:2 (packed)", "single-plane"), &formats);

    check(!relabelled, "a repeated fourcc with a different description was appended");
    check(formats.size() == 1, "the description leaked into the de-duplication key");
  }

  // --- a null destination is refused, not dereferenced -----------------------
  {
    check(!v4l2diag::append_distinct_format(fmt("UYVY", "UYVY 4:2:2", "single-plane"), nullptr),
          "a null format vector was accepted");
  }

  if (failures == 0) {
    std::cout << "format enumeration: de-duplication holds on (fourcc, buffer type)\n";
    return 0;
  }
  std::cout << failures << " check(s) failed\n";
  return 1;
}
