import { ConfirmDialogState } from "../types";

type Props = {
  dialog: ConfirmDialogState | null;
  onClose: () => void;
};

export function ConfirmDialog({ dialog, onClose }: Props) {
  if (!dialog) return null;
  return (
    <div className="dialog-overlay" onClick={onClose}>
      <div className="dialog" onClick={(event) => event.stopPropagation()}>
        <h3>{dialog.title}</h3>
        <p>{dialog.message}</p>
        <div className="dialog-actions">
          <button className="dialog-cancel" onClick={onClose}>Cancel</button>
          <button className={`dialog-confirm ${dialog.variant}`} onClick={dialog.onConfirm}>
            {dialog.confirmLabel}
          </button>
        </div>
      </div>
    </div>
  );
}
