import React from "react";

type StatTileProps = {
  icon: React.ReactNode;
  label: string;
  value: React.ReactNode;
  tone?: "default" | "success" | "warn" | "error";
};

export function StatTile({ icon, label, value, tone = "default" }: StatTileProps) {
  return (
    <div className={`stat-tile tone-${tone}`}>
      <div className="stat-tile-icon">{icon}</div>
      <div>
        <p className="stat-tile-label">{label}</p>
        <p className="stat-tile-value">{value}</p>
      </div>
    </div>
  );
}
