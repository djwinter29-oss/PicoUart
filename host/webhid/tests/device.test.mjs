import assert from "node:assert/strict";
import test from "node:test";
import { decodeBoardConfiguration, DEVICE_FILTER, identityDiscoveryLength, matchesPicoUart,
  PicoUartWebHid } from "../js/device.mjs";

test("WebHID accepts valid distinct UART and CDC buffer capacities", () => {
  const bytes = new Uint8Array(36);
  bytes[0] = 1;
  bytes[1] = 0;
  bytes[2] = 6;
  bytes.fill(255, 3, 27);
  bytes[27] = 2;
  const report = new DataView(bytes.buffer);
  report.setUint16(28, 4096, true);
  report.setUint16(30, 4096, true);
  report.setUint16(32, 2048, true);
  report.setUint16(34, 2048, true);

  assert.deepEqual(decodeBoardConfiguration(report, { hardware_channel_mask: 0x3f }).capacities,
    { uart_tx: 4096, uart_rx: 4096, cdc_tx: 2048, cdc_rx: 2048 });

  report.setUint16(32, 0, true);
  assert.throws(() => decodeBoardConfiguration(report, { hardware_channel_mask: 0x3f }),
    /Invalid board-configuration fields/);
});

test("identity report length comes from the protocol rather than OS collection metadata", () => {
  assert.equal(identityDiscoveryLength(), 27);
});

test("WebHID strips the leading Report ID from Windows Feature responses", async () => {
  const client = new PicoUartWebHid(null);
  const report = Uint8Array.from([1, 1, 1, 0, ...Array(24).fill(0)]);
  client.device = { receiveFeatureReport: async () => new DataView(report.buffer) };
  const payload = await client.readFeature(1, 27);
  assert.equal(payload.byteLength, 27);
  assert.deepEqual(Array.from(new Uint8Array(payload.buffer, payload.byteOffset, 3)), [1, 1, 0]);

  const payloadOnly = Uint8Array.from([1, 1, 0, ...Array(24).fill(0)]);
  client.device.receiveFeatureReport = async () => new DataView(payloadOnly.buffer);
  const unchanged = await client.readFeature(1, 27);
  assert.equal(unchanged.byteLength, 27);
  assert.deepEqual(Array.from(new Uint8Array(unchanged.buffer, unchanged.byteOffset, 3)), [1, 1, 0]);
});

test("WebHID normalizes Report ID prefixes on Input snapshots", () => {
  const client = new PicoUartWebHid(null);
  const device = { addEventListener(_name, callback) { this.inputListener = callback; }, removeEventListener() {} };
  client.device = device;
  client.generation = 1;
  client.addInputListener(device, 1);

  const report = new Uint8Array(31);
  report[0] = 10;
  const data = new DataView(report.buffer, 1);
  data.setUint8(0, 1);
  data.setUint8(1, 2);
  data.setUint8(2, 1);
  data.setUint32(3, 115200, true);
  data.setUint8(7, 8);
  data.setUint8(8, 1);
  data.setUint8(9, 0);
  data.setUint16(10, 1, true);
  data.setUint16(12, 500, true);
  for (let index = 0; index < 4; index++) data.setUint32(14 + index * 4, index + 1, true);
  device.inputListener({ device, reportId: 10, data: new DataView(report.buffer) });

  assert.equal(client.state.dashboard.channels[0].baud_rate, 115200);
  assert.equal(client.state.dashboard.channels[0].sequence, 1);
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