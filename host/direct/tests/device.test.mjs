// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import assert from "node:assert/strict";
import test from "node:test";
import { decodeBoardConfiguration, DEVICE_FILTER, identityDiscoveryLength, matchesPicoUart,
  decodeChannelSnapshot, PicoUartWebHid } from "../src/js/device.mjs";

function channelSnapshot({ id = 0, sequence, interval = 500, measurement = 1, state = 2, counters = 0 }) {
  const values = typeof counters === "number"
    ? Object.fromEntries(["uart_tx", "uart_rx", "usb_tx", "usb_rx"].map(name => [name, counters]))
    : counters;
  return {
    id,
    sequence,
    interval_ms: interval,
    uart_measurement_state: measurement,
    channel_state: state,
    channel_state_name: ["Closed", "Starting", "Ready", "Configuring", "Error"][state],
    uart_measurement_state_name: ["Unavailable", "Current", "Cached"][measurement],
    baud_rate: 115200,
    data_bits: 8,
    stop_bits: 1,
    parity: "N",
    counters: values,
  };
}

function identityPayload(uidByte = 0x10) {
  const bytes = new Uint8Array(27);
  const data = new DataView(bytes.buffer);
  data.setUint8(0, 1);
  data.setUint16(1, 1, true);
  bytes.fill(uidByte, 3, 11);
  data.setUint8(14, 2);
  data.setUint8(15, 3);
  data.setUint8(16, 0x3f);
  data.setUint8(17, 4);
  data.setUint8(18, 0x1f);
  data.setUint32(19, 524288, true);
  data.setUint32(23, 32768, true);
  return bytes;
}

function configurationPayload(profile = 2) {
  const bytes = new Uint8Array(36);
  const data = new DataView(bytes.buffer);
  data.setUint8(0, 1);
  data.setUint8(1, profile);
  data.setUint8(2, 6);
  bytes.fill(255, 3, 27);
  data.setUint16(28, 4096, true);
  data.setUint16(30, 4096, true);
  data.setUint16(32, 2048, true);
  data.setUint16(34, 2048, true);
  return bytes;
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

test("identity discovery uses the selected Feature report's declared byte length", () => {
  const device = { collections: [{
    featureReports: [{ reportId: 2, items: [{ reportSize: 8, reportCount: 36 }] }],
    children: [{ featureReports: [{ reportId: 1, items: [{ reportSize: 8, reportCount: 8 }] }] }],
  }] };
  assert.equal(identityDiscoveryLength(device), 8);
  device.collections[0].children[0].featureReports[0].items[0].reportCount = 27;
  assert.equal(identityDiscoveryLength(device), 27);
  assert.throws(() => identityDiscoveryLength({ collections: [] }), /Feature report 1 length unavailable/);
});

test("unknown products are identified from the discovery prefix before PicoUart layout validation", async () => {
  const bytes = Uint8Array.from([1, 2, 0, 0, 0, 0, 0, 0]);
  const client = new PicoUartWebHid(null);
  client.device = {
    collections: [{ featureReports: [{ reportId: 1, items: [{ reportSize: 8, reportCount: 8 }] }] }],
    async receiveFeatureReport() { return new DataView(bytes.buffer); },
  };

  assert.equal(await client.discoverIdentity(), false);
  assert.equal(client.state.dashboard.identity.product_name, "Unknown product (0x0002)");
  assert.equal(client.state.dashboard.identity_error, "No supported report contract for this product");
});

test("recovery storage denial does not stop read-only device monitoring", async () => {
  const originalStorage = Object.getOwnPropertyDescriptor(globalThis, "sessionStorage");
  const identity = identityPayload();
  const configuration = configurationPayload();
  const telemetry = new Uint8Array(20);
  telemetry[0] = 1;
  new DataView(telemetry.buffer).setUint32(16, 150000000, true);
  const reports = new Map([
    [1, identity],
    [2, configuration],
    [3, telemetry],
    [20, Uint8Array.from([1, 0, 0, 0])],
  ]);
  const reads = [];
  const client = new PicoUartWebHid(null);
  client.device = {
    collections: [{ featureReports: [{ reportId: 1, items: [{ reportSize: 8, reportCount: 27 }] }] }],
    async receiveFeatureReport(id) {
      reads.push(id);
      return new DataView(reports.get(id).buffer);
    },
  };
  client.generation = 1;
  client.state.phase = "connected";
  client.readChannelDetails = async () => true;
  Object.defineProperty(globalThis, "sessionStorage", {
    configurable: true,
    get() { throw new Error("Recovery storage denied"); },
  });

  try {
    await client.refresh(true);
    assert.deepEqual(reads, [1, 2, 20, 3]);
    assert.equal(client.state.dashboard.identity.product_name, "PicoUart");
    assert.equal(client.state.dashboard.configuration.stored_profile, 2);
    assert.equal(client.state.dashboard.telemetry.system_clock_hz, 150000000);
    assert.match(client.state.dashboard.configuration_save_storage_error, /Recovery storage denied/);
  } finally {
    if (originalStorage) Object.defineProperty(globalThis, "sessionStorage", originalStorage);
    else delete globalThis.sessionStorage;
  }
});

test("failure to persist save recovery prevents command submission", async () => {
  const originalStorage = Object.getOwnPropertyDescriptor(globalThis, "sessionStorage");
  let writes = 0;
  const client = new PicoUartWebHid(null);
  client.device = {
    async sendFeatureReport() { writes++; },
  };
  client.information = {
    serial_number: "5151515151515151",
    capabilities: 4,
    clock_profile_mask: 0x1f,
  };
  client.state.phase = "connected";
  client.state.dashboard.channels = Array.from({ length: 6 }, () => ({
    channel_state: 0,
    input_received_at: performance.now(),
  }));
  client.readActionResult = async () => {
    client.state.dashboard.action_result = { action: 0, status: 0 };
  };
  Object.defineProperty(globalThis, "sessionStorage", {
    configurable: true,
    value: {
      getItem: () => null,
      setItem() { throw new Error("Quota exceeded"); },
      removeItem() {},
    },
  });

  try {
    await assert.rejects(client.saveConfiguration(1, true, true), /no command was sent/);
    assert.equal(writes, 0);
    assert.match(client.state.dashboard.configuration_save_storage_error, /Quota exceeded/);
  } finally {
    if (originalStorage) Object.defineProperty(globalThis, "sessionStorage", originalStorage);
    else delete globalThis.sessionStorage;
  }
});

test("uncertain saves persist by board UID until a fresh readback is explicitly acknowledged", async () => {
  const originalStorage = Object.getOwnPropertyDescriptor(globalThis, "sessionStorage");
  const values = new Map();
  Object.defineProperty(globalThis, "sessionStorage", {
    configurable: true,
    value: {
      getItem: key => values.get(key) ?? null,
      setItem: (key, value) => values.set(key, value),
      removeItem: key => values.delete(key),
    },
  });
  const identity = {
    serial_number: "1010101010101010",
    capabilities: 4,
    clock_profile_mask: 0x1f,
  };
  const savedReports = new Map();
  let firstWrites = 0;
  const first = new PicoUartWebHid(null);
  first.device = {
    async receiveFeatureReport(id) {
      assert.equal(id, 20);
      return new DataView(Uint8Array.from([1, 0, 0, 0]).buffer);
    },
    async sendFeatureReport() {
      firstWrites++;
      throw new Error("USB transfer rejected");
    },
  };
  first.information = identity;
  first.state.phase = "connected";
  first.state.dashboard.connected = true;
  first.state.dashboard.identity = identity;
  first.state.dashboard.channels = Array.from({ length: 6 }, () => ({
    channel_state: 0,
    input_received_at: performance.now(),
  }));

  try {
    await assert.rejects(first.saveConfiguration(1, true, true), /outcome is unconfirmed/);
    assert.equal(firstWrites, 1);
    assert.equal(values.get("picouart.direct.save-recovery.1010101010101010"), "1");
    assert.equal(first.state.dashboard.configuration_save_unconfirmed, true);

    const bytesByReport = new Map([
      [1, identityPayload(0x10)],
      [2, configurationPayload()],
    ]);
    let retryWrites = 0;
    const reconnected = new PicoUartWebHid(null);
    reconnected.device = {
      collections: [{ featureReports: [{ reportId: 1, items: [{ reportSize: 8, reportCount: 27 }] }] }],
      async receiveFeatureReport(id) {
        return new DataView(bytesByReport.get(id).buffer);
      },
      async sendFeatureReport() { retryWrites++; },
    };
    reconnected.generation = 1;
    reconnected.state.phase = "connected";
    assert.equal(await reconnected.discoverIdentity(), true);
    assert.equal(reconnected.state.dashboard.configuration_save_unconfirmed, true);
    assert.equal(reconnected.state.dashboard.configuration_save_profile, 1);
    await assert.rejects(reconnected.saveConfiguration(1, true, true),
      /Inspect and acknowledge the stored configuration/);
    assert.equal(retryWrites, 0);
    assert.throws(() => reconnected.acknowledgeConfigurationRecovery(),
      /current board configuration/);

    await reconnected.readConfiguration();
    assert.equal(reconnected.state.dashboard.configuration_recovery_ready, true);
    reconnected.acknowledgeConfigurationRecovery();
    assert.equal(reconnected.state.dashboard.configuration_save_unconfirmed, false);
    assert.equal(values.has("picouart.direct.save-recovery.1010101010101010"), false);
  } finally {
    if (originalStorage) Object.defineProperty(globalThis, "sessionStorage", originalStorage);
    else delete globalThis.sessionStorage;
  }
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

test("PIO framing restrictions use validated identity when configuration is unavailable", () => {
  const bytes = new Uint8Array(30);
  const report = new DataView(bytes.buffer);
  report.setUint8(0, 1);
  report.setUint8(1, 2);
  report.setUint8(2, 1);
  report.setUint32(3, 115200, true);
  report.setUint8(7, 7);
  report.setUint8(8, 2);
  report.setUint8(9, 2);
  report.setUint16(10, 1, true);
  report.setUint16(12, 500, true);

  assert.throws(() => decodeChannelSnapshot(report, 12, { hardware_channel_mask: 0x03 }),
    /Invalid channel snapshot fields/);

  report.setUint8(7, 8);
  report.setUint8(8, 1);
  report.setUint8(9, 0);
  assert.equal(decodeChannelSnapshot(report, 12, { hardware_channel_mask: 0x03 }).parity, "N");
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

test("timed-out HID open blocks reconnect until a late open is safely closed", async () => {
  let finishOpen;
  let openCount = 0;
  const device = {
    vendorId: 0x2e8a,
    productId: 0x116b,
    opened: false,
    open() {
      openCount++;
      return new Promise(resolve => {
        finishOpen = () => {
          this.opened = true;
          resolve();
        };
      });
    },
    async close() { this.opened = false; },
  };
  let requestCount = 0;
  const client = new PicoUartWebHid({
    addEventListener() {},
    async requestDevice() { requestCount++; return [device]; },
  });

  const connecting = client.connect();
  await new Promise(resolve => globalThis.setTimeout(resolve, 3100));
  assert.equal(client.lifecycleBlocked, true);
  assert.equal(client.state.phase, "disconnected");
  await client.connect();
  assert.equal(requestCount, 1);
  assert.equal(openCount, 1);

  finishOpen();
  await new Promise(resolve => globalThis.setImmediate(resolve));
  await connecting;
  assert.equal(device.opened, false);
  assert.equal(client.lifecycleBlocked, false);
});

test("unplug during a pending HID open serializes reconnect until stale cleanup completes", async () => {
  let finishFirstOpen;
  let openCount = 0;
  let closeCount = 0;
  let disconnect;
  let requestCount = 0;
  const device = {
    vendorId: 0x2e8a,
    productId: 0x116b,
    opened: false,
    open() {
      openCount++;
      if (openCount === 1) {
        return new Promise(resolve => {
          finishFirstOpen = () => {
            this.opened = true;
            resolve();
          };
        });
      }
      this.opened = true;
      return Promise.resolve();
    },
    async close() {
      closeCount++;
      this.opened = false;
    },
    addEventListener() {},
    removeEventListener() {},
  };
  const client = new PicoUartWebHid({
    addEventListener(_name, callback) { disconnect = callback; },
    async requestDevice() {
      requestCount++;
      return [device];
    },
  });
  client.refresh = async () => {};

  const firstConnection = client.connect();
  await new Promise(resolve => globalThis.setImmediate(resolve));
  assert.equal(client.lifecycleBlocked, true);
  disconnect({ device });
  await client.connect();
  assert.equal(requestCount, 1);
  assert.equal(openCount, 1);

  finishFirstOpen();
  await firstConnection;
  assert.equal(device.opened, false);
  assert.equal(closeCount, 1);
  assert.equal(client.lifecycleBlocked, false);
  assert.equal(client.state.phase, "disconnected");

  await client.connect();
  assert.equal(requestCount, 2);
  assert.equal(openCount, 2);
  assert.equal(client.state.phase, "connected");
  assert.equal(device.opened, true);
});

test("failed cleanup after a late HID open stays blocked until the device is unplugged", async () => {
  let finishOpen;
  const device = {
    vendorId: 0x2e8a,
    productId: 0x116b,
    opened: false,
    open() {
      return new Promise(resolve => {
        finishOpen = () => {
          this.opened = true;
          resolve();
        };
      });
    },
    async close() { throw new Error("HID close failed"); },
  };
  let disconnect;
  let requestCount = 0;
  const client = new PicoUartWebHid({
    addEventListener(_name, callback) { disconnect = callback; },
    async requestDevice() { requestCount++; return [device]; },
  });

  const connecting = client.connect();
  await new Promise(resolve => globalThis.setTimeout(resolve, 3100));
  finishOpen();
  await new Promise(resolve => globalThis.setImmediate(resolve));
  await connecting;
  assert.equal(client.lifecycleBlocked, true);
  assert.match(client.state.error, /HID close failed/);
  await client.connect();
  assert.equal(requestCount, 1);

  device.opened = false;
  disconnect({ device });
  assert.equal(client.lifecycleBlocked, false);
  await client.connect();
  assert.equal(requestCount, 2);
});

test("timed-out HID close blocks reconnect until the pending close completes", async () => {
  let finishClose;
  let requestCount = 0;
  const device = {
    vendorId: 0x2e8a,
    productId: 0x116b,
    opened: true,
    close() {
      return new Promise(resolve => {
        finishClose = () => {
          this.opened = false;
          resolve();
        };
      });
    },
    addEventListener() {},
    removeEventListener() {},
  };
  const client = new PicoUartWebHid({
    addEventListener() {},
    async requestDevice() { requestCount++; return []; },
  });
  client.device = device;
  client.state.phase = "connected";

  await client.disconnect();
  assert.equal(client.lifecycleBlocked, true);
  assert.equal(client.state.phase, "disconnecting");
  await client.connect();
  assert.equal(requestCount, 0);

  finishClose();
  await new Promise(resolve => globalThis.setImmediate(resolve));
  assert.equal(client.lifecycleBlocked, false);
  assert.equal(client.state.phase, "disconnected");
  assert.equal(device.opened, false);
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
  client.information = { serial_number: "2020202020202020", capabilities: 4, clock_profile_mask: 31 };
  client.state.dashboard.channels = Array.from({ length: 6 }, () => ({
    channel_state: 0,
    input_received_at: performance.now(),
  }));
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
  client.information = { serial_number: "3030303030303030", capabilities: 4, clock_profile_mask: 31 };
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
  client.information = { serial_number: "4040404040404040", capabilities: 4, clock_profile_mask: 31 };
  client.state.dashboard.channels = Array.from({ length: 6 }, () => ({
    channel_state: 0,
    input_received_at: performance.now(),
  }));
  client.readActionResult = async () => {
    client.state.dashboard.action_result = { action: 0, status: 0 };
  };

  const result = await client.saveConfiguration(1, true, true);

  assert.equal(writes, 1);
  assert.deepEqual(result, { action: 4, status: 2, status_name: "Completed", error_reason: 0 });
});

test("configuration save rechecks Closed and fresh channels after action preflight", async () => {
  for (const invalidState of ["ready", "stale"]) {
    let finishPreflight;
    let writes = 0;
    const client = new PicoUartWebHid(null);
    client.device = { async sendFeatureReport() { writes++; } };
    client.state.phase = "connected";
    client.information = {
      serial_number: invalidState === "ready" ? "6161616161616161" : "6262626262626262",
      capabilities: 4,
      clock_profile_mask: 31,
    };
    client.state.dashboard.channels = Array.from({ length: 6 }, () => ({
      channel_state: 0,
      input_received_at: performance.now(),
    }));
    client.readActionResult = () => new Promise(resolve => {
      finishPreflight = () => {
        client.state.dashboard.action_result = { action: 0, status: 0 };
        resolve();
      };
    });

    const action = client.saveConfiguration(0, false, true);
    await new Promise(resolve => globalThis.setImmediate(resolve));
    assert.equal(typeof finishPreflight, "function");
    if (invalidState === "ready") {
      client.state.dashboard.channels = client.state.dashboard.channels.map(channel => ({
        ...channel,
        channel_state: 2,
      }));
    } else {
      client.state.dashboard.channels = client.state.dashboard.channels.map(channel => ({
        ...channel,
        input_received_at: performance.now() - 4000,
      }));
    }
    finishPreflight();

    await assert.rejects(action, /six Closed channels with fresh Input reports; no command was sent/);
    assert.equal(writes, 0);
  }
});

test("traffic rates use clean device intervals and reset across gaps", () => {
  const client = new PicoUartWebHid(null);
  const originalDateNow = Date.now;
  const originalPerformance = Object.getOwnPropertyDescriptor(globalThis, "performance");
  let now = 1000;
  let wallNow = 10000;
  Date.now = () => wallNow;
  Object.defineProperty(globalThis, "performance", {
    configurable: true,
    value: { now: () => now },
  });
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

    now = 7500;
    wallNow += 3_600_000;
    client.applyInputSnapshot(channelSnapshot({ sequence: 18, counters: 7100 }));
    assert.equal(client.state.dashboard.channels[0].traffic_gaps, 1);
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, 2000);

    now += 4200;
    wallNow -= 7_200_000;
    client.applyInputSnapshot(channelSnapshot({ sequence: 19, counters: 8100 }));
    assert.equal(client.state.dashboard.channels[0].traffic_gaps, 2);
    assert.equal(client.state.dashboard.channels[0].rates.usb_tx, null);
    assert.equal(client.state.dashboard.traffic_incomplete, true);
  } finally {
    Date.now = originalDateNow;
    if (originalPerformance) Object.defineProperty(globalThis, "performance", originalPerformance);
    else delete globalThis.performance;
  }
});

test("Feature read failures preserve healthy Input continuity and clear after the next Input report", async () => {
  const client = new PicoUartWebHid(null);
  const originalNow = Date.now;
  let now = 1000;
  Date.now = () => now;
  try {
    for (const [sequence, counters] of [[1, 0], [2, 1000], [3, 2000]]) {
      client.applyInputSnapshot(channelSnapshot({ sequence, counters }));
      now += 500;
    }

    const before = client.state.dashboard.channels[0];
    assert.equal(before.rates.usb_tx, 2000);
    assert.equal(client.state.dashboard.traffic_incomplete, false);

    client.device = {};
    client.state.phase = "connected";
    client.information = {};
    client.readFeature = async () => { throw new Error("Feature report unavailable"); };
    await client.readChannelDetails();

    let channel = client.state.dashboard.channels[0];
    assert.equal(channel.input_updated_at, before.input_updated_at);
    assert.equal(channel.rates.usb_tx, 2000);
    assert.equal(channel.traffic_gaps, 0);
    assert.equal(channel.feature_error, "Feature report unavailable");
    assert.equal(client.state.dashboard.traffic_incomplete, false);

    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 4, counters: 3000 }));
    channel = client.state.dashboard.channels[0];
    assert.equal(channel.rates.usb_tx, 2000);
    assert.equal(channel.traffic_gaps, 0);
    assert.equal(channel.feature_error, null);
    assert.equal(client.state.dashboard.traffic_incomplete, false);
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
    client.applyFeatureSnapshot(channelSnapshot({ sequence: 50, measurement: 2, counters: 150 }));
    const channelAfterFeature = client.state.dashboard.channels[0];
    assert.equal(channelAfterFeature.counters.uart_tx, 150);
    assert.equal(channelAfterFeature.sequence, 10);
    assert.equal(channelAfterFeature.input_counters.uart_tx, 100);
    assert.equal(channelAfterFeature.uart_measurement_state, 2);

    now += 500;
    client.applyInputSnapshot(channelSnapshot({ sequence: 11, counters: 200 }));
    assert.equal(client.state.dashboard.channels[0].deltas.uart_tx, 100);
    assert.equal(client.state.dashboard.channels[0].sequence, 11);
    assert.equal(client.state.dashboard.traffic_incomplete, false);
  } finally {
    Date.now = originalNow;
  }
});

test("stale Feature snapshots cannot replace newer Input state or live rates", () => {
  const client = new PicoUartWebHid(null);
  client.applyInputSnapshot(channelSnapshot({ sequence: 1, counters: 0 }));
  client.applyInputSnapshot(channelSnapshot({ sequence: 2, counters: 500 }));
  client.applyInputSnapshot(channelSnapshot({ sequence: 3, counters: 1000 }));
  client.applyFeatureSnapshot(channelSnapshot({ sequence: 2, state: 0, measurement: 2, counters: 500 }));

  const channel = client.state.dashboard.channels[0];
  assert.equal(channel.sequence, 3);
  assert.equal(channel.channel_state, 2);
  assert.equal(channel.uart_measurement_state, 1);
  assert.equal(channel.counters.usb_tx, 1000);
  assert.equal(channel.rates.usb_tx, 1000);
  assert.equal(channel.rates.uart_tx, 1000);
});

test("Cached or Unavailable Feature measurements invalidate only UART rates", () => {
  const client = new PicoUartWebHid(null);
  client.applyInputSnapshot(channelSnapshot({ sequence: 1, counters: 0 }));
  client.applyInputSnapshot(channelSnapshot({ sequence: 2, counters: 500 }));
  client.applyInputSnapshot(channelSnapshot({ sequence: 3, counters: 1000 }));
  assert.equal(client.state.dashboard.channels[0].rates.uart_tx, 1000);

  client.applyFeatureSnapshot(channelSnapshot({ sequence: 3, measurement: 2, counters: 1000 }));
  let channel = client.state.dashboard.channels[0];
  assert.equal(channel.rates.uart_tx, null);
  assert.equal(channel.rates.usb_tx, 1000);

  client.applyFeatureSnapshot(channelSnapshot({ sequence: 3, measurement: 0, counters: 1000 }));
  channel = client.state.dashboard.channels[0];
  assert.equal(channel.rates.uart_tx, null);
  assert.equal(channel.rates.usb_tx, 1000);

  client.applyInputSnapshot(channelSnapshot({ sequence: 4, counters: 1500 }));
  assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
  client.applyInputSnapshot(channelSnapshot({ sequence: 5, counters: 2000 }));
  assert.equal(client.state.dashboard.channels[0].rates.uart_tx, null);
  client.applyInputSnapshot(channelSnapshot({ sequence: 6, counters: 2500 }));
  assert.equal(client.state.dashboard.channels[0].rates.uart_tx, 1000);
});

test("board action polling deadline uses monotonic time and bounds Feature read timeouts", async () => {
  const client = new PicoUartWebHid(null);
  let monotonicNow = 0;
  let wallNow = 10000;
  let readCount = 0;
  let timerCount = 0;
  const readTimeouts = [];
  const originalDateNow = Date.now;
  const originalSetTimeout = globalThis.setTimeout;
  const originalPerformance = Object.getOwnPropertyDescriptor(globalThis, "performance");
  client.device = { async sendFeatureReport() {} };
  client.information = { capabilities: 1 };
  client.state.phase = "connected";
  client.readActionResult = async () => {
    client.state.dashboard.action_result = { action: 0, status: 0 };
  };
  client.transfer = async operation => operation();
  client.readFeature = async (_id, _length, timeout) => {
    readTimeouts.push(timeout);
    readCount++;
    if (readCount === 1) wallNow -= 5000;
    return new DataView(Uint8Array.from([1, 2, 1, 0]).buffer);
  };
  Date.now = () => wallNow;
  Object.defineProperty(globalThis, "performance", {
    configurable: true,
    value: { now: () => monotonicNow },
  });
  globalThis.setTimeout = (callback, delay) => {
    monotonicNow += delay;
    wallNow += delay;
    timerCount++;
    if (timerCount === 70) wallNow = 13000;
    callback();
    return 0;
  };

  try {
    await assert.rejects(client.performAction(2), /Timed out awaiting board action; outcome is unconfirmed/);
    assert.ok(readCount <= 61, `Expected a three-second deadline, got ${readCount} reads`);
    assert.ok(readTimeouts.every(timeout => timeout > 0 && timeout <= 3000),
      `Feature read timeout exceeded the action deadline: ${readTimeouts}`);
    assert.equal(wallNow < 13000, true, "wall clock adjustment should not control the deadline");
  } finally {
    Date.now = originalDateNow;
    globalThis.setTimeout = originalSetTimeout;
    if (originalPerformance) Object.defineProperty(globalThis, "performance", originalPerformance);
    else delete globalThis.performance;
  }
});

test("initial channel reads survive a refresh already running for the previous connection", async () => {
  const client = new PicoUartWebHid(null);
  const oldDevice = {};
  const newDevice = {};
  client.device = oldDevice;
  client.generation = 1;
  client.state.phase = "connected";
  client.information = { hardware_channel_mask: 0x3f };
  client.state.dashboard.configuration = {};
  client.state.dashboard.action_result = {};
  let finishOldTelemetry;
  client.readTelemetry = () => new Promise(resolve => { finishOldTelemetry = resolve; });

  const oldRefresh = client.refresh();
  await Promise.resolve();
  assert.equal(typeof finishOldTelemetry, "function");

  client.device = newDevice;
  client.generation = 2;
  client.information = null;
  client.state.dashboard = {
    ...client.state.dashboard,
    identity: null,
    configuration: null,
    action_result: null,
  };
  const initialReads = [];
  client.discoverIdentity = async () => {
    client.information = { hardware_channel_mask: 0x3f };
    return true;
  };
  client.readConfiguration = async () => { client.state.dashboard.configuration = {}; };
  client.readActionResult = async () => { client.state.dashboard.action_result = {}; };
  client.readTelemetry = async () => {};
  client.readChannelDetails = async () => { initialReads.push("channels and diagnostics"); return true; };

  await client.refresh(true);
  assert.deepEqual(initialReads, []);
  finishOldTelemetry();
  await oldRefresh;

  await client.refresh();
  assert.deepEqual(initialReads, ["channels and diagnostics"]);
  assert.equal(client.initialRefreshGeneration, null);
});