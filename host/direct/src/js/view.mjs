// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

const directions = [
  ["UART TX", "uart_tx"], ["UART RX", "uart_rx"], ["USB RX", "usb_rx"], ["USB TX", "usb_tx"],
];
const faultReasonName = reason => reason?.last_configuration_error_reason
  ? reason.last_configuration_error : "None recorded";

function uptimeLabel(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  return [[Math.floor(seconds / 86400), "d"], [Math.floor(seconds % 86400 / 3600), "h"],
    [Math.floor(seconds % 3600 / 60), "m"], [seconds % 60, "s"]]
    .filter(([value, unit]) => value || unit === "s").map(([value, unit]) => `${value}${unit}`).join(" ");
}

function serviceLoad(value) {
  return value === null ? "--" : `${(value / 100).toFixed(1)}%`;
}

export function updateReportAge(data, now = Date.now()) {
  const sample = Date.parse(data.updated_at);
  const age = document.querySelector("#sample-time");
  const seconds = Number.isFinite(sample) ? Math.max(0, Math.floor((now - sample) / 1000)) : null;
  const stale = seconds !== null && seconds > 3;
  age.hidden = !data.connected || (seconds !== null && !stale);
  age.textContent = seconds === null ? "Waiting for telemetry"
    : `Telemetry stale · ${seconds} s ago`;
  age.dataset.stale = String(stale);
}

function channelFresh(channel, now) {
  const inputTime = Date.parse(channel.input_updated_at);
  const featureTime = Date.parse(channel.feature_updated_at);
  const sample = Number.isFinite(inputTime) ? inputTime : featureTime;
  return Number.isFinite(sample) && now >= sample && now - sample <= 3000;
}

function displayPeak(value, capacity) {
  const formatted = Number.isSafeInteger(value) && value >= 0 ? `${value.toLocaleString()} B` : "--";
  return Number.isSafeInteger(value) && Number.isSafeInteger(capacity) && capacity > 0 && value <= capacity
    ? `${formatted} (${(value / capacity * 100).toFixed(1)}%)` : formatted;
}

let openDiagnosticsId = null;
let openDiagnosticsDialog = null;
let diagnosticsAgeElement = null;
let diagnosticsReadAt = null;
let openDiagnosticsSample = null;
let openDiagnosticsError = null;

export function updateDiagnosticsAge(now = Date.now()) {
  if (!openDiagnosticsDialog?.open || !diagnosticsAgeElement) return;
  const seconds = Number.isFinite(diagnosticsReadAt)
    ? Math.max(0, Math.floor((now - diagnosticsReadAt) / 1000)) : null;
  diagnosticsAgeElement.textContent = seconds === null
    ? "Diagnostics read time unavailable" : `Diagnostics read ${seconds} s ago`;
}

function showChannelDiagnostics(id, channel, diagnostic, capacities, data) {
  const dialog = document.querySelector("#channel-diagnostics-dialog");
  const content = document.querySelector("#channel-diagnostics-content");
  openDiagnosticsId = id;
  openDiagnosticsDialog = dialog;
  openDiagnosticsSample = diagnostic;
  openDiagnosticsError = data.diagnostics_errors?.[id] ?? null;
  document.querySelector("#channel-diagnostics-title").textContent = `CDC${id} diagnostics`;
  content.replaceChildren();
  diagnosticsAgeElement = null;
  diagnosticsReadAt = null;
  if (!diagnostic) {
    content.textContent = data.diagnostics_errors?.[id] ?? "Channel diagnostics unavailable.";
    if (!dialog.open) dialog.showModal();
    return;
  }
  diagnosticsAgeElement = document.createElement("p");
  diagnosticsAgeElement.className = "diagnostics-age";
  diagnosticsReadAt = Date.parse(diagnostic.updated_at);
  content.append(diagnosticsAgeElement);
  updateDiagnosticsAge();
  const uartAvailable = diagnostic.uart_measurement_state !== 0;
  const values = [
    ["UART measurement", diagnostic.uart_measurement_state_name],
    ["UART sample age", diagnostic.uart_measurement_state === 0 ? "Unavailable"
      : diagnostic.uart_sample_age_100ms === 255 ? "At least 25.5 s"
        : `${(diagnostic.uart_sample_age_100ms / 10).toFixed(1)} s`],
    ["Known RX bytes dropped", uartAvailable ? `${diagnostic.rx_dropped_bytes.toLocaleString()} B` : "Unavailable"],
    ["RX controller error observations",
      uartAvailable ? diagnostic.rx_error_observations.toLocaleString() : "Unavailable"],
    ["Last configuration error", faultReasonName(diagnostic)],
    ["Last fault observed", diagnostic.last_fault_uptime_s === 0 && diagnostic.last_configuration_error_reason === 0
      && (!uartAvailable || diagnostic.rx_dropped_bytes === 0 && diagnostic.rx_error_observations === 0)
      ? uartAvailable ? "None recorded" : "Unavailable"
      : `${diagnostic.last_fault_uptime_s.toLocaleString()} s uptime`],
    ["UART TX ring observed peak since boot", uartAvailable
      ? displayPeak(diagnostic.peaks.uart_tx, capacities?.uart_tx) : "Unavailable"],
    ["UART RX ring observed peak since boot", uartAvailable
      ? displayPeak(diagnostic.peaks.uart_rx, capacities?.uart_rx) : "Unavailable"],
    ["CDC TX FIFO observed peak since boot", displayPeak(diagnostic.peaks.cdc_tx, capacities?.cdc_tx)],
    ["CDC RX FIFO observed peak since boot", displayPeak(diagnostic.peaks.cdc_rx, capacities?.cdc_rx)],
  ];
  const list = document.createElement("dl");
  for (const [label, value] of values) {
    const term = document.createElement("dt");
    const detail = document.createElement("dd");
    term.textContent = label;
    detail.textContent = value;
    list.append(term, detail);
  }
  content.append(list);
  if (!dialog.open) dialog.showModal();
}

const channelRowViews = new Map();

function createChannelRow(id) {
  const row = document.createElement("tr");
  const port = document.createElement("td");
  const name = document.createElement("span");
  name.className = "port-name";
  const number = document.createElement("span");
  number.className = "port-index";
  number.textContent = String(id + 1).padStart(2, "0");
  name.append(number, document.createTextNode(`CDC${id}`));
  port.append(name);
  row.append(port);

  const lifecycle = document.createElement("td");
    lifecycle.className = "channel-status";
  const state = document.createElement("span");
  const validity = document.createElement("small");
  validity.className = "health-flags";
  lifecycle.append(state, validity);
  row.append(lifecycle);

  const applied = document.createElement("td");
  const settings = document.createElement("strong");
  applied.append(settings);
  row.append(applied);

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

  const diagnostics = document.createElement("td");
  const diagnosticsButton = document.createElement("button");
  diagnosticsButton.className = "button button-small diagnostics-action";
  diagnosticsButton.type = "button";
  diagnosticsButton.textContent = "Diagnostics";
  diagnostics.append(diagnosticsButton);
  row.append(diagnostics);

  const view = { row, state, validity, applied, settings, metrics, diagnosticsButton,
    channel: null, diagnostic: null, capacities: null, data: null };
  diagnosticsButton.addEventListener("click", () => {
    if (!view.channel || !view.data) return;
    showChannelDiagnostics(id, view.channel, view.diagnostic, view.capacities, view.data);
  });
  return view;
}

function renderChannels(data, now = Date.now()) {
  const body = document.querySelector("#channel-rows");
  const capacities = data.configuration?.capacities;
  const activeIds = new Set();
  data.channels.forEach((channel, index) => {
    const id = channel.id;
    activeIds.add(id);
    let view = channelRowViews.get(id);
    if (!view) {
      view = createChannelRow(id);
      channelRowViews.set(id, view);
      body.append(view.row);
    }
    const diagnostic = data.channel_diagnostics[index];
    const fresh = data.connected && channelFresh(channel, now);
    const stateName = fresh ? channel.channel_state_name : "Unknown";
    const stateClass = stateName === "Error" ? "attention"
      : ["Starting", "Configuring"].includes(stateName) ? "initializing"
        : stateName === "Ready" ? "ready" : "idle";
    view.state.className = `state state-${stateClass}`;
    view.state.textContent = stateName;
    const measurementState = String(channel.uart_measurement_state_name ?? "unavailable").toLowerCase();
    view.state.hidden = !fresh;
    view.validity.textContent = !fresh || measurementState === "unavailable" ? "— Unavailable"
      : measurementState === "cached" ? "CACHED" : measurementState === "current" ? "LIVE" : "— Unavailable";
    if (channel.error) view.validity.textContent = "READ ERROR";
    else if (channel.feature_error && !fresh) view.validity.textContent = "FEATURE ERROR";
    view.validity.title = channel.error ? `Input snapshot unavailable: ${channel.error}`
      : channel.feature_error ? `Feature snapshot unavailable: ${channel.feature_error}`
      : !fresh ? "Channel snapshot unavailable or stale."
      : measurementState === "unavailable" ? "Measurement data unavailable."
        : `Measurement status: ${measurementState}.`;
    view.settings.textContent = fresh && channel.baud_rate
      ? `${channel.baud_rate.toLocaleString()} ${channel.data_bits}${channel.parity}${channel.stop_bits}`
      : "--";
    view.applied.title = fresh && channel.channel_state === 0
      ? "Last successfully applied UART setting; backend is stopped."
      : "Last successfully applied UART setting, not the latest requested setting.";
    directions.forEach(([label, key]) => {
      const metric = view.metrics.get(key);
      const uartUnavailable = key.startsWith("uart_") && measurementState !== "current";
      metric.rate.textContent = fresh && !uartUnavailable && channel.rates?.[key] != null
        ? `${(channel.rates[key] / 1000).toFixed(1)} KB/s` : "-- KB/s";
      const counter = channel.counters[key];
      const uartTotalUnavailable = key.startsWith("uart_") && measurementState === "unavailable";
      metric.total.textContent = fresh && !uartTotalUnavailable && counter !== null
        ? `${counter.toLocaleString()} B` : "--";
      metric.metric.title = `${label}: bytes since firmware boot, modulo 2^32.`;
    });
    view.channel = channel;
    view.diagnostic = diagnostic;
    view.capacities = capacities;
    view.data = data;
    const dialog = document.querySelector("#channel-diagnostics-dialog");
    if (dialog.open && openDiagnosticsId === id
      && (diagnostic !== openDiagnosticsSample
        || (data.diagnostics_errors?.[id] ?? null) !== openDiagnosticsError)) {
      showChannelDiagnostics(id, channel, diagnostic, capacities, data);
    }
  });
  for (const [id, view] of channelRowViews) {
    if (!activeIds.has(id)) {
      view.row.remove();
      channelRowViews.delete(id);
    }
  }
}

export function updateChannelFreshness(data, now = Date.now()) {
  renderChannels(data, now);
}

export function renderDashboard(data) {
  const statusMessage = document.querySelector("#status-message");
  const continuityWarning = document.querySelector("#traffic-continuity-warning");
  continuityWarning.hidden = !data.traffic_incomplete;
  const continuityMessage = "Some input reports, intervals, or UART measurements were unavailable. Rates use valid "
    + "samples and may be unavailable while they rebaseline; continuous observation cannot be verified.";
  if (continuityWarning.textContent !== continuityMessage) continuityWarning.textContent = continuityMessage;
  document.querySelector("#connection-label").textContent = data.connected ? "Connected" : "Disconnected";
  document.querySelector("#product-name").textContent = data.identity?.product_name ?? "--";
  document.querySelector("#board-model").textContent = data.identity?.board_model && data.identity?.mcu
    ? `${data.identity.board_model} (${data.identity.mcu})`
    : data.identity?.board_model ?? "--";
  document.querySelector("#firmware").textContent = data.identity?.firmware_version ?? "--";
  document.querySelector("#serial").textContent = data.identity?.serial_number ?? "--";
  document.querySelector("#system-clock").textContent = data.telemetry
    ? `${(data.telemetry.system_clock_hz / 1000000).toLocaleString(undefined, { maximumFractionDigits: 3 })} MHz`
    : "--";
  const runtime = data.telemetry?.runtime;
  document.querySelector("#uptime").textContent = runtime ? uptimeLabel(runtime.uptime_ms) : "--";
  document.querySelector("#service-load").textContent = runtime
    ? `${serviceLoad(runtime.service_load_basis_points[0])} / ${serviceLoad(runtime.service_load_basis_points[1])}`
    : "--";
  const identity = data.identity;
  const ramKnown = Number.isSafeInteger(identity?.ram_reserved_bytes) && identity.ram_reserved_bytes >= 0
    && Number.isSafeInteger(identity.ram_total_bytes) && identity.ram_total_bytes > 0
    && identity.ram_reserved_bytes <= identity.ram_total_bytes;
  document.querySelector("#ram-usage").textContent = ramKnown
    ? `${(identity.ram_reserved_bytes / 1024).toFixed(1)} / ${(identity.ram_total_bytes / 1024).toFixed(0)} KiB`
      + ` (${(identity.ram_reserved_bytes / identity.ram_total_bytes * 100).toFixed(1)}%)`
    : "--";
  document.querySelector("#temperature").textContent = data.telemetry
    ? `${data.telemetry.temperature_celsius.toFixed(1)} \u00b0C` : "--";
  const led = data.connected ? data.telemetry?.led.output_on : null;
  const ledButton = document.querySelector("#toggle-led");
  ledButton.textContent = `Board LED: ${led == null ? "--" : led ? "On" : "Off"}`;
  ledButton.dataset.state = led == null ? "unknown" : led ? "on" : "off";
  ledButton.title = led == null ? "Board identification LED state unavailable"
    : `Set board identification LED ${led ? "Off" : "On"}`;
  updateReportAge(data);
  const action = data.action_result;
  renderChannels(data);
  const actionMessage = action && action.status_name !== "Idle"
    ? `Latest board action: ${action.status_name}${action.action ? `, action ${action.action}` : ""}`
      + (action.error_reason ? ` (reason ${action.error_reason})` : "") : null;
  const channelError = data.channels.find(channel => channel.error);
  const featureError = data.channels.find(channel => channel.feature_error);
  statusMessage.textContent = data.identity_error ? `Device identity unavailable: ${data.identity_error}`
    : data.configuration_error ? `Board configuration unavailable: ${data.configuration_error}`
      : data.telemetry_error ? `Live telemetry unavailable: ${data.telemetry_error}`
        : data.action_error ? `Board action: ${data.action_error}`
          : channelError ? `CDC${channelError.id} snapshot unavailable: ${channelError.error}`
            : featureError ? `CDC${featureError.id} Feature snapshot unavailable: ${featureError.feature_error}`
              : actionMessage ?? (data.connected ? "HID status is available" : "Waiting for a PicoUart HID interface");
  statusMessage.parentElement.dataset.error = String(Boolean(data.identity_error || data.configuration_error
    || data.telemetry_error || data.action_error || channelError || featureError || !data.connected));
}
