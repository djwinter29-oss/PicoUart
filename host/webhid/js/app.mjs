import { PicoUartWebHid } from "./device.mjs?v=5";
import { renderDashboard, updateReportAge } from "./view.mjs?v=6";

const connectButton = document.querySelector("#connect");
const toggleLedButton = document.querySelector("#toggle-led");
const connection = document.querySelector("#connection");
const statusMessage = document.querySelector("#status-message");
const configurationProfile = document.querySelector("#configuration-profile");
const configurationUnsafeAck = document.querySelector("#configuration-unsafe-ack");
const saveConfigurationButton = document.querySelector("#save-configuration");
const configurationMessage = document.querySelector("#configuration-message");
let configurationDraft = null;
let selectedSerial = null;
let previousPhase = null;

function resetConfigurationDraft(state) {
  configurationDraft = null;
  selectedSerial = state.dashboard.identity?.serial_number ?? null;
  configurationUnsafeAck.checked = false;
  client?.clearConfigurationSaveMessage();
}

function renderConfiguration(state) {
  const dashboard = state.dashboard;
  const hardwareId = dashboard.hardware?.mcu_id;
  const options = hardwareId === 1 ? [[0, "SDK default (125 MHz)"], [1, "150 MHz"], [2, "200 MHz"], [3, "250 MHz"]]
    : hardwareId === 2 ? [[0, "SDK default (150 MHz)"], [1, "150 MHz"], [2, "200 MHz"], [3, "250 MHz"], [4, "280 MHz (RP2350)"]] : [];
  const configurationReady = dashboard.configuration || dashboard.configuration_error
    || dashboard.configuration_save || dashboard.configuration_save_error;
  if (configurationDraft === null && options.length && configurationReady) {
    const saved = dashboard.configuration_save?.status === 2 ? dashboard.configuration_save.profile : null;
    const stored = dashboard.configuration?.stored_profile;
    configurationDraft = options.some(([value]) => value === saved) ? saved
      : options.some(([value]) => value === stored) ? stored : 0;
  }
  if (configurationProfile.children.length !== options.length
    || options.some(([value], index) => configurationProfile.children[index]?.value !== String(value))) {
    configurationProfile.replaceChildren();
    for (const [value, label] of options) {
      const option = document.createElement("option");
      option.value = String(value);
      option.textContent = label;
      configurationProfile.append(option);
    }
  }
  configurationProfile.value = String(configurationDraft ?? 0);
  const capabilityAvailable = dashboard.configuration_save != null && !dashboard.configuration_save_error;
  const cdcOpen = !Array.isArray(dashboard.channels) || dashboard.channels.length !== 6
    || dashboard.channels.some(channel => channel.cdc_open || channel.health === null);
  const enabled = state.phase === "connected" && options.length > 0 && capabilityAvailable && !cdcOpen;
  configurationProfile.disabled = !enabled;
  document.querySelector("#configuration-acknowledgement").hidden = !configurationDraft;
  configurationUnsafeAck.disabled = !enabled || configurationDraft === 0;
  saveConfigurationButton.disabled = !enabled
    || (configurationDraft !== 0 && !configurationUnsafeAck.checked);
  configurationMessage.textContent = state.configurationSaveMessage
    || (dashboard.connected ? dashboard.configuration_save_error : "") || "";
  configurationMessage.dataset.error = String(Boolean(!state.configurationSaveMessage
    && dashboard.connected && dashboard.configuration_save_error) || Boolean(state.configurationSaveMessage
    && state.configurationSaveMessage !== "Saving configuration..."
    && state.configurationSaveMessage !== "Saved. Pico is rebooting; USB will reconnect shortly."));
}

function render(state) {
  if (state.phase === "connecting" && previousPhase !== "connecting"
    || state.phase === "disconnected" && previousPhase !== "disconnected") {
    resetConfigurationDraft(state);
  }
  const serial = state.dashboard.identity?.serial_number ?? null;
  if (serial && selectedSerial && serial !== selectedSerial) resetConfigurationDraft(state);
  if (serial) selectedSerial = serial;
  previousPhase = state.phase;
  renderDashboard(state.dashboard);
  renderConfiguration(state);
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
configurationProfile.addEventListener("change", () => {
  configurationDraft = Number(configurationProfile.value);
  configurationUnsafeAck.checked = false;
  client.clearConfigurationSaveMessage();
  renderConfiguration(client.state);
});
configurationUnsafeAck.addEventListener("change", () => {
  client.clearConfigurationSaveMessage();
  renderConfiguration(client.state);
});
saveConfigurationButton.addEventListener("click", () => {
  client.saveConfiguration(Number(configurationProfile.value), configurationUnsafeAck.checked);
});
globalThis.setInterval(() => client.refresh(), 1000);
globalThis.setInterval(() => updateReportAge(client.state.dashboard), 1000);