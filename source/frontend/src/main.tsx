import { createRoot } from "react-dom/client";
import { getInitialTheme, applyTheme } from "./theme";
import App from "./App";
import "./styles/theme.css";
import "./styles.css";

// Set data-theme synchronously before mount so there's no flash of the wrong theme.
applyTheme(getInitialTheme());

createRoot(document.getElementById("root")!).render(<App />);
