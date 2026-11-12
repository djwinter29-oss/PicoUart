// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import { PicoUartWebHid } from "./device.mjs?v=25";
import { renderDashboard, updateChannelFreshness, updateDiagnosticsAge, updateReportAge } from "./view.mjs?v=25";

const connectButton = document.querySelector("#connect");
const toggleLedButton = document.querySelector("#toggle-led");
const rebootButton = document.querySelector("#reboot-board");
const connection = document.querySelector("#connection");
const statusMessage = document.querySelector("#status-message");
const profileSelect = document.querySelector("#configuration-profile");
const unsafeAcknowledgement = document.querySelector("#configuration-unsafe-ack");
const saveButton = document.querySelector("#save-configuration");
const configurationDialog = document.querySelector("#configuration-dialog");
const rebootDialog = document.querySelector("#reboot-dialog");
const configurationDialogProfile = document.querySelector("#configuration-dialog-profile");
const acknowledgementLabel = document.querySelector("#configuration-acknowledgement");
const confirmConfigurationButton = document.querySelector("#confirm-configuration");
const configurationMessage = document.querySelector("#configuration-message");
const diagnosticsButton = document.querySelector("#refresh-diagnostics");
document.querySelector("#close-channel-diagnostics").addEventListener("click", () =>
  document.querySelector("#channel-diagnostics-dialog").close());
let selectedProfile = null;
let saveMessage = "";
let operationNotice = "";
let operationNoticeError = false;

function resetConfigurationDraft() {
  if (configurationDialog.open) configurationDialog.close();
  if (rebootDialog.open) rebootDialog.close();
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
  const baselineProfile = dashboard.configuration?.stored_profile ?? options[0]?.[0] ?? null;
  if (selectedProfile === null) {
    selectedProfile = baselineProfile;
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
  const profileChanged = saveAvailable && selectedProfile !== baselineProfile;
  profileSelect.disabled = state.phase !== "connected" || !Boolean(identity?.capabilities & 4) || !options.length;
  unsafeAcknowledgement.disabled = !enabled || selectedProfile === 0;
  saveButton.hidden = !Boolean(identity?.capabilities & 4);
  saveButton.disabled = !enabled || !profileChanged;
  const resetCause = dashboard.configuration?.reset_cause;
  document.querySelector("#reset-cause").textContent = resetCause == null ? "--"
    : ["Unknown (not distinguished)", "Power-on", "Watchdog reset", "Software-requested reset", "HardFault recovery",
      "Power-on or brownout", "Brownout", "RUN-pin reset", "Debugger reset", "User-requested reboot",
      "Configuration-save reboot"][resetCause] ?? `Unknown (${resetCause})`;
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
  const generation = client.generation;
  const description = profileName(profile);
  configurationDialog.close();
  saveMessage = "Saving profile; do not retry if the result is unconfirmed.";
  renderConfiguration(client.state);
  try {
    const result = await client.saveConfiguration(profile, unsafeAcknowledgement.checked, true);
    if (result?.action !== 4 || result.status !== 2) {
      throw new Error("Save did not return confirmed completion; outcome is unconfirmed.");
    }
    if (generation !== client.generation) {
      showOperationNotice(`Previous connection configuration save: Saved ${description}; Pico is rebooting. `
        + "Reconnect manually to verify its boot configuration.");
      return;
    }
    saveMessage = `Saved ${description}; Pico is rebooting. Reconnect manually to verify its boot configuration.`;
    renderConfiguration(client.state);
  } catch (error) {
    if (generation !== client.generation) {
      showOperationError("Configuration save", error, generation);
      return;
    }
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
  if (operationNotice) {
    statusMessage.textContent = operationNotice;
    statusMessage.parentElement.dataset.error = String(operationNoticeError);
  } else if (!connected || state.error) {
    statusMessage.textContent = state.error ?? state.message;
    statusMessage.parentElement.dataset.error = String(Boolean(state.error) || state.phase === "unsupported");
  }
}

const hid = globalThis.isSecureContext ? navigator.hid : undefined;
const client = new PicoUartWebHid(hid, render);
function showOperationError(label, error, generation) {
  showOperationNotice(generation === client.generation
    ? `${label}: ${error.message}` : `Previous connection ${label.toLowerCase()}: ${error.message}`, true);
}
function showOperationNotice(message, isError = false) {
  operationNotice = message;
  operationNoticeError = isError;
  render(client.state);
}
function clearOperationNotice() {
  operationNotice = "";
  operationNoticeError = false;
}

render(client.state);
connectButton.addEventListener("click", () => {
  if (client.state.phase === "disconnected") {
    clearOperationNotice();
    client.connect();
  } else {
    client.disconnect();
  }
});
toggleLedButton.addEventListener("click", async () => {
  const on = client.state.dashboard.telemetry?.led.output_on;
  if (on == null) return;
  clearOperationNotice();
  render(client.state);
  const generation = client.generation;
  try {
    await client.setLed(!on);
  } catch (error) {
    showOperationError("Board action", error, generation);
  }
});
rebootButton.addEventListener("click", () => {
  if (!rebootButton.disabled) rebootDialog.showModal();
});
document.querySelector("#cancel-reboot").addEventListener("click", () => rebootDialog.close());
document.querySelector("#confirm-reboot").addEventListener("click", async () => {
  if (!rebootDialog.open || rebootButton.disabled) return;
  rebootDialog.close();
  clearOperationNotice();
  render(client.state);
  const generation = client.generation;
  try {
    await client.reboot(true);
  } catch (error) {
    showOperationError("Board action", error, generation);
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
  clearOperationNotice();
  render(client.state);
  const generation = client.generation;
  try {
    await client.refreshDiagnostics();
  } catch (error) {
    showOperationError("Diagnostics", error, generation);
  }
});
globalThis.setInterval(() => client.refresh(), 1000);
globalThis.setInterval(() => {
  updateReportAge(client.state.dashboard);
  updateChannelFreshness(client.state.dashboard);
  updateDiagnosticsAge();
}, 1000);
