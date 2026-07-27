import { useEffect, useRef } from "react";
import { AlertTriangle, CheckCircle2, Info, X } from "lucide-react";

type Props = {
  message: string | null;
  tone?: "error" | "success" | "info";
  onDismiss: () => void;
};

export function Toast({ message, tone = "error", onDismiss }: Props) {
  const onDismissRef = useRef(onDismiss);

  useEffect(() => {
    onDismissRef.current = onDismiss;
  }, [onDismiss]);

  useEffect(() => {
    if (!message || tone === "error") return;
    const timer = window.setTimeout(() => {
      onDismissRef.current();
    }, 4000);
    return () => window.clearTimeout(timer);
  }, [message, tone]);

  if (!message) return null;
  const Icon = tone === "success" ? CheckCircle2 : tone === "info" ? Info : AlertTriangle;
  return (
    <div className={`toast ${tone}`}>
      <Icon size={16} />
      <span>{message}</span>
      <button type="button" onClick={onDismiss}><X size={14} /></button>
    </div>
  );
}
