import { useState } from "react";
import { ConfirmDialogState } from "../types";

export function useConfirmDialog() {
  const [confirmDialog, setConfirmDialog] = useState<ConfirmDialogState | null>(null);

  function requestConfirm(dialog: ConfirmDialogState) {
    setConfirmDialog(dialog);
  }

  function closeConfirm() {
    setConfirmDialog(null);
  }

  return { confirmDialog, requestConfirm, closeConfirm };
}
