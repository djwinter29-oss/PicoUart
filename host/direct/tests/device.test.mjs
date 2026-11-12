// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import assert from "node:assert/strict";
import test from "node:test";
import { decodeBoardConfiguration, DEVICE_FILTER, identityDiscoveryLength, matchesPicoUart,
  PicoUartWebHid } from "../src/js/device.mjs";

function channelSnapshot({ id = 0, sequence, interval = 500, measurement = 1, counters = 0 }) {
  const values = typeof counters === "number"
    ? Object.fromEntries(["uart_tx", "uart_rx", "usb_tx", "usb_rx"].map(name => [name, counters]))
    : counters;
  return {
    id,
    sequence,
    interval_ms: interval,
    uart_measurement_state: measurement,
    channel_state: 2,
    channel_state_name: "Ready",
    uart_measurement_state_name: ["Unavailable", "Current", "Cached"][measurement],
    baud_rate: 115200,
    data_bits: 8,
    stop_bits: 1,
    parity: "N",
    counters: values,
  };
}

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
  for (const cause of [...Array(11).keys(), 255]) {
    report.setUint8(27, cause);
    assert.equal(decodeBoardConfiguration(report).reset_cause, cause);
  }

  report.setUint16(32, 0, true);
  assert.throws(() => decodeBoardConfiguration(report, { hardware_channel_mask: 0x3f }),
    /Invalid board-configuration fields/);
});

test("background refresh preserves connected controls and serializes user operations", async () => {
  for (const operation of ["performAction", "refreshDiagnostics"]) {
    const phases = [];
    const client = new PicoUartWebHid(null, state => phases.push(state.phase));
    client.device = {};
    client.state.phase = "connected";
    client.information = { capabilities: 0 };
    client.state.dashboard.configuration = {};
    client.state.dashboard.action_result = {};
    let finishTelemetry;
    let telemetryReads = 0;
    client.readTelemetry = () => {
      telemetryReads++;
      return new Promise(resolve => { finishTelemetry = resolve; });
    };
    let diagnosticsRead = false;
    client.readChannelDetails = async () => {
      diagnosticsRead = true;
      await client.refresh();
    };
    const refresh = client.refresh();
    assert.equal(client.refreshInFlight, true);
    assert.equal(client.state.phase, "connected");
    assert.ok(phases.every(phase => phase === "connected"));
    let settled = false;
    const pending = (operation === "performAction" ? client.performAction(2) : client.refreshDiagnostics())
      .then(() => { settled = true; }, error => {
        assert.match(error.message, /Board-action capability unavailable/);
        settled = true;
      });
    await Promise.resolve();
    assert.equal(settled, false);
    assert.equal(diagnosticsRead, false);
    finishTelemetry();
    await refresh;
    await pending;
    assert.equal(settled, true);
    assert.equal(diagnosticsRead, operation === "refreshDiagnostics");
    assert.equal(telemetryReads, 1);
    assert.equal(client.state.phase, "connected");
  }
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

  assert.equal(client.state.dashboard.channels[0].input_updated_at, null);
  client.information = { hardware_channel_mask: 0x3f };
  client.state.dashboard.configuration = {};
  device.inputListener({ device, reportId: 10, data: new DataView(report.buffer) });
  assert.equal(client.state.dashboard.channels[0].baud_rate, 115200);
  assert.equal(client.state.dashboard.channels[0].sequence, 1);
});

test("diagnostic reads stop without updating state when the connection changes", async () => {
  let finishOldRead;
  const oldDevice = {
    async receiveFeatureReport(reportId) {
      assert.equal(reportId, 10);
      return new Promise(resolve => { finishOldRead = resolve; });
    },
  };
  const newReads = [];
  const newDevice = { async receiveFeatureReport(reportId) { newReads.push(reportId); } };
  const client = new PicoUartWebHid(null);
  client.device = oldDevice;
  client.generation = 1;
  client.information = { hardware_channel_mask: 0x3f };
  client.state.phase = "connected";
  client.state.dashboard.connected = true;

  const reads = client.readChannelDetails();
  await Promise.resolve();
  assert.equal(typeof finishOldRead, "function");
  client.device = newDevice;
  client.generation++;
  client.information = null;
  finishOldRead(new DataView(new Uint8Array([
    1, 2, 1, 0, 194, 1, 0, 8, 1, 0, 1, 0, 244, 1,
    ...Array(16).fill(0),
  ]).buffer));
  await reads;

  assert.deepEqual(newReads, []);
  assert.equal(client.state.dashboard.channels[0].error, null);
});

test("WebHID discovery filters by PicoUart VID/PID and leaves descriptor validation to connection", () => {
  assert.deepEqual(DEVICE_FILTER, { vendorId: 0x2e8a, productId: 0x116b });
  assert.equal(matchesPicoUart({ vendorId: 0x2e8a, productId: 0x116b, collections: [] }), true);
  assert.equal(matchesPicoUart({ vendorId: 0x2e8a, productId: 0x116c }), false);
  assert.equal(matchesPicoUart({ vendorId: 0x2e8a, productId: 0x116b }), true);
});

test("WebHID chooser requests PicoUart vendor devices before exact product validation", async () => {
  const requests = [];
  const hid = { addEventListener() {}, requestDevice: async options => { requests.push(options); return []; } };
  const client = new PicoUartWebHid(hid);
  await client.connect();
  assert.deepEqual(requests, [{ filters: [{ vendorId: 0x2e8a }] }]);
  assert.equal(client.state.phase, "disconnected");
});

test("timed-out transfers release local state after native completion and allow reconnect", async () => {
  const device = {
    vendorId: 0x2e8a,
    productId: 0x116b,
    opened: true,
    async open() { this.opened = true; },
    async close() { this.opened = false; },
    addEventListener() {},
    removeEventListener() {},
    async receiveFeatureReport() { throw new Error("Identity report unavailable"); },
  };
  const client = new PicoUartWebHid({
    addEventListener() {},
    async requestDevice() { return [device]; },
  });
  client.device = device;
  client.state.phase = "connected";
  let finishNativeTransfer;
  await assert.rejects(client.transfer(
    () => new Promise(resolve => { finishNativeTransfer = resolve; }), 5), /timed out/);
  assert.equal(client.transferActive, false);

  await assert.rejects(client.transfer(() => "still pending"), /still active or unconfirmed/);
  finishNativeTransfer("late result");
  await new Promise(resolve => globalThis.setImmediate(resolve));

  await client.disconnect();
  await client.connect();
  assert.equal(client.state.phase, "connected");
  assert.equal(await client.transfer(() => "reconnected"), "reconnected");
  assert.equal(client.transferActive, false);
});

test("an action cancelled during refresh rejects without submitting a command", async () => {
  let writes = 0;
  const client = new PicoUartWebHid({ addEventListener() {} });
  client.device = { opened: false, async close() {}, async sendFeatureReport() { writes++; } };
  client.state.phase = "connected";
  client.information = { capabilities: 4, clock_profile_mask: 31 };
  let finishRefresh;
  client.refreshComplete = new Promise(resolve => { finishRefresh = resolve; });

  const action = client.saveConfiguration(1, true, true);
  await client.disconnect();
  finishRefresh();
  await assert.rejects(action, /cancelled before submission; no command was sent/);
  assert.equal(writes, 0);
});

test("a SET transport rejection identifies the board-action outcome as unconfirmed", async () => {
  const client = new PicoUartWebHid(null);
  client.device = { async sendFeatureReport() { throw new Error("USB transfer rejected"); } };
  client.state.phase = "connected";
  client.information = { capabilities: 4, clock_profile_mask: 31 };
  client.readActionResult = async () => {
    client.state.dashboard.action_result = { action: 0, status: 0 };
  };

  await assert.rejects(client.saveConfiguration(1, true, true),
    /submission failed; outcome is unconfirmed: USB transfer rejected/);
});

test("a failed action preflight makes clear that no command was submitted", async () => {
  let writes = 0;
  const client = new PicoUartWebHid(null);
  client.device = { async sendFeatureReport() { writes++; } };
  client.state.phase = "connected";
  client.information = { capabilities: 4, clock_profile_mask: 31 };
  client.readActionResult = async () => { throw new Error("Action result read failed"); };

  await assert.rejects(client.saveConfiguration(1, true, true),
    /Action result read failed; no command was sent/);
  assert.equal(writes, 0);
});

test("a completed save returns its confirmed action result", async () => {
  let writes = 0;
  const completed = new DataView(Uint8Array.from([1, 4, 2, 0]).buffer);
  const client = new PicoUartWebHid(null);
  client.device = {
    async sendFeatureReport() { writes++; },
    async receiveFeatureReport() { return completed; },
  };
  client.state.phase = "connected";
  client.information = { capabilities: 4, clock_profile_mask: 31 };
  client.readActionResult = async () => {
    client.state.dashboard.action_result = { action: 0, status: 0 };
  };

  const result = await client.saveConfiguration(1, true, true);

  assert.equal(writes, 1);
  assert.deepEqual(result, { action: 4, status: 2, status_name: "Completed", error_reason: 0 });
});

test("traffic rates use clean device intervals and reset across gaps", () => {
  const client = new PicoUartWebHid(null);
  const originalNow = Date.now;
  let now = 1000;
  Date.now = () => now;
  try {
    client.applyInputSnapshot(channelSnapshot({ sequence: 10, counters: 100 }));
    now = 2500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 11, counters: 1100 }));
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, null);

    now = 5000;
    client.applyInputSnapshot(channelSnapshot({ sequence: 12, counters: 2100 }));
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, 2000);
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, 2000);

    now = 5500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 14, counters: 3100 }));
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, null);
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
    assert.equal(client.state.dashboard.channels[0].traffic_gaps, 1);

    now = 6000;
    client.applyInputSnapshot(channelSnapshot({ sequence: 15, interval: 65535, counters: 4100 }));
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, null);
    now = 6500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 16, counters: 5100 }));
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, null);
    now = 7000;
    client.applyInputSnapshot(channelSnapshot({ sequence: 17, counters: 6100 }));
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, 2000);
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, 2000);
    assert.equal(client.state.dashboard.traffic_incomplete, true);
  } finally {
    Date.now = originalNow;
  }
});

test("Cached and Unavailable UART samples invalidate UART rates but not USB rates", () => {
  const client = new PicoUartWebHid(null);
  const originalNow = Date.now;
  let now = 1000;
  Date.now = () => now;
  try {
    client.applyInputSnapshot(channelSnapshot({ sequence: 1, counters: 100 }));
    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 2, counters: 600 }));
    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 3, measurement: 2, counters: 1100 }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
    assert.notEqual(client.state.dashboard.channels[0].rates.usb_tx, null);

    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 4, counters: 1600 }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 5, counters: 2100 }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 6, counters: 2600 }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, 1000);

    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 7, measurement: 0, counters: {
      uart_tx: 0, uart_rx: 0, usb_tx: 3100, usb_rx: 3100,
    } }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 8, counters: 3600 }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 9, counters: 4100 }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 10, counters: 4600 }));
    assert.equal(client.state.dashboard.channels[0].rates.uart_tx, 1000);
  } finally {
    Date.now = originalNow;
  }
});

test("Feature snapshots do not change Input counters, sequence or rate baselines", () => {
  const client = new PicoUartWebHid(null);
  const originalNow = Date.now;
  let now = 1000;
  Date.now = () => now;
  try {
    client.applyInputSnapshot(channelSnapshot({ sequence: 10, counters: 100 }));
    client.applyFeatureSnapshot(channelSnapshot({ sequence: 50, counters: 150 }));
    const channelAfterFeature = client.state.dashboard.channels[0];
    assert.equal(channelAfterFeature.counters.uart_tx, 150);
    assert.equal(channelAfterFeature.sequence, 10);
    assert.equal(channelAfterFeature.input_counters.uart_tx, 100);

    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 11, counters: 200 }));
    assert.equal(client.state.dashboard.channels[0].deltas.uart_tx, 100);
    assert.equal(client.state.dashboard.channels[0].sequence, 11);
  } finally {
    Date.now = originalNow;
  }
});