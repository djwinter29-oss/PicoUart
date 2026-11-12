// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import {
  fetchJson, STATUS_REQUEST_TIMEOUT_MS, BOARD_SCAN_TIMEOUT_MS, CONTROL_REQUEST_TIMEOUT_MS,
} from "./api.mjs?v=1";
import { channelHistory } from "./traffic.mjs?v=1";
import {
  formatBytes, uptimeLabel, formatPin, boardIdentityName, deviceUid, resetCauseName, renderChannels,
} from "./view.mjs?v=1";
import { createDiagnostics } from "./diagnostics.mjs?v=1";
import {
  createControlConfirmation, matchesControlConfirmation, resolveStatusMessage,
} from "./control-confirmation.mjs?v=1";

const token = document.querySelector('meta[name="csrf-token"]').content;
const connection = document.querySelector("#connection");
const channelRows = document.querySelector("#channel-rows");
const diagnosticsDialog = document.querySelector("#diagnostics-dialog");
const diagnosticsContent = document.querySelector("#diagnostics-content");
const statusMessage = document.querySelector("#status-message");
const clockProfile = document.querySelector("#clock-profile");
const saveConfigurationButton = document.querySelector("#save-configuration");
const configurationDialog = document.querySelector("#configuration-dialog");
const rebootDialog = document.querySelector("#reboot-dialog");
const configurationDialogProfile = document.querySelector("#configuration-dialog-profile");
const confirmConfigurationButton = document.querySelector("#confirm-configuration");
const overclockAcknowledgement = document.querySelector("#overclock-ack");
const overclockAcknowledgementLabel = document.querySelector("#overclock-acknowledgement");
const configurationRecovery = document.querySelector("#configuration-recovery");
const configurationRecoveryMessage = document.querySelector("#configuration-recovery-message");
const acknowledgeConfigurationOutcomeButton = document.querySelector("#acknowledge-configuration-outcome");
const actionButtons = [...document.querySelectorAll(".actions button")];
const boardSelect = document.querySelector("#board-select");
const rescanButton = document.querySelector("#rescan-boards");
let actionBusy = false;
let latestData = null;
let selectedBoard = null;
let selectedLabel = "";
let configurationProfileDirty = false;
let configurationSavePending = false;
const pendingConfigurationProfiles = new Map();
const uncertainConfigurationBoards = new Set();
const acknowledgedConfigurationWarnings = new Map();
const configurationRecoveryStatusFloor = new Map();
const configurationRecoveryReadyBoards = new Set();
let selectionVersion = 0;
let refreshing = null;
let statusRequestId = 0;
let latestAcceptedStatusRequestId = 0;
let configurationPreflightInProgress = false;
let scanning = false;
let scanError = null;
let deviceGeneration = 0;
let rebootConfirmation = null;
let configurationConfirmation = null;
let configurationConfirmationProfile = null;
let refreshIntervalMs = 500;
let refreshTimer = null;
let reportSample = null;
let reportReceivedAt = 0;
const boardActionErrors = new Map();

const { buildDiagnosticsCell, invalidateDiagnostics, updateDiagnosticsAge, refreshDiagnostics } = createDiagnostics({
  diagnosticsDialog,
  diagnosticsContent,
  getContext: () => ({ selectedBoard, selectionVersion, latestData }),
});

function setRefreshInterval(interval) {
  const delay = Number.isFinite(interval) ? Math.max(500, Math.min(1000, interval)) : 500;
  if (refreshTimer !== null && delay === refreshIntervalMs) return;
  if (refreshTimer !== null) window.clearInterval(refreshTimer);
  refreshIntervalMs = delay;
  refreshTimer = window.setInterval(refresh, delay);
}

function updateReportAge(data, now = Date.now()) {
  const sample = Date.parse(data.updated_at);
  const age = document.querySelector("#sample-time");
  if (!data.connected) reportSample = null;
  if (data.connected && Number.isFinite(sample) && sample !== reportSample) {
    reportSample = sample;
    reportReceivedAt = now;
  }
  const seconds = Math.max(0, Math.floor((now - reportReceivedAt) / 1000));
  const stale = Number.isFinite(sample) && now - reportReceivedAt > 3000;
  age.hidden = !data.connected || (Number.isFinite(sample) && !stale);
  age.textContent = Number.isFinite(sample)
    ? `Telemetry stale · ${seconds} s ago` : "Waiting for telemetry";
  age.dataset.stale = String(stale);
}

function renderStaticConfiguration(data) {
  const identity = data.identity;
  const configuration = data.configuration;
  const profileMask = identity?.clock_profile_mask ?? 0;
  for (const option of clockProfile.options) {
    option.disabled = !(profileMask & (1 << Number(option.value)));
  }
  const pendingProfile = pendingConfigurationProfiles.get(selectedBoard);
  const storedProfile = configuration?.stored_profile ?? 0;
  if (!actionBusy && configurationSavePending && Number.isInteger(pendingProfile) && configuration
    && (configuration.stored_profile === null || Number.isInteger(configuration.stored_profile))
    && storedProfile === pendingProfile) {
    configurationSavePending = false;
    configurationProfileDirty = false;
    pendingConfigurationProfiles.delete(selectedBoard);
    uncertainConfigurationBoards.delete(selectedBoard);
    const warning = configurationSaveWarning(data);
    if (warning) acknowledgedConfigurationWarnings.set(selectedBoard, warning);
    else acknowledgedConfigurationWarnings.delete(selectedBoard);
    configurationRecoveryStatusFloor.delete(selectedBoard);
    configurationRecoveryReadyBoards.delete(selectedBoard);
    if (boardActionErrors.get(selectedBoard)?.action === "configuration") {
      boardActionErrors.delete(selectedBoard);
    }
    overclockAcknowledgement.checked = false;
  } else if (configurationProfileDirty && !configurationSavePending
    && configuration?.stored_profile === Number(clockProfile.value)) {
    configurationProfileDirty = false;
    overclockAcknowledgement.checked = false;
  }
  if (!configurationProfileDirty && configuration
    && (configuration.stored_profile === null || Number.isInteger(configuration.stored_profile))) {
    clockProfile.value = String(configuration.stored_profile ?? 0);
  }
  document.querySelector("#reset-cause").textContent = resetCauseName(configuration?.reset_cause);
  document.querySelector("#uart-tx-capacity").textContent = formatBytes(configuration?.uart_tx_ring_capacity_bytes);
  document.querySelector("#uart-rx-capacity").textContent = formatBytes(configuration?.uart_rx_ring_capacity_bytes);
  document.querySelector("#cdc-tx-capacity").textContent = formatBytes(configuration?.cdc_tx_fifo_capacity_bytes);
  document.querySelector("#cdc-rx-capacity").textContent = formatBytes(configuration?.cdc_rx_fifo_capacity_bytes);

  const total = identity?.ram_total_bytes;
  const reserved = identity?.ram_reserved_bytes;
  document.querySelector("#ram-usage").textContent = Number.isSafeInteger(total) && total > 0
    && Number.isSafeInteger(reserved) && reserved >= 0 && reserved <= total
    ? `${(reserved / 1024).toFixed(1)} / ${(total / 1024).toFixed(0)} KiB`
      + ` (${(reserved / total * 100).toFixed(1)}%)`
    : "--";

  const pinMap = document.querySelector("#pin-map");
  pinMap.replaceChildren();
  const pins = configuration?.channel_pins;
  if (!Array.isArray(pins)) {
    pinMap.textContent = "Static pin assignments unavailable";
    return;
  }
  const table = document.createElement("table");
  table.className = "pin-table";
  const head = document.createElement("tr");
  for (const label of ["Channel", "TX", "RX", "RTS", "CTS"]) {
    const cell = document.createElement("th");
    cell.scope = "col";
    cell.textContent = label;
    head.append(cell);
  }
  const thead = document.createElement("thead");
  thead.append(head);
  const body = document.createElement("tbody");
  pins.forEach((channel, index) => {
    const row = document.createElement("tr");
    for (const value of [
      `CDC${index}`,
      formatPin(channel.tx_pin),
      formatPin(channel.rx_pin),
      formatPin(channel.rts_pin),
      formatPin(channel.cts_pin),
    ]) {
      const cell = document.createElement("td");
      cell.textContent = value;
      row.append(cell);
    }
    body.append(row);
  });
  table.append(thead, body);
  pinMap.append(table);
}

function configurationSaveWarning(data) {
  const error = data?.configuration_save_error;
  if (data?.configuration_save_unconfirmed !== true) return null;
  return typeof error === "string" ? error : "Configuration save outcome is unconfirmed";
}

function renderConfigurationRecovery(data) {
  const configuration = data?.configuration;
  const canAcknowledge = uncertainConfigurationBoards.has(selectedBoard)
    && configurationRecoveryReadyBoards.has(selectedBoard)
    && !actionBusy
    && data?.connected && configuration
    && (configuration.stored_profile === null || Number.isInteger(configuration.stored_profile))
    && !data.configuration_error && !data.error;
  configurationRecovery.hidden = !canAcknowledge;
  if (!canAcknowledge) return;
  const storedProfile = configuration.stored_profile ?? 0;
  const storedLabel = [...clockProfile.options].find(option => Number(option.value) === storedProfile)?.textContent
    ?? `Unknown profile (${storedProfile})`;
  configurationRecoveryMessage.textContent = `The save outcome is unconfirmed. Current stored profile: ${storedLabel}.`;
}

function updateConfigurationControls(data) {
  const identity = data?.identity;
  const capabilities = identity?.capabilities ?? 0;
  const profile = Number(clockProfile.value);
  clockProfile.disabled = actionBusy || configurationSavePending || !data?.connected
    || !(capabilities & 4) || !identity?.clock_profile_mask;
  saveConfigurationButton.hidden = !(capabilities & 4);
  saveConfigurationButton.disabled = actionBusy || configurationSavePending
    || !configurationProfileCanBeSaved(data, profile);
}

function configurationProfileCanBeSaved(data, profile) {
  const identity = data?.identity;
  const capabilities = identity?.capabilities ?? 0;
  const storedProfile = data?.configuration?.stored_profile;
  const controlReadFailed = data?.configuration_save == null && Boolean(data?.configuration_save_error)
    && !configurationSaveWarning(data);
  const available = Boolean(data?.connected && (capabilities & 4) && data?.configuration
    && !controlReadFailed && (storedProfile === null || Number.isInteger(storedProfile)));
  const profileSupported = Number.isInteger(profile)
    && Boolean((identity?.clock_profile_mask ?? 0) & (1 << profile));
  return available && profile !== (storedProfile ?? 0) && profileSupported;
}

function closeControlConfirmations() {
  if (configurationDialog.open) configurationDialog.close();
  if (rebootDialog.open) rebootDialog.close();
  rebootConfirmation = null;
  configurationConfirmation = null;
  configurationConfirmationProfile = null;
}

function invalidateControlConfirmations() {
  deviceGeneration++;
  closeControlConfirmations();
}

function rejectControlConfirmation(message) {
  invalidateControlConfirmations();
  statusMessage.textContent = message;
  statusMessage.parentElement.dataset.error = "true";
}

function currentControlConfirmation(action) {
  return createControlConfirmation(
    action, selectedBoard, deviceUid(latestData?.identity), deviceGeneration, latestData?.connected === true,
  );
}

function controlConfirmationIsCurrent(binding, action) {
  return matchesControlConfirmation(
    binding, action, selectedBoard, deviceUid(latestData?.identity), deviceGeneration, latestData?.connected === true,
  );
}

async function revalidateControlConfirmation(binding, action) {
  if (!controlConfirmationIsCurrent(binding, action)) {
    rejectControlConfirmation("Confirmation canceled: the selected board or device connection changed.");
    return false;
  }
  try {
    const query = `?board=${encodeURIComponent(binding.boardId)}`;
    const requestId = ++statusRequestId;
    const { response, data } = await fetchJson(
      `/api/status${query}`, { cache: "no-store" }, STATUS_REQUEST_TIMEOUT_MS);
    if (selectedBoard !== binding.boardId || deviceGeneration !== binding.generation) {
      rejectControlConfirmation("Confirmation canceled: the selected board or device connection changed.");
      return false;
    }
    let status = data;
    if (response.ok) {
      if (!acceptStatus(data, requestId)) status = latestData;
    } else if (requestId <= latestAcceptedStatusRequestId) {
      status = latestData;
    } else {
      throw new Error(data.error || `Status request failed (${response.status})`);
    }
    if (!status) throw new Error("Status is unavailable");
    const physicalUid = deviceUid(status.identity);
    if (!status.connected) {
      rejectControlConfirmation("Confirmation canceled: the selected board is disconnected.");
      return false;
    }
    if (physicalUid === "--") {
      rejectControlConfirmation("Cannot confirm action: the selected board's physical device UID is unavailable.");
      return false;
    }
    if (physicalUid !== binding.physicalUid) {
      rejectControlConfirmation("Confirmation canceled: the selected board's physical device UID changed.");
      return false;
    }
    const saveWarning = configurationSaveWarning(status);
    if (action === "configuration" && saveWarning
      && acknowledgedConfigurationWarnings.get(binding.boardId) !== saveWarning) {
      rejectControlConfirmation("Inspect the unconfirmed configuration save outcome before retrying.");
      return false;
    }
    if (!controlConfirmationIsCurrent(binding, action)) {
      rejectControlConfirmation("Confirmation canceled: the selected board or device connection changed.");
      return false;
    }
    return true;
  } catch (error) {
    rejectControlConfirmation(`Cannot verify the selected board before control: ${error.message}`);
    return false;
  }
}

function clearSelectedBoardStateForDeviceChange(data) {
  configurationProfileDirty = false;
  configurationSavePending = false;
  pendingConfigurationProfiles.delete(selectedBoard);
  uncertainConfigurationBoards.delete(selectedBoard);
  acknowledgedConfigurationWarnings.delete(selectedBoard);
  configurationRecoveryStatusFloor.delete(selectedBoard);
  configurationRecoveryReadyBoards.delete(selectedBoard);
  boardActionErrors.delete(selectedBoard);
  overclockAcknowledgement.checked = false;

  const uid = deviceUid(data.identity);
  const product = data.identity?.product_name;
  if (typeof product === "string" && uid !== "--") {
    selectedLabel = `${product} / ${uid}`;
    const option = [...boardSelect.options].find(item => item.value === selectedBoard);
    if (option) option.textContent = selectedLabel;
  }
}

function acceptStatus(data, requestId) {
  if (requestId <= latestAcceptedStatusRequestId) return false;
  latestAcceptedStatusRequestId = requestId;

  const previousUid = deviceUid(latestData?.identity);
  const currentUid = deviceUid(data.identity);
  const deviceChanged = data.connected && latestData?.connected && previousUid !== currentUid;
  if (deviceChanged) clearSelectedBoardStateForDeviceChange(data);

  const saveWarning = configurationSaveWarning(data);
  if (!saveWarning) {
    acknowledgedConfigurationWarnings.delete(selectedBoard);
  } else if (acknowledgedConfigurationWarnings.has(selectedBoard)) {
    const acknowledgedWarning = acknowledgedConfigurationWarnings.get(selectedBoard);
    if (acknowledgedWarning === null) {
      acknowledgedConfigurationWarnings.set(selectedBoard, saveWarning);
    } else if (acknowledgedWarning !== saveWarning) {
      acknowledgedConfigurationWarnings.delete(selectedBoard);
    }
  }
  if ((!actionBusy || configurationPreflightInProgress)
    && saveWarning && !acknowledgedConfigurationWarnings.has(selectedBoard)) {
    if (!uncertainConfigurationBoards.has(selectedBoard)) {
      uncertainConfigurationBoards.add(selectedBoard);
      pendingConfigurationProfiles.set(selectedBoard, null);
      configurationRecoveryStatusFloor.set(selectedBoard, requestId - 1);
    }
    if (selectedBoard !== null) configurationSavePending = true;
  }
  if (uncertainConfigurationBoards.has(selectedBoard)
    && requestId > (configurationRecoveryStatusFloor.get(selectedBoard) ?? Number.POSITIVE_INFINITY)) {
    configurationRecoveryReadyBoards.add(selectedBoard);
  }

  setRefreshInterval(data.connected ? data.refresh_interval_ms : 500);
  render(data);
  return true;
}

function render(data) {
  const reconnected = data.connected && !latestData?.connected;
  const previousUptime = latestData?.runtime?.uptime_ms;
  const uptime = data.runtime?.uptime_ms;
  const rebooted = data.connected && latestData?.connected
    && Number.isFinite(previousUptime) && Number.isFinite(uptime) && uptime < previousUptime;
  const previousUid = deviceUid(latestData?.identity);
  const currentUid = deviceUid(data.identity);
  const deviceChanged = data.connected && latestData?.connected
    && previousUid !== currentUid;
  const disconnected = !data.connected && latestData?.connected;
  const diagnosticsInvalidated = reconnected || disconnected || rebooted || deviceChanged;
  if (diagnosticsInvalidated) {
    invalidateControlConfirmations();
    channelHistory.clear();
    invalidateDiagnostics();
  }
  latestData = data;
  connection.dataset.state = data.connected ? "connected" : "disconnected";
  document.querySelector("#connection-label").textContent = data.connected ? "Connected" : "Disconnected";
  document.querySelector("#board-model").textContent = boardIdentityName(
    data.identity?.board_model_id ?? data.identity?.board_model,
    data.identity?.mcu_id ?? data.identity?.mcu,
  );
  const systemClock = data.telemetry?.system_clock_hz ?? data.hardware?.system_clock_hz;
  document.querySelector("#system-clock").textContent = Number.isFinite(systemClock)
    ? `${(systemClock / 1000000).toLocaleString(undefined, { maximumFractionDigits: 3 })} MHz` : "--";
  document.querySelector("#firmware").textContent = data.identity?.firmware_version ?? "--";
  document.querySelector("#product-name").textContent = data.identity?.product_name ?? "--";
  document.querySelector("#serial").textContent = deviceUid(data.identity);
  renderStaticConfiguration(data);
  updateConfigurationControls(data);
  renderConfigurationRecovery(data);
  const runtime = data.runtime;
  document.querySelector("#uptime").textContent = Number.isFinite(runtime?.uptime_ms)
    ? uptimeLabel(runtime.uptime_ms) : "--";
  const serviceLoad = runtime?.service_load_basis_points;
  document.querySelector("#service-load").textContent = Array.isArray(serviceLoad)
    ? serviceLoad.map(value => Number.isInteger(value) && value >= 0 && value <= 10000
      ? `${(value / 100).toFixed(1)}%` : "--").join(" / ")
    : "--";
  const temperature = data.telemetry?.temperature_centidegrees_celsius;
  document.querySelector("#temperature").textContent = Number.isInteger(temperature)
    ? `${(temperature / 100).toFixed(1)} °C`
    : Number.isFinite(data.board?.temperature_celsius) ? `${data.board.temperature_celsius.toFixed(1)} °C` : "--";
  const led = data.connected ? data.led : null;
  const capabilities = data.identity?.capabilities ?? 0;
  const ledButton = document.querySelector("#toggle-led");
  const ledOutputOn = data.telemetry?.led_flags == null ? led?.output_on
    : Boolean(data.telemetry.led_flags & 1);
  ledButton.textContent = `Board LED: ${ledOutputOn == null ? "--" : ledOutputOn ? "On" : "Off"}`;
  ledButton.dataset.state = ledOutputOn == null ? "unknown" : ledOutputOn ? "on" : "off";
  ledButton.title = ledOutputOn == null ? "Board identification LED state unavailable"
    : `Set board identification LED ${ledOutputOn ? "Off" : "On"}`;
  ledButton.hidden = !(capabilities & 1);
  ledButton.disabled = actionBusy || !data.connected || !(capabilities & 1) || ledOutputOn == null;
  const resetButton = document.querySelector("#reset-board");
  resetButton.hidden = !(capabilities & 2);
  resetButton.disabled = actionBusy || !data.connected;
  updateReportAge(data);
  renderChannels(data, channelRows, buildDiagnosticsCell);

  const saveWarning = configurationSaveWarning(data);
  const configurationSaveError = saveWarning
    && acknowledgedConfigurationWarnings.get(selectedBoard) === saveWarning
    ? null : data.configuration_save_error;
  const message = resolveStatusMessage({
    boardActionError: boardActionErrors.get(selectedBoard)?.message,
    scanError,
    dataError: data.error,
    identityError: data.identity_error,
    configurationError: data.configuration_error,
    configurationSaveError,
    runtimeError: data.runtime_error,
    ledError: data.led_error,
    connected: data.connected,
  });
  statusMessage.textContent = message;
  statusMessage.parentElement.dataset.error = Boolean(scanError || data.error || data.identity_error
    || boardActionErrors.has(selectedBoard) || data.configuration_error || configurationSaveError
    || data.runtime_error || data.led_error || !data.connected);
  if (diagnosticsInvalidated && diagnosticsDialog.open) {
    refreshDiagnostics(data.connected);
  }
}

async function refresh() {
  if (refreshing === selectionVersion) return;
  const version = selectionVersion;
  const requestId = ++statusRequestId;
  refreshing = version;
  try {
    const query = selectedBoard ? `?board=${encodeURIComponent(selectedBoard)}` : "";
    const { response, data } = await fetchJson(
      `/api/status${query}`, { cache: "no-store" }, STATUS_REQUEST_TIMEOUT_MS);
    if (version !== selectionVersion) return;
    if (!response.ok) throw new Error(data.error || `Status request failed (${response.status})`);
    acceptStatus(data, requestId);
  } catch (error) {
    if (version !== selectionVersion) return;
    if (requestId < latestAcceptedStatusRequestId) return;
    setRefreshInterval(500);
    if (latestData) render({ ...latestData, error: error.message });
    statusMessage.textContent = error.message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    if (refreshing === version) refreshing = null;
  }
}

function selectBoard(id, label) {
  if (selectedBoard === id) return;
  invalidateControlConfirmations();
  configurationProfileDirty = false;
  configurationSavePending = id !== null && pendingConfigurationProfiles.has(id);
  overclockAcknowledgement.checked = false;
  selectedBoard = id;
  setRefreshInterval(500);
  selectedLabel = label;
  selectionVersion++;
  channelHistory.clear();
  reportSample = null;
  invalidateDiagnostics();
  diagnosticsDialog.close();
  render({
    connected: false,
    channels: Array.from({ length: 6 }, (_, id) => ({ id, state: "unknown" })),
  });
}

async function rescanBoards() {
  if (scanning) return;
  scanning = true;
  rescanButton.disabled = true;
  try {
    const { response, data: result } = await fetchJson(
      "/api/boards", { cache: "no-store" }, BOARD_SCAN_TIMEOUT_MS);
    if (!response.ok) throw new Error(result.error || `Board scan failed (${response.status})`);
    const discoveryErrors = result.discovery_errors || [];
    scanError = discoveryErrors.length
      ? `Some boards could not be inspected: ${discoveryErrors
        .map(item => `${item.device_path}: ${item.error}`).join("; ")}`
      : null;
    boardSelect.replaceChildren();
    for (const board of result.boards) {
      const option = document.createElement("option");
      option.value = board.id;
      option.textContent = `${board.product_name} / ${board.serial_number || board.device_path}`;
      boardSelect.append(option);
    }
    if (selectedBoard && !result.boards.some(board => board.id === selectedBoard)) {
      invalidateControlConfirmations();
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

async function runAction(action, confirmation = null) {
  if (!selectedBoard || actionBusy) return;
  if (action === "reset" && !controlConfirmationIsCurrent(confirmation, action)) {
    rejectControlConfirmation("Confirmation canceled: the selected board or device connection changed.");
    return;
  }
  const board = confirmation?.boardId ?? selectedBoard;
  if (action === "reset") invalidateControlConfirmations();
  let responseStatus = null;
  let errorResult = null;
  actionBusy = true;
  boardSelect.disabled = true;
  actionButtons.forEach(button => { button.disabled = true; });
  try {
    const { response, data: result } = await fetchJson(
      `/api/actions/${action}?board=${encodeURIComponent(board)}`,
      { method: "POST", headers: { "X-CSRF-Token": token } },
      CONTROL_REQUEST_TIMEOUT_MS,
    );
    if (!response.ok) {
      responseStatus = response.status;
      errorResult = result;
      throw new Error(result.error || `Action failed (${response.status})`);
    }
    if (boardActionErrors.get(board)?.action === action) boardActionErrors.delete(board);
    statusMessage.textContent = action === "reset"
      ? "Reboot command sent; USB will reconnect shortly." : "LED toggle command sent";
    statusMessage.parentElement.dataset.error = "false";
    await refresh();
  } catch (error) {
    const uncertain = typeof errorResult?.outcome_unconfirmed === "boolean"
      ? errorResult.outcome_unconfirmed
      : responseStatus === null || responseStatus >= 500;
    const message = uncertain
      ? `Board action outcome is unconfirmed; reconnect and inspect before retrying. ${error.message}`
      : error.message;
    boardActionErrors.set(board, { action, message });
    statusMessage.textContent = message;
    statusMessage.parentElement.dataset.error = "true";
  } finally {
    actionBusy = false;
    boardSelect.disabled = !boardSelect.value;
    rescanButton.disabled = scanning;
    const ledOutput = latestData?.telemetry?.led_flags == null
      ? latestData?.led?.output_on : Boolean(latestData.telemetry.led_flags & 1);
    document.querySelector("#toggle-led").disabled = !latestData?.connected
      || !(latestData.identity?.capabilities & 1) || ledOutput == null;
    document.querySelector("#reset-board").disabled = !latestData?.connected;
    updateConfigurationControls(latestData);
  }
}

function openConfigurationConfirmation() {
  if (saveConfigurationButton.disabled) return;
  const binding = currentControlConfirmation("configuration");
  if (!binding) {
    rejectControlConfirmation(
      "Cannot confirm action: the selected board is disconnected or its physical device UID is unavailable.",
    );
    return;
  }
  const profile = Number(clockProfile.value);
  const requiresAcknowledgement = profile !== 0;
  const profileLabel = clockProfile.selectedOptions[0]?.textContent ?? "selected clock profile";
  configurationDialogProfile.textContent = `Selected profile: ${profileLabel}.`;
  overclockAcknowledgement.checked = false;
  overclockAcknowledgement.disabled = !requiresAcknowledgement;
  overclockAcknowledgementLabel.hidden = !requiresAcknowledgement;
  confirmConfigurationButton.disabled = requiresAcknowledgement;
  configurationConfirmation = binding;
  configurationConfirmationProfile = profile;
  configurationDialog.showModal();
}

async function saveConfiguration() {
  const binding = configurationConfirmation;
  const profile = configurationConfirmationProfile;
  const unsafeOverclock = profile !== 0;
  if (!configurationDialog.open || !selectedBoard || actionBusy || !Number.isInteger(profile)
    || (unsafeOverclock && !overclockAcknowledgement.checked)) return;
  let responseStatus = null;
  let errorResult = null;
  actionBusy = true;
  boardSelect.disabled = true;
  actionButtons.forEach(button => { button.disabled = true; });
  updateConfigurationControls(latestData);
  try {
    configurationPreflightInProgress = true;
    const preflightValid = await revalidateControlConfirmation(binding, "configuration");
    configurationPreflightInProgress = false;
    if (!preflightValid) return;
    if (!configurationDialog.open || configurationConfirmation !== binding) return;
    if (profile !== Number(clockProfile.value) || !configurationProfileCanBeSaved(latestData, profile)
      || (unsafeOverclock && !overclockAcknowledgement.checked)) {
      rejectControlConfirmation("Configuration changed; review and reopen the save confirmation.");
      return;
    }
    const profileLabel = clockProfile.selectedOptions[0]?.textContent ?? "selected clock profile";
    configurationDialog.close();
    configurationConfirmation = null;
    configurationConfirmationProfile = null;
    invalidateControlConfirmations();

    const board = binding.boardId;
    configurationSavePending = true;
    pendingConfigurationProfiles.set(board, profile);
    statusMessage.textContent = `Saving ${profileLabel} and waiting for board confirmation...`;
    statusMessage.parentElement.dataset.error = "false";
    try {
      const { response, data: result } = await fetchJson(
        `/api/configuration?board=${encodeURIComponent(board)}`,
        {
          method: "POST",
          headers: { "X-CSRF-Token": token, "Content-Type": "application/json" },
          body: JSON.stringify({ profile, unsafe_overclock: unsafeOverclock }),
        },
        CONTROL_REQUEST_TIMEOUT_MS,
      );
      if (!response.ok) {
        responseStatus = response.status;
        errorResult = result;
        throw new Error(result.error || `Configuration save failed (${response.status})`);
      }
      configurationProfileDirty = true;
      configurationSavePending = true;
      uncertainConfigurationBoards.delete(board);
      acknowledgedConfigurationWarnings.delete(board);
      configurationRecoveryStatusFloor.delete(board);
      configurationRecoveryReadyBoards.delete(board);
      if (boardActionErrors.get(board)?.action === "configuration") boardActionErrors.delete(board);
      statusMessage.textContent = `Saved ${profileLabel}; board is rebooting.`;
      statusMessage.parentElement.dataset.error = "false";
    } catch (error) {
      const uncertain = typeof errorResult?.outcome_unconfirmed === "boolean"
        ? errorResult.outcome_unconfirmed
        : responseStatus === null || responseStatus >= 500;
      const message = uncertain
        ? `Configuration save outcome is unconfirmed; reconnect and inspect before retrying. ${error.message}`
        : error.message;
      configurationSavePending = uncertain;
      if (uncertain) uncertainConfigurationBoards.add(board);
      else pendingConfigurationProfiles.delete(board);
      if (uncertain) {
        acknowledgedConfigurationWarnings.delete(board);
        configurationRecoveryStatusFloor.set(board, statusRequestId);
        configurationRecoveryReadyBoards.delete(board);
      } else {
        configurationRecoveryStatusFloor.delete(board);
        configurationRecoveryReadyBoards.delete(board);
        uncertainConfigurationBoards.delete(board);
      }
      boardActionErrors.set(board, { action: "configuration", message });
      statusMessage.textContent = message;
      statusMessage.parentElement.dataset.error = "true";
    }
  } finally {
    configurationPreflightInProgress = false;
    actionBusy = false;
    boardSelect.disabled = !boardSelect.value;
    rescanButton.disabled = scanning;
    const ledOutput = latestData?.telemetry?.led_flags == null
      ? latestData?.led?.output_on : Boolean(latestData.telemetry.led_flags & 1);
    document.querySelector("#toggle-led").disabled = !latestData?.connected
      || !(latestData.identity?.capabilities & 1) || ledOutput == null;
    document.querySelector("#reset-board").disabled = !latestData?.connected;
    updateConfigurationControls(latestData);
    renderConfigurationRecovery(latestData);
  }
}

function acknowledgeConfigurationOutcome() {
  const board = selectedBoard;
  const configuration = latestData?.configuration;
  if (!board || !configurationSavePending || !uncertainConfigurationBoards.has(board)
    || !configurationRecoveryReadyBoards.has(board)
    || actionBusy
    || !latestData.connected || latestData.configuration_error || !configuration
    || (configuration.stored_profile !== null && !Number.isInteger(configuration.stored_profile))) return;
  clockProfile.value = String(configuration.stored_profile ?? 0);
  configurationProfileDirty = false;
  configurationSavePending = false;
  pendingConfigurationProfiles.delete(board);
  uncertainConfigurationBoards.delete(board);
  acknowledgedConfigurationWarnings.set(board, configurationSaveWarning(latestData));
  configurationRecoveryStatusFloor.delete(board);
  configurationRecoveryReadyBoards.delete(board);
  if (boardActionErrors.get(board)?.action === "configuration") boardActionErrors.delete(board);
  overclockAcknowledgement.checked = false;
  updateConfigurationControls(latestData);
  renderConfigurationRecovery(latestData);
  statusMessage.textContent = "Stored profile inspected; choose a profile to retry if needed.";
  statusMessage.parentElement.dataset.error = "false";
}

document.querySelector("#toggle-led").addEventListener("click", () => runAction("toggle-led"));
document.querySelector("#reset-board").addEventListener("click", () => {
  if (actionBusy || !selectedBoard) return;
  const binding = currentControlConfirmation("reset");
  if (!binding) {
    rejectControlConfirmation(
      "Cannot confirm action: the selected board is disconnected or its physical device UID is unavailable.",
    );
    return;
  }
  rebootConfirmation = binding;
  rebootDialog.showModal();
});
document.querySelector("#cancel-reboot").addEventListener("click", () => {
  rebootDialog.close();
  rebootConfirmation = null;
});
document.querySelector("#confirm-reboot").addEventListener("click", async () => {
  const binding = rebootConfirmation;
  if (!rebootDialog.open || actionBusy || !await revalidateControlConfirmation(binding, "reset")) return;
  if (!rebootDialog.open || rebootConfirmation !== binding) return;
  rebootDialog.close();
  rebootConfirmation = null;
  runAction("reset", binding);
});
clockProfile.addEventListener("change", () => {
  configurationProfileDirty = true;
  overclockAcknowledgement.checked = false;
  updateConfigurationControls(latestData);
});
saveConfigurationButton.addEventListener("click", openConfigurationConfirmation);
document.querySelector("#cancel-configuration").addEventListener("click", () => {
  configurationDialog.close();
  configurationConfirmation = null;
  configurationConfirmationProfile = null;
});
confirmConfigurationButton.addEventListener("click", saveConfiguration);
acknowledgeConfigurationOutcomeButton.addEventListener("click", acknowledgeConfigurationOutcome);
overclockAcknowledgement.addEventListener("change", () => {
  confirmConfigurationButton.disabled = !overclockAcknowledgement.checked;
});
boardSelect.addEventListener("change", () => {
  if (actionBusy) return;
  selectBoard(boardSelect.value || null, boardSelect.selectedOptions[0]?.textContent || "");
  refresh();
});
rescanButton.addEventListener("click", rescanBoards);
rescanBoards();
setRefreshInterval(500);
window.setInterval(() => {
  if (latestData) render(latestData);
  updateDiagnosticsAge();
}, 1000);
