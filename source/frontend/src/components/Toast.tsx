import { AlertTriangle, CheckCircle2, Info, X } from "lucide-react";

type Props = {
  message: string | null;
  tone?: "error" | "success" | "info";
  onDismiss: () => void;
};

export function Toast({ message, tone = "error", onDismiss }: Props) {
  if (!message) return null;
  const Icon = tone === "success" ? CheckCircle2 : tone === "info" ? Info : AlertTriangle;
  return (
    <div className={`toast ${tone}`}>
      <Icon size={16} />
      <span>{message}</span>
      <button onClick={onDismiss}><X size={14} /></button>
    </div>
  );
}
