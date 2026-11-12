// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
export const STATUS_REQUEST_TIMEOUT_MS = 5000;
export const BOARD_SCAN_TIMEOUT_MS = 10000;
export const DIAGNOSTICS_REQUEST_TIMEOUT_MS = 15000;
export const CONTROL_REQUEST_TIMEOUT_MS = 30000;

export async function fetchJson(url, options, timeoutMs) {
  const controller = new AbortController();
  let rejectTimeout;
  const timeout = new Promise((_, reject) => { rejectTimeout = reject; });
  const timeoutId = window.setTimeout(() => {
    controller.abort();
    rejectTimeout(new Error(`Request timed out after ${timeoutMs} ms`));
  }, timeoutMs);
  const request = (async () => {
    const response = await fetch(url, { ...options, signal: controller.signal });
    return { response, data: await response.json() };
  })();
  try {
    return await Promise.race([request, timeout]);
  } finally {
    window.clearTimeout(timeoutId);
  }
}
