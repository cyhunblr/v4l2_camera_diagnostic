import { FileDown } from "lucide-react";

const FORMAT_OPTIONS = ["json", "md", "html"];

type Props = {
  reports: string[];
  onToggle: (format: string) => void;
};

export function ReportFormatsPage({ reports, onToggle }: Props) {
  return (
    <div className="page">
      <header className="topbar">
        <div>
          <p className="eyebrow">Configure</p>
          <h2>Report Formats</h2>
        </div>
      </header>

      <div className="panel">
        <div className="panel-title">
          <FileDown size={18} />
          <h3>Output Formats</h3>
        </div>
        <p className="panel-hint">Choose which report formats to generate when a diagnostic run completes.</p>
        <p className="panel-hint">The HTML report includes an "Export as PDF" button that uses your browser's print dialog — no separate PDF format needed.</p>
        <div className="choice-row">
          {FORMAT_OPTIONS.map((format) => (
            <button
              key={format}
              className={reports.includes(format) ? "selected pill" : "pill"}
              onClick={() => onToggle(format)}
            >
              {format.toUpperCase()}
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}
