import { AlertTriangle, X } from "lucide-react";

type Props = {
  message: string | null;
  onDismiss: () => void;
};

export function Toast({ message, onDismiss }: Props) {
  if (!message) return null;
  return (
    <div className="toast error">
      <AlertTriangle size={16} />
      <span>{message}</span>
      <button onClick={onDismiss}><X size={14} /></button>
    </div>
  );
}
