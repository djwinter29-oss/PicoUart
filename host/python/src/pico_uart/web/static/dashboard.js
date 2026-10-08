const token = document.querySelector('meta[name="csrf-token"]').content;
const connection = document.querySelector("#connection");
const channelRows = document.querySelector("#channel-rows");
const statusMessage = document.querySelector("#status-message");
const actionButtons = [...document.querySelectorAll(".actions button")];

function makeCell(className, text) {
  const cell = document.createElement("td");
  if (className) cell.className = className;
  cell.textContent = text;
  return cell;
}

function renderChannels(data) {
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
    const alerts = channel.health_labels.filter(
      (label) => !["ready", "cdc_open", "pio"].includes(label),
    );
    flags.textContent = alerts.join(" · ") || "No active flags";
    health.append(flags);
    row.append(health);

    row.append(makeCell("backend", channel.backend));
    row.append(makeCell(
      `cdc-state ${channel.cdc_open ? "cdc-open" : "cdc-closed"}`,
      channel.cdc_open ? "Open" : "Closed",
    ));

    const traffic = document.createElement("td");
    traffic.className = "traffic";
    [
      ["UART TX", channel.totals.uart_tx],
      ["UART RX", channel.totals.uart_rx],
      ["USB TX", channel.totals.usb_tx],
      ["USB RX", channel.totals.usb_rx],
    ].forEach(([label, count]) => {
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

function render(data) {
  connection.dataset.state = data.connected ? "connected" : "disconnected";
  document.querySelector("#connection-label").textContent = data.connected ? "Connected" : "Disconnected";
  document.querySelector("#firmware").textContent = data.board?.firmware_version ?? "--";
  document.querySelector("#temperature").textContent = data.board
    ? `${data.board.temperature_celsius.toFixed(2)} °C`
    : "--";
  document.querySelector("#sequence").textContent = data.sequence ?? "--";
  document.querySelector("#sample-time").textContent = data.updated_at
    ? `Last report ${new Date(data.updated_at).toLocaleTimeString()}`
    : "Waiting for HID telemetry";
  document.querySelector("#overflow-total").textContent = data.overflow_counts.every((value) => value !== null)
    ? `${data.overflow_counts.reduce((sum, count) => sum + count, 0)} B`
    : "--";
  document.querySelector("#reset-board").hidden = !data.board?.hid_reset_enabled;
  renderChannels(data);

  const message = data.error || (data.connected ? "HID telemetry is live" : "Waiting for a PicoUart HID interface");
  statusMessage.textContent = message;
  statusMessage.parentElement.dataset.error = Boolean(data.error || !data.connected);
}

async function refresh() {
  try {
    const response = await fetch("/api/status", { cache: "no-store" });
    if (!response.ok) throw new Error(`Status request failed (${response.status})`);
    render(await response.json());
  } catch (error) {
    statusMessage.textContent = error.message;
    statusMessage.parentElement.dataset.error = "true";
  }
}

async function runAction(action) {
  if (action === "reset" && !window.confirm("Reset the PicoUart board?")) return;
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
  } catch (error) {
    statusMessage.textContent = error.message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    actionButtons.forEach((button) => { button.disabled = false; });
  }
}

document.querySelector("#toggle-led").addEventListener("click", () => runAction("toggle-led"));
document.querySelector("#reset-board").addEventListener("click", () => runAction("reset"));
refresh();
window.setInterval(refresh, 1000);