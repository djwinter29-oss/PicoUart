function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

function renderChannels(data) {
  const channelRows = document.querySelector("#channel-rows");
  channelRows.replaceChildren();
  data.channels.forEach((channel) => {
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
    const health = document.createElement("td");
    const state = document.createElement("span");
    state.className = `state state-${channel.state}`;
    state.textContent = channel.state;
    health.append(state);
    const flags = document.createElement("small");
    flags.className = "health-flags";
    flags.textContent = channel.health_labels.filter(label => !["ready", "cdc_open", "pio"].includes(label))
      .join(" \u00b7 ") || "No active flags";
    health.append(flags);
    row.append(health);
    row.append(makeCell("backend", channel.backend));
    row.append(makeCell(`cdc-state ${channel.cdc_open ? "cdc-open" : "cdc-closed"}`,
      channel.cdc_open ? "Open" : "Closed"));
    const traffic = document.createElement("td");
    traffic.className = "traffic";
    [["UART TX >=", channel.totals.uart_tx], ["UART RX >=", channel.totals.uart_rx],
      ["USB TX >=", channel.totals.usb_tx], ["USB RX >=", channel.totals.usb_rx]].forEach(([label, count]) => {
      const metric = document.createElement("span");
      const value = document.createElement("b");
      metric.textContent = `${label} `;
      value.textContent = `${count} B`;
      metric.append(value);
      traffic.append(metric);
    });
    row.append(traffic);
    const overflow = data.overflow_counts[channel.id];
    row.append(makeCell("overflow", overflow === null ? "--" : `${overflow} B`));
    row.append(makeCell("ring", `${channel.ring_high_watermark} B`));
    channelRows.append(row);
  });
}

export function renderDashboard(data) {
  const connection = document.querySelector("#connection");
  const statusMessage = document.querySelector("#status-message");
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
    ? `${(runtime.ram_reserved_bytes / 1024).toFixed(1)} / ${(runtime.ram_total_bytes / 1024).toFixed(0)} KiB `
      + `(${(runtime.ram_reserved_bytes * 100 / runtime.ram_total_bytes).toFixed(1)}%)` : "--";
  document.querySelector("#temperature").textContent = data.board
    ? `${data.board.temperature_celsius.toFixed(2)} \u00b0C` : "--";
  const led = data.connected ? data.led : null;
  const ledButton = document.querySelector("#toggle-led");
  ledButton.dataset.state = led?.output_on == null ? "unknown" : led.output_on ? "on" : "off";
  ledButton.textContent = `Board LED: ${led?.output_on == null ? "--" : led.output_on ? "On" : "Off"}`;
  ledButton.title = led?.manual_on == null ? "Toggle manual LED state; USB activity may also light the LED"
    : `Toggle manual LED state (currently ${led.manual_on ? "On" : "Off"}); USB activity may also light the LED`;
  document.querySelector("#sample-time").textContent = data.updated_at
    ? `Last report ${new Date(data.updated_at).toLocaleTimeString()}` : "Waiting for HID telemetry";
  document.querySelector("#overflow-total").textContent = data.overflow_counts.every(value => value !== null)
    ? `${data.overflow_counts.reduce((sum, count) => sum + count, 0)} B` : "--";
  const resetButton = document.querySelector("#reset-board");
  if (resetButton) resetButton.hidden = !data.board?.hid_reset_enabled;
  const trafficNote = document.querySelector("#traffic-note");
  trafficNote.dataset.incomplete = String(data.traffic_incomplete);
  trafficNote.textContent = data.traffic_incomplete
    ? "A report gap or saturated delta was detected; traffic totals are lower bounds."
    : "Observed lower bounds since monitoring began; missed reports and saturated deltas can reduce totals.";
  renderChannels(data);
  statusMessage.textContent = data.error
    || (data.metadata_error ? `Board metadata unavailable: ${data.metadata_error}` : null)
    || (data.hardware_error ? `Hardware information unavailable: ${data.hardware_error}` : null)
    || (data.identity_error ? `Device identity unavailable: ${data.identity_error}` : null)
    || (data.runtime_error ? `Runtime status unavailable: ${data.runtime_error}` : null)
    || (data.led_error ? `LED state unavailable: ${data.led_error}` : null)
    || (data.connected ? "HID telemetry is live" : "Waiting for a PicoUart HID interface");
  statusMessage.parentElement.dataset.error = Boolean(
    data.error || data.metadata_error || data.hardware_error || data.identity_error || data.runtime_error
      || data.led_error || !data.connected,
  );
}