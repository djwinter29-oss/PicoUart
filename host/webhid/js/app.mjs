import { PicoUartWebHid } from "./device.mjs?v=3";
import { renderDashboard, updateReportAge } from "./view.mjs?v=3";

const connectButton = document.querySelector("#connect");
const toggleLedButton = document.querySelector("#toggle-led");
const connection = document.querySelector("#connection");
const statusMessage = document.querySelector("#status-message");

function render(state) {
  renderDashboard(state.dashboard);
  const connected = ["connected", "reading", "writing"].includes(state.phase);
  const busy = ["connecting", "reading", "writing", "disconnecting"].includes(state.phase);
  connection.dataset.state = connected ? "connected" : "disconnected";
  document.querySelector("#connection-label").textContent = state.phase === "unsupported" ? "Unavailable"
    : connected ? "Connected" : state.phase === "connecting" ? "Connecting" : "Disconnected";
  connectButton.textContent = state.phase === "connecting" ? "Connecting..."
    : state.phase === "disconnecting" ? "Disconnecting..." : connected ? "Disconnect" : "Connect";
  connectButton.className = `button ${connected ? "button-danger" : "button-secondary"}`;
  connectButton.disabled = !["disconnected", "connected", "reading"].includes(state.phase);
  toggleLedButton.disabled = !connected || busy || state.dashboard.led?.output_on == null;
  if (!connected || state.error) {
    statusMessage.textContent = state.error ?? state.message;
    statusMessage.parentElement.dataset.error = String(Boolean(state.error) || state.phase === "unsupported");
  }
}

const hid = globalThis.isSecureContext ? navigator.hid : undefined;
const client = new PicoUartWebHid(hid, render);
render(client.state);
connectButton.addEventListener("click", () =>
  client.state.phase === "disconnected" ? client.connect() : client.disconnect());
toggleLedButton.addEventListener("click", () => client.toggleLed());
globalThis.setInterval(() => client.refresh(), 1000);
globalThis.setInterval(() => updateReportAge(client.state.dashboard), 1000);