// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import { fetchJson, DIAGNOSTICS_REQUEST_TIMEOUT_MS } from "./api.mjs?v=1";
import { formatBytes, measurementLabel } from "./view.mjs?v=2";

export function createDiagnostics({ diagnosticsDialog, diagnosticsContent, getContext }) {
  const errorReasons = [
    "None recorded",
    "Unsupported baud setting",
    "Unsupported framing",
    "Backend configuration failed",
    "Control operation timed out",
    "Other or unknown configuration error",
  ];

  let diagnosticBoard = null;
  let diagnosticData = null;
  let diagnosticError = null;
  let diagnosticRequest = null;
  let diagnosticReadAt = null;
  let diagnosticAgeElement = null;
  let diagnosticEpoch = 0;

  document.querySelector("#close-diagnostics").addEventListener("click", () => diagnosticsDialog.close());
  document.querySelector("#refresh-diagnostics").addEventListener("click", async () => {
    const selection = getContext().selectionVersion;
    if (diagnosticRequest?.selection === selection && diagnosticRequest.epoch === diagnosticEpoch) {
      await diagnosticRequest.promise;
      return;
    }
    invalidateDiagnostics();
    renderDiagnosticContent(diagnosticsContent, Number(diagnosticsContent.dataset.channel));
    await loadDiagnostics();
  });

  function diagnosticEntry(id) {
    if (Array.isArray(diagnosticData)) {
      return diagnosticData.find(item => item?.id === id) ?? diagnosticData[id] ?? null;
    }
    if (diagnosticData && typeof diagnosticData === "object") {
      return diagnosticData[id] ?? diagnosticData.channel_diagnostics?.[id] ?? null;
    }
    return null;
  }

  function peakValue(bytes, capacity) {
    const result = formatBytes(bytes);
    return Number.isSafeInteger(bytes) && Number.isSafeInteger(capacity) && capacity > 0 && bytes <= capacity
      ? `${result} (${(bytes / capacity * 100).toFixed(1)}%)` : result;
  }

  function renderDiagnosticContent(container, id) {
    container.replaceChildren();
    diagnosticAgeElement = null;
    if (!diagnosticData && !diagnosticError) {
      container.textContent = "Loading channel diagnostics...";
      return;
    }
    if (diagnosticError) {
      container.textContent = `Diagnostics unavailable: ${diagnosticError}`;
      return;
    }
    diagnosticAgeElement = document.createElement("p");
    diagnosticAgeElement.className = "diagnostics-age";
    container.append(diagnosticAgeElement);
    const diagnostic = diagnosticEntry(id);
    if (!diagnostic) {
      const unavailable = document.createElement("p");
      unavailable.textContent = "Diagnostics unavailable for this channel.";
      container.append(unavailable);
      updateDiagnosticsAge();
      return;
    }
    const config = getContext().latestData?.configuration ?? {};
    const uartMeasurementAvailable = diagnostic.uart_measurement_state === 1
      || diagnostic.uart_measurement_state === 2;
    const noFaultRecorded = diagnostic.last_fault_uptime_s === 0 && diagnostic.rx_overrun_count === 0
      && diagnostic.rx_error_count === 0 && diagnostic.last_control_error_reason === 0;
    const lastFault = noFaultRecorded
      ? uartMeasurementAvailable ? "None recorded" : "Unavailable"
      : `${diagnostic.last_fault_uptime_s?.toLocaleString() ?? "--"} s uptime`;
    const fields = [
      ["UART measurement", measurementLabel(diagnostic.uart_measurement_state)],
      ["UART sample age", diagnostic.uart_measurement_state === 0 ? "Unavailable"
        : diagnostic.uart_sample_age_100ms === 255 ? "At least 25.5 s"
          : `${(diagnostic.uart_sample_age_100ms / 10).toFixed(1)} s`],
      ["Known RX bytes dropped", uartMeasurementAvailable ? formatBytes(diagnostic.rx_overrun_count) : "Unavailable"],
      ["RX controller error observations",
        uartMeasurementAvailable ? diagnostic.rx_error_count?.toLocaleString() ?? "--" : "Unavailable"],
      ["Last configuration error", errorReasons[diagnostic.last_control_error_reason]
        ?? `Unknown (${diagnostic.last_control_error_reason ?? "--"})`],
      ["Last fault observed", lastFault],
      ["UART TX ring observed peak since boot", uartMeasurementAvailable
        ? peakValue(diagnostic.uart_tx_ring_peak_bytes, config.uart_tx_ring_capacity_bytes) : "Unavailable"],
      ["UART RX ring observed peak since boot", uartMeasurementAvailable
        ? peakValue(diagnostic.uart_rx_ring_peak_bytes, config.uart_rx_ring_capacity_bytes) : "Unavailable"],
      ["CDC TX FIFO observed peak since boot", peakValue(
        diagnostic.cdc_tx_fifo_peak_bytes, config.cdc_tx_fifo_capacity_bytes)],
      ["CDC RX FIFO observed peak since boot", peakValue(
        diagnostic.cdc_rx_fifo_peak_bytes, config.cdc_rx_fifo_capacity_bytes)],
    ];
    const list = document.createElement("dl");
    for (const [label, value] of fields) {
      const term = document.createElement("dt");
      const detail = document.createElement("dd");
      term.textContent = label;
      detail.textContent = value;
      list.append(term, detail);
    }
    container.append(list);
    updateDiagnosticsAge();
  }

  function updateDiagnosticsAge(now = performance.now()) {
    if (!diagnosticAgeElement || !diagnosticsDialog.open || !Number.isFinite(diagnosticReadAt)) return;
    const seconds = Math.max(0, Math.floor((now - diagnosticReadAt) / 1000));
    diagnosticAgeElement.textContent = `Diagnostics read ${seconds} s ago`;
  }

  function invalidateDiagnostics() {
    diagnosticEpoch++;
    diagnosticBoard = null;
    diagnosticData = null;
    diagnosticError = null;
    diagnosticReadAt = null;
  }

  async function loadDiagnostics() {
    const { selectedBoard } = getContext();
    if (!selectedBoard) return;
    const board = selectedBoard;
    const selection = getContext().selectionVersion;
    const epoch = diagnosticEpoch;
    if (diagnosticRequest?.selection === selection && diagnosticRequest.epoch === epoch) {
      return diagnosticRequest.promise;
    }
    const request = { selection, epoch, promise: null };
    request.promise = (async () => {
      diagnosticError = null;
      try {
        const { response, data: result } = await fetchJson(
          `/api/diagnostics?board=${encodeURIComponent(board)}`,
          { cache: "no-store" },
          DIAGNOSTICS_REQUEST_TIMEOUT_MS,
        );
        if (!response.ok) throw new Error(result.error || `Diagnostics request failed (${response.status})`);
        if (selection !== getContext().selectionVersion || epoch !== diagnosticEpoch) return;
        diagnosticData = result.channel_diagnostics ?? result;
        diagnosticBoard = board;
        diagnosticReadAt = performance.now();
      } catch (error) {
        if (selection === getContext().selectionVersion && epoch === diagnosticEpoch) diagnosticError = error.message;
      } finally {
        if (diagnosticRequest === request) diagnosticRequest = null;
        if (selection === getContext().selectionVersion && epoch === diagnosticEpoch) {
          renderDiagnosticContent(diagnosticsContent, Number(diagnosticsContent.dataset.channel));
        }
      }
    })();
    diagnosticRequest = request;
    return request.promise;
  }

  function buildDiagnosticsCell(channel) {
    const cell = document.createElement("td");
    const button = document.createElement("button");
    button.className = "button button-small diagnostics-action";
    button.type = "button";
    button.textContent = "Diagnostics";
    button.disabled = !getContext().latestData?.connected;
    button.addEventListener("click", async () => {
      diagnosticsDialog.querySelector("h2").textContent = `CDC${channel.id} diagnostics`;
      diagnosticsContent.dataset.channel = String(channel.id);
      renderDiagnosticContent(diagnosticsContent, channel.id);
      diagnosticsDialog.showModal();
      if (diagnosticBoard !== getContext().selectedBoard || (!diagnosticData && !diagnosticError)) await loadDiagnostics();
    });
    cell.append(button);
    return cell;
  }

  function refreshDiagnostics(connected) {
    if (!connected) diagnosticError = "Board disconnected; reconnect before reading diagnostics.";
    renderDiagnosticContent(diagnosticsContent, Number(diagnosticsContent.dataset.channel));
    if (connected) loadDiagnostics();
  }

  return { buildDiagnosticsCell, invalidateDiagnostics, updateDiagnosticsAge, refreshDiagnostics };
}
