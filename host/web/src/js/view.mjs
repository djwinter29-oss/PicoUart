// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import { currentRates, directions } from "./traffic.mjs?v=1";

const channelRowViews = new Map();
const channelStates = ["closed", "starting", "ready", "configuring", "error"];
const measurementStates = ["Unavailable", "Current", "Cached"];
const resetCauses = [
  "Unknown (not distinguished)", "Power-on", "Watchdog reset", "Software-requested reset", "HardFault recovery",
  "Power-on or brownout", "Brownout", "RUN-pin reset", "Debugger reset", "User-requested reboot",
  "Configuration-save reboot",
];

function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

export function formatBytes(value) {
  return Number.isSafeInteger(value) && value >= 0 ? `${value.toLocaleString()} B` : "--";
}

export function uptimeLabel(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor(seconds / 3600) % 24;
  const minutes = Math.floor(seconds / 60) % 60;
  const remainder = seconds % 60;
  return `${days ? `${days}d ` : ""}${hours}h ${minutes}m ${remainder}s`;
}

export function formatPin(value) {
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

export function boardIdentityName(board, microcontroller) {
  const boardName = boardModelName(board);
  const mcuName = microcontrollerName(microcontroller);
  return boardName === "--" || mcuName === "--" ? boardName : `${boardName} (${mcuName})`;
}

export function deviceUid(identity) {
  const uid = identity?.device_uid ?? identity?.serial_number;
  if (Array.isArray(uid)) return uid.map(value => Number(value).toString(16).padStart(2, "0")).join("").toUpperCase();
  return typeof uid === "string" ? uid.toUpperCase() : "--";
}

export function resetCauseName(value) {
  return resetCauses[value] ?? (value == null ? "--" : `Unknown (${value})`);
}

export function measurementLabel(value) {
  return measurementStates[value] ?? "Unknown";
}

function measurementStatus(value) {
  const state = measurementLabel(value).toLowerCase();
  return state === "current" ? "LIVE" : state === "cached" ? "CACHED" : "— Unavailable";
}

function createChannelRow(id, buildDiagnosticsCell) {
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

export function renderChannels(data, channelRows, buildDiagnosticsCell) {
  const channels = Array.isArray(data.channels) ? data.channels : [];
  const now = performance.now();
  const activeIds = new Set();
  channels.forEach(channel => {
    const id = channel.id;
    activeIds.add(id);
    let view = channelRowViews.get(id);
    if (!view) {
      view = createChannelRow(id, buildDiagnosticsCell);
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
