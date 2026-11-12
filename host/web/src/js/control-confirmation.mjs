// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
export function createControlConfirmation(action, boardId, physicalUid, generation, connected) {
  if (!connected || !["reset", "configuration"].includes(action)
    || typeof boardId !== "string" || boardId.length === 0
    || typeof physicalUid !== "string" || physicalUid === "--"
    || !Number.isSafeInteger(generation)) return null;
  return Object.freeze({ action, boardId, physicalUid, generation });
}

export function matchesControlConfirmation(binding, action, boardId, physicalUid, generation, connected) {
  return Boolean(binding && connected
    && binding.action === action
    && binding.boardId === boardId
    && binding.physicalUid === physicalUid
    && binding.physicalUid !== "--"
    && binding.generation === generation);
}

export function resolveStatusMessage({
  boardActionError, scanError, dataError, identityError, configurationError, configurationSaveError,
  runtimeError, ledError, connected,
}) {
  return boardActionError || scanError || dataError
    || (identityError ? `Device identity unavailable: ${identityError}` : null)
    || (configurationError ? `Board configuration unavailable: ${configurationError}` : null)
    || (configurationSaveError ? `Configuration save outcome: ${configurationSaveError}` : null)
    || (runtimeError ? `Runtime telemetry unavailable: ${runtimeError}` : null)
    || (ledError ? `LED state unavailable: ${ledError}` : null)
    || (connected ? "HID telemetry is live" : "Waiting for a PicoUart HID interface");
}
