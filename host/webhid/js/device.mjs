export const DEVICE_FILTER = Object.freeze({
  vendorId: 0xcafe,
  productId: 0x4010,
});

const PRODUCT_ID_PICOUART = 1;
const CHANNEL_COUNT = 6;
const MIN_TRAFFIC_RATE_WINDOW_MS = 1000;
const TRAFFIC_RATE_WINDOW_MS = 5000;
const MAX_RATE_ELAPSED_MS = 8000;
const MAX_REPORT_AGE_MS = 3000;
const ACTION_DEADLINE_MS = 3000;
const ACTION_POLL_MS = 50;
const UINT32_MAX = 0xffffffff;
const FEATURE_LENGTHS = new Map([
  [1, 27], [2, 36], [3, 20], [20, 4],
  ...Array.from({ length: CHANNEL_COUNT }, (_, channel) => [10 + channel, 30]),
  ...Array.from({ length: CHANNEL_COUNT }, (_, channel) => [30 + channel, 24]),
]);
const INPUT_LENGTHS = new Map(
  Array.from({ length: CHANNEL_COUNT }, (_, channel) => [10 + channel, 30]),
);
const CLOCK_PROFILES = new Map([
  [0, "SDK default"], [1, "150 MHz"], [2, "200 MHz"], [3, "250 MHz"], [4, "280 MHz"],
]);
const CONFIGURATION_ERRORS = [
  "None", "Unsupported baud setting", "Unsupported framing", "Backend configuration failed",
  "Control operation timed out", "Other or unknown configuration error",
];
const ACTION_STATUS = ["Idle", "Pending", "Completed", "Invalid", "Unsupported", "Busy", "Failed"];

function payload(report, expectedLength, reportName) {
  if (!(report instanceof DataView) || report.byteLength !== expectedLength) {
    throw new Error(`${reportName} must contain exactly ${expectedLength} payload bytes`);
  }
  return new DataView(report.buffer, report.byteOffset, report.byteLength);
}

function reportInfo(device, type, reportId) {
  const entries = [];
  const visit = collection => {
    for (const info of collection[`${type}Reports`] ?? []) {
      if (info.reportId === reportId) entries.push(info);
    }
    for (const child of collection.children ?? []) visit(child);
  };
  for (const collection of device.collections ?? []) visit(collection);
  if (!entries.length) return null;
  const lengths = entries.map(info => {
    const bits = (info.items ?? []).reduce((total, item) => total + item.reportSize * item.reportCount, 0);
    if (!Number.isSafeInteger(bits) || bits % 8) throw new Error(`Invalid HID descriptor length for report ${reportId}`);
    return bits / 8;
  });
  if (lengths.some(length => length !== lengths[0])) {
    throw new Error(`Conflicting HID descriptor lengths for report ${reportId}`);
  }
  return lengths[0];
}

export function validatePicoUartDescriptor(device) {
  for (const [reportId, length] of FEATURE_LENGTHS) {
    const reportedLength = reportInfo(device, "feature", reportId);
    if (reportedLength !== null && reportedLength !== length) {
      throw new Error(`HID descriptor has an invalid Feature report ${reportId} length`);
    }
  }
  for (const [reportId, length] of INPUT_LENGTHS) {
    const reportedLength = reportInfo(device, "input", reportId);
    if (reportedLength !== null && reportedLength !== length) {
      throw new Error(`HID descriptor has an invalid Input report ${reportId} length`);
    }
  }
}

export function identityDiscoveryLength(device) {
  const length = reportInfo(device, "feature", 1);
  const identityLength = length ?? FEATURE_LENGTHS.get(1);
  if (identityLength < 3) throw new Error("Identity Feature report is too short for product discovery");
  return identityLength;
}

export function decodeIdentityPrefix(report, declaredLength) {
  const data = payload(report, declaredLength, "Identity Feature report");
  if (data.byteLength < 3) throw new Error("Identity report is missing its product/layout prefix");
  return { layout_version: data.getUint8(0), product_id: data.getUint16(1, true) };
}

export function decodeDeviceInformation(report) {
  const data = payload(report, 27, "Identity Feature report");
  if (data.getUint8(0) !== 1) throw new Error("Unsupported PicoUart identity layout");
  const productId = data.getUint16(1, true);
  if (productId !== PRODUCT_ID_PICOUART) throw new Error(`Unsupported PicoUart product ID 0x${productId.toString(16).padStart(4, "0")}`);
  const uid = Array.from({ length: 8 }, (_, index) => data.getUint8(3 + index).toString(16).padStart(2, "0"))
    .join("").toUpperCase();
  const mcuId = data.getUint8(14);
  const modelId = data.getUint8(15);
  const channels = data.getUint8(16);
  const capabilities = data.getUint8(17);
  const profiles = data.getUint8(18);
  const ramTotal = data.getUint32(19, true);
  const ramReserved = data.getUint32(23, true);
  if (channels & ~0x3f || capabilities & ~0x07 || !(profiles & 1) || profiles & ~0x1f
    || !ramTotal || ramReserved > ramTotal) {
    throw new Error("Invalid PicoUart identity capabilities or RAM accounting");
  }
  return {
    product_id: productId,
    product_name: "PicoUart",
    serial_number: uid,
    firmware_version: `${data.getUint8(11)}.${data.getUint8(12)}.${data.getUint8(13)}`,
    mcu_id: mcuId,
    mcu: { 1: "RP2040", 2: "RP2350" }[mcuId] ?? `Unknown MCU (${mcuId})`,
    board_model_id: modelId,
    board_model: { 0: "Unknown", 1: "Pico", 2: "Pico W", 3: "Pico 2", 4: "Pico 2 W" }[modelId]
      ?? `Unknown board model (${modelId})`,
    hardware_channel_mask: channels,
    capabilities,
    clock_profile_mask: profiles,
    ram_total_bytes: ramTotal,
    ram_reserved_bytes: ramReserved,
  };
}

function validateProfile(profile) {
  return profile <= 4 || profile === 255;
}

export function decodeBoardConfiguration(report, identity) {
  const data = payload(report, 36, "Board Configuration Feature report");
  if (data.getUint8(0) !== 1) throw new Error("Unsupported board-configuration layout");
  const storedProfile = data.getUint8(1);
  const channelCount = data.getUint8(2);
  const pins = Array.from({ length: 6 }, (_, channel) =>
    Array.from({ length: 4 }, (_, signal) => data.getUint8(3 + channel * 4 + signal)));
  const resetCause = data.getUint8(27);
  const capacities = {
    uart_tx: data.getUint16(28, true),
    uart_rx: data.getUint16(30, true),
    cdc_tx: data.getUint16(32, true),
    cdc_rx: data.getUint16(34, true),
  };
  if (!validateProfile(storedProfile) || channelCount !== CHANNEL_COUNT || resetCause > 4
    || pins.some(channel => channel.some(pin => pin > 47 && pin !== 255))
    || Object.values(capacities).some(capacity => capacity === 0)
    || new Set(Object.values(capacities)).size !== 1
    || identity && identity.hardware_channel_mask >>> channelCount) {
    throw new Error("Invalid board-configuration fields");
  }
  return {
    stored_profile: storedProfile === 255 ? null : storedProfile,
    channel_count: channelCount,
    pins: pins.map(([tx, rx, rts, cts]) => ({ tx, rx, rts, cts })),
    reset_cause: resetCause,
    capacities,
  };
}

export function decodeTelemetryReport(report, identity) {
  const data = payload(report, 20, "Board Telemetry Feature report");
  if (data.getUint8(0) !== 1) throw new Error("Unsupported board-telemetry layout");
  const ledFlags = data.getUint8(1);
  const temperature = data.getInt16(2, true);
  const uptime = data.getBigUint64(4, true);
  const serviceLoad = [data.getUint16(12, true), data.getUint16(14, true)];
  const systemClock = data.getUint32(16, true);
  if (ledFlags & ~1 || uptime > BigInt(Number.MAX_SAFE_INTEGER) || !systemClock
    || serviceLoad.some(load => load > 10000 && load !== 65535)
    || !(identity.capabilities & 1) && ledFlags) {
    throw new Error("Invalid board telemetry");
  }
  const outputOn = identity.capabilities & 1 ? Boolean(ledFlags & 1) : null;
  return {
    temperature_celsius: temperature / 100,
    system_clock_hz: systemClock,
    runtime: {
      uptime_ms: Number(uptime),
      service_load_basis_points: serviceLoad.map(load => load === 65535 ? null : load),
    },
    led: { output_on: outputOn },
  };
}

const CHANNEL_STATES = ["Closed", "Starting", "Ready", "Configuring", "Error"];
const MEASUREMENT_STATES = ["Unavailable", "Current", "Cached"];
const PARITY_NAMES = ["N", "O", "E"];

export function decodeChannelSnapshot(report, reportId, identity, configuration) {
  if (!Number.isInteger(reportId) || reportId < 10 || reportId > 15) {
    throw new Error("Unexpected channel snapshot report ID");
  }
  const data = payload(report, 30, `Channel ${reportId - 10} snapshot`);
  const version = data.getUint8(0);
  const state = data.getUint8(1);
  const measurementState = data.getUint8(2);
  const baud = data.getUint32(3, true);
  const dataBits = data.getUint8(7);
  const stopBits = data.getUint8(8);
  const parityId = data.getUint8(9);
  const sequence = data.getUint16(10, true);
  const interval = data.getUint16(12, true);
  const counters = {
    uart_tx: data.getUint32(14, true),
    uart_rx: data.getUint32(18, true),
    usb_tx: data.getUint32(22, true),
    usb_rx: data.getUint32(26, true),
  };
  const pio = Boolean(identity && configuration
    && !(identity.hardware_channel_mask & (1 << (reportId - 10))));
  if (version !== 1 || state > 4 || measurementState > 2
    || (baud ? baud < 50 || baud > 3000000 || dataBits < 5 || dataBits > 8
      || ![1, 2].includes(stopBits) || parityId > 2
      || pio && (dataBits !== 8 || stopBits !== 1 || parityId !== 0)
      : dataBits !== 0 || stopBits !== 0 || parityId !== 0)
    || measurementState === 0 && (counters.uart_tx !== 0 || counters.uart_rx !== 0)) {
    throw new Error("Invalid channel snapshot fields");
  }
  return {
    id: reportId - 10,
    channel_state: state,
    channel_state_name: CHANNEL_STATES[state],
    uart_measurement_state: measurementState,
    uart_measurement_state_name: MEASUREMENT_STATES[measurementState],
    baud_rate: baud,
    data_bits: dataBits,
    stop_bits: stopBits,
    parity: baud ? PARITY_NAMES[parityId] : null,
    sequence,
    interval_ms: interval,
    counters,
  };
}

export function decodeChannelDiagnostics(report, reportId, configuration) {
  if (!Number.isInteger(reportId) || reportId < 30 || reportId > 35) {
    throw new Error("Unexpected channel-diagnostics report ID");
  }
  const data = payload(report, 24, `Channel ${reportId - 30} diagnostics`);
  const measurementState = data.getUint8(1);
  const sampleAge = data.getUint8(2);
  const lastConfigurationError = data.getUint8(3);
  const rxDroppedBytes = data.getUint32(4, true);
  const rxErrorObservations = data.getUint32(8, true);
  const lastFaultUptime = data.getUint32(12, true);
  const peaks = {
    uart_tx: data.getUint16(16, true),
    uart_rx: data.getUint16(18, true),
    cdc_tx: data.getUint16(20, true),
    cdc_rx: data.getUint16(22, true),
  };
  const unavailable = measurementState === 0;
  const noHistory = rxDroppedBytes === 0 && rxErrorObservations === 0 && lastConfigurationError === 0;
  if (data.getUint8(0) !== 1 || measurementState > 2
    || measurementState === 0 && sampleAge !== 255
    || measurementState === 1 && sampleAge !== 0
    || lastConfigurationError > 5 || noHistory && lastFaultUptime !== 0
    || unavailable && (rxDroppedBytes !== 0 || rxErrorObservations !== 0
      || peaks.uart_tx !== 0 || peaks.uart_rx !== 0)
    || configuration && Object.entries(peaks).some(([name, value]) => value > configuration.capacities[name])) {
    throw new Error("Invalid channel diagnostics");
  }
  return {
    id: reportId - 30,
    uart_measurement_state: measurementState,
    uart_measurement_state_name: MEASUREMENT_STATES[measurementState],
    uart_sample_age_100ms: sampleAge,
    last_configuration_error_reason: lastConfigurationError,
    last_configuration_error: CONFIGURATION_ERRORS[lastConfigurationError],
    rx_dropped_bytes: rxDroppedBytes,
    rx_error_observations: rxErrorObservations,
    last_fault_uptime_s: lastFaultUptime,
    peaks,
  };
}

function expectedActionStatus(status, error) {
  if (error === 0) return status <= 2;
  if (error >= 1 && error <= 4) return status === 3;
  if (error === 5) return status === 4;
  if (error === 6) return status === 5;
  return error >= 7 && error <= 9 && status === 6;
}

export function decodeActionResult(report) {
  const data = payload(report, 4, "Board Action & Result Feature report");
  const version = data.getUint8(0);
  const action = data.getUint8(1);
  const status = data.getUint8(2);
  const error = data.getUint8(3);
  const validIdle = status === 0 && action === 0 && error === 0;
  const validUnknownRejection = action === 0 && (status === 3 && error === 1 || status === 4 && error === 5);
  if (version !== 1 || status > 6 || error > 9 || !expectedActionStatus(status, error)
    || status === 0 && !validIdle
    || status > 0 && (action < 1 || action > 4) && !validUnknownRejection
    || action === 0 && status > 0 && !validUnknownRejection
    || status === 5 && action !== 4
    || status === 6 && action < 1) {
    throw new Error("Invalid board-action result");
  }
  return { action, status, status_name: ACTION_STATUS[status], error_reason: error };
}

export function encodeActionRequest(action, profile = 0, flags = 0) {
  const request = new Uint8Array(4);
  const data = new DataView(request.buffer);
  data.setUint8(0, 1);
  data.setUint8(1, action);
  data.setUint8(2, flags);
  data.setUint8(3, profile);
  return request;
}

function emptyChannel(id) {
  return {
    id,
    channel_state: null,
    channel_state_name: null,
    uart_measurement_state: null,
    uart_measurement_state_name: null,
    baud_rate: 0,
    data_bits: 0,
    stop_bits: 0,
    parity: null,
    sequence: null,
    interval_ms: null,
    counters: Object.fromEntries(["uart_tx", "uart_rx", "usb_tx", "usb_rx"].map(name => [name, null])),
    deltas: null,
    rates: null,
    rate_samples: [],
    input_updated_at: null,
    feature_updated_at: null,
    diagnostics: null,
    error: null,
    traffic_gaps: 0,
  };
}

function emptyDashboard() {
  return {
    connected: false,
    identity: null,
    identity_error: null,
    configuration: null,
    configuration_error: null,
    telemetry: null,
    telemetry_error: null,
    action_result: null,
    action_error: null,
    channel_diagnostics: Array(CHANNEL_COUNT).fill(null),
    diagnostics_errors: Array(CHANNEL_COUNT).fill(null),
    channels: Array.from({ length: CHANNEL_COUNT }, (_, id) => emptyChannel(id)),
    traffic_gaps: 0,
    traffic_incomplete: false,
    updated_at: null,
  };
}

export function matchesPicoUart(device) {
  return device?.vendorId === DEVICE_FILTER.vendorId && device?.productId === DEVICE_FILTER.productId;
}

export class PicoUartWebHid {
  constructor(hid, onChange = () => {}) {
    this.hid = hid;
    this.onChange = onChange;
    this.device = null;
    this.pendingDevice = null;
    this.generation = 0;
    this.information = null;
    this.actionInFlight = false;
    this.transferActive = false;
    this.transferToken = 0;
    this.ioBlocked = false;
    this.pendingTransfers = new WeakSet();
    this.inputListener = null;
    this.retryAfter = new Map();
    this.state = {
      phase: hid ? "disconnected" : "unsupported",
      message: hid ? "No device selected" : "WebHID is unavailable in this browser or context",
      error: null,
      dashboard: emptyDashboard(),
    };
    this.onDisconnect = event => {
      if (event.device !== this.device && event.device !== this.pendingDevice) return;
      this.generation++;
      this.removeInputListener(event.device);
      this.device = null;
      this.pendingDevice = null;
      this.information = null;
      this.update({
        phase: "disconnected", message: "Device unplugged", error: null,
        dashboard: emptyDashboard(),
      });
    };
    hid?.addEventListener("disconnect", this.onDisconnect);
  }

  update(changes) {
    this.state = { ...this.state, ...changes };
    this.onChange(this.state);
  }

  isCurrent(generation, device) {
    return generation === this.generation && device === this.device;
  }

  addInputListener(device, generation) {
    this.removeInputListener(device);
    this.inputListener = event => {
      if (event.device !== device || !this.isCurrent(generation, device)
        || event.reportId < 10 || event.reportId > 15) return;
      try {
        const snapshot = decodeChannelSnapshot(event.data, event.reportId, this.information,
          this.state.dashboard.configuration);
        this.applyInputSnapshot(snapshot);
      } catch (error) {
        this.failChannel(event.reportId - 10, error.message);
      }
    };
    device.addEventListener("inputreport", this.inputListener);
  }

  removeInputListener(device) {
    if (device && this.inputListener) device.removeEventListener("inputreport", this.inputListener);
    this.inputListener = null;
  }

  async transfer(operation, timeout = 3000) {
    const device = this.device;
    const generation = this.generation;
    if (!device || this.ioBlocked || this.transferActive || this.pendingTransfers.has(device)) {
      throw new Error("A HID transfer is still active or unconfirmed; disconnect before continuing.");
    }
    this.transferActive = true;
    const token = ++this.transferToken;
    let timer;
    const pending = Promise.resolve().then(operation);
    this.pendingTransfers.add(device);
    pending.finally(() => this.pendingTransfers.delete(device)).catch(() => {});
    try {
      const result = await Promise.race([pending, new Promise((_, reject) => {
        timer = globalThis.setTimeout(() => reject(new Error("HID operation timed out")), timeout);
      })]);
      if (!this.isCurrent(generation, device) || token !== this.transferToken) {
        throw new Error("HID connection changed during operation");
      }
      return result;
    } catch (error) {
      if (error.message === "HID operation timed out" && this.isCurrent(generation, device)) {
        this.ioBlocked = true;
        this.transferToken++;
        throw new Error("HID operation timed out; outcome is unconfirmed. Disconnect before continuing.");
      }
      throw error;
    } finally {
      globalThis.clearTimeout(timer);
      if (token === this.transferToken) this.transferActive = false;
    }
  }

  async readFeature(reportId, length, timeout = 3000) {
    const device = this.device;
    const data = await this.transfer(() => device.receiveFeatureReport(reportId), timeout);
    return payload(data, length, `Feature report ${reportId}`);
  }

  async discoverIdentity() {
    const declaredLength = identityDiscoveryLength(this.device);
    const identityReport = await this.readFeature(1, declaredLength);
    const prefix = decodeIdentityPrefix(identityReport, declaredLength);
    if (prefix.product_id !== PRODUCT_ID_PICOUART) {
      const productId = prefix.product_id;
      this.update({ dashboard: { ...this.state.dashboard, identity: {
        product_id: productId,
        product_name: `Unknown product (0x${productId.toString(16).padStart(4, "0")})`,
        layout_version: prefix.layout_version,
      }, identity_error: "No supported report contract for this product" } });
      return false;
    }
    if (prefix.layout_version !== 1) {
      throw new Error(`Unsupported PicoUart identity layout version ${prefix.layout_version}`);
    }
    validatePicoUartDescriptor(this.device);
    const identity = decodeDeviceInformation(identityReport);
    this.information = identity;
    this.update({ dashboard: { ...this.state.dashboard, identity, identity_error: null } });
    return true;
  }

  updateChannel(index, changes) {
    const channels = [...this.state.dashboard.channels];
    channels[index] = { ...channels[index], ...changes };
    this.update({ dashboard: { ...this.state.dashboard, channels } });
  }

  applyFeatureSnapshot(snapshot) {
    const previous = this.state.dashboard.channels[snapshot.id];
    this.updateChannel(snapshot.id, {
      ...snapshot,
      counters: snapshot.counters,
      feature_updated_at: new Date().toISOString(),
      error: null,
      rates: previous.rates,
      deltas: previous.deltas,
      input_updated_at: previous.input_updated_at,
      traffic_gaps: previous.traffic_gaps,
    });
  }

  applyInputSnapshot(snapshot) {
    const dashboard = this.state.dashboard;
    const previous = dashboard.channels[snapshot.id];
    const now = Date.now();
    const previousTime = Date.parse(previous.input_updated_at);
    const stale = previous.sequence !== null
      && (!Number.isFinite(previousTime) || now - previousTime > MAX_REPORT_AGE_MS);
    const first = previous.sequence === null || stale;
    const advance = first ? null : (snapshot.sequence - previous.sequence + 65536) % 65536;
    if (!first && (advance === 0 || advance >= 32768)) return;
    const sequenceGap = !first && advance !== 1;
    const intervalValid = snapshot.interval_ms > 0 && snapshot.interval_ms < 65535;
    const continuous = !first && !sequenceGap && intervalValid;
    const elapsed = Number.isFinite(previousTime) ? now - previousTime : 0;
    const rateSamples = first ? [] : [...previous.rate_samples];
    rateSamples.push({ receivedAt: now, counters: snapshot.counters,
      measurement: snapshot.uart_measurement_state });
    const cutoff = now - TRAFFIC_RATE_WINDOW_MS;
    while (rateSamples.length > 2 && rateSamples[1].receivedAt <= cutoff) rateSamples.shift();
    const baseline = rateSamples[0];
    const rateElapsed = now - baseline.receivedAt;
    const rateWindowValid = rateSamples.length > 1 && rateElapsed > 0 && rateElapsed <= MAX_RATE_ELAPSED_MS;
    const deltas = Object.fromEntries(Object.keys(snapshot.counters).map(name => [
      name,
      continuous ? (snapshot.counters[name] - previous.counters[name]) >>> 0 : null,
    ]));
    const rates = Object.fromEntries(Object.entries(snapshot.counters).map(([name, counter]) => {
      const uart = name === "uart_tx" || name === "uart_rx";
      const samples = uart ? rateSamples.filter(sample => sample.measurement !== 0) : rateSamples;
      const rateBaseline = samples[0];
      const latest = samples.at(-1);
      const elapsed = rateBaseline && latest ? latest.receivedAt - rateBaseline.receivedAt : 0;
      const delta = rateBaseline && latest ? (counter - rateBaseline.counters[name]) >>> 0 : 0;
      return [name, elapsed >= MIN_TRAFFIC_RATE_WINDOW_MS && elapsed <= MAX_RATE_ELAPSED_MS
        ? delta * 1000 / elapsed : null];
    }));
    const channelGaps = previous.traffic_gaps + Number(sequenceGap || stale);
    const channels = [...dashboard.channels];
    channels[snapshot.id] = {
      ...snapshot,
      counters: snapshot.counters,
      deltas,
      rates,
      rate_samples: rateSamples,
      input_updated_at: new Date(now).toISOString(),
      feature_updated_at: previous.feature_updated_at,
      error: null,
      traffic_gaps: channelGaps,
    };
    const incomplete = sequenceGap || stale || !intervalValid
      || snapshot.uart_measurement_state !== 1
      || previous.sequence !== null && previous.uart_measurement_state !== 1;
    this.update({ dashboard: {
      ...dashboard,
      channels,
      traffic_gaps: dashboard.traffic_gaps + Number(sequenceGap || stale),
      traffic_incomplete: dashboard.traffic_incomplete || incomplete,
    } });
  }

  failChannel(id, message) {
    const dashboard = this.state.dashboard;
    const channels = [...dashboard.channels];
    channels[id] = {
      ...channels[id],
      channel_state: null,
      channel_state_name: null,
      rates: null,
      error: message,
      input_updated_at: null,
      feature_updated_at: null,
    };
    this.update({ dashboard: { ...dashboard, channels } });
  }

  async readConfiguration() {
    const configuration = decodeBoardConfiguration(
      await this.readFeature(2, 36), this.information);
    this.update({ dashboard: { ...this.state.dashboard, configuration, configuration_error: null } });
  }

  async readTelemetry() {
    const telemetry = decodeTelemetryReport(await this.readFeature(3, 20), this.information);
    this.update({ dashboard: { ...this.state.dashboard, telemetry, telemetry_error: null,
      updated_at: new Date().toISOString() } });
  }

  async readActionResult() {
    const result = decodeActionResult(await this.readFeature(20, 4));
    this.update({ dashboard: { ...this.state.dashboard, action_result: result, action_error: null } });
  }

  async readChannelDetails() {
    for (let channel = 0; channel < CHANNEL_COUNT; channel++) {
      if (!this.device || this.ioBlocked) return;
      try {
        const snapshot = decodeChannelSnapshot(
          await this.readFeature(10 + channel, 30), 10 + channel,
          this.information, this.state.dashboard.configuration);
        this.applyFeatureSnapshot(snapshot);
      } catch (error) {
        if (this.device) this.failChannel(channel, error.message);
        if (this.ioBlocked) return;
      }
    }
    for (let channel = 0; channel < CHANNEL_COUNT; channel++) {
      if (!this.device || this.ioBlocked) return;
      try {
        const diagnostic = decodeChannelDiagnostics(
          await this.readFeature(30 + channel, 24), 30 + channel,
          this.state.dashboard.configuration);
        const diagnostics = [...this.state.dashboard.channel_diagnostics];
        const errors = [...this.state.dashboard.diagnostics_errors];
        diagnostics[channel] = { ...diagnostic, updated_at: new Date().toISOString() };
        errors[channel] = null;
        this.update({ dashboard: { ...this.state.dashboard,
          channel_diagnostics: diagnostics, diagnostics_errors: errors } });
      } catch (error) {
        if (!this.device) return;
        const diagnostics = [...this.state.dashboard.channel_diagnostics];
        const errors = [...this.state.dashboard.diagnostics_errors];
        diagnostics[channel] = null;
        errors[channel] = error.message;
        this.update({ dashboard: { ...this.state.dashboard,
          channel_diagnostics: diagnostics, diagnostics_errors: errors } });
        if (this.ioBlocked) return;
      }
    }
  }

  async refresh(initial = false) {
    const device = this.device;
    const generation = this.generation;
    if (!device || this.actionInFlight || !["connected", "reading"].includes(this.state.phase)
      || this.ioBlocked || this.refreshInFlight) return;
    this.refreshInFlight = true;
    this.update({ phase: "reading", message: "Reading HID status", error: null });
    try {
      if (!this.information) {
        try {
          if (!await this.discoverIdentity()) return;
        } catch (error) {
          if (!this.isCurrent(generation, device)) return;
          this.update({ dashboard: { ...this.state.dashboard,
            identity: null, identity_error: error.message } });
          return;
        }
      }
      if (!this.state.dashboard.configuration) {
        try {
          await this.readConfiguration();
        } catch (error) {
          if (!this.isCurrent(generation, device)) return;
          this.update({ dashboard: { ...this.state.dashboard, configuration: null,
            configuration_error: error.message } });
        }
      }
      if (!this.state.dashboard.action_result) {
        try {
          await this.readActionResult();
        } catch (error) {
          if (!this.isCurrent(generation, device)) return;
          this.update({ dashboard: { ...this.state.dashboard, action_error: error.message } });
        }
      }
      try {
        await this.readTelemetry();
      } catch (error) {
        if (!this.isCurrent(generation, device)) return;
        this.update({ dashboard: { ...this.state.dashboard, telemetry: null, telemetry_error: error.message } });
      }
      if (initial) await this.readChannelDetails();
      if (!this.isCurrent(generation, device)) return;
      this.update({ phase: "connected", message: "HID status received",
        error: this.state.dashboard.identity_error ?? this.state.dashboard.configuration_error
          ?? this.state.dashboard.telemetry_error ?? this.state.dashboard.action_error });
    } finally {
      this.refreshInFlight = false;
      if (this.isCurrent(generation, device) && this.state.phase === "reading") {
        this.update({ phase: "connected" });
      }
    }
  }

  async refreshDiagnostics() {
    if (!this.device || this.actionInFlight || this.state.phase !== "connected") return;
    const generation = this.generation, device = this.device;
    this.update({ phase: "reading", message: "Refreshing channel diagnostics", error: null });
    try {
      if (!this.state.dashboard.configuration && this.information) await this.readConfiguration();
      await this.readChannelDetails();
    } finally {
      if (this.isCurrent(generation, device)) this.update({ phase: "connected" });
    }
  }

  async performAction(action, { profile = 0, acknowledgeOverclock = false, confirmed = false } = {}) {
    const device = this.device, generation = this.generation;
    if (!device || this.state.phase !== "connected" || this.actionInFlight) return;
    const identity = this.information;
    if (!identity) throw new Error("Read a supported device identity before sending commands.");
    const capability = action <= 2 ? 1 : action === 3 ? 2 : 4;
    if (!(identity.capabilities & capability)) throw new Error("Board-action capability unavailable.");
    if (action === 3 && !confirmed || action === 4 && !confirmed) {
      throw new Error("Confirm the destructive board action before continuing.");
    }
    if (action === 4 && (!Number.isInteger(profile) || profile < 0 || profile > 4
      || !(identity.clock_profile_mask & (1 << profile))
      || typeof acknowledgeOverclock !== "boolean"
      || profile === 0 && acknowledgeOverclock || profile !== 0 && !acknowledgeOverclock)) {
      throw new Error("Choose an advertised profile and acknowledge non-default overclocking.");
    }
    this.actionInFlight = true;
    this.update({ phase: "writing", message: "Sending board action", error: null });
    try {
      await this.readActionResult();
      const previous = this.state.dashboard.action_result;
      if (previous.status === 1 || previous.status === 2 && [3, 4].includes(previous.action)) {
        throw new Error("A board action is pending or awaiting reboot.");
      }
      const flags = action === 3 ? 1 : action === 4 ? 1 | (profile === 0 ? 0 : 2) : 0;
      const request = encodeActionRequest(action, action === 4 ? profile : 0, flags);
      await this.transfer(() => device.sendFeatureReport(20, request), 3000);
      const deadline = Date.now() + ACTION_DEADLINE_MS;
      while (Date.now() < deadline) {
        if (!this.isCurrent(generation, device)) {
          throw new Error("HID connection changed during action; outcome is unconfirmed.");
        }
        const remaining = deadline - Date.now();
        const result = decodeActionResult(await this.readFeature(20, 4, remaining));
        this.update({ dashboard: { ...this.state.dashboard, action_result: result, action_error: null } });
        if (result.action !== action) {
          throw new Error("Latest board-action result does not match; outcome is unconfirmed.");
        }
        if (result.status === 2) return result;
        if (result.status !== 1) {
          throw new Error(`Board action ${result.status_name.toLowerCase()} (reason ${result.error_reason}).`);
        }
        await new Promise(resolve => globalThis.setTimeout(resolve, Math.min(ACTION_POLL_MS,
          Math.max(0, deadline - Date.now()))));
      }
      throw new Error("Timed out awaiting board action; outcome is unconfirmed.");
    } catch (error) {
      if (this.isCurrent(generation, device)) {
        this.update({ dashboard: { ...this.state.dashboard, action_error: error.message } });
      }
      throw error;
    } finally {
      this.actionInFlight = false;
      if (this.isCurrent(generation, device)) this.update({ phase: "connected" });
    }
  }

  async setLed(on) {
    if (typeof on !== "boolean") throw new Error("LED state must be explicit.");
    const result = await this.performAction(on ? 2 : 1);
    await this.refresh();
    return result;
  }

  async reboot(confirmed) {
    return this.performAction(3, { confirmed });
  }

  async saveConfiguration(profile, acknowledgeOverclock, confirmed) {
    return this.performAction(4, { profile, acknowledgeOverclock, confirmed });
  }

  async connect() {
    if (!this.hid || this.state.phase !== "disconnected") return;
    const generation = ++this.generation;
    this.update({ phase: "connecting", message: "Selecting PicoUart HID interface", error: null,
      dashboard: emptyDashboard() });
    let selected;
    try {
      const devices = await this.hid.requestDevice({ filters: [{ vendorId: DEVICE_FILTER.vendorId }] });
      if (generation !== this.generation) return;
      if (!devices.length) {
        this.update({ phase: "disconnected", message: "No device selected" });
        return;
      }
      if (devices.length !== 1 || !matchesPicoUart(devices[0])) {
        throw new Error("Select one PicoUart vendor HID interface.");
      }
      selected = devices[0];
      this.pendingDevice = selected;
      if (!selected.opened) await selected.open();
      if (generation !== this.generation) {
        if (selected.opened) await selected.close();
        return;
      }
      this.pendingDevice = null;
      this.device = selected;
      this.information = null;
      this.ioBlocked = false;
      this.retryAfter.clear();
      this.update({ phase: "connected", message: "Device connected",
        dashboard: { ...emptyDashboard(), connected: true } });
      this.addInputListener(selected, generation);
      await this.refresh(true);
    } catch (error) {
      if (selected?.opened) {
        this.removeInputListener(selected);
        try {
          await selected.close();
        } catch (closeError) {
          error = new Error(`${error.message}; ${closeError.message}`);
        }
      }
      if (generation !== this.generation) return;
      this.device = null;
      this.pendingDevice = null;
      this.update({ phase: "disconnected", message: "Connection failed", error: error.message,
        dashboard: emptyDashboard() });
    }
  }

  async disconnect() {
    if (!this.hid) return;
    const device = this.device ?? this.pendingDevice;
    const generation = ++this.generation;
    this.transferToken++;
    this.device = null;
    this.pendingDevice = device;
    this.removeInputListener(device);
    this.update({ phase: "disconnecting", message: "Closing HID interface" });
    try {
      if (device?.opened) await device.close();
      if (generation !== this.generation) return;
      this.pendingDevice = null;
      this.information = null;
      this.ioBlocked = false;
      this.update({ phase: "disconnected", message: "Device disconnected",
        dashboard: emptyDashboard() });
    } catch (error) {
      if (generation !== this.generation) return;
      this.device = device;
      this.pendingDevice = null;
      this.addInputListener(device, generation);
      this.update({ phase: "connected", message: "Disconnect failed", error: error.message });
    }
  }
}
