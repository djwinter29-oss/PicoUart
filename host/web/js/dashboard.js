const token = document.querySelector('meta[name="csrf-token"]').content;
const connection = document.querySelector("#connection");
const channelRows = document.querySelector("#channel-rows");
const statusMessage = document.querySelector("#status-message");
const actionButtons = [...document.querySelectorAll(".actions button")];
let actionBusy = false;
let latestData = null;

function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

const channelHistory = new Map();
const directions = [["UART TX", "uart_tx"], ["UART RX", "uart_rx"], ["USB TX", "usb_tx"], ["USB RX", "usb_rx"]];
const faultHelp = {
  init_failed: "Backend initialization failed. Check firmware resources and configuration.",
  control_error: "A line-coding request failed. Check the applied settings; PIO supports 8N1 only.",
  rx_overrun: "UART-to-USB data was dropped. Check whether the host is reading and whether flow control is needed.",
  rx_error: "Receive error events, not a count of bad bytes. Check baud/framing, common ground and RX wiring. "
    + "An undriven RX input or a break can cause errors; the monitor cannot determine the cause.",
};

function uptimeLabel(milliseconds) {
  const seconds = Math.floor(milliseconds / 1000);
  return `${Math.floor(seconds / 86400)}d ${Math.floor(seconds % 86400 / 3600)}h `
    + `${Math.floor(seconds % 3600 / 60)}m ${seconds % 60}s`;
}

function channelPresentation(data, channel, now = Date.now()) {
  const sample = Date.parse(data.updated_at);
  let history = channelHistory.get(channel.id);
  if (!data.connected || !history || history.serial !== data.identity?.serial_number
    || data.runtime?.uptime_ms < history.uptime
    || directions.some(([, key]) => channel.totals[key] < history.totals[key])) {
    history = { serial: data.identity?.serial_number, uptime: data.runtime?.uptime_ms,
      totals: { ...channel.totals }, sample, observedSample: sample, receivedAt: now,
      gaps: data.traffic_gaps, rates: null, open: false };
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
  const faults = Object.entries(diagnostic?.faults ?? {}).filter(([, value]) => value.count > 0);
  const hasFault = faults.length > 0 || channel.health_labels.some(name => name in faultHelp);
  const traffic = directions.some(([, key]) => (history.rates?.[key] ?? 0) > 0);
  const state = !fresh || channel.health === null ? "unknown"
    : hasFault ? "attention"
    : !channel.health_labels.includes("ready") ? "initializing"
    : !channel.cdc_open && !traffic ? "idle" : "ready";
  return { history, diagnostic, faults, state, rates: fresh ? history.rates : null };
}

function renderChannels(data) {
  const focusedAction = document.activeElement?.dataset.channelAction;
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
    flags.textContent = channel.health_labels.includes("control_pending") ? "Applying settings"
      : view.faults.length ? "Recorded history; expand for details" : view.state === "idle" ? "CDC closed; receiver still active"
      : channel.health_labels.filter(label => label in faultHelp).join(", ") || "No active flags";
    health.append(flags);
    if (view.faults.length) {
      const details = document.createElement("details");
      details.className = "fault-details";
      details.open = view.history.open;
      details.addEventListener("toggle", () => {
        if (details.isConnected) view.history.open = details.open;
      });
      const summary = document.createElement("summary");
      summary.dataset.channelAction = `details-${channel.id}`;
      summary.textContent = view.faults.map(([name, value]) => `${name} (${value.count})`).join(", ");
      details.append(summary);
      for (const [name, value] of view.faults) {
        const fault = document.createElement("p");
        const unit = name === "rx_overrun" ? "dropped bytes" : name === "rx_error" ? "events" : "observed assertions";
        fault.textContent = `${name}: ${value.count} ${unit}; last detected at uptime `
          + `${uptimeLabel(value.last_seen_ms)}. ${faultHelp[name]}`;
        details.append(fault);
      }
      health.append(details);
    }
    row.append(health);

    row.append(makeCell(
      `cdc-state ${channel.cdc_open ? "cdc-open" : "cdc-closed"}`,
      channel.cdc_open ? "Open" : "Closed",
    ));

    const traffic = document.createElement("td");
    traffic.className = "traffic";
    const metrics = document.createElement("div");
    metrics.className = "traffic-metrics";
    directions.forEach(([label, key]) => {
      const metric = document.createElement("span");
      const value = document.createElement("b");
      metric.textContent = `${label} `;
      value.textContent = view.rates
        ? `${view.rates[key].toLocaleString(undefined, { maximumFractionDigits: 1 })} B/s` : "-- B/s";
      metric.title = `${label}: ${channel.totals[key]} B observed total since monitoring began (lower bound).`;
      metric.append(value);
      metrics.append(metric);
    });
    traffic.append(metrics);
    row.append(traffic);

    const overflow = data.overflow_counts[channel.id];
    row.append(makeCell("overflow", overflow === null ? "--" : `${overflow} B`));
    channelRows.append(row);
  });
  if (focusedAction) {
    document.querySelector(`[data-channel-action="${focusedAction}"]`)?.focus({ preventScroll: true });
  }
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
  const seconds = runtime ? Math.floor(runtime.uptime_ms / 1000) : 0;
  document.querySelector("#uptime").textContent = runtime
    ? `${Math.floor(seconds / 86400)}d ${Math.floor(seconds % 86400 / 3600)}h `
      + `${Math.floor(seconds % 3600 / 60)}m ${seconds % 60}s` : "--";
  document.querySelector("#cpu-usage").textContent = runtime
    ? runtime.cpu_usage_basis_points.map(value => value == null ? "--" : `${(value / 100).toFixed(1)}%`).join(" / ")
    : "--";
  document.querySelector("#ram-usage").textContent = runtime
    ? `${(runtime.ram_reserved_bytes / 1024).toFixed(1)} / ${(runtime.ram_total_bytes / 1024).toFixed(0)} KiB`
    : "--";
  document.querySelector("#temperature").textContent = data.board
    ? `${data.board.temperature_celsius.toFixed(1)} °C`
    : "--";
  const led = data.connected ? data.led : null;
  const ledButton = document.querySelector("#toggle-led");
  ledButton.dataset.state = led?.output_on == null ? "unknown" : led.output_on ? "on" : "off";
  ledButton.textContent = `Board LED: ${led?.output_on == null ? "--" : led.output_on ? "On" : "Off"}`;
  ledButton.title = led?.manual_on == null ? "Toggle manual LED state; USB activity may also light the LED"
    : `Toggle manual LED state (currently ${led.manual_on ? "On" : "Off"}); USB activity may also light the LED`;
  document.querySelector("#toggle-led").disabled = actionBusy || !data.connected || led?.output_on === null;
  document.querySelector("#reset-board").disabled = actionBusy || !data.connected;
  document.querySelector("#sample-time").textContent = data.updated_at
    ? `Last report ${new Date(data.updated_at).toLocaleTimeString()}`
    : "Waiting for HID telemetry";
  document.querySelector("#overflow-total").textContent = data.overflow_counts.every((value) => value !== null)
    ? `${data.overflow_counts.reduce((sum, count) => sum + count, 0)} B`
    : "--";
  document.querySelector("#reset-board").hidden = !data.board?.hid_reset_enabled;
  const trafficNote = document.querySelector("#traffic-note");
  trafficNote.dataset.incomplete = String(data.traffic_incomplete);
  trafficNote.textContent = data.traffic_incomplete
    ? "A report gap or saturated delta was detected. Affected rates are unavailable; hover a rate for observed totals."
    : "Rates use observed bytes over at least one second; -- means warming up or stale. Hover a rate for total bytes.";
  renderChannels(data);

  const message = data.error
    || (data.metadata_error ? `Board metadata unavailable: ${data.metadata_error}` : null)
    || (data.hardware_error ? `Hardware information unavailable: ${data.hardware_error}` : null)
    || (data.identity_error ? `Device identity unavailable: ${data.identity_error}` : null)
    || (data.runtime_error ? `Runtime status unavailable: ${data.runtime_error}` : null)
    || (data.led_error ? `LED state unavailable: ${data.led_error}` : null)
    || (data.diagnostics_error ? `Channel diagnostics unavailable: ${data.diagnostics_error}` : null)
    || (data.connected ? "HID telemetry is live" : "Waiting for a PicoUart HID interface");
  statusMessage.textContent = message;
  statusMessage.parentElement.dataset.error = Boolean(
    data.error || data.metadata_error || data.hardware_error || data.identity_error || data.runtime_error
      || data.led_error || data.diagnostics_error || !data.connected,
  );
}

async function refresh() {
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    if (!response.ok) throw new Error(`Status request failed (${response.status})`);
    render(await response.json());
  } catch (error) {
    if (latestData) render({ ...latestData, connected: false, error: error.message });
    statusMessage.textContent = error.message;
    statusMessage.parentElement.dataset.error = "true";
  }
}

async function runAction(action) {
  if (action === "reset" && !window.confirm("Reset the PicoUart board?")) return;
  actionBusy = true;
  actionButtons.forEach((button) => { button.disabled = true; });
  try {
    const response = await fetch(`/api/actions/${action}`, {
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
    document.querySelector("#toggle-led").disabled = !latestData?.connected || latestData.led?.output_on === null;
    document.querySelector("#reset-board").disabled = !latestData?.connected;
  }
}

document.querySelector("#toggle-led").addEventListener("click", () => runAction("toggle-led"));
document.querySelector("#reset-board").addEventListener("click", () => runAction("reset"));
refresh();
window.setInterval(refresh, 1000);