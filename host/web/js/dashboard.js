const token = document.querySelector('meta[name="csrf-token"]').content;
const connection = document.querySelector("#connection");
const channelRows = document.querySelector("#channel-rows");
const diagnosticsDialog = document.querySelector("#diagnostics-dialog");
const diagnosticsContent = document.querySelector("#diagnostics-content");
const statusMessage = document.querySelector("#status-message");
document.querySelector("#close-diagnostics").addEventListener("click", () => diagnosticsDialog.close());
document.querySelector("#refresh-diagnostics").addEventListener("click", async () => {
  diagnosticData = null;
  diagnosticError = null;
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
let selectionVersion = 0;
let refreshing = null;
let scanning = false;
let scanError = null;
let refreshIntervalMs = 500;
let refreshTimer = null;
let diagnosticBoard = null;
let diagnosticData = null;
let diagnosticError = null;
let diagnosticRequest = null;
const channelHistory = new Map();
let reportSample = null;
let reportReceivedAt = 0;

const directions = [
  ["UART TX", "uart_tx", true],
  ["UART RX", "uart_rx", true],
  ["USB RX", "usb_rx", false],
  ["USB TX", "usb_tx", false],
];
const channelStates = ["closed", "starting", "ready", "configuring", "error"];
const measurementStates = ["Unavailable", "Current", "Cached"];
const profileNames = ["SDK default", "150 MHz", "200 MHz", "250 MHz", "280 MHz"];
const resetCauses = ["Unknown", "Power-on", "Watchdog", "Software-requested"];
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

function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

function updateReportAge(data, now = Date.now()) {
  const sample = Date.parse(data.updated_at);
  const age = document.querySelector("#sample-time");
  age.hidden = !data.connected;
  if (!data.connected) reportSample = null;
  if (data.connected && Number.isFinite(sample) && sample !== reportSample) {
    reportSample = sample;
    reportReceivedAt = now;
  }
  const seconds = Math.max(0, Math.floor((now - reportReceivedAt) / 1000));
  const stale = Number.isFinite(sample) && now - reportReceivedAt > 3000;
  age.textContent = Number.isFinite(sample)
    ? `Report ${seconds} s ago${stale ? " (stale)" : ""}` : "Waiting for report";
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

function mcuName(value) {
  return ({ 1: "RP2040", 2: "RP2350" })[value] ?? (value == null ? "--" : `Unknown MCU (${value})`);
}

function deviceUid(identity) {
  const uid = identity?.device_uid ?? identity?.serial_number;
  if (Array.isArray(uid)) return uid.map(value => Number(value).toString(16).padStart(2, "0")).join("").toUpperCase();
  return typeof uid === "string" ? uid.toUpperCase() : "--";
}

function resetCauseName(value) {
  return resetCauses[value] ?? (value == null ? "--" : `Unknown (${value})`);
}

function storedProfileName(value) {
  if (value === 255) return "Unavailable";
  return profileNames[value] ?? (value == null ? "--" : `Unknown profile (${value})`);
}

function renderStaticConfiguration(data) {
  const identity = data.identity;
  const configuration = data.configuration;
  document.querySelector("#stored-profile").textContent = storedProfileName(configuration?.stored_profile);
  document.querySelector("#reset-cause").textContent = resetCauseName(configuration?.reset_cause);
  document.querySelector("#uart-tx-capacity").textContent = formatBytes(configuration?.uart_tx_ring_capacity_bytes);
  document.querySelector("#uart-rx-capacity").textContent = formatBytes(configuration?.uart_rx_ring_capacity_bytes);
  document.querySelector("#cdc-tx-capacity").textContent = formatBytes(configuration?.cdc_tx_fifo_capacity_bytes);
  document.querySelector("#cdc-rx-capacity").textContent = formatBytes(configuration?.cdc_rx_fifo_capacity_bytes);

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

  const total = identity?.ram_total_bytes;
  const reserved = identity?.ram_reserved_bytes;
  document.querySelector("#ram-usage").textContent = Number.isSafeInteger(total) && total > 0
    && Number.isSafeInteger(reserved) && reserved >= 0 && reserved <= total
    ? `${(reserved / 1024).toFixed(1)} / ${(total / 1024).toFixed(0)} KiB`
      + ` (${(reserved / total * 100).toFixed(1)}%)`
    : "--";
}

function currentRates(channel, now, data) {
  const id = channel.id;
  const identity = data.identity?.device_uid ?? data.identity?.serial_number;
  const uptime = data.runtime?.uptime_ms;
  const totals = Object.fromEntries(directions.map(([, key]) => [key, channel.totals?.[key]]));
  let history = channelHistory.get(id);
  if (!data.connected || !history || history.identity !== identity
    || (Number.isFinite(uptime) && Number.isFinite(history.uptime) && uptime < history.uptime)) {
    history = { identity, uptime, sequence: channel.sequence, measurement: channel.uart_measurement_state,
      totals, rates: null, sequenceReceivedAt: now, baselineReady: Number.isInteger(channel.sequence)
        && channel.sequence !== 0 };
    channelHistory.set(id, history);
  } else if (channel.sequence !== history.sequence) {
    const sequenceDelta = (channel.sequence - history.sequence + 65536) % 65536;
    const contiguous = sequenceDelta === 1;
    const interval = channel.interval_ms;
    const intervalValid = Number.isInteger(interval) && interval > 0 && interval < 65535;
    const rates = {};
    for (const [, key, uart] of directions) {
      const current = channel[key];
      const previous = history.totals[key];
      const measurementValid = !uart
        || (channel.uart_measurement_state === 1 && history.measurement === 1);
      rates[key] = contiguous && intervalValid && measurementValid
        && Number.isInteger(current) && Number.isInteger(previous)
        ? ((current - previous) >>> 0) * 1000 / interval : null;
    }
    history.rates = history.baselineReady ? rates : null;
    history.sequence = channel.sequence;
    history.measurement = channel.uart_measurement_state;
    history.totals = totals;
    history.sequenceReceivedAt = now;
    history.baselineReady = Number.isInteger(channel.sequence) && channel.sequence !== 0;
  }
  history.uptime = uptime;
  const fresh = data.connected && now - history.sequenceReceivedAt <= 3000;
  return { rates: fresh ? history.rates : null, fresh };
}

function measurementLabel(value) {
  return measurementStates[value] ?? "Unknown";
}

function diagnosticEntry(id) {
  if (Array.isArray(diagnosticData)) return diagnosticData.find(item => item.id === id) ?? diagnosticData[id] ?? null;
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
  if (!diagnosticData && !diagnosticError) {
    container.textContent = "Loading channel diagnostics...";
    return;
  }
  if (diagnosticError) {
    container.textContent = `Diagnostics unavailable: ${diagnosticError}`;
    return;
  }
  const diagnostic = diagnosticEntry(id);
  if (!diagnostic) {
    container.textContent = "Diagnostics unavailable for this channel.";
    return;
  }
  const config = latestData?.configuration ?? {};
  const fields = [
    ["UART measurement", measurementLabel(diagnostic.uart_measurement_state)],
    ["UART sample age", diagnostic.uart_measurement_state === 0 ? "Unavailable"
      : diagnostic.uart_sample_age_100ms === 255 ? "At least 25.5 s"
        : `${(diagnostic.uart_sample_age_100ms / 10).toFixed(1)} s`],
    ["Known RX bytes dropped", formatBytes(diagnostic.rx_overrun_count)],
    ["RX controller error observations", diagnostic.rx_error_count?.toLocaleString() ?? "--"],
    ["Last configuration error", errorReasons[diagnostic.last_control_error_reason]
      ?? `Unknown (${diagnostic.last_control_error_reason ?? "--"})`],
    ["Last fault observed", diagnostic.last_fault_uptime_s === 0 && diagnostic.rx_overrun_count === 0
      && diagnostic.rx_error_count === 0 && diagnostic.last_control_error_reason === 0
      ? "None recorded" : `${diagnostic.last_fault_uptime_s?.toLocaleString() ?? "--"} s uptime`],
    ["UART TX ring observed peak since boot", peakValue(
      diagnostic.uart_tx_ring_peak_bytes, config.uart_tx_ring_capacity_bytes)],
    ["UART RX ring observed peak since boot", peakValue(
      diagnostic.uart_rx_ring_peak_bytes, config.uart_rx_ring_capacity_bytes)],
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
}

async function loadDiagnostics() {
  if (!selectedBoard || diagnosticRequest) return diagnosticRequest;
  const board = selectedBoard;
  const version = selectionVersion;
  diagnosticRequest = (async () => {
    diagnosticError = null;
    try {
      const response = await fetch(`/api/diagnostics?board=${encodeURIComponent(board)}`, { cache: "no-store" });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || `Diagnostics request failed (${response.status})`);
      if (version !== selectionVersion) return;
      diagnosticData = result.channel_diagnostics ?? result;
      diagnosticBoard = board;
    } catch (error) {
      if (version === selectionVersion) diagnosticError = error.message;
    } finally {
      diagnosticRequest = null;
      if (version === selectionVersion) {
        renderDiagnosticContent(diagnosticsContent, Number(diagnosticsContent.dataset.channel));
      }
    }
  })();
  return diagnosticRequest;
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

function renderChannels(data) {
  channelRows.replaceChildren();
  if (!Array.isArray(data.channels)) return;
  const now = Date.now();
  data.channels.forEach(channel => {
    const view = currentRates(channel, now, data);
    const row = document.createElement("tr");
    const port = document.createElement("td");
    const portName = document.createElement("span");
    portName.className = "port-name";
    const portIndex = document.createElement("span");
    portIndex.className = "port-index";
    portIndex.textContent = String(channel.id + 1).padStart(2, "0");
    portName.append(portIndex, document.createTextNode(`CDC${channel.id}`));
    port.append(portName);
    row.append(port);
    row.append(makeCell(
      `cdc-state ${channel.state === "closed" ? "cdc-closed" : "cdc-open"}`,
      channel.state === "closed" ? "Closed" : channel.state === "unknown" ? "Unknown" : "Open",
    ));

    const configuration = makeCell("uart-configuration", "");
    const settings = document.createElement("strong");
    settings.textContent = Number.isInteger(channel.baud_rate) && channel.baud_rate > 0
      ? `${channel.baud_rate.toLocaleString()} ${channel.data_bits}`
        + `${({ none: "N", odd: "O", even: "E" })[channel.parity] ?? "?"}${channel.stop_bits}`
      : "--";
    const pins = document.createElement("small");
    const pin = data.configuration?.channel_pins?.[channel.id];
    pins.textContent = `TX ${formatPin(pin?.tx_pin)} / RX ${formatPin(pin?.rx_pin)}`
      + ` · UART ${measurementLabel(channel.uart_measurement_state)}`;
    configuration.title = "Applied baud and framing; GPIO assignments are static board configuration.";
    configuration.append(settings, pins);
    row.append(configuration);

    const lifecycle = document.createElement("td");
    const state = document.createElement("span");
    const stale = !view.fresh;
    const stateName = stale ? "unknown" : channelStates[channel.state] ?? channel.state;
    state.className = `state state-${stateName === "error" ? "attention" : stateName}`;
    state.textContent = stateName === "error" ? "Attention" : stateName;
    lifecycle.append(state);
    if (channel.uart_measurement_state !== 1) {
      const measurement = document.createElement("small");
      measurement.className = "health-flags";
      measurement.textContent = `UART sample ${measurementLabel(channel.uart_measurement_state).toLowerCase()}`;
      lifecycle.append(measurement);
    }
    row.append(lifecycle);

    const traffic = document.createElement("td");
    traffic.className = "traffic";
    const metrics = document.createElement("div");
    metrics.className = "traffic-metrics";
    directions.forEach(([label, key]) => {
      const metric = document.createElement("span");
      const direction = document.createElement("span");
      const value = document.createElement("b");
      direction.className = "traffic-label";
      direction.textContent = label;
      value.textContent = view.rates?.[key] != null
        ? `${(view.rates[key] / 1000).toFixed(1)} KB/s` : "-- KB/s";
      const total = document.createElement("span");
      total.className = "traffic-total";
      total.textContent = formatBytes(channel.totals?.[key]);
      metric.title = `${label}: ${formatBytes(channel.totals?.[key])} since firmware boot, modulo 2^32.`;
      metric.append(direction, value, total);
      metrics.append(metric);
    });
    traffic.append(metrics);
    row.append(traffic);
    row.append(buildDiagnosticsCell(channel));
    channelRows.append(row);
  });
}

function render(data) {
  const reconnected = data.connected && !latestData?.connected;
  if (reconnected) {
    channelHistory.clear();
    diagnosticBoard = null;
    diagnosticData = null;
    diagnosticError = null;
  }
  latestData = data;
  connection.dataset.state = data.connected ? "connected" : "disconnected";
  document.querySelector("#connection-label").textContent = data.connected ? "Connected" : "Disconnected";
  document.querySelector("#mcu").textContent = mcuName(data.identity?.mcu_id ?? data.identity?.mcu);
  document.querySelector("#board-model").textContent = boardModelName(
    data.identity?.board_model_id ?? data.identity?.board_model,
  );
  const systemClock = data.telemetry?.system_clock_hz ?? data.hardware?.system_clock_hz;
  document.querySelector("#system-clock").textContent = Number.isFinite(systemClock)
    ? `${(systemClock / 1000000).toLocaleString(undefined, { maximumFractionDigits: 3 })} MHz` : "--";
  document.querySelector("#firmware").textContent = data.identity?.firmware_version ?? "--";
  document.querySelector("#product-name").textContent = data.identity?.product_name ?? "--";
  document.querySelector("#serial").textContent = deviceUid(data.identity);
  renderStaticConfiguration(data);
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
  const trafficNote = document.querySelector("#traffic-note");
  trafficNote.hidden = !data.traffic_incomplete;
  trafficNote.dataset.incomplete = String(data.traffic_incomplete);
  trafficNote.textContent = data.traffic_incomplete
    ? "Input report gaps detected; affected rate windows restart after a contiguous sample. Byte counters are modulo 2^32 since boot."
    : "";
  renderChannels(data);

  const message = scanError || data.error
    || (data.identity_error ? `Device identity unavailable: ${data.identity_error}` : null)
    || (data.configuration_error ? `Board configuration unavailable: ${data.configuration_error}` : null)
    || (data.runtime_error ? `Runtime telemetry unavailable: ${data.runtime_error}` : null)
    || (data.led_error ? `LED state unavailable: ${data.led_error}` : null)
    || (data.connected ? "HID telemetry is live" : "Waiting for a PicoUart HID interface");
  statusMessage.textContent = message;
  statusMessage.parentElement.dataset.error = Boolean(scanError || data.error || data.identity_error
    || data.configuration_error || data.runtime_error || data.led_error || !data.connected);
}

async function refresh() {
  if (refreshing === selectionVersion) return;
  const version = selectionVersion;
  refreshing = version;
  try {
    const query = selectedBoard ? `?board=${encodeURIComponent(selectedBoard)}` : "";
    const response = await fetch(`/api/status${query}`, { cache: "no-store" });
    const data = await response.json();
    if (version !== selectionVersion) return;
    if (!response.ok) throw new Error(data.error || `Status request failed (${response.status})`);
    setRefreshInterval(data.connected ? data.refresh_interval_ms : 500);
    render(data);
  } catch (error) {
    if (version !== selectionVersion) return;
    setRefreshInterval(500);
    if (latestData) render({ ...latestData, connected: false, error: error.message });
    statusMessage.textContent = error.message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    if (refreshing === version) refreshing = null;
  }
}

function selectBoard(id, label) {
  if (selectedBoard === id) return;
  selectedBoard = id;
  setRefreshInterval(500);
  selectedLabel = label;
  selectionVersion++;
  channelHistory.clear();
  reportSample = null;
  diagnosticBoard = null;
  diagnosticData = null;
  diagnosticError = null;
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
    const response = await fetch("/api/boards", { cache: "no-store" });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || `Board scan failed (${response.status})`);
    scanError = null;
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
  if (action === "reset" && !window.confirm(
    "Reboot PicoUart? USB serial ports will disconnect briefly.",
  )) return;
  const board = selectedBoard;
  actionBusy = true;
  boardSelect.disabled = true;
  actionButtons.forEach(button => { button.disabled = true; });
  try {
    const response = await fetch(`/api/actions/${action}?board=${encodeURIComponent(board)}`, {
      method: "POST",
      headers: { "X-CSRF-Token": token },
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || `Action failed (${response.status})`);
    statusMessage.textContent = action === "reset"
      ? "Reboot command sent; USB will reconnect shortly." : "LED toggle command sent";
    statusMessage.parentElement.dataset.error = "false";
    await refresh();
  } catch (error) {
    statusMessage.textContent = error.message;
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
  }
}

document.querySelector("#toggle-led").addEventListener("click", () => runAction("toggle-led"));
document.querySelector("#reset-board").addEventListener("click", () => runAction("reset"));
boardSelect.addEventListener("change", () => {
  if (actionBusy) return;
  selectBoard(boardSelect.value || null, boardSelect.selectedOptions[0]?.textContent || "");
  refresh();
});
rescanButton.addEventListener("click", rescanBoards);
rescanBoards();
setRefreshInterval(500);
