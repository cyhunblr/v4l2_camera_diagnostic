import React from "react";

export type SelectableCardProps = {
  selected: boolean;
  onToggle: () => void;
  title: React.ReactNode;
  subtitle?: React.ReactNode;
  meta?: React.ReactNode;
  badges?: React.ReactNode;
  /** e.g. an InfoPopover trigger — must stopPropagation so it doesn't also toggle selection. */
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
  function handleKeyDown(event: React.KeyboardEvent<HTMLDivElement>) {
    if (disabled) return;
    if (event.key === "Enter" || event.key === " ") {
      event.preventDefault();
      onToggle();
    }
  }

  return (
    <div
      role="button"
      tabIndex={disabled ? -1 : 0}
      aria-pressed={selected}
      aria-disabled={disabled}
      className={`selectable-card layout-${layout}${selected ? " selected" : ""}${disabled ? " disabled" : ""}`}
      onClick={() => !disabled && onToggle()}
      onKeyDown={handleKeyDown}
    >
      {cornerAction && (
        <div className="card-corner-action" onClick={(event) => event.stopPropagation()}>
          {cornerAction}
        </div>
      )}
      <div className="card-body">
        <div className="card-title">{title}</div>
        {subtitle && <div className="card-subtitle">{subtitle}</div>}
        {meta && <div className="card-meta">{meta}</div>}
        {badges && <div className="card-badges">{badges}</div>}
      </div>
    </div>
  );
}
