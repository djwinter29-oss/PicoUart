import { DEVICE_FILTER, PicoUartWebHid } from "./device.mjs";
import { renderDashboard } from "./view.mjs";

const connectButton = document.querySelector("#connect");
const refreshButton = document.querySelector("#refresh");
const disconnectButton = document.querySelector("#disconnect");
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
  connectButton.disabled = state.phase !== "disconnected";
  refreshButton.disabled = !connected || busy;
  disconnectButton.disabled = !connected || state.phase === "disconnecting";
  toggleLedButton.disabled = !connected || busy || state.dashboard.led?.output_on === null;
  document.querySelector("#usb-identity").textContent = connected
    ? `${DEVICE_FILTER.vendorId.toString(16).toUpperCase()}:${DEVICE_FILTER.productId.toString(16).toUpperCase()}`
    : "--";
  if (!connected || state.error) {
    statusMessage.textContent = state.error ?? state.message;
    statusMessage.parentElement.dataset.error = String(Boolean(state.error) || state.phase === "unsupported");
  }
}

const hid = globalThis.isSecureContext ? navigator.hid : undefined;
const client = new PicoUartWebHid(hid, render);
render(client.state);
connectButton.addEventListener("click", () => client.connect());
refreshButton.addEventListener("click", () => client.refresh());
disconnectButton.addEventListener("click", () => client.disconnect());
toggleLedButton.addEventListener("click", () => client.toggleLed());
globalThis.setInterval(() => client.refresh(), 2000);