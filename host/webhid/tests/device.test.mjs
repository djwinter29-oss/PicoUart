import assert from "node:assert/strict";
import test from "node:test";
import { DEVICE_FILTER, identityDiscoveryLength, matchesPicoUart, PicoUartWebHid,
  validatePicoUartDescriptor } from "../js/device.mjs";

test("WebHID uses protocol report lengths when collection metadata is omitted", () => {
  const device = { collections: [] };
  assert.equal(identityDiscoveryLength(device), 27);
  assert.doesNotThrow(() => validatePicoUartDescriptor(device));
});

test("WebHID rejects reported descriptor lengths that conflict with the protocol", () => {
  const device = { collections: [{
    featureReports: [{ reportId: 1, items: [{ reportSize: 8, reportCount: 28 }] }],
    inputReports: [],
    children: [],
  }] };
  assert.throws(() => validatePicoUartDescriptor(device), /invalid Feature report 1 length/);
});

test("identity report uses its 27-byte protocol length when Windows reports 26 bytes", () => {
  const device = { collections: [{
    featureReports: [{ reportId: 1, items: [{ reportSize: 8, reportCount: 26 }] }],
    inputReports: [],
    children: [],
  }] };
  assert.equal(identityDiscoveryLength(device), 27);
  assert.doesNotThrow(() => validatePicoUartDescriptor(device, new Set([1])));
});

test("WebHID discovery filters by PicoUart VID/PID and leaves descriptor validation to connection", () => {
  assert.deepEqual(DEVICE_FILTER, { vendorId: 0xcafe, productId: 0x4010 });
  assert.equal(matchesPicoUart({ vendorId: 0xcafe, productId: 0x4010, collections: [] }), true);
  assert.equal(matchesPicoUart({ vendorId: 0xcafe, productId: 0x4011 }), false);
  assert.equal(matchesPicoUart({ vendorId: 0xcafe, productId: 0x4010 }), true);
});

test("WebHID chooser requests PicoUart vendor devices before exact product validation", async () => {
  const requests = [];
  const hid = { addEventListener() {}, requestDevice: async options => { requests.push(options); return []; } };
  const client = new PicoUartWebHid(hid);
  await client.connect();
  assert.deepEqual(requests, [{ filters: [{ vendorId: 0xcafe }] }]);
  assert.equal(client.state.phase, "disconnected");
});

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
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);

    const samples = [
      [2000, 11, 500, { uart_tx: 2100, uart_rx: 1200, usb_tx: 1300, usb_rx: 900 }],
      [3000, 12, 500, { uart_tx: 2100, uart_rx: 1200, usb_tx: 1300, usb_rx: 900 }],
      [4000, 13, 500, { uart_tx: 6100, uart_rx: 3200, usb_tx: 4300, usb_rx: 2400 }],
      [5000, 14, 500, { uart_tx: 6100, uart_rx: 3200, usb_tx: 4300, usb_rx: 2400 }],
      [6000, 16, 65535, { uart_tx: 7100, uart_rx: 3700, usb_tx: 5300, usb_rx: 2900 }],
    ];
    const expectedRates = [
      { uart_tx: 2000, uart_rx: 1000, usb_tx: 1000, usb_rx: 500 },
      { uart_tx: 1000, uart_rx: 500, usb_tx: 500, usb_rx: 250 },
      { uart_tx: 2000, uart_rx: 1000, usb_tx: 4000 / 3, usb_rx: 2000 / 3 },
      { uart_tx: 1500, uart_rx: 750, usb_tx: 1000, usb_rx: 500 },
      { uart_tx: 1400, uart_rx: 700, usb_tx: 1000, usb_rx: 500 },
    ];
    for (const [index, [time, sequence, interval_ms, counters]] of samples.entries()) {
      now = time;
      client.applyInputSnapshot({ id: 0, sequence, interval_ms, uart_measurement_state: 1, counters });
      assert.deepEqual(client.state.dashboard.channels[0].rates, expectedRates[index]);
    }

    const dashboard = client.state.dashboard;
    const channel = dashboard.channels[0];
    assert.equal(channel.deltas.uart_tx, null);
    assert.deepEqual(channel.rates, expectedRates.at(-1));
    assert.equal(channel.traffic_gaps, 1);
    assert.equal(dashboard.traffic_gaps, 1);
    assert.equal(dashboard.traffic_incomplete, true);
  } finally {
    Date.now = originalNow;
  }
});