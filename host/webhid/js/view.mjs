function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

const directions = [
  ["UART TX", "uart_tx"], ["UART RX", "uart_rx"], ["CDC TX", "usb_tx"], ["CDC RX", "usb_rx"],
];
const faultNames = ["Known RX bytes dropped", "RX controller error observations"];
const faultReasonName = reason => reason?.last_configuration_error_reason
  ? reason.last_configuration_error : "None recorded";

function uptimeLabel(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  return [[Math.floor(seconds / 86400), "d"], [Math.floor(seconds % 86400 / 3600), "h"],
    [Math.floor(seconds % 3600 / 60), "m"], [seconds % 60, "s"]]
    .filter(([value, unit]) => value || unit === "s").map(([value, unit]) => `${value}${unit}`).join(" ");
}

function serviceLoad(value) {
  return value === null ? "Warming up / unavailable" : `${(value / 100).toFixed(1)}%`;
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

function renderChannels(data) {
  const body = document.querySelector("#channel-rows");
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
      ? `UART measurements: ${channel.uart_measurement_state_name}`
      : channel.error ? `Snapshot unavailable: ${channel.error}` : "Waiting for a fresh Input snapshot";
    lifecycle.append(validity);
    row.append(lifecycle);

    const applied = document.createElement("td");
    const settings = document.createElement("strong");
    settings.textContent = fresh && channel.baud_rate
      ? `${channel.baud_rate.toLocaleString()} ${channel.data_bits}${channel.parity}${channel.stop_bits}`
      : "--";
    const pins = data.configuration?.pins[index];
    const pinLabel = document.createElement("small");
    pinLabel.textContent = pins
      ? `TX ${pins.tx === 255 ? "--" : `GP${pins.tx}`} / RX ${pins.rx === 255 ? "--" : `GP${pins.rx}`}`
      : "Board pin map unavailable";
    applied.title = fresh && channel.channel_state === 0
      ? "Last successfully applied UART setting; backend is stopped."
      : "Last successfully applied UART setting, not the latest requested setting.";
    applied.append(settings, pinLabel);
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
        ? `${(channel.rates[key] / 1000).toLocaleString(undefined,
          { minimumFractionDigits: 1, maximumFractionDigits: 1 })} KB/s` : "-- KB/s";
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

    const history = document.createElement("td");
    const faultValues = [
      diagnostic && diagnostic.uart_measurement_state !== 0
        ? `${faultNames[0]}: ${diagnostic.rx_dropped_bytes.toLocaleString()} B` : `${faultNames[0]}: --`,
      diagnostic && diagnostic.uart_measurement_state !== 0
        ? `${faultNames[1]}: ${diagnostic.rx_error_observations.toLocaleString()}` : `${faultNames[1]}: --`,
      `Last configuration error: ${faultReasonName(diagnostic)}`,
    ];
    history.textContent = faultValues.join("; ");
    history.title = diagnostic
      ? `Diagnostic read ${Math.max(0, Math.floor((now - Date.parse(diagnostic.updated_at)) / 1000))} s ago.`
      : data.diagnostics_errors[index] ?? "Channel diagnostics unavailable.";
    row.append(history);

    const peaks = document.createElement("td");
    const diagnosticUnavailable = !diagnostic || diagnostic.uart_measurement_state === 0;
    const peakRows = [
      ["UART TX ring", "uart_tx", diagnosticUnavailable],
      ["UART RX ring", "uart_rx", diagnosticUnavailable],
      ["CDC TX FIFO", "cdc_tx", !diagnostic],
      ["CDC RX FIFO", "cdc_rx", !diagnostic],
    ];
    peaks.textContent = diagnostic ? peakRows.map(([label, key, unavailable]) =>
      `${label}: ${displayPeak(diagnostic.peaks[key], capacities?.[key], unavailable)}`).join("; ")
      : "Diagnostics unavailable";
    peaks.title = "Observed peak since boot; exact byte units. A peak is not current congestion or proof of loss.";
    row.append(peaks);
    body.append(row);
  });
}

export function renderDashboard(data) {
  const statusMessage = document.querySelector("#status-message");
  document.querySelector("#connection-label").textContent = data.connected ? "Connected" : "Disconnected";
  document.querySelector("#product-name").textContent = data.identity?.product_name ?? "--";
  document.querySelector("#mcu").textContent = data.identity?.mcu ?? "--";
  document.querySelector("#board-model").textContent = data.identity?.board_model ?? "--";
  document.querySelector("#firmware").textContent = data.identity?.firmware_version ?? "--";
  document.querySelector("#serial").textContent = data.identity?.serial_number ?? "--";
  document.querySelector("#system-clock").textContent = data.telemetry
    ? `${(data.telemetry.system_clock_hz / 1000000).toLocaleString(undefined, { maximumFractionDigits: 3 })} MHz`
    : "--";
  const runtime = data.telemetry?.runtime;
  document.querySelector("#uptime").textContent = runtime ? uptimeLabel(runtime.uptime_ms) : "--";
  document.querySelector("#core0-service-load").textContent = runtime
    ? serviceLoad(runtime.service_load_basis_points[0]) : "--";
  document.querySelector("#core1-service-load").textContent = runtime
    ? serviceLoad(runtime.service_load_basis_points[1]) : "--";
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
  document.querySelector("#action-result").textContent = action
    ? action.status_name === "Idle" ? "No recent action"
      : `${action.status_name}${action.action ? ` — action ${action.action}` : ""}`
        + (action.error_reason ? ` (reason ${action.error_reason})` : "")
    : data.action_error ?? "--";
  document.querySelector("#traffic-note").hidden = !data.traffic_incomplete;
  document.querySelector("#traffic-note").textContent = data.traffic_incomplete
    ? "Some traffic rates are unavailable after a sequence gap, stale stream, saturated interval, or cached UART sample. Totals remain modulo 2^32."
    : "";
  renderChannels(data);
  statusMessage.textContent = data.identity_error ? `Device identity unavailable: ${data.identity_error}`
    : data.configuration_error ? `Board configuration unavailable: ${data.configuration_error}`
      : data.telemetry_error ? `Live telemetry unavailable: ${data.telemetry_error}`
        : data.action_error ? `Board action: ${data.action_error}`
          : data.connected ? "HID status is available" : "Waiting for a PicoUart HID interface";
  statusMessage.parentElement.dataset.error = String(Boolean(data.identity_error || data.configuration_error
    || data.telemetry_error || data.action_error || !data.connected));
}
