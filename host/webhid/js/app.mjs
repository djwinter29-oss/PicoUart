import { PicoUartWebHid } from "./device.mjs?v=20";
import { renderDashboard, updateReportAge } from "./view.mjs?v=14";

const connectButton = document.querySelector("#connect");
const toggleLedButton = document.querySelector("#toggle-led");
const rebootButton = document.querySelector("#reboot-board");
const connection = document.querySelector("#connection");
const statusMessage = document.querySelector("#status-message");
const profileSelect = document.querySelector("#configuration-profile");
const unsafeAcknowledgement = document.querySelector("#configuration-unsafe-ack");
const saveButton = document.querySelector("#save-configuration");
const configurationDialog = document.querySelector("#configuration-dialog");
const configurationDialogProfile = document.querySelector("#configuration-dialog-profile");
const acknowledgementLabel = document.querySelector("#configuration-acknowledgement");
const confirmConfigurationButton = document.querySelector("#confirm-configuration");
const configurationMessage = document.querySelector("#configuration-message");
const diagnosticsButton = document.querySelector("#refresh-diagnostics");
document.querySelector("#close-channel-diagnostics").addEventListener("click", () =>
  document.querySelector("#channel-diagnostics-dialog").close());
let selectedProfile = null;
let saveMessage = "";

function resetConfigurationDraft() {
  if (configurationDialog.open) configurationDialog.close();
  selectedProfile = null;
  unsafeAcknowledgement.checked = false;
  saveMessage = "";
}

function renderConfiguration(state) {
  const { dashboard } = state;
  const identity = dashboard.identity;
  const options = identity
    ? [...Array(5).keys()].filter(profile => identity.clock_profile_mask & (1 << profile))
      .map(profile => [profile, ["SDK default", "150 MHz", "200 MHz", "250 MHz", "280 MHz"][profile]])
    : [];
  if (selectedProfile === null) {
    selectedProfile = dashboard.configuration?.stored_profile
      ?? options[0]?.[0] ?? null;
  }
  if (profileSelect.children.length !== options.length
    || options.some(([profile], index) => profileSelect.children[index]?.value !== String(profile))) {
    profileSelect.replaceChildren();
    for (const [profile, label] of options) {
      const option = document.createElement("option");
      option.value = String(profile);
      option.textContent = label;
      profileSelect.append(option);
    }
  }
  profileSelect.value = selectedProfile === null ? "" : String(selectedProfile);
  const channels = Array.isArray(dashboard.channels) ? dashboard.channels : [];
  const channelsReady = channels.length === 6 && channels.every(channel =>
    channel?.channel_state === 0 && channel?.input_updated_at
      && Date.now() - Date.parse(channel.input_updated_at) <= 3000);
  const saveAvailable = Boolean(identity?.capabilities & 4) && dashboard.configuration !== null;
  const enabled = state.phase === "connected" && saveAvailable && channelsReady;
  const profileChanged = saveAvailable && selectedProfile !== dashboard.configuration.stored_profile;
  profileSelect.disabled = !enabled;
  unsafeAcknowledgement.disabled = !enabled || selectedProfile === 0;
  saveButton.hidden = !Boolean(identity?.capabilities & 4);
  saveButton.disabled = !enabled || !profileChanged;
  document.querySelector("#reset-cause").textContent = dashboard.configuration
    ? ["Unknown", "Power-on", "Watchdog reset", "Software-requested reset", "Unassigned"][dashboard.configuration.reset_cause]
    : "--";
  document.querySelector("#board-pins").replaceChildren();
  const pinRows = dashboard.configuration?.pins ?? dashboard.configuration?.channel_pins ?? [];
  pinRows.forEach((pins, index) => {
    const row = document.createElement("tr");
    const cell = document.createElement("td");
    cell.textContent = `CDC${index}`;
    row.append(cell);
    for (const signal of ["tx", "rx", "rts", "cts"]) {
      const pin = document.createElement("td");
      const value = pins[signal] ?? pins[`${signal}_pin`];
      pin.textContent = value == null || value === 255 ? "--" : `GP${value}`;
      row.append(pin);
    }
    document.querySelector("#board-pins").append(row);
  });
  const capacity = dashboard.configuration?.capacities;
  for (const [id, key] of [["uart-tx-capacity", "uart_tx"], ["uart-rx-capacity", "uart_rx"],
    ["cdc-tx-capacity", "cdc_tx"], ["cdc-rx-capacity", "cdc_rx"]]) {
    const value = capacity?.[key];
    document.querySelector(`#${id}`).textContent = Number.isSafeInteger(value) ? `${value.toLocaleString()} B` : "--";
  }
  configurationMessage.textContent = saveMessage || dashboard.configuration_error || "";
  configurationMessage.dataset.error = String(Boolean(!saveMessage && dashboard.configuration_error)
    || Boolean(saveMessage && !saveMessage.includes("rebooting")));
}

function profileName(profile) {
  return ["SDK default", "150 MHz", "200 MHz", "250 MHz", "280 MHz"][profile] ?? "Unknown profile";
}

function openConfigurationDialog() {
  if (saveButton.disabled) return;
  const nonDefault = selectedProfile !== 0;
  configurationDialogProfile.textContent = `Selected profile: ${profileName(selectedProfile)}.`;
  unsafeAcknowledgement.checked = false;
  unsafeAcknowledgement.disabled = !nonDefault;
  acknowledgementLabel.hidden = !nonDefault;
  confirmConfigurationButton.disabled = nonDefault;
  configurationDialog.showModal();
}

async function saveConfiguration() {
  const profile = selectedProfile;
  const nonDefault = profile !== 0;
  if (!configurationDialog.open || saveButton.disabled || nonDefault && !unsafeAcknowledgement.checked) return;
  const description = profileName(profile);
  configurationDialog.close();
  saveMessage = "Saving profile; do not retry if the result is unconfirmed.";
  renderConfiguration(client.state);
  try {
    await client.saveConfiguration(profile, unsafeAcknowledgement.checked, true);
    saveMessage = `Saved ${description}; Pico is rebooting. Reconnect manually to verify its boot configuration.`;
    renderConfiguration(client.state);
  } catch (error) {
    saveMessage = error.message;
    renderConfiguration(client.state);
  }
}

function render(state) {
  const dashboard = state.dashboard;
  if (state.phase === "connecting" || state.phase === "disconnected") resetConfigurationDraft();
  renderDashboard(dashboard);
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
  const led = dashboard.telemetry?.led.output_on;
  toggleLedButton.disabled = !connected || busy || led == null || !(dashboard.identity?.capabilities & 1);
  rebootButton.hidden = !(dashboard.identity?.capabilities & 2);
  rebootButton.disabled = !connected || busy;
  diagnosticsButton.disabled = !connected || busy || !dashboard.identity;
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
toggleLedButton.addEventListener("click", async () => {
  const on = client.state.dashboard.telemetry?.led.output_on;
  if (on == null) return;
  try {
    await client.setLed(!on);
  } catch (error) {
    if (client.state.phase !== "disconnected") client.update({ error: error.message });
  }
});
rebootButton.addEventListener("click", async () => {
  if (!globalThis.confirm("Reboot this PicoUart board now?")) return;
  try {
    await client.reboot(true);
  } catch (error) {
    if (client.state.phase !== "disconnected") client.update({ error: error.message });
  }
});
profileSelect.addEventListener("change", () => {
  selectedProfile = Number(profileSelect.value);
  unsafeAcknowledgement.checked = false;
  saveMessage = "";
  renderConfiguration(client.state);
});
saveButton.addEventListener("click", openConfigurationDialog);
document.querySelector("#cancel-configuration").addEventListener("click", () => configurationDialog.close());
unsafeAcknowledgement.addEventListener("change", () => {
  confirmConfigurationButton.disabled = !unsafeAcknowledgement.checked;
});
confirmConfigurationButton.addEventListener("click", saveConfiguration);
diagnosticsButton.addEventListener("click", async () => {
  try {
    await client.refreshDiagnostics();
  } catch (error) {
    if (client.state.phase !== "disconnected") client.update({ error: error.message });
  }
});
globalThis.setInterval(() => client.refresh(), 1000);
globalThis.setInterval(() => updateReportAge(client.state.dashboard), 1000);
