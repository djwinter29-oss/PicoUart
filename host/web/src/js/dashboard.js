// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
const token = document.querySelector('meta[name="csrf-token"]').content;
const connection = document.querySelector("#connection");
const channelRows = document.querySelector("#channel-rows");
const diagnosticsDialog = document.querySelector("#diagnostics-dialog");
const diagnosticsContent = document.querySelector("#diagnostics-content");
const statusMessage = document.querySelector("#status-message");
const clockProfile = document.querySelector("#clock-profile");
const saveConfigurationButton = document.querySelector("#save-configuration");
const configurationDialog = document.querySelector("#configuration-dialog");
const rebootDialog = document.querySelector("#reboot-dialog");
const configurationDialogProfile = document.querySelector("#configuration-dialog-profile");
const confirmConfigurationButton = document.querySelector("#confirm-configuration");
const overclockAcknowledgement = document.querySelector("#overclock-ack");
const overclockAcknowledgementLabel = document.querySelector("#overclock-acknowledgement");
const configurationRecovery = document.querySelector("#configuration-recovery");
const configurationRecoveryMessage = document.querySelector("#configuration-recovery-message");
const acknowledgeConfigurationOutcomeButton = document.querySelector("#acknowledge-configuration-outcome");
document.querySelector("#close-diagnostics").addEventListener("click", () => diagnosticsDialog.close());
document.querySelector("#refresh-diagnostics").addEventListener("click", async () => {
  invalidateDiagnostics();
  renderDiagnosticContent(diagnosticsContent, Number(diagnosticsContent.dataset.channel));
  await loadDiagnostics();
});
const actionButtons = [...document.querySelectorAll(".actions button")];
const boardSelect = document.querySelector("#board-select");
const rescanButton = document.querySelector("#rescan-boards");
let actionBusy = false;
let latestData = null;
let selectedBoard = null;
let selectedLabel = "";
let configurationProfileDirty = false;
let configurationSavePending = false;
const pendingConfigurationProfiles = new Map();
const uncertainConfigurationBoards = new Set();
const acknowledgedConfigurationWarnings = new Map();
const configurationRecoveryStatusFloor = new Map();
const configurationRecoveryReadyBoards = new Set();
let selectionVersion = 0;
let refreshing = null;
let statusRequestId = 0;
let scanning = false;
let scanError = null;
let refreshIntervalMs = 500;
let refreshTimer = null;
let diagnosticBoard = null;
let diagnosticData = null;
let diagnosticError = null;
let diagnosticRequest = null;
const channelHistory = new Map();
const channelRowViews = new Map();
const MIN_TRAFFIC_RATE_WINDOW_MS = 1000;
const TRAFFIC_RATE_WINDOW_MS = 5000;
const MAX_RATE_ELAPSED_MS = 8000;
const STATUS_REQUEST_TIMEOUT_MS = 5000;
const BOARD_SCAN_TIMEOUT_MS = 10000;
const DIAGNOSTICS_REQUEST_TIMEOUT_MS = 15000;
const CONTROL_REQUEST_TIMEOUT_MS = 30000;
let reportSample = null;
let reportReceivedAt = 0;
const boardActionErrors = new Map();
let diagnosticReadAt = null;
let diagnosticAgeElement = null;
let diagnosticEpoch = 0;

const directions = [
  ["UART TX", "uart_tx", true],
  ["UART RX", "uart_rx", true],
  ["USB RX", "usb_rx", false],
  ["USB TX", "usb_tx", false],
];
const channelStates = ["closed", "starting", "ready", "configuring", "error"];
const measurementStates = ["Unavailable", "Current", "Cached"];
const resetCauses = [
  "Unknown (not distinguished)", "Power-on", "Watchdog reset", "Software-requested reset", "HardFault recovery",
  "Power-on or brownout", "Brownout", "RUN-pin reset", "Debugger reset", "User-requested reboot",
  "Configuration-save reboot",
];
const errorReasons = [
  "None recorded",
  "Unsupported baud setting",
  "Unsupported framing",
  "Backend configuration failed",
  "Control operation timed out",
  "Other or unknown configuration error",
];

function setRefreshInterval(interval) {
  const delay = Number.isFinite(interval) ? Math.max(500, Math.min(1000, interval)) : 500;
  if (refreshTimer !== null && delay === refreshIntervalMs) return;
  if (refreshTimer !== null) window.clearInterval(refreshTimer);
  refreshIntervalMs = delay;
  refreshTimer = window.setInterval(refresh, delay);
}

async function fetchJson(url, options, timeoutMs) {
  const controller = new AbortController();
  let rejectTimeout;
  const timeout = new Promise((_, reject) => { rejectTimeout = reject; });
  const timeoutId = window.setTimeout(() => {
    controller.abort();
    rejectTimeout(new Error(`Request timed out after ${timeoutMs} ms`));
  }, timeoutMs);
  const request = (async () => {
    const response = await fetch(url, { ...options, signal: controller.signal });
    return { response, data: await response.json() };
  })();
  try {
    return await Promise.race([request, timeout]);
  } finally {
    window.clearTimeout(timeoutId);
  }
}

function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

function updateReportAge(data, now = Date.now()) {
  const sample = Date.parse(data.updated_at);
  const age = document.querySelector("#sample-time");
  if (!data.connected) reportSample = null;
  if (data.connected && Number.isFinite(sample) && sample !== reportSample) {
    reportSample = sample;
    reportReceivedAt = now;
  }
  const seconds = Math.max(0, Math.floor((now - reportReceivedAt) / 1000));
  const stale = Number.isFinite(sample) && now - reportReceivedAt > 3000;
  age.hidden = !data.connected || (Number.isFinite(sample) && !stale);
  age.textContent = Number.isFinite(sample)
    ? `Telemetry stale · ${seconds} s ago` : "Waiting for telemetry";
  age.dataset.stale = String(stale);
}

function formatBytes(value) {
  return Number.isSafeInteger(value) && value >= 0 ? `${value.toLocaleString()} B` : "--";
}

function uptimeLabel(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor(seconds / 3600) % 24;
  const minutes = Math.floor(seconds / 60) % 60;
  const remainder = seconds % 60;
  return `${days ? `${days}d ` : ""}${hours}h ${minutes}m ${remainder}s`;
}

function formatPin(value) {
  return Number.isInteger(value) && value >= 0 && value < 255 ? `GP${value}` : "--";
}

function boardModelName(value) {
  return ({ 0: "Unknown / unspecified", 1: "Pico", 2: "Pico W", 3: "Pico 2", 4: "Pico 2 W" })[value]
    ?? (value == null ? "--" : `Unknown board model (${value})`);
}

function microcontrollerName(value) {
  if (typeof value === "string") return value;
  return ({ 1: "RP2040", 2: "RP2350" })[value] ?? (value == null ? "--" : `Unknown MCU (${value})`);
}

function boardIdentityName(board, microcontroller) {
  const boardName = boardModelName(board);
  const mcuName = microcontrollerName(microcontroller);
  return boardName === "--" || mcuName === "--" ? boardName : `${boardName} (${mcuName})`;
}

function deviceUid(identity) {
  const uid = identity?.device_uid ?? identity?.serial_number;
  if (Array.isArray(uid)) return uid.map(value => Number(value).toString(16).padStart(2, "0")).join("").toUpperCase();
  return typeof uid === "string" ? uid.toUpperCase() : "--";
}

function resetCauseName(value) {
  return resetCauses[value] ?? (value == null ? "--" : `Unknown (${value})`);
}

function renderStaticConfiguration(data) {
  const identity = data.identity;
  const configuration = data.configuration;
  const profileMask = identity?.clock_profile_mask ?? 0;
  for (const option of clockProfile.options) {
    option.disabled = !(profileMask & (1 << Number(option.value)));
  }
  const pendingProfile = pendingConfigurationProfiles.get(selectedBoard);
  const storedProfile = configuration?.stored_profile ?? 0;
  if (!actionBusy && configurationSavePending && Number.isInteger(pendingProfile) && configuration
    && (configuration.stored_profile === null || Number.isInteger(configuration.stored_profile))
    && storedProfile === pendingProfile) {
    configurationSavePending = false;
    configurationProfileDirty = false;
    pendingConfigurationProfiles.delete(selectedBoard);
    uncertainConfigurationBoards.delete(selectedBoard);
    const warning = configurationSaveWarning(data);
    if (warning) acknowledgedConfigurationWarnings.set(selectedBoard, warning);
    else acknowledgedConfigurationWarnings.delete(selectedBoard);
    configurationRecoveryStatusFloor.delete(selectedBoard);
    configurationRecoveryReadyBoards.delete(selectedBoard);
    if (boardActionErrors.get(selectedBoard)?.action === "configuration") {
      boardActionErrors.delete(selectedBoard);
    }
    overclockAcknowledgement.checked = false;
  } else if (configurationProfileDirty && !configurationSavePending
    && configuration?.stored_profile === Number(clockProfile.value)) {
    configurationProfileDirty = false;
    overclockAcknowledgement.checked = false;
  }
  if (!configurationProfileDirty && configuration
    && (configuration.stored_profile === null || Number.isInteger(configuration.stored_profile))) {
    clockProfile.value = String(configuration.stored_profile ?? 0);
  }
  document.querySelector("#reset-cause").textContent = resetCauseName(configuration?.reset_cause);
  document.querySelector("#uart-tx-capacity").textContent = formatBytes(configuration?.uart_tx_ring_capacity_bytes);
  document.querySelector("#uart-rx-capacity").textContent = formatBytes(configuration?.uart_rx_ring_capacity_bytes);
  document.querySelector("#cdc-tx-capacity").textContent = formatBytes(configuration?.cdc_tx_fifo_capacity_bytes);
  document.querySelector("#cdc-rx-capacity").textContent = formatBytes(configuration?.cdc_rx_fifo_capacity_bytes);

  const total = identity?.ram_total_bytes;
  const reserved = identity?.ram_reserved_bytes;
  document.querySelector("#ram-usage").textContent = Number.isSafeInteger(total) && total > 0
    && Number.isSafeInteger(reserved) && reserved >= 0 && reserved <= total
    ? `${(reserved / 1024).toFixed(1)} / ${(total / 1024).toFixed(0)} KiB`
      + ` (${(reserved / total * 100).toFixed(1)}%)`
    : "--";

  const pinMap = document.querySelector("#pin-map");
  pinMap.replaceChildren();
  const pins = configuration?.channel_pins;
  if (!Array.isArray(pins)) {
    pinMap.textContent = "Static pin assignments unavailable";
    return;
  }
  const table = document.createElement("table");
  table.className = "pin-table";
  const head = document.createElement("tr");
  for (const label of ["Channel", "TX", "RX", "RTS", "CTS"]) {
    const cell = document.createElement("th");
    cell.scope = "col";
    cell.textContent = label;
    head.append(cell);
  }
  const thead = document.createElement("thead");
  thead.append(head);
  const body = document.createElement("tbody");
  pins.forEach((channel, index) => {
    const row = document.createElement("tr");
    for (const value of [
      `CDC${index}`,
      formatPin(channel.tx_pin),
      formatPin(channel.rx_pin),
      formatPin(channel.rts_pin),
      formatPin(channel.cts_pin),
    ]) {
      const cell = document.createElement("td");
      cell.textContent = value;
      row.append(cell);
    }
    body.append(row);
  });
  table.append(thead, body);
  pinMap.append(table);
}

function configurationSaveWarning(data) {
  const error = data?.configuration_save_error;
  return typeof error === "string" && /outcome is unconfirmed/i.test(error) ? error : null;
}

function renderConfigurationRecovery(data) {
  const configuration = data?.configuration;
  const canAcknowledge = uncertainConfigurationBoards.has(selectedBoard)
    && configurationRecoveryReadyBoards.has(selectedBoard)
    && !actionBusy
    && data?.connected && configuration
    && (configuration.stored_profile === null || Number.isInteger(configuration.stored_profile))
    && !data.configuration_error && !data.error;
  configurationRecovery.hidden = !canAcknowledge;
  if (!canAcknowledge) return;
  const storedProfile = configuration.stored_profile ?? 0;
  const storedLabel = [...clockProfile.options].find(option => Number(option.value) === storedProfile)?.textContent
    ?? `Unknown profile (${storedProfile})`;
  configurationRecoveryMessage.textContent = `The save outcome is unconfirmed. Current stored profile: ${storedLabel}.`;
}

function updateConfigurationControls(data) {
  const identity = data?.identity;
  const capabilities = identity?.capabilities ?? 0;
  const profile = Number(clockProfile.value);
  const profileSupported = Number.isInteger(profile) && Boolean((identity?.clock_profile_mask ?? 0) & (1 << profile));
  const storedProfile = data?.configuration?.stored_profile;
  const available = Boolean(data?.connected && (capabilities & 4) && data?.configuration
    && (storedProfile === null || Number.isInteger(storedProfile)));
  const profileChanged = available && profile !== (storedProfile ?? 0);
  clockProfile.disabled = actionBusy || configurationSavePending || !data?.connected
    || !(capabilities & 4) || !identity?.clock_profile_mask;
  saveConfigurationButton.hidden = !(capabilities & 4);
  saveConfigurationButton.disabled = actionBusy || configurationSavePending || !available
    || !profileChanged || !profileSupported;
}

function currentRates(channel, now, data) {
  const id = channel.id;
  const identity = data.identity?.device_uid ?? data.identity?.serial_number;
  const uptime = data.runtime?.uptime_ms;
  const observedInterval = channel.observed_interval_ms;
  const totals = Object.fromEntries(directions.map(([, key]) => [key, channel.totals?.[key]]));
  let history = channelHistory.get(id);
  const readerRestarted = history && Number.isSafeInteger(observedInterval)
    && Number.isSafeInteger(history.observedInterval) && observedInterval < history.observedInterval;
  if (!data.connected || !history || history.identity !== identity
    || (Number.isFinite(uptime) && Number.isFinite(history.uptime) && uptime < history.uptime)
    || readerRestarted) {
    history = { identity, uptime, sequence: null, trafficGaps: channel.traffic_gaps ?? 0,
      observedInterval, sequenceReceivedAt: now, stale: false, usbSamples: [], uartSamples: [] };
    channelHistory.set(id, history);
  }
  const stale = now - history.sequenceReceivedAt > 3000;
  const sequenceDelta = Number.isInteger(channel.sequence) && Number.isInteger(history.sequence)
    ? (channel.sequence - history.sequence + 65536) % 65536 : null;
  const newSample = Number.isInteger(channel.sequence)
    && (history.sequence === null || sequenceDelta > 0 && sequenceDelta < 32768);
  const sequenceJump = sequenceDelta !== null && sequenceDelta > 1 && sequenceDelta < 32768;
  const trafficGaps = channel.traffic_gaps ?? history.trafficGaps;
  const gap = trafficGaps !== history.trafficGaps;
  const intervalValid = Number.isInteger(channel.interval_ms)
    && channel.interval_ms > 0 && channel.interval_ms < 65535;
  const totalsValid = Object.values(totals).every(value =>
    Number.isInteger(value) && value >= 0 && value <= 0xffffffff);
  if (stale && !history.stale || gap || newSample && !intervalValid) {
    history.usbSamples = [];
    history.uartSamples = [];
  }
  if (sequenceJump) history.uartSamples = [];
  if (newSample) {
    history.sequence = channel.sequence;
    history.sequenceReceivedAt = now;
    history.stale = false;
  } else if (stale) {
    history.stale = true;
  }
  if (gap) history.trafficGaps = trafficGaps;
  const baselineAfterGap = gap && !newSample && history.sequence !== null;
  if (data.connected && (newSample || baselineAfterGap) && totalsValid && Number.isSafeInteger(observedInterval)) {
    const sample = { observedInterval, totals };
    history.usbSamples.push(sample);
    if (channel.uart_measurement_state === 1) history.uartSamples.push(sample);
    else history.uartSamples = [];
    for (const samples of [history.usbSamples, history.uartSamples]) {
      while (samples.length > 2
        && samples[1].observedInterval <= observedInterval - TRAFFIC_RATE_WINDOW_MS) samples.shift();
    }
  }
  const rates = {};
  for (const [, key, uart] of directions) {
    const samples = uart ? history.uartSamples : history.usbSamples;
    const baseline = samples[0];
    const latest = samples.at(-1);
    const elapsed = latest && baseline ? latest.observedInterval - baseline.observedInterval : 0;
    rates[key] = !stale && elapsed >= MIN_TRAFFIC_RATE_WINDOW_MS && elapsed <= MAX_RATE_ELAPSED_MS
      ? ((latest.totals[key] - baseline.totals[key]) >>> 0) * 1000 / elapsed : null;
  }
  if (Number.isFinite(uptime)) history.uptime = uptime;
  if (Number.isSafeInteger(observedInterval)) history.observedInterval = observedInterval;
  const fresh = data.connected && Number.isInteger(history.sequence) && now - history.sequenceReceivedAt <= 3000;
  return { rates, fresh };
}

function measurementLabel(value) {
  return measurementStates[value] ?? "Unknown";
}

function measurementStatus(value) {
  const state = measurementLabel(value).toLowerCase();
  return state === "current" ? "LIVE" : state === "cached" ? "CACHED" : "— Unavailable";
}

function diagnosticEntry(id) {
  if (Array.isArray(diagnosticData)) {
    return diagnosticData.find(item => item?.id === id) ?? diagnosticData[id] ?? null;
  }
  if (diagnosticData && typeof diagnosticData === "object") {
    return diagnosticData[id] ?? diagnosticData.channel_diagnostics?.[id] ?? null;
  }
  return null;
}

function peakValue(bytes, capacity) {
  const result = formatBytes(bytes);
  return Number.isSafeInteger(bytes) && Number.isSafeInteger(capacity) && capacity > 0 && bytes <= capacity
    ? `${result} (${(bytes / capacity * 100).toFixed(1)}%)` : result;
}

function renderDiagnosticContent(container, id) {
  container.replaceChildren();
  diagnosticAgeElement = null;
  if (!diagnosticData && !diagnosticError) {
    container.textContent = "Loading channel diagnostics...";
    return;
  }
  if (diagnosticError) {
    container.textContent = `Diagnostics unavailable: ${diagnosticError}`;
    return;
  }
  diagnosticAgeElement = document.createElement("p");
  diagnosticAgeElement.className = "diagnostics-age";
  container.append(diagnosticAgeElement);
  const diagnostic = diagnosticEntry(id);
  if (!diagnostic) {
    const unavailable = document.createElement("p");
    unavailable.textContent = "Diagnostics unavailable for this channel.";
    container.append(unavailable);
    updateDiagnosticsAge();
    return;
  }
  const config = latestData?.configuration ?? {};
  const uartMeasurementAvailable = diagnostic.uart_measurement_state === 1
    || diagnostic.uart_measurement_state === 2;
  const noFaultRecorded = diagnostic.last_fault_uptime_s === 0 && diagnostic.rx_overrun_count === 0
    && diagnostic.rx_error_count === 0 && diagnostic.last_control_error_reason === 0;
  const lastFault = noFaultRecorded
    ? uartMeasurementAvailable ? "None recorded" : "Unavailable"
    : `${diagnostic.last_fault_uptime_s?.toLocaleString() ?? "--"} s uptime`;
  const fields = [
    ["UART measurement", measurementLabel(diagnostic.uart_measurement_state)],
    ["UART sample age", diagnostic.uart_measurement_state === 0 ? "Unavailable"
      : diagnostic.uart_sample_age_100ms === 255 ? "At least 25.5 s"
        : `${(diagnostic.uart_sample_age_100ms / 10).toFixed(1)} s`],
    ["Known RX bytes dropped", uartMeasurementAvailable ? formatBytes(diagnostic.rx_overrun_count) : "Unavailable"],
    ["RX controller error observations",
      uartMeasurementAvailable ? diagnostic.rx_error_count?.toLocaleString() ?? "--" : "Unavailable"],
    ["Last configuration error", errorReasons[diagnostic.last_control_error_reason]
      ?? `Unknown (${diagnostic.last_control_error_reason ?? "--"})`],
    ["Last fault observed", lastFault],
    ["UART TX ring observed peak since boot", uartMeasurementAvailable
      ? peakValue(diagnostic.uart_tx_ring_peak_bytes, config.uart_tx_ring_capacity_bytes) : "Unavailable"],
    ["UART RX ring observed peak since boot", uartMeasurementAvailable
      ? peakValue(diagnostic.uart_rx_ring_peak_bytes, config.uart_rx_ring_capacity_bytes) : "Unavailable"],
    ["CDC TX FIFO observed peak since boot", peakValue(
      diagnostic.cdc_tx_fifo_peak_bytes, config.cdc_tx_fifo_capacity_bytes)],
    ["CDC RX FIFO observed peak since boot", peakValue(
      diagnostic.cdc_rx_fifo_peak_bytes, config.cdc_rx_fifo_capacity_bytes)],
  ];
  const list = document.createElement("dl");
  for (const [label, value] of fields) {
    const term = document.createElement("dt");
    const detail = document.createElement("dd");
    term.textContent = label;
    detail.textContent = value;
    list.append(term, detail);
  }
  container.append(list);
  updateDiagnosticsAge();
}

function updateDiagnosticsAge(now = Date.now()) {
  if (!diagnosticAgeElement || !diagnosticsDialog.open || !Number.isFinite(diagnosticReadAt)) return;
  const seconds = Math.max(0, Math.floor((now - diagnosticReadAt) / 1000));
  diagnosticAgeElement.textContent = `Diagnostics read ${seconds} s ago`;
}

function invalidateDiagnostics() {
  diagnosticEpoch++;
  diagnosticBoard = null;
  diagnosticData = null;
  diagnosticError = null;
  diagnosticReadAt = null;
}

async function loadDiagnostics() {
  if (!selectedBoard) return;
  const board = selectedBoard;
  const selection = selectionVersion;
  const epoch = diagnosticEpoch;
  if (diagnosticRequest?.selection === selection && diagnosticRequest.epoch === epoch) {
    return diagnosticRequest.promise;
  }
  const request = { selection, epoch, promise: null };
  request.promise = (async () => {
    diagnosticError = null;
    try {
      const { response, data: result } = await fetchJson(
        `/api/diagnostics?board=${encodeURIComponent(board)}`,
        { cache: "no-store" },
        DIAGNOSTICS_REQUEST_TIMEOUT_MS,
      );
      if (!response.ok) throw new Error(result.error || `Diagnostics request failed (${response.status})`);
      if (selection !== selectionVersion || epoch !== diagnosticEpoch) return;
      diagnosticData = result.channel_diagnostics ?? result;
      diagnosticBoard = board;
      diagnosticReadAt = Date.now();
    } catch (error) {
      if (selection === selectionVersion && epoch === diagnosticEpoch) diagnosticError = error.message;
    } finally {
      if (diagnosticRequest === request) diagnosticRequest = null;
      if (selection === selectionVersion && epoch === diagnosticEpoch) {
        renderDiagnosticContent(diagnosticsContent, Number(diagnosticsContent.dataset.channel));
      }
    }
  })();
  diagnosticRequest = request;
  return request.promise;
}

function buildDiagnosticsCell(channel) {
  const cell = document.createElement("td");
  const button = document.createElement("button");
  button.className = "button button-small diagnostics-action";
  button.type = "button";
  button.textContent = "Diagnostics";
  button.disabled = !latestData?.connected;
  button.addEventListener("click", async () => {
    diagnosticsDialog.querySelector("h2").textContent = `CDC${channel.id} diagnostics`;
    diagnosticsContent.dataset.channel = String(channel.id);
    renderDiagnosticContent(diagnosticsContent, channel.id);
    diagnosticsDialog.showModal();
    if (diagnosticBoard !== selectedBoard || (!diagnosticData && !diagnosticError)) await loadDiagnostics();
  });
  cell.append(button);
  return cell;
}

function createChannelRow(id) {
  const row = document.createElement("tr");
  const port = document.createElement("td");
  const portName = document.createElement("span");
  portName.className = "port-name";
  const portIndex = document.createElement("span");
  portIndex.className = "port-index";
  portIndex.textContent = String(id + 1).padStart(2, "0");
  portName.append(portIndex, document.createTextNode(`CDC${id}`));
  port.append(portName);
  row.append(port);

  const lifecycle = document.createElement("td");
  lifecycle.className = "channel-status";
  const state = document.createElement("span");
  const measurement = document.createElement("small");
  measurement.className = "health-flags";
  const history = document.createElement("small");
  history.className = "traffic-history";
  lifecycle.append(state, measurement, history);
  row.append(lifecycle);

  const configuration = document.createElement("td");
  configuration.className = "uart-configuration";
  configuration.title = "Applied baud and framing; GPIO assignments are static board configuration.";
  const settings = document.createElement("strong");
  configuration.append(settings);
  row.append(configuration);

  const traffic = document.createElement("td");
  traffic.className = "traffic";
  const metrics = new Map();
  const metricGrid = document.createElement("div");
  metricGrid.className = "traffic-metrics";
  directions.forEach(([label, key]) => {
    const metric = document.createElement("span");
    const direction = document.createElement("span");
    direction.className = "traffic-label";
    direction.textContent = label;
    const rate = document.createElement("b");
    const total = document.createElement("span");
    total.className = "traffic-total";
    metric.append(direction, rate, total);
    metricGrid.append(metric);
    metrics.set(key, { metric, rate, total });
  });
  traffic.append(metricGrid);
  row.append(traffic);

  const diagnostics = buildDiagnosticsCell({ id });
  row.append(diagnostics);
  return { row, configuration, settings, state, measurement, metrics,
    history, diagnosticsButton: diagnostics.children[0] };
}

function renderChannels(data) {
  const channels = Array.isArray(data.channels) ? data.channels : [];
  const now = Date.now();
  const activeIds = new Set();
  channels.forEach(channel => {
    const id = channel.id;
    activeIds.add(id);
    let view = channelRowViews.get(id);
    if (!view) {
      view = createChannelRow(id);
      channelRowViews.set(id, view);
      channelRows.append(view.row);
    }
    const rates = currentRates(channel, now, data);
    view.settings.textContent = Number.isInteger(channel.baud_rate) && channel.baud_rate > 0
      ? `${channel.baud_rate.toLocaleString()} ${channel.data_bits}`
        + `${({ none: "N", odd: "O", even: "E" })[channel.parity] ?? "?"}${channel.stop_bits}`
      : "--";
    const stale = !rates.fresh;
    const stateName = stale ? "unknown" : channelStates[channel.state] ?? channel.state;
    view.state.className = `state state-${stateName === "error" ? "attention" : stateName}`;
    view.state.textContent = stateName === "error" ? "Attention" : stateName;
    view.state.hidden = stale;
    view.measurement.textContent = stale ? "— Unavailable" : measurementStatus(channel.uart_measurement_state);
    view.measurement.title = stale ? "Channel snapshot unavailable or stale."
      : `Measurement status: ${measurementLabel(channel.uart_measurement_state).toLowerCase()}.`;
    view.history.hidden = channel.traffic_incomplete !== true;
    view.history.textContent = view.history.hidden ? "" : "History incomplete";
    view.history.title = `Traffic history includes ${channel.traffic_gaps ?? 0} known gap(s).`;
    directions.forEach(([label, key, uart]) => {
      const metric = view.metrics.get(key);
      metric.rate.textContent = rates.rates?.[key] != null
        ? `${(rates.rates[key] / 1000).toFixed(1)} KB/s` : "-- KB/s";
      const sampleAvailable = Number.isInteger(channel.sequence);
      const uartMeasurementAvailable = !uart
        || channel.uart_measurement_state === 1 || channel.uart_measurement_state === 2;
      const total = sampleAvailable && uartMeasurementAvailable ? formatBytes(channel.totals?.[key]) : "--";
      metric.total.textContent = total;
      metric.metric.title = `${label}: ${total} since firmware boot, modulo 2^32.`;
    });
    view.diagnosticsButton.disabled = !data.connected;
  });
  for (const [id, view] of channelRowViews) {
    if (!activeIds.has(id)) {
      view.row.remove();
      channelRowViews.delete(id);
    }
  }
}

function render(data) {
  const reconnected = data.connected && !latestData?.connected;
  const previousUptime = latestData?.runtime?.uptime_ms;
  const uptime = data.runtime?.uptime_ms;
  const rebooted = data.connected && latestData?.connected
    && Number.isFinite(previousUptime) && Number.isFinite(uptime) && uptime < previousUptime;
  const previousUid = deviceUid(latestData?.identity);
  const currentUid = deviceUid(data.identity);
  const deviceChanged = data.connected && latestData?.connected
    && previousUid !== "--" && currentUid !== "--" && previousUid !== currentUid;
  const disconnected = !data.connected && latestData?.connected;
  const diagnosticsInvalidated = reconnected || disconnected || rebooted || deviceChanged;
  if (diagnosticsInvalidated) {
    channelHistory.clear();
    invalidateDiagnostics();
  }
  latestData = data;
  connection.dataset.state = data.connected ? "connected" : "disconnected";
  document.querySelector("#connection-label").textContent = data.connected ? "Connected" : "Disconnected";
  document.querySelector("#board-model").textContent = boardIdentityName(
    data.identity?.board_model_id ?? data.identity?.board_model,
    data.identity?.mcu_id ?? data.identity?.mcu,
  );
  const systemClock = data.telemetry?.system_clock_hz ?? data.hardware?.system_clock_hz;
  document.querySelector("#system-clock").textContent = Number.isFinite(systemClock)
    ? `${(systemClock / 1000000).toLocaleString(undefined, { maximumFractionDigits: 3 })} MHz` : "--";
  document.querySelector("#firmware").textContent = data.identity?.firmware_version ?? "--";
  document.querySelector("#product-name").textContent = data.identity?.product_name ?? "--";
  document.querySelector("#serial").textContent = deviceUid(data.identity);
  renderStaticConfiguration(data);
  updateConfigurationControls(data);
  renderConfigurationRecovery(data);
  const runtime = data.runtime;
  document.querySelector("#uptime").textContent = Number.isFinite(runtime?.uptime_ms)
    ? uptimeLabel(runtime.uptime_ms) : "--";
  const serviceLoad = runtime?.service_load_basis_points;
  document.querySelector("#service-load").textContent = Array.isArray(serviceLoad)
    ? serviceLoad.map(value => Number.isInteger(value) && value >= 0 && value <= 10000
      ? `${(value / 100).toFixed(1)}%` : "--").join(" / ")
    : "--";
  const temperature = data.telemetry?.temperature_centidegrees_celsius;
  document.querySelector("#temperature").textContent = Number.isInteger(temperature)
    ? `${(temperature / 100).toFixed(1)} °C`
    : Number.isFinite(data.board?.temperature_celsius) ? `${data.board.temperature_celsius.toFixed(1)} °C` : "--";
  const led = data.connected ? data.led : null;
  const capabilities = data.identity?.capabilities ?? 0;
  const ledButton = document.querySelector("#toggle-led");
  const ledOutputOn = data.telemetry?.led_flags == null ? led?.output_on
    : Boolean(data.telemetry.led_flags & 1);
  ledButton.textContent = `Board LED: ${ledOutputOn == null ? "--" : ledOutputOn ? "On" : "Off"}`;
  ledButton.dataset.state = ledOutputOn == null ? "unknown" : ledOutputOn ? "on" : "off";
  ledButton.title = ledOutputOn == null ? "Board identification LED state unavailable"
    : `Set board identification LED ${ledOutputOn ? "Off" : "On"}`;
  ledButton.hidden = !(capabilities & 1);
  ledButton.disabled = actionBusy || !data.connected || !(capabilities & 1) || ledOutputOn == null;
  const resetButton = document.querySelector("#reset-board");
  resetButton.hidden = !(capabilities & 2);
  resetButton.disabled = actionBusy || !data.connected;
  updateReportAge(data);
  renderChannels(data);

  const saveWarning = configurationSaveWarning(data);
  const configurationSaveError = saveWarning
    && acknowledgedConfigurationWarnings.get(selectedBoard) === saveWarning
    ? null : data.configuration_save_error;
  const message = scanError || data.error || boardActionErrors.get(selectedBoard)?.message
    || (data.identity_error ? `Device identity unavailable: ${data.identity_error}` : null)
    || (data.configuration_error ? `Board configuration unavailable: ${data.configuration_error}` : null)
    || (configurationSaveError ? `Configuration save outcome: ${configurationSaveError}` : null)
    || (data.runtime_error ? `Runtime telemetry unavailable: ${data.runtime_error}` : null)
    || (data.led_error ? `LED state unavailable: ${data.led_error}` : null)
    || (data.connected ? "HID telemetry is live" : "Waiting for a PicoUart HID interface");
  statusMessage.textContent = message;
  statusMessage.parentElement.dataset.error = Boolean(scanError || data.error || data.identity_error
    || boardActionErrors.has(selectedBoard) || data.configuration_error || configurationSaveError
    || data.runtime_error || data.led_error || !data.connected);
  if (diagnosticsInvalidated && diagnosticsDialog.open) {
    if (!data.connected) diagnosticError = "Board disconnected; reconnect before reading diagnostics.";
    renderDiagnosticContent(diagnosticsContent, Number(diagnosticsContent.dataset.channel));
    if (data.connected) loadDiagnostics();
  }
}

async function refresh() {
  if (refreshing === selectionVersion) return;
  const version = selectionVersion;
  const requestId = ++statusRequestId;
  refreshing = version;
  try {
    const query = selectedBoard ? `?board=${encodeURIComponent(selectedBoard)}` : "";
    const { response, data } = await fetchJson(
      `/api/status${query}`, { cache: "no-store" }, STATUS_REQUEST_TIMEOUT_MS);
    if (version !== selectionVersion) return;
    if (!response.ok) throw new Error(data.error || `Status request failed (${response.status})`);
    const saveWarning = configurationSaveWarning(data);
    if (!saveWarning) {
      acknowledgedConfigurationWarnings.delete(selectedBoard);
    } else if (acknowledgedConfigurationWarnings.has(selectedBoard)) {
      const acknowledgedWarning = acknowledgedConfigurationWarnings.get(selectedBoard);
      if (acknowledgedWarning === null) {
        acknowledgedConfigurationWarnings.set(selectedBoard, saveWarning);
      } else if (acknowledgedWarning !== saveWarning) {
        acknowledgedConfigurationWarnings.delete(selectedBoard);
      }
    }
    if (!actionBusy && saveWarning && !acknowledgedConfigurationWarnings.has(selectedBoard)) {
      if (!uncertainConfigurationBoards.has(selectedBoard)) {
        uncertainConfigurationBoards.add(selectedBoard);
        pendingConfigurationProfiles.set(selectedBoard, null);
        configurationRecoveryStatusFloor.set(selectedBoard, requestId - 1);
      }
      if (selectedBoard !== null) configurationSavePending = true;
    }
    if (uncertainConfigurationBoards.has(selectedBoard)
      && requestId > (configurationRecoveryStatusFloor.get(selectedBoard) ?? Number.POSITIVE_INFINITY)) {
      configurationRecoveryReadyBoards.add(selectedBoard);
    }
    setRefreshInterval(data.connected ? data.refresh_interval_ms : 500);
    render(data);
  } catch (error) {
    if (version !== selectionVersion) return;
    setRefreshInterval(500);
    if (latestData) render({ ...latestData, error: error.message });
    statusMessage.textContent = error.message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    if (refreshing === version) refreshing = null;
  }
}

function selectBoard(id, label) {
  if (selectedBoard === id) return;
  configurationProfileDirty = false;
  configurationSavePending = id !== null && pendingConfigurationProfiles.has(id);
  overclockAcknowledgement.checked = false;
  selectedBoard = id;
  setRefreshInterval(500);
  selectedLabel = label;
  selectionVersion++;
  channelHistory.clear();
  reportSample = null;
  invalidateDiagnostics();
  diagnosticsDialog.close();
  render({
    connected: false,
    channels: Array.from({ length: 6 }, (_, id) => ({ id, state: "unknown" })),
  });
}

async function rescanBoards() {
  if (scanning) return;
  scanning = true;
  rescanButton.disabled = true;
  try {
    const { response, data: result } = await fetchJson(
      "/api/boards", { cache: "no-store" }, BOARD_SCAN_TIMEOUT_MS);
    if (!response.ok) throw new Error(result.error || `Board scan failed (${response.status})`);
    const discoveryErrors = result.discovery_errors || [];
    scanError = discoveryErrors.length
      ? `Some boards could not be inspected: ${discoveryErrors
        .map(item => `${item.device_path}: ${item.error}`).join("; ")}`
      : null;
    boardSelect.replaceChildren();
    for (const board of result.boards) {
      const option = document.createElement("option");
      option.value = board.id;
      option.textContent = `${board.product_name} / ${board.serial_number || board.device_path}`;
      boardSelect.append(option);
    }
    if (selectedBoard && !result.boards.some(board => board.id === selectedBoard)) {
      const missing = document.createElement("option");
      missing.value = selectedBoard;
      missing.textContent = `${selectedLabel} (not found)`;
      boardSelect.append(missing);
    }
    if (!selectedBoard) {
      const initial = result.boards.find(board => board.id === result.default_board_id);
      if (initial) selectBoard(initial.id, `${initial.product_name} / ${initial.serial_number || initial.device_path}`);
      else {
        const placeholder = document.createElement("option");
        placeholder.value = "";
        placeholder.textContent = result.boards.length ? "Select a board" : "No boards found";
        boardSelect.append(placeholder);
      }
    }
    boardSelect.value = selectedBoard || "";
    boardSelect.disabled = actionBusy || !result.boards.length;
    await refresh();
  } catch (error) {
    scanError = `Board scan failed: ${error.message}`;
    statusMessage.textContent = scanError;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    scanning = false;
    rescanButton.disabled = actionBusy;
  }
}

async function runAction(action) {
  if (!selectedBoard || actionBusy) return;
  const board = selectedBoard;
  let responseStatus = null;
  actionBusy = true;
  boardSelect.disabled = true;
  actionButtons.forEach(button => { button.disabled = true; });
  try {
    const { response, data: result } = await fetchJson(
      `/api/actions/${action}?board=${encodeURIComponent(board)}`,
      { method: "POST", headers: { "X-CSRF-Token": token } },
      CONTROL_REQUEST_TIMEOUT_MS,
    );
    if (!response.ok) {
      responseStatus = response.status;
      throw new Error(result.error || `Action failed (${response.status})`);
    }
    if (boardActionErrors.get(board)?.action === action) boardActionErrors.delete(board);
    statusMessage.textContent = action === "reset"
      ? "Reboot command sent; USB will reconnect shortly." : "LED toggle command sent";
    statusMessage.parentElement.dataset.error = "false";
    await refresh();
  } catch (error) {
    const uncertain = responseStatus === null || responseStatus >= 500 || /outcome is unconfirmed/i.test(error.message);
    const message = uncertain
      ? `Board action outcome is unconfirmed; reconnect and inspect before retrying. ${error.message}`
      : error.message;
    boardActionErrors.set(board, { action, message });
    statusMessage.textContent = message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    actionBusy = false;
    boardSelect.disabled = !boardSelect.value;
    rescanButton.disabled = scanning;
    const ledOutput = latestData?.telemetry?.led_flags == null
      ? latestData?.led?.output_on : Boolean(latestData.telemetry.led_flags & 1);
    document.querySelector("#toggle-led").disabled = !latestData?.connected
      || !(latestData.identity?.capabilities & 1) || ledOutput == null;
    document.querySelector("#reset-board").disabled = !latestData?.connected;
    updateConfigurationControls(latestData);
  }
}

function openConfigurationConfirmation() {
  if (saveConfigurationButton.disabled) return;
  const profile = Number(clockProfile.value);
  const requiresAcknowledgement = profile !== 0;
  const profileLabel = clockProfile.selectedOptions[0]?.textContent ?? "selected clock profile";
  configurationDialogProfile.textContent = `Selected profile: ${profileLabel}.`;
  overclockAcknowledgement.checked = false;
  overclockAcknowledgement.disabled = !requiresAcknowledgement;
  overclockAcknowledgementLabel.hidden = !requiresAcknowledgement;
  confirmConfigurationButton.disabled = requiresAcknowledgement;
  configurationDialog.showModal();
}

async function saveConfiguration() {
  const profile = Number(clockProfile.value);
  const unsafeOverclock = profile !== 0;
  if (!configurationDialog.open || !selectedBoard || actionBusy || saveConfigurationButton.disabled
    || (unsafeOverclock && !overclockAcknowledgement.checked)) return;
  const profileLabel = clockProfile.selectedOptions[0]?.textContent ?? "selected clock profile";
  configurationDialog.close();

  const board = selectedBoard;
  let responseStatus = null;
  configurationSavePending = true;
  pendingConfigurationProfiles.set(board, profile);
  actionBusy = true;
  boardSelect.disabled = true;
  actionButtons.forEach(button => { button.disabled = true; });
  updateConfigurationControls(latestData);
  statusMessage.textContent = `Saving ${profileLabel} and waiting for board confirmation...`;
  statusMessage.parentElement.dataset.error = "false";
  try {
    const { response, data: result } = await fetchJson(
      `/api/configuration?board=${encodeURIComponent(board)}`,
      {
        method: "POST",
        headers: { "X-CSRF-Token": token, "Content-Type": "application/json" },
        body: JSON.stringify({ profile, unsafe_overclock: unsafeOverclock }),
      },
      CONTROL_REQUEST_TIMEOUT_MS,
    );
    if (!response.ok) {
      responseStatus = response.status;
      throw new Error(result.error || `Configuration save failed (${response.status})`);
    }
    configurationProfileDirty = true;
    configurationSavePending = true;
    uncertainConfigurationBoards.delete(board);
    acknowledgedConfigurationWarnings.delete(board);
    configurationRecoveryStatusFloor.delete(board);
    configurationRecoveryReadyBoards.delete(board);
    if (boardActionErrors.get(board)?.action === "configuration") boardActionErrors.delete(board);
    statusMessage.textContent = `Saved ${profileLabel}; board is rebooting.`;
    statusMessage.parentElement.dataset.error = "false";
  } catch (error) {
    const uncertain = responseStatus === null || responseStatus >= 500 || /outcome is unconfirmed/i.test(error.message);
    const message = uncertain
      ? `Configuration save outcome is unconfirmed; reconnect and inspect before retrying. ${error.message}`
      : error.message;
    configurationSavePending = uncertain;
    if (uncertain) uncertainConfigurationBoards.add(board);
    else pendingConfigurationProfiles.delete(board);
    if (uncertain) {
      acknowledgedConfigurationWarnings.delete(board);
      configurationRecoveryStatusFloor.set(board, statusRequestId);
      configurationRecoveryReadyBoards.delete(board);
    } else {
      configurationRecoveryStatusFloor.delete(board);
      configurationRecoveryReadyBoards.delete(board);
      uncertainConfigurationBoards.delete(board);
    }
    boardActionErrors.set(board, { action: "configuration", message });
    statusMessage.textContent = message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    actionBusy = false;
    boardSelect.disabled = !boardSelect.value;
    rescanButton.disabled = scanning;
    const ledOutput = latestData?.telemetry?.led_flags == null
      ? latestData?.led?.output_on : Boolean(latestData.telemetry.led_flags & 1);
    document.querySelector("#toggle-led").disabled = !latestData?.connected
      || !(latestData.identity?.capabilities & 1) || ledOutput == null;
    document.querySelector("#reset-board").disabled = !latestData?.connected;
    updateConfigurationControls(latestData);
    renderConfigurationRecovery(latestData);
  }
}

function acknowledgeConfigurationOutcome() {
  const board = selectedBoard;
  const configuration = latestData?.configuration;
  if (!board || !configurationSavePending || !uncertainConfigurationBoards.has(board)
    || !configurationRecoveryReadyBoards.has(board)
    || actionBusy
    || !latestData.connected || latestData.configuration_error || !configuration
    || (configuration.stored_profile !== null && !Number.isInteger(configuration.stored_profile))) return;
  clockProfile.value = String(configuration.stored_profile ?? 0);
  configurationProfileDirty = false;
  configurationSavePending = false;
  pendingConfigurationProfiles.delete(board);
  uncertainConfigurationBoards.delete(board);
  acknowledgedConfigurationWarnings.set(board, configurationSaveWarning(latestData));
  configurationRecoveryStatusFloor.delete(board);
  configurationRecoveryReadyBoards.delete(board);
  if (boardActionErrors.get(board)?.action === "configuration") boardActionErrors.delete(board);
  overclockAcknowledgement.checked = false;
  updateConfigurationControls(latestData);
  renderConfigurationRecovery(latestData);
  statusMessage.textContent = "Stored profile inspected; choose a profile to retry if needed.";
  statusMessage.parentElement.dataset.error = "false";
}

document.querySelector("#toggle-led").addEventListener("click", () => runAction("toggle-led"));
document.querySelector("#reset-board").addEventListener("click", () => {
  if (!actionBusy && selectedBoard) rebootDialog.showModal();
});
document.querySelector("#cancel-reboot").addEventListener("click", () => rebootDialog.close());
document.querySelector("#confirm-reboot").addEventListener("click", () => {
  if (!rebootDialog.open || actionBusy || !selectedBoard) return;
  rebootDialog.close();
  runAction("reset");
});
clockProfile.addEventListener("change", () => {
  configurationProfileDirty = true;
  overclockAcknowledgement.checked = false;
  updateConfigurationControls(latestData);
});
saveConfigurationButton.addEventListener("click", openConfigurationConfirmation);
document.querySelector("#cancel-configuration").addEventListener("click", () => configurationDialog.close());
confirmConfigurationButton.addEventListener("click", saveConfiguration);
acknowledgeConfigurationOutcomeButton.addEventListener("click", acknowledgeConfigurationOutcome);
overclockAcknowledgement.addEventListener("change", () => {
  confirmConfigurationButton.disabled = !overclockAcknowledgement.checked;
});
boardSelect.addEventListener("change", () => {
  if (actionBusy) return;
  selectBoard(boardSelect.value || null, boardSelect.selectedOptions[0]?.textContent || "");
  refresh();
});
rescanButton.addEventListener("click", rescanBoards);
rescanBoards();
setRefreshInterval(500);
window.setInterval(() => {
  if (latestData) render(latestData);
  updateDiagnosticsAge();
}, 1000);
