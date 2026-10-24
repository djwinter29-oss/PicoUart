import assert from "node:assert/strict";
import test from "node:test";
import { PicoUartWebHid } from "../js/device.mjs";

test("input rates smooth cumulative counters over five seconds across sequence gaps", () => {
  const client = new PicoUartWebHid(null);
  const originalNow = Date.now;
  let now = 1000;
  Date.now = () => now;
  try {
    client.applyInputSnapshot({
      id: 0,
      sequence: 10,
      interval_ms: 500,
      uart_measurement_state: 1,
      counters: { uart_tx: 100, uart_rx: 200, usb_tx: 300, usb_rx: 400 },
    });

    const samples = [
      [2000, 11, 500, { uart_tx: 2100, uart_rx: 1200, usb_tx: 1300, usb_rx: 900 }],
      [3000, 12, 500, { uart_tx: 2100, uart_rx: 1200, usb_tx: 1300, usb_rx: 900 }],
      [4000, 13, 500, { uart_tx: 6100, uart_rx: 3200, usb_tx: 4300, usb_rx: 2400 }],
      [5000, 14, 500, { uart_tx: 6100, uart_rx: 3200, usb_tx: 4300, usb_rx: 2400 }],
      [6000, 16, 65535, { uart_tx: 7100, uart_rx: 3700, usb_tx: 5300, usb_rx: 2900 }],
    ];
    for (const [time, sequence, interval_ms, counters] of samples) {
      now = time;
      client.applyInputSnapshot({ id: 0, sequence, interval_ms, uart_measurement_state: 1, counters });
    }

    const dashboard = client.state.dashboard;
    const channel = dashboard.channels[0];
    assert.equal(channel.deltas.uart_tx, null);
    assert.deepEqual(channel.rates, { uart_tx: 1400, uart_rx: 700, usb_tx: 1000, usb_rx: 500 });
    assert.equal(channel.traffic_gaps, 1);
    assert.equal(dashboard.traffic_gaps, 1);
    assert.equal(dashboard.traffic_incomplete, true);
  } finally {
    Date.now = originalNow;
  }
});