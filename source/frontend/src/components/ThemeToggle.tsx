import { Moon, Sun } from "lucide-react";
import { ThemeMode } from "../theme";

type Props = {
  theme: ThemeMode;
  onToggle: () => void;
};

export function ThemeToggle({ theme, onToggle }: Props) {
  return (
    <button
      className="theme-toggle"
      onClick={onToggle}
      title={`Switch to ${theme === "dark" ? "light" : "dark"} theme`}
    >
      {theme === "dark" ? <Sun size={16} /> : <Moon size={16} />}
      <span>{theme === "dark" ? "Light mode" : "Dark mode"}</span>
    </button>
  );
}
