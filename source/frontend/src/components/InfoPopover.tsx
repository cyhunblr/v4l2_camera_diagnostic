import React, { useEffect, useRef, useState } from "react";
import { Info } from "lucide-react";

type Props = {
  content: React.ReactNode;
};

export function InfoPopover({ content }: Props) {
  const [open, setOpen] = useState(false);
  const ref = useRef<HTMLDivElement | null>(null);

  useEffect(() => {
    if (!open) return;
    function handleOutsideClick(event: MouseEvent) {
      if (ref.current && !ref.current.contains(event.target as Node)) {
        setOpen(false);
      }
    }
    document.addEventListener("mousedown", handleOutsideClick);
    return () => document.removeEventListener("mousedown", handleOutsideClick);
  }, [open]);

  return (
    <div className="info-popover" ref={ref}>
      <button
        type="button"
        className="info-popover-trigger"
        onClick={(event) => {
          event.stopPropagation();
          setOpen((value) => !value);
        }}
        aria-label="More information"
        aria-expanded={open}
      >
        <Info size={13} />
      </button>
      {open && <div className="info-popover-content">{content}</div>}
    </div>
  );
}
