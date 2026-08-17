/**
 * Human-readable duration (plan 1.1 / 3.2).
 *
 *   < 10s      at most two decimals   "1.23s", "6s"
 *   10s - <60s at most one decimal    "12.4s", "19s"
 *   >= 60s     whole seconds          "1m 27s", "2m"
 *   zero       "0s"
 *
 * Trailing zeros are always stripped: "19.0s" and "1m 0s" never appear.
 *
 * The language-local twin of the backend's `format_duration_ms`. Both are tested against
 * ONE shared vector file (tests/data/duration_format_vectors.txt), so the same run reads
 * the same in the HTML report and here. The Dashboard used to render "1:27" from its own
 * private helper, which agreed with the report on nothing.
 *
 * A DISPLAY function: `duration_ms` is what gets stored and served, and is never replaced
 * by this string.
 */

/** "1.50" -> "1.5", "6.00" -> "6". */
function stripTrailingZeros(text: string): string {
  if (!text.includes(".")) {
    return text;
  }
  return text.replace(/\.?0+$/, "");
}

/**
 * Seconds at a fixed precision, zeros stripped, unit attached.
 *
 * Rounds half-up by hand rather than trusting `toFixed`, which follows the binary
 * representation of the value (`(0.125).toFixed(2)` is "0.13" but `(1.005).toFixed(2)` is
 * "1.00"). The backend rounds the same way, and the two outputs have to match exactly.
 */
function secondsText(seconds: number, precision: number): string {
  const scale = 10 ** precision;
  const rounded = Math.floor(seconds * scale + 0.5) / scale;
  return `${stripTrailingZeros(rounded.toFixed(precision))}s`;
}

export function formatDuration(milliseconds: number): string {
  // NaN fails every comparison, so it lands here instead of reaching toFixed and
  // rendering as "NaN". A negative duration is not a real measurement either; it is
  // clamped rather than shown with a minus sign.
  if (!(milliseconds > 0)) {
    return "0s";
  }
  if (!Number.isFinite(milliseconds)) {
    return "0s";
  }

  const seconds = milliseconds / 1000;

  if (seconds < 10) {
    const text = secondsText(seconds, 2);
    // Two decimals of a very small duration round to "0s", which reads as "no time at
    // all" for something that was actually measured.
    if (text === "0s") {
      return "0.01s";
    }
    // 9.999s rounds to "10s", which belongs to the next band -- where it formats to the
    // same "10s", so it is re-dispatched rather than special-cased.
    if (text === "10s") {
      return secondsText(10, 1);
    }
    return text;
  }

  if (seconds < 60) {
    const text = secondsText(seconds, 1);
    // 59.96s rounds to 60.0s, a whole minute: it must read "1m", not "60s".
    if (text !== "60s") {
      return text;
    }
  }

  // Whole seconds from here: rounding first keeps "1m 59.6s" from appearing as "1m 60s".
  const totalSeconds = Math.round(seconds);
  const minutes = Math.floor(totalSeconds / 60);
  const remainder = totalSeconds % 60;
  // "1m 0s" says nothing that "1m" does not.
  return remainder === 0 ? `${minutes}m` : `${minutes}m ${remainder}s`;
}
