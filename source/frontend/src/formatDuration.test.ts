import { describe, expect, it } from "vitest";
import { formatDuration } from "./formatDuration";
// The one shared vector file, pulled in as raw text by Vite. Imported rather than read
// with node:fs so the test needs no Node type definitions -- and so a missing or renamed
// file is a build error here instead of a runtime surprise.
import vectorText from "../../../tests/data/duration_format_vectors.txt?raw";

// Duration formatting, frontend side (plan 3.2).
//
// The expectations are NOT written out here: they are read from
// tests/data/duration_format_vectors.txt, the same file the C++ test
// (tests/duration_format_test.cpp) reads. A duration must read identically in the HTML
// report and in the web UI, so two hand-maintained copies of the cases would be exactly
// the wrong shape -- one could be updated while the other kept the old value, and both
// suites would stay green while the two surfaces disagreed.

interface Vector {
  rawMs: string;
  ms: number;
  expected: string;
  line: number;
}

function loadVectors(): Vector[] {
  const vectors: Vector[] = [];
  vectorText.split("\n").forEach((line, index) => {
    if (line === "" || line.startsWith("#")) {
      return;
    }
    const tab = line.indexOf("\t");
    if (tab < 0) {
      throw new Error(`vector line ${index + 1} has no tab separator: ${line}`);
    }
    const rawMs = line.slice(0, tab);
    vectors.push({ rawMs, ms: Number(rawMs), expected: line.slice(tab + 1), line: index + 1 });
  });
  return vectors;
}

describe("formatDuration", () => {
  const vectors = loadVectors();

  it("reads a non-trivial number of shared vectors", () => {
    // A silently empty or unreadable vector file would make the table below vacuous.
    expect(vectors.length).toBeGreaterThanOrEqual(40);
  });

  it.each(vectors)("formats $rawMs ms as $expected (vector line $line)", ({ ms, expected }) => {
    expect(formatDuration(ms)).toBe(expected);
  });

  it("puts the band boundaries exactly where the contract does", () => {
    expect(formatDuration(9990)).toBe("9.99s");
    expect(formatDuration(10000)).toBe("10s");
    expect(formatDuration(59500)).toBe("59.5s");
    expect(formatDuration(60000)).toBe("1m");
  });

  it("never emits a trailing zero, a zero seconds part, or a sign", () => {
    // The bug this contract exists to kill: "19.0s", "20.0s", "1m 0s".
    for (let ms = 0; ms <= 600000; ms += 137) {
      const actual = formatDuration(ms);
      expect(actual, `${ms}ms`).not.toMatch(/\.0+s$/);
      expect(actual, `${ms}ms`).not.toContain(" 0s");
      expect(actual, `${ms}ms`).not.toContain("-");
    }
  });

  it("does not render non-finite input as NaN or Infinity", () => {
    expect(formatDuration(Number.NaN)).toBe("0s");
    expect(formatDuration(Number.POSITIVE_INFINITY)).toBe("0s");
    expect(formatDuration(Number.NEGATIVE_INFINITY)).toBe("0s");
  });

  it("does not use the old mm:ss shape anywhere", () => {
    // The Dashboard's private helper rendered 87.2s as "1:27", which matched neither the
    // report nor any other surface.
    for (const ms of [87200, 60000, 600000, 3600000]) {
      expect(formatDuration(ms), `${ms}ms`).not.toContain(":");
    }
  });
});
