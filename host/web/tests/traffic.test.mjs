// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import assert from "node:assert/strict";
import test from "node:test";
import { channelHistory, currentRates } from "../src/js/traffic.mjs";

test("a state-only transition away from Current clears UART rates but retains USB rates", () => {
  channelHistory.clear();
  const data = { connected: true, identity: { device_uid: "A1B2C3" }, runtime: { uptime_ms: 1000 } };
  const channel = {
    id: 0,
    sequence: 10,
    observed_interval_ms: 1000,
    updated_at: "2026-01-01T00:00:01.000Z",
    interval_ms: 500,
    uart_measurement_state: 1,
    traffic_gaps: 0,
    totals: { uart_tx: 0, uart_rx: 0, usb_rx: 0, usb_tx: 0 },
  };
  currentRates(channel, 1000, data);

  channel.sequence = 11;
  channel.observed_interval_ms = 2000;
  channel.updated_at = "2026-01-01T00:00:02.000Z";
  channel.totals = { uart_tx: 1000, uart_rx: 2000, usb_rx: 3000, usb_tx: 4000 };
  const current = currentRates(channel, 2000, data);
  assert.equal(current.rates.uart_tx, 1000);
  assert.equal(current.rates.usb_rx, 3000);

  channel.uart_measurement_state = 2;
  const cached = currentRates(channel, 2001, data);
  assert.equal(cached.rates.uart_tx, null);
  assert.equal(cached.rates.uart_rx, null);
  assert.equal(cached.rates.usb_rx, 3000);
  assert.equal(cached.rates.usb_tx, 4000);
});
