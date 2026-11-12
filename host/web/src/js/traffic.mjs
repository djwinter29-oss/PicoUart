// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
export const channelHistory = new Map();
const MIN_TRAFFIC_RATE_WINDOW_MS = 1000;
const TRAFFIC_RATE_WINDOW_MS = 5000;
const MAX_RATE_ELAPSED_MS = 8000;

export const directions = [
  ["UART TX", "uart_tx", true],
  ["UART RX", "uart_rx", true],
  ["USB RX", "usb_rx", false],
  ["USB TX", "usb_tx", false],
];

export function currentRates(channel, now, data) {
  const id = channel.id;
  const identity = data.identity?.device_uid ?? data.identity?.serial_number;
  const uptime = data.runtime?.uptime_ms;
  const observedInterval = channel.observed_interval_ms;
  const updatedAt = typeof channel.updated_at === "string" ? Date.parse(channel.updated_at) : NaN;
  const totals = Object.fromEntries(directions.map(([, key]) => [key, channel.totals?.[key]]));
  let history = channelHistory.get(id);
  const readerRestarted = history && Number.isSafeInteger(observedInterval)
    && Number.isSafeInteger(history.observedInterval) && observedInterval < history.observedInterval;
  if (!data.connected || !history || history.identity !== identity
    || (Number.isFinite(uptime) && Number.isFinite(history.uptime) && uptime < history.uptime)
    || readerRestarted) {
    history = { identity, uptime, sequence: null, trafficGaps: channel.traffic_gaps ?? 0,
      observedInterval, updatedAt, sequenceReceivedAt: now, stale: false,
      measurementState: channel.uart_measurement_state, usbSamples: [], uartSamples: [] };
    channelHistory.set(id, history);
  }
  const measurementBecameNonCurrent = history.measurementState === 1 && channel.uart_measurement_state !== 1;
  if (measurementBecameNonCurrent) history.uartSamples = [];
  history.measurementState = channel.uart_measurement_state;
  const historyStale = now - history.sequenceReceivedAt > 3000;
  const observationAdvanced = Number.isSafeInteger(observedInterval)
    && Number.isSafeInteger(history.observedInterval) && observedInterval > history.observedInterval;
  const reportAdvanced = Number.isFinite(updatedAt) && Number.isFinite(history.updatedAt)
    && updatedAt !== history.updatedAt || observationAdvanced;
  const rebaseline = historyStale && reportAdvanced && Number.isInteger(channel.sequence);
  const stale = historyStale && !rebaseline;
  const sequenceDelta = Number.isInteger(channel.sequence) && Number.isInteger(history.sequence)
    ? (channel.sequence - history.sequence + 65536) % 65536 : null;
  const newSample = Number.isInteger(channel.sequence)
    && (history.sequence === null || rebaseline || sequenceDelta > 0 && sequenceDelta < 32768);
  const sequenceJump = sequenceDelta !== null && sequenceDelta > 1 && sequenceDelta < 32768;
  const trafficGaps = channel.traffic_gaps ?? history.trafficGaps;
  const gap = trafficGaps !== history.trafficGaps;
  const intervalValid = Number.isInteger(channel.interval_ms)
    && channel.interval_ms > 0 && channel.interval_ms < 65535;
  const totalsValid = Object.values(totals).every(value =>
    Number.isInteger(value) && value >= 0 && value <= 0xffffffff);
  if (rebaseline || stale && !history.stale || gap || newSample && !intervalValid) {
    history.usbSamples = [];
    history.uartSamples = [];
  }
  if (sequenceJump) history.uartSamples = [];
  if (newSample) {
    history.sequence = channel.sequence;
    history.sequenceReceivedAt = now;
    history.stale = false;
  } else if (stale) {
    history.stale = true;
  }
  if (gap) history.trafficGaps = trafficGaps;
  const baselineAfterGap = gap && !newSample && history.sequence !== null;
  if (data.connected && (newSample || baselineAfterGap) && totalsValid && Number.isSafeInteger(observedInterval)) {
    const sample = { observedInterval, totals };
    history.usbSamples.push(sample);
    if (channel.uart_measurement_state === 1) history.uartSamples.push(sample);
    else history.uartSamples = [];
    for (const samples of [history.usbSamples, history.uartSamples]) {
      while (samples.length > 2
        && samples[1].observedInterval <= observedInterval - TRAFFIC_RATE_WINDOW_MS) samples.shift();
    }
  }
  const rates = {};
  for (const [, key, uart] of directions) {
    const samples = uart ? history.uartSamples : history.usbSamples;
    const baseline = samples[0];
    const latest = samples.at(-1);
    const elapsed = latest && baseline ? latest.observedInterval - baseline.observedInterval : 0;
    rates[key] = !stale && elapsed >= MIN_TRAFFIC_RATE_WINDOW_MS && elapsed <= MAX_RATE_ELAPSED_MS
      ? ((latest.totals[key] - baseline.totals[key]) >>> 0) * 1000 / elapsed : null;
  }
  if (Number.isFinite(uptime)) history.uptime = uptime;
  if (newSample || baselineAfterGap) {
    if (Number.isSafeInteger(observedInterval)) history.observedInterval = observedInterval;
    if (Number.isFinite(updatedAt)) history.updatedAt = updatedAt;
  }
  const fresh = data.connected && Number.isInteger(history.sequence) && now - history.sequenceReceivedAt <= 3000;
  return { rates, fresh };
}
