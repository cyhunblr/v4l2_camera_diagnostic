// Verifies how the camera's frame rate is derived in t20-sequence-continuity.
//
// The arithmetic lives inline in run_sequence_continuity, which needs a real
// V4L2 device, so the formula is restated here against the same inputs the
// runner collects (sequence numbers and buffer timestamps). The point under test
// is not the multiplication — it is the choice of inputs: rate must come from the
// sequence span, not from the number of frames the loop managed to dequeue.

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << message << "\n";
  }
  return condition;
}

// Mirrors the derivation in run_sequence_continuity.
struct Rate {
  bool valid = false;
  double hz = 0.0;
  double interval_ms = 0.0;
};

Rate derive_rate(const std::vector<uint32_t> &seqs, const std::vector<double> &ts_us) {
  Rate out;
  if (seqs.size() < 2) {
    return out;
  }
  // int64_t, not int: sequence is uint32_t and a wrap narrowed to int reads as a
  // large positive span, which would report a fabricated rate.
  const int64_t span = static_cast<int64_t>(seqs.back()) - static_cast<int64_t>(seqs.front());
  const double elapsed_ms = (ts_us.back() - ts_us.front()) / 1000.0;
  if (span <= 0 || elapsed_ms <= 0.0) {
    return out;
  }
  out.valid = true;
  out.hz = static_cast<double>(span) * 1000.0 / elapsed_ms;
  out.interval_ms = elapsed_ms / static_cast<double>(span);
  return out;
}

bool near(double a, double b, double tol) {
  return (a > b ? a - b : b - a) <= tol;
}

}  // namespace

int main() {
  bool ok = true;

  // A loop that reads every frame: 31 frames spanning 30 intervals of 33.33ms
  // is 30 Hz. Sampled and produced rates agree here, so this only pins the
  // formula.
  {
    std::vector<uint32_t> seqs;
    std::vector<double> ts;
    for (int i = 0; i < 31; i++) {
      seqs.push_back(static_cast<uint32_t>(100 + i));
      ts.push_back(i * 33333.0);
    }
    const Rate r = derive_rate(seqs, ts);
    ok &= require(r.valid, "a full capture run should yield a rate");
    ok &= require(near(r.hz, 30.0, 0.1), "expected ~30Hz, got " + std::to_string(r.hz));
    ok &= require(near(r.interval_ms, 33.33, 0.1), "expected ~33.33ms, got " + std::to_string(r.interval_ms));
  }

  // The case that motivates using the sequence span. The camera runs at 30Hz but
  // the loop samples every third frame, so consecutive dequeued frames are 100ms
  // apart. Counting dequeued frames would report ~10Hz; the sequence span still
  // recovers 30Hz. This is the mistake the metric exists to avoid.
  {
    std::vector<uint32_t> seqs;
    std::vector<double> ts;
    for (int i = 0; i < 11; i++) {
      seqs.push_back(static_cast<uint32_t>(500 + i * 3));  // two frames drained between reads
      ts.push_back(i * 100000.0);                          // 100ms between reads
    }
    const Rate r = derive_rate(seqs, ts);
    ok &= require(r.valid, "a sub-sampled run should still yield a rate");
    ok &= require(near(r.hz, 30.0, 0.5),
                  "sequence span should recover the camera's 30Hz despite 10Hz sampling, got " + std::to_string(r.hz));
    // Guard against a regression to counting dequeued frames.
    const double dequeued_rate =
        static_cast<double>(seqs.size() - 1) * 1000.0 / ((ts.back() - ts.front()) / 1000.0);
    ok &= require(near(dequeued_rate, 10.0, 0.5), "sanity: the dequeued-frame rate should be ~10Hz here");
    ok &= require(!near(r.hz, dequeued_rate, 1.0),
                  "the reported rate must not collapse to the sampling rate: " + std::to_string(r.hz));
  }

  // Degenerate inputs must not divide by zero or report a rate. A stalled camera
  // repeats one sequence number; a single frame has no interval at all.
  {
    ok &= require(!derive_rate({7, 7, 7}, {0.0, 1000.0, 2000.0}).valid,
                  "a stalled sequence has no frame rate to report");
    ok &= require(!derive_rate({1}, {0.0}).valid, "one frame is not an interval");
    ok &= require(!derive_rate({1, 2}, {5000.0, 5000.0}).valid, "zero elapsed time must not divide");
    // Sequence counters wrap; a backwards span must be rejected rather than
    // reported as a negative rate.
    ok &= require(!derive_rate({4000000000u, 5u}, {0.0, 100000.0}).valid,
                  "a wrapped/backwards sequence span must not produce a rate");
  }

  if (!ok) {
    return 1;
  }
  std::cout << "frame_rate tests passed\n";
  return 0;
}
