const token = document.querySelector('meta[name="csrf-token"]').content;
const connection = document.querySelector("#connection");
const channelRows = document.querySelector("#channel-rows");
const statusMessage = document.querySelector("#status-message");
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

function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

const channelHistory = new Map();
let reportSample = null;
let reportReceivedAt = 0;

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

const directions = [["UART TX", "uart_tx"], ["UART RX", "uart_rx"], ["USB TX", "usb_tx"], ["USB RX", "usb_rx"]];
const faultHelp = {
  init_failed: "Initialization failure; check firmware resources and configuration.",
  control_error: "UART configuration failure; check the applied settings.",
  rx_overrun: "Dropped UART data recorded; check that the host is reading.",
  rx_error: "Receive error recorded; check baud rate, framing and wiring.",
};

function uptimeLabel(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  return [[Math.floor(seconds / 86400), "d"], [Math.floor(seconds % 86400 / 3600), "h"],
    [Math.floor(seconds % 3600 / 60), "m"], [seconds % 60, "s"]]
    .filter(([value, unit]) => value || unit === "s").map(([value, unit]) => `${value}${unit}`).join(" ");
}

function channelPresentation(data, channel, now = Date.now()) {
  const sample = Date.parse(data.updated_at);
  let history = channelHistory.get(channel.id);
  if (!data.connected || !history || history.serial !== data.identity?.serial_number
    || data.runtime?.uptime_ms < history.uptime
    || directions.some(([, key]) => channel.totals[key] < history.totals[key])) {
    history = { serial: data.identity?.serial_number, uptime: data.runtime?.uptime_ms,
      totals: { ...channel.totals }, sample, observedSample: sample, receivedAt: now,
      gaps: data.traffic_gaps, rates: null };
    channelHistory.set(channel.id, history);
  }
  const elapsed = sample - history.sample;
  if (data.traffic_gaps !== history.gaps || elapsed < 0 || elapsed > 3000) {
    history.rates = null;
    history.sample = sample;
    history.totals = { ...channel.totals };
    history.gaps = data.traffic_gaps;
  } else if (elapsed >= 1000) {
    history.rates = Object.fromEntries(directions.map(([, key]) =>
      [key, (channel.totals[key] - history.totals[key]) * 1000 / elapsed]));
    history.sample = sample;
    history.totals = { ...channel.totals };
  }
  if (!Number.isFinite(history.sample) && Number.isFinite(sample)) {
    history.sample = sample;
    history.totals = { ...channel.totals };
    history.rates = null;
  }
  if (data.runtime) history.uptime = data.runtime.uptime_ms;
  if (Number.isFinite(sample) && sample !== history.observedSample) {
    history.observedSample = sample;
    history.receivedAt = now;
  }
  const fresh = data.connected && Number.isFinite(sample) && now - history.receivedAt >= 0
    && now - history.receivedAt <= 3000;
  const diagnostic = data.connected ? data.channel_diagnostics?.[channel.id] : null;
  const faults = Object.keys(faultHelp).filter(name =>
    channel.health_labels.includes(name) || diagnostic?.faults[name]?.count > 0);
  const traffic = directions.some(([, key]) => (history.rates?.[key] ?? 0) > 0);
  const state = !fresh || channel.health === null ? "unknown"
    : faults.length ? "attention"
    : !channel.health_labels.includes("ready") ? "initializing"
    : !channel.cdc_open && !traffic ? "idle" : "ready";
  return { diagnostic, faults, state, fresh, rates: fresh ? history.rates : null };
}

function renderChannels(data) {
  channelRows.replaceChildren();
  data.channels.forEach((channel) => {
    const view = channelPresentation(data, channel);
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
      `cdc-state ${channel.cdc_open ? "cdc-open" : "cdc-closed"}`,
      channel.cdc_open ? "Open" : "Closed",
    ));
    const configuration = makeCell("uart-configuration", "");
    const settings = document.createElement("strong");
    const diagnostic = view.diagnostic;
    settings.textContent = diagnostic?.baud_rate
      ? `${diagnostic.baud_rate} ${diagnostic.data_bits}${diagnostic.parity}${diagnostic.stop_bits}` : "--";
    const pins = document.createElement("small");
    pins.textContent = diagnostic
      ? `TX ${diagnostic.tx_pin === null ? "--" : `GP${diagnostic.tx_pin}`} / `
        + `RX ${diagnostic.rx_pin === null ? "--" : `GP${diagnostic.rx_pin}`}`
      : "Firmware diagnostics unavailable";
    configuration.title = diagnostic ? "Successfully applied UART settings, not the latest requested settings."
      : data.diagnostics_error ?? "Connect a device with channel diagnostic reports.";
    configuration.append(settings, pins);
    row.append(configuration);

    const health = document.createElement("td");
    const state = document.createElement("span");
    state.className = `state state-${view.state}`;
    state.textContent = view.state;
    health.append(state);
    const flags = document.createElement("small");
    flags.className = "health-flags";
    flags.textContent = view.faults.map(name => {
      const current = ["init_failed", "control_error"].includes(name) && channel.health_labels.includes(name);
      return `${!view.fresh ? "Last reported" : current ? "Current" : "Recorded"}: ${name}`;
    }).join("; ") || (view.state === "unknown" ? "Waiting for status"
      : channel.health_labels.includes("control_pending") ? "Applying settings"
      : view.state === "idle" ? "Receiver enabled" : "No active flags");
    flags.title = view.faults.map(name => faultHelp[name]).join(" ");
    health.append(flags);
    row.append(health);

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
      value.textContent = view.rates
        ? `${view.rates[key].toLocaleString(undefined, { maximumFractionDigits: 1 })} B/s` : "-- B/s";
      metric.title = `${label}: ${channel.totals[key]} B observed total since monitoring began (lower bound).`;
      metric.append(direction, value);
      metrics.append(metric);
    });
    traffic.append(metrics);
    row.append(traffic);

    const overflow = data.overflow_counts[channel.id];
    row.append(makeCell("overflow", overflow === null ? "--" : `${overflow} B`));
    channelRows.append(row);
  });
}

function render(data) {
  latestData = data;
  connection.dataset.state = data.connected ? "connected" : "disconnected";
  document.querySelector("#connection-label").textContent = data.connected ? "Connected" : "Disconnected";
  document.querySelector("#mcu").textContent = data.hardware?.mcu ?? "--";
  document.querySelector("#system-clock").textContent = data.hardware
    ? `${(data.hardware.system_clock_hz / 1000000).toLocaleString(undefined, { maximumFractionDigits: 3 })} MHz`
    : "--";
  document.querySelector("#firmware").textContent = data.board?.firmware_version ?? "--";
  document.querySelector("#product-name").textContent = data.identity?.product_name ?? "--";
  document.querySelector("#serial").textContent = data.identity?.serial_number ?? "--";
  const runtime = data.runtime;
  document.querySelector("#uptime").textContent = runtime ? uptimeLabel(runtime.uptime_ms) : "--";
  document.querySelector("#cpu-usage").textContent = runtime
    ? runtime.cpu_usage_basis_points.map(value => value == null ? "--" : `${(value / 100).toFixed(1)}%`).join(" / ")
    : "--";
  document.querySelector("#ram-usage").textContent = runtime
    ? `${(runtime.ram_reserved_bytes / 1024).toFixed(1)} / ${(runtime.ram_total_bytes / 1024).toFixed(0)} KiB`
      + ` (${(runtime.ram_reserved_bytes / runtime.ram_total_bytes * 100).toFixed(1)}%)`
    : "--";
  document.querySelector("#temperature").textContent = data.board
    ? `${data.board.temperature_celsius.toFixed(1)} °C`
    : "--";
  const led = data.connected ? data.led : null;
  const ledButton = document.querySelector("#toggle-led");
  ledButton.textContent = `Board LED: ${led?.output_on == null ? "--" : led.output_on ? "On" : "Off"}`;
  ledButton.dataset.state = led?.output_on == null ? "unknown" : led.output_on ? "on" : "off";
  ledButton.title = led?.manual_on == null ? "Toggle manual LED state; USB activity may also light the LED"
    : `Toggle manual LED state (currently ${led.manual_on ? "On" : "Off"}); USB activity may also light the LED`;
  document.querySelector("#toggle-led").disabled = actionBusy || !data.connected || led?.output_on == null;
  document.querySelector("#reset-board").disabled = actionBusy || !data.connected;
  updateReportAge(data);
  document.querySelector("#overflow-total").textContent = data.overflow_counts.every((value) => value !== null)
    ? `${data.overflow_counts.reduce((sum, count) => sum + count, 0)} B`
    : "--";
  document.querySelector("#reset-board").hidden = !data.board?.hid_reset_enabled;
  const trafficNote = document.querySelector("#traffic-note");
  trafficNote.hidden = !data.traffic_incomplete;
  trafficNote.dataset.incomplete = String(data.traffic_incomplete);
  trafficNote.textContent = data.traffic_incomplete
    ? "Incomplete traffic samples detected; totals may undercount."
    : "";
  renderChannels(data);

  const message = scanError || data.error
    || (data.metadata_error ? `Board metadata unavailable: ${data.metadata_error}` : null)
    || (data.hardware_error ? `Hardware information unavailable: ${data.hardware_error}` : null)
    || (data.identity_error ? `Device identity unavailable: ${data.identity_error}` : null)
    || (data.runtime_error ? `Runtime status unavailable: ${data.runtime_error}` : null)
    || (data.led_error ? `LED state unavailable: ${data.led_error}` : null)
    || (data.diagnostics_error ? `Channel diagnostics unavailable: ${data.diagnostics_error}` : null)
    || (data.connected ? "HID telemetry is live" : "Waiting for a PicoUart HID interface");
  statusMessage.textContent = message;
  statusMessage.parentElement.dataset.error = Boolean(
    scanError || data.error || data.metadata_error || data.hardware_error || data.identity_error || data.runtime_error
      || data.led_error || data.diagnostics_error || !data.connected,
  );
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
    render(data);
  } catch (error) {
    if (version !== selectionVersion) return;
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
  selectedLabel = label;
  selectionVersion++;
  channelHistory.clear();
  reportSample = null;
  render({
    connected: false, updated_at: null, traffic_incomplete: false, traffic_gaps: 0,
    overflow_counts: Array(6).fill(null),
    channels: Array.from({ length: 6 }, (_, index) => ({
      id: index, health: null, health_labels: [], cdc_open: false,
      totals: Object.fromEntries(directions.map(([, key]) => [key, 0])),
    })),
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
  if (action === "reset" && !window.confirm("Reset the PicoUart board?")) return;
  const board = selectedBoard;
  actionBusy = true;
  boardSelect.disabled = true;
  actionButtons.forEach((button) => { button.disabled = true; });
  try {
    const response = await fetch(`/api/actions/${action}?board=${encodeURIComponent(board)}`, {
      method: "POST",
      headers: { "X-CSRF-Token": token },
    });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || `Action failed (${response.status})`);
    statusMessage.textContent = action === "reset" ? "Reset command sent" : "LED toggle command sent";
    statusMessage.parentElement.dataset.error = "false";
    await refresh();
  } catch (error) {
    statusMessage.textContent = error.message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    actionBusy = false;
    boardSelect.disabled = !boardSelect.value;
    rescanButton.disabled = scanning;
    document.querySelector("#toggle-led").disabled = !latestData?.connected || latestData.led?.output_on == null;
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
window.setInterval(refresh, 1000);