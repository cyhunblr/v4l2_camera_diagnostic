import React from "react";

export type SelectableCardProps = {
  selected: boolean;
  onToggle: () => void;
  title: React.ReactNode;
  subtitle?: React.ReactNode;
  meta?: React.ReactNode;
  badges?: React.ReactNode;
  /** e.g. an InfoPopover trigger. Rendered as a sibling of the selection button,
   *  never inside it — a control nested in a control is invalid, and it made
   *  Enter/Space on the trigger toggle the card as well. */
  cornerAction?: React.ReactNode;
  layout?: "vertical" | "horizontal";
  disabled?: boolean;
};

export function SelectableCard({
  selected,
  onToggle,
  title,
  subtitle,
  meta,
  badges,
  cornerAction,
  layout = "vertical",
  disabled = false
}: SelectableCardProps) {
  return (
    <div className="selectable-card-wrap">
      {/* A native button gives Enter/Space activation for free. It is marked
          aria-disabled rather than disabled so a keyboard user can still reach
          the card and hear the subtitle explaining why it cannot be selected. */}
      <button
        type="button"
        aria-pressed={selected}
        aria-disabled={disabled}
        className={`selectable-card layout-${layout}${selected ? " selected" : ""}${disabled ? " disabled" : ""}`}
        onClick={() => {
          if (!disabled) onToggle();
        }}
      >
        <span className="card-body">
          <span className="card-header-row">
            <span className="card-title">{title}</span>
          </span>
          {subtitle && <span className="card-subtitle">{subtitle}</span>}
          {meta && <span className="card-meta">{meta}</span>}
          {badges && <span className="card-badges">{badges}</span>}
        </span>
      </button>
      {cornerAction && <div className="card-corner-action">{cornerAction}</div>}
    </div>
  );
}
