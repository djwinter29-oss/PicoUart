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
  age.hidden = !data.connected;
  const seconds = Number.isFinite(sample) ? Math.max(0, Math.floor((now - sample) / 1000)) : null;
  const stale = seconds !== null && seconds > 3;
  age.textContent = seconds === null ? "Waiting for telemetry"
    : `Telemetry ${seconds} s ago${stale ? " (stale)" : ""}`;
  age.dataset.stale = String(stale);
}

function channelFresh(channel, now) {
  const inputTime = Date.parse(channel.input_updated_at);
  const featureTime = Date.parse(channel.feature_updated_at);
  const sample = Number.isFinite(inputTime) ? inputTime : featureTime;
  return Number.isFinite(sample) && now >= sample && now - sample <= 3000;
}

function displayPeak(value, capacity, unavailable) {
  if (unavailable || value === undefined || value === null) return "--";
  if (!capacity) return `${value.toLocaleString()} B`;
  return `${value.toLocaleString()} / ${capacity.toLocaleString()} B (${(value / capacity * 100).toFixed(1)}%)`;
}

function showChannelDiagnostics(id, channel, diagnostic, capacities, data) {
  const dialog = document.querySelector("#channel-diagnostics-dialog");
  const content = document.querySelector("#channel-diagnostics-content");
  document.querySelector("#channel-diagnostics-title").textContent = `CDC${id} diagnostics`;
  content.replaceChildren();
  if (!diagnostic) {
    content.textContent = data.diagnostics_errors?.[id] ?? "Channel diagnostics unavailable.";
    dialog.showModal();
    return;
  }
  const uartUnavailable = diagnostic.uart_measurement_state === 0;
  const values = [
    ["UART measurement", channel.uart_measurement_state_name],
    ["Known RX bytes dropped", uartUnavailable ? "--" : `${diagnostic.rx_dropped_bytes.toLocaleString()} B`],
    ["RX controller error observations", uartUnavailable ? "--" : diagnostic.rx_error_observations.toLocaleString()],
    ["Last configuration error", faultReasonName(diagnostic)],
    ["UART TX ring peak", displayPeak(diagnostic.peaks.tx_ring, capacities?.uart_tx, uartUnavailable)],
    ["UART RX ring peak", displayPeak(diagnostic.peaks.rx_ring, capacities?.uart_rx, uartUnavailable)],
    ["CDC TX FIFO peak", displayPeak(diagnostic.peaks.cdc_tx, capacities?.cdc_tx, false)],
    ["CDC RX FIFO peak", displayPeak(diagnostic.peaks.cdc_rx, capacities?.cdc_rx, false)],
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
  dialog.showModal();
}

let channelPointerInside = false;
let pendingChannelData = null;
let trackedChannelBody = null;

function channelInteractionActive(body) {
  if (trackedChannelBody !== body) {
    trackedChannelBody = body;
    body.addEventListener("pointerenter", () => { channelPointerInside = true; });
    body.addEventListener("pointerleave", () => {
      channelPointerInside = false;
      flushPendingChannelData();
    });
    body.addEventListener("focusout", () => globalThis.setTimeout(flushPendingChannelData, 0));
  }
  return channelPointerInside || Boolean(document.activeElement && body.contains(document.activeElement));
}

function flushPendingChannelData() {
  if (!pendingChannelData || channelPointerInside
    || document.activeElement && trackedChannelBody?.contains(document.activeElement)) return;
  const data = pendingChannelData;
  pendingChannelData = null;
  renderChannels(data);
}

function renderChannels(data) {
  const body = document.querySelector("#channel-rows");
  if (channelInteractionActive(body)) {
    pendingChannelData = data;
    return;
  }
  pendingChannelData = null;
  body.replaceChildren();
  const now = Date.now();
  const capacities = data.configuration?.capacities;
  data.channels.forEach((channel, index) => {
    const diagnostic = data.channel_diagnostics[index];
    const fresh = data.connected && channelFresh(channel, now);
    const row = document.createElement("tr");
    const port = document.createElement("td");
    const name = document.createElement("span");
    name.className = "port-name";
    const number = document.createElement("span");
    number.className = "port-index";
    number.textContent = String(index + 1).padStart(2, "0");
    name.append(number, document.createTextNode(`CDC${index}`));
    port.append(name);
    row.append(port);

    const lifecycle = document.createElement("td");
    const stateName = fresh ? channel.channel_state_name : "Unknown";
    const stateClass = stateName === "Error" ? "attention"
      : ["Starting", "Configuring"].includes(stateName) ? "initializing"
        : stateName === "Ready" ? "ready" : "idle";
    const state = document.createElement("span");
    state.className = `state state-${stateClass}`;
    state.textContent = stateName;
    lifecycle.append(state);
    const validity = document.createElement("small");
    validity.className = "health-flags";
    validity.textContent = fresh
      ? `UART ${channel.uart_measurement_state_name}` : "Waiting for sample";
    validity.title = fresh ? `UART measurements: ${channel.uart_measurement_state_name}.`
      : channel.error ?? "Waiting for a fresh Input snapshot.";
    lifecycle.append(validity);
    row.append(lifecycle);

    const applied = document.createElement("td");
    const settings = document.createElement("strong");
    settings.textContent = fresh && channel.baud_rate
      ? `${channel.baud_rate.toLocaleString()} ${channel.data_bits}${channel.parity}${channel.stop_bits}`
      : "--";
    applied.title = fresh && channel.channel_state === 0
      ? "Last successfully applied UART setting; backend is stopped."
      : "Last successfully applied UART setting, not the latest requested setting.";
    applied.append(settings);
    row.append(applied);

    const traffic = document.createElement("td");
    traffic.className = "traffic";
    const metrics = document.createElement("div");
    metrics.className = "traffic-metrics";
    directions.forEach(([label, key]) => {
      const metric = document.createElement("span");
      const direction = document.createElement("span");
      direction.className = "traffic-label";
      direction.textContent = label;
      const rate = document.createElement("b");
      rate.textContent = fresh && channel.rates?.[key] != null
        ? `${(channel.rates[key] / 1000).toFixed(1)} KB/s` : "-- KB/s";
      const total = document.createElement("span");
      total.className = "traffic-total";
      const counter = channel.counters[key];
      total.textContent = fresh && counter !== null ? `${counter.toLocaleString()} B` : "--";
      metric.title = `${label}: bytes since firmware boot, modulo 2^32.`;
      metric.append(direction, rate, total);
      metrics.append(metric);
    });
    traffic.append(metrics);
    row.append(traffic);

    const diagnostics = document.createElement("td");
    const diagnosticsButton = document.createElement("button");
    diagnosticsButton.className = "button button-small diagnostics-action";
    diagnosticsButton.type = "button";
    diagnosticsButton.textContent = "Diagnostics";
    diagnosticsButton.addEventListener("click", () => {
      const current = pendingChannelData ?? data;
      showChannelDiagnostics(index, current.channels[index] ?? channel,
        current.channel_diagnostics?.[index] ?? diagnostic,
        current.configuration?.capacities ?? capacities, current);
    });
    diagnostics.append(diagnosticsButton);
    row.append(diagnostics);
    body.append(row);
  });
}

export function renderDashboard(data) {
  const statusMessage = document.querySelector("#status-message");
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
  document.querySelector("#ram-usage").textContent = identity
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
  statusMessage.textContent = data.identity_error ? `Device identity unavailable: ${data.identity_error}`
    : data.configuration_error ? `Board configuration unavailable: ${data.configuration_error}`
      : data.telemetry_error ? `Live telemetry unavailable: ${data.telemetry_error}`
        : data.action_error ? `Board action: ${data.action_error}`
          : actionMessage ?? (data.connected ? "HID status is available" : "Waiting for a PicoUart HID interface");
  statusMessage.parentElement.dataset.error = String(Boolean(data.identity_error || data.configuration_error
    || data.telemetry_error || data.action_error || !data.connected));
}
