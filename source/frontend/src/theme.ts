export type ThemeMode = "dark" | "light";

const STORAGE_KEY = "v4l2diag_theme";

/** Default is "dark" — no prefers-color-scheme fallback, an explicit toggle is required. */
export function getInitialTheme(): ThemeMode {
  try {
    const saved = localStorage.getItem(STORAGE_KEY);
    return saved === "light" ? "light" : "dark";
  } catch {
    return "dark";
  }
}

export function applyTheme(theme: ThemeMode): void {
  document.documentElement.setAttribute("data-theme", theme);
  try {
    localStorage.setItem(STORAGE_KEY, theme);
  } catch {
    // localStorage unavailable — theme still applies for this session.
  }
}
