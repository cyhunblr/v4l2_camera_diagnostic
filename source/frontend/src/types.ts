export type Device = {
  path: string;
  driver: string;
  card: string;
  bus_info: string;
  supports_capture: boolean;
  supports_streaming: boolean;
  error: string;
  formats?: Array<{ fourcc: string; description: string; buffer_type: string }>;
};

export type Profile = {
  schema_version: number;
  id: string;
  name: string;
  description: string;
  enabled: boolean;
  camera_match: CameraMatcher;
  defaults: {
    trigger_mode: TriggerMode;
    memory_backends: string[];
    test_selectors: string[];
    report_formats: string[];
  };
  trigger_channels: TriggerChannel[];
  camera_bindings: Array<{ camera: CameraMatcher; trigger_channel_id: string }>;
};

export type TriggerMode = "hardware" | "software" | "free-run";

export type CameraMatcher = {
  driver: string;
  card: string;
  bus_info: string;
};

export type ControlWrite = {
  id: number;
  name: string;
  type: number;
  value: number;
};

export type TriggerChannel = {
  id: string;
  name: string;
  description: string;
  type: "hardware" | "software";
  gpio?: { chip_id: number; line_number: number; description: string };
  control_device?: {
    kind: "capture" | "video" | "subdevice";
    driver: string;
    card: string;
    bus_info: string;
    sysfs_name: string;
  };
  setup?: ControlWrite[];
  fire?: ControlWrite[];
  teardown?: ControlWrite[];
};

export type CameraAssignment = {
  path: string;
  profile_id: string;
  trigger_channel_id: string;
};

export type ControlDevice = {
  path: string;
  kind: "video" | "subdevice";
  driver: string;
  card: string;
  bus_info: string;
  sysfs_name: string;
  error: string;
  controls: Array<{
    id: number;
    type: number;
    name: string;
    minimum: number;
    maximum: number;
    step: number;
    default_value: number;
    current_value: number;
    writable: boolean;
    supported_for_trigger: boolean;
    menu_items: Array<{ value: number; name: string }>;
  }>;
};

export type TestDefinition = {
  id: string;
  name: string;
  category: string;
  description: string;
  implemented_in_core: boolean;
  long_running: boolean;
  risky: boolean;
  uses_trigger: boolean;
  supported_trigger_modes: TriggerMode[];
};

export type LogLine = {
  offset: number;
  timestamp_utc: string;
  severity: "info" | "warn" | "error";
  log_type: "section_start" | "progress" | "data" | "summary";
  camera: string;
  test: string;
  message: string;
};

export type ReportLink = {
  format: string;
  url: string;
  path?: string;
};

export type ConfirmDialogState = {
  title: string;
  message: string;
  confirmLabel: string;
  variant: "danger" | "primary";
  onConfirm: () => void;
};

/** Historical run summary — shape fixed by the GET /api/runs backend contract. */
export type RunSummary = {
  id: string;
  status: string;
  profile_id: string;
  camera_paths: string[];
  started_at_utc: string;
  finished_at_utc: string;
  duration_ms: number;
  pass_count: number;
  fail_count: number;
  warn_count: number;
  skip_count: number;
  reports: Array<{ format: string; url: string }>;
};

export type TestSummary = {
  test: string;
  status: string;
  message: string;
  camera: string;
};

export type StartRunPayload = {
  trigger_mode: TriggerMode;
  cameras: CameraAssignment[];
  memory_backends: string[];
  test_selectors: string[];
  report_formats: string[];
  run_mode: "parallel" | "sequential";
  include_long_tests: boolean;
  include_experimental_tests: boolean;
  threshold_config_id?: string;
};

export type PageId = "dashboard" | "cameras" | "profiles" | "tests" | "config" | "reports" | "output" | "results";

export type ThresholdConfig = {
  id: string;
  name: string;
  description: string;
  values: Record<string, Record<string, number>>;
  params: Record<string, Record<string, number>>;
};
