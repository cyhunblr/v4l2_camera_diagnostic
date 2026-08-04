export type Device = {
  path: string;
  driver: string;
  card: string;
  bus_info: string;
  supports_capture: boolean;
  supports_streaming: boolean;
  /** Backend already sends this (web_server.cpp `out["readable"]`); the type had it missing. */
  readable: boolean;
  error: string;
  formats?: Array<{ fourcc: string; description: string; buffer_type: string }>;
};

export type Profile = {
  schema_version: number;
  id: string;
  name: string;
  description: string;
  defaults: {
    trigger_mode: TriggerMode;
    memory_backends: string[];
    test_selectors: string[];
    trigger_rate_hz: number;
    pulse_width_ms: number;
  };
  trigger_channels: TriggerChannel[];
  /**
   * role -> trigger channel. v4 replaced `camera_match` and matcher-based
   * `camera_bindings`: a Trigger Profile does not identify a physical camera, since
   * {driver, card, bus_info} cannot tell this hardware's four video nodes apart.
   * Roles come from the run topology — "master", then "slave-1".."slave-N".
   * Empty is legitimate for a free-run profile.
   */
  role_bindings: RoleBinding[];
};

/** One role bound to one trigger channel. A channel may serve several roles. */
export type RoleBinding = {
  role: string;
  trigger_channel_id: string;
};

/** The canonical master role; slaves are `slave-1`.. in RunConfig.slaves order. */
export const MASTER_ROLE = "master";

/** The role name for slaves[index], 0-based index -> "slave-1". */
export function slaveRole(index: number): string {
  return `slave-${index + 1}`;
}

/** The roles a run with `slaveCount` slaves expects, in topology order. */
export function expectedRoles(slaveCount: number): string[] {
  const roles = [MASTER_ROLE];
  for (let i = 0; i < slaveCount; i++) {
    roles.push(slaveRole(i));
  }
  return roles;
}

export type TriggerMode = "hardware" | "software" | "free-run";

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

/**
 * A camera selected for a run, plus the role it will play. v4 dropped the
 * per-camera `profile_id` and `trigger_channel_id`: one run uses one Trigger
 * Profile, and the channel follows from the role.
 */
export type CameraAssignment = {
  path: string;
  role: string;
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

/** Backend migration state for one stored config file. Wire model for plan 2.4. */
export type ConfigVersionState = "current" | "legacy" | "future" | "malformed";

/**
 * How one config relates to this build. Carries no file identity: the same report
 * shape comes back from GET /api/profiles' `configs[]`, from an import preview and
 * from a rejected save, and only the first of those is about a file on disk.
 */
export type MigrationReport = {
  state: ConfigVersionState;
  source_version: number;
  target_version: number;
  /** Fields carried across, as "old -> new". */
  migrated: string[];
  /** Fields the target schema no longer has; their values are discarded. */
  dropped: string[];
  /** Mandatory fields the user must supply before this config can run. */
  required: string[];
  /** Field-level validation failures. These block a run just as `required` does. */
  invalid: string[];
  usable_for_run: boolean;
  needs_user_input: boolean;
  /** True exactly when `draft_profile` accompanies this report. */
  has_draft: boolean;
  error: string;
};

/**
 * One stored config file, as GET /api/profiles reports it. The backend flattens
 * the report's fields into the same object, so this extends it.
 *
 * A config listed here is NOT necessarily runnable — `profiles[]` is the runnable
 * list. When `has_draft` is true, `draft_profile` holds the migrated values for
 * prefilling the migration form.
 */
export type StoredProfileConfig = MigrationReport & {
  file: string;
  /** Profile id read from the file; empty when the file could not be parsed. */
  profile_id: string;
  draft_profile?: Profile;
};

export type TestDefinition = {
  id: string;
  name: string;
  category: string;
  description: string;
  uses_trigger: boolean;
  requires_dmabuf?: boolean;
  /**
   * Optional because a consumer may be reading an older response: the key used to be
   * created only inside its serialisation loop, so a test supporting no mode sent none
   * at all. Current builds always send an array (plan 2.7), but the type stays honest
   * about what may arrive.
   */
  supported_trigger_modes?: TriggerMode[];
  tags: string[];
  /** 1-7, the user-facing layer. Backend metadata — never derived from the id. */
  layer: number;
  /** Short layer name, e.g. "Buffer & memory". */
  layer_name: string;
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
  /**
   * The run's trigger mode. The backend has always sent this
   * (`run_summary_to_json`); the type was missing it, so the Dashboard could not show
   * a free-run row's Trigger Profile cell correctly.
   */
  trigger_mode: TriggerMode;
  /** The single run-level Trigger Profile. Empty for free-run. */
  trigger_profile_id: string;
  /** The validated routing, in topology order. Empty for free-run. */
  role_bindings: RoleBinding[];
  camera_paths: string[];
  started_at_utc: string;
  finished_at_utc: string;
  duration_ms: number;
  pass_count: number;
  fail_count: number;
  warn_count: number;
  skip_count: number;
  /**
   * True when the run stopped before recording every result. Optional because history
   * entries written before the field existed do not carry it -- and their absence is
   * not evidence of truncation (plan 2.8.1).
   */
  truncated?: boolean;
  reports: Array<{ format: string; url: string }>;
};

export type TestSummary = {
  test: string;
  status: string;
  message: string;
  camera: string;
};

/** One camera's structured result, as GET /api/runs/{id} reports it. */
export type CameraResult = {
  camera_path: string;
  role: string;
  trigger_description: string;
  memory_backends: string[];
  tests: Array<{
    id: string;
    name: string;
    category: string;
    memory_backend: string;
    status: string;
    summary: string;
    duration_ms: number;
    metrics: Array<{ name: string; unit: string; value: number; description: string }>;
    details: string[];
    notes: string[];
    warnings: string[];
  }>;
};

/**
 * The run's structured result. Read from GET /api/runs/{id} -- never derived from log
 * text: a human-readable message is not a data source for a status (plan 2.6.1).
 */
export type RunResultPayload = {
  result_schema_version: number;
  project_name: string;
  started_at_utc: string;
  finished_at_utc: string;
  host_name: string;
  kernel_release: string;
  kernel_version: string;
  run_mode: string;
  trigger_mode: TriggerMode;
  trigger_profile_id: string;
  role_bindings: RoleBinding[];
  trigger_rate_hz?: number;
  pulse_width_ms?: number;
  cameras: CameraResult[];
};

export type StartRunPayload = {
  trigger_mode: TriggerMode;
  /** The single run-level Trigger Profile. Omitted/empty for free-run. */
  trigger_profile_id: string;
  /** The single camera under test — the full test suite runs against it. */
  master: CameraAssignment;
  /** Extra cameras that only participate in t25-multi-camera. Empty means t25 is skipped. */
  slaves: CameraAssignment[];
  memory_backends: string[];
  test_selectors: string[];
  threshold_config_id?: string;
};

/** No "reports": the Report Formats step was removed in v5 (plan 2.10). */
export type PageId = "dashboard" | "cameras" | "profiles" | "tests" | "config" | "output" | "results";

export type ThresholdConfig = {
  id: string;
  name: string;
  description: string;
  values: Record<string, Record<string, number>>;
  params: Record<string, Record<string, number>>;
};

/**
 * Flattens a structured run result into the per-test rows the results table renders.
 *
 * This replaced two byte-identical copies of a regex over log text
 * (`line.message.match(/^(.+?) \[(.+?)\] (.+)$/)` in ResultsPage and App). A
 * human-readable message is not a data source: when the wording changed, both copies
 * silently fell through to a `"done"` status. The status now comes from the backend's
 * own field (plan 2.6.1).
 */
export function summariesFromResult(result: RunResultPayload | null | undefined): TestSummary[] {
  if (!result?.cameras) return [];
  const rows: TestSummary[] = [];
  for (const camera of result.cameras) {
    for (const test of camera.tests ?? []) {
      rows.push({
        test: test.name || test.id,
        status: test.status,
        message: test.summary,
        camera: camera.camera_path
      });
    }
  }
  return rows;
}

/**
 * Heading for a test's layer group, composed from backend metadata.
 *
 * This replaced a function that parsed the number out of the test id and mapped
 * it through hard-coded ranges: every test past t26 fell into an "Other
 * Diagnostics" bucket, and a renumbering silently regrouped the UI.
 */
export function testLayerHeading(test: TestDefinition): string {
  return `Layer ${test.layer} — ${test.layer_name}`;
}

/** A layer heading with the items that belong under it. */
export type LayerGroup<T> = [heading: string, items: T[]];

/**
 * Groups tests by their backend layer, ordered 1-7 by layer number.
 *
 * Order comes from the metadata, not from the order the tests arrive in: the
 * previous implementation grouped into a Map and returned insertion order, so
 * the page reordered itself whenever the API response order changed.
 */
export function groupTestsByLayer(tests: TestDefinition[]): Array<LayerGroup<TestDefinition>> {
  const groups = new Map<number, LayerGroup<TestDefinition>>();
  for (const test of tests) {
    const group = groups.get(test.layer);
    if (group) group[1].push(test);
    else groups.set(test.layer, [testLayerHeading(test), [test]]);
  }
  return [...groups.entries()].sort((a, b) => a[0] - b[0]).map(([, group]) => group);
}

/** Heading used for preset keys that no registered test claims. */
export const UNKNOWN_TEST_HEADING = "Unknown test";

/**
 * Groups threshold-preset test ids by the layer the registry reports for them.
 *
 * Ids with no registry entry are collected under UNKNOWN_TEST_HEADING and placed
 * after every real layer — those are stale preset keys, not a new layer, and the
 * previous id-range guess silently mixed them in with unrecognised test numbers.
 */
export function groupTestIdsByLayer(ids: string[], tests: TestDefinition[]): Array<LayerGroup<string>> {
  const byId = new Map(tests.map((test) => [test.id, test]));
  const unknownKey = Number.MAX_SAFE_INTEGER;
  const groups = new Map<number, LayerGroup<string>>();
  for (const id of ids) {
    const test = byId.get(id);
    const key = test ? test.layer : unknownKey;
    const heading = test ? testLayerHeading(test) : UNKNOWN_TEST_HEADING;
    const group = groups.get(key);
    if (group) group[1].push(id);
    else groups.set(key, [heading, [id]]);
  }
  return [...groups.entries()].sort((a, b) => a[0] - b[0]).map(([, group]) => group);
}
