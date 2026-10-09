import { DEVICE_FILTER, PicoUartWebHid } from "./device.mjs";
import { renderDashboard } from "./view.mjs";

const connectButton = document.querySelector("#connect");
const refreshButton = document.querySelector("#refresh");
const disconnectButton = document.querySelector("#disconnect");
const connection = document.querySelector("#connection");
const statusMessage = document.querySelector("#status-message");

function render(state) {
  renderDashboard(state.dashboard);
  const connected = ["connected", "reading"].includes(state.phase);
  const busy = ["connecting", "reading", "disconnecting"].includes(state.phase);
  connection.dataset.state = connected ? "connected" : "disconnected";
  document.querySelector("#connection-label").textContent = state.phase === "unsupported" ? "Unavailable"
    : connected ? "Connected" : state.phase === "connecting" ? "Connecting" : "Disconnected";
  connectButton.disabled = state.phase !== "disconnected";
  refreshButton.disabled = !connected || busy;
  disconnectButton.disabled = !connected || state.phase === "disconnecting";
  document.querySelector("#mcu-id").textContent = state.hardware?.mcu_id ?? "--";
  document.querySelector("#device-name").textContent = state.deviceName ?? "--";
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