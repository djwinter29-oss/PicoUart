import assert from "node:assert/strict";
import test from "node:test";
import { DEVICE_FILTER, decodeDeviceInformation, decodeControlReport, decodeTelemetryReport,
  decodeChannelSnapshot, PicoUartWebHid, matchesPicoUart } from "../js/device.mjs";

function feature(id, size) {
  const data = new DataView(new ArrayBuffer(size + 1));
  data.setUint8(0, id);
  data.setUint8(1, 1);
  return data;
}

function information({ mcu = 2, capabilities = 7, profiles = 31 } = {}) {
  const data = feature(1, 63);
  new Uint8Array(data.buffer, 2).set(new TextEncoder().encode("PicoUart"));
  new Uint8Array(data.buffer, 34).set(new TextEncoder().encode("0123456789ABCDEF"));
  [1, 2, 3, mcu, 6, 3, 60].forEach((value, i) => data.setUint8(50 + i, value));
  data.setUint16(57, capabilities, true);
  data.setUint8(59, profiles);
  data.setUint32(60, 532480, true);
  return data;
}

function control({ command = 0, flags = 0, status = 0, id = 0, profile = 0xffffffff, error = 0 } = {}) {
  const data = feature(2, 24);
  data.setUint8(2, command);
  data.setUint8(3, flags);
  data.setUint8(4, status);
  data.setUint32(5, id, true);
  data.setUint32(9, 0xffffffff, true);
  data.setUint32(17, profile, true);
  data.setUint8(22, 1);
  data.setUint8(23, error);
  return data;
}

function telemetry(led = 0) {
  const data = feature(3, 24);
  data.setUint8(2, led);
  data.setInt16(3, 2500, true);
  data.setBigUint64(5, 4294967296n, true);
  data.setUint16(13, 10000, true);
  data.setUint16(15, 65535, true);
  data.setUint32(17, 65536, true);
  data.setUint32(21, 150000000, true);
  return data;
}

function channel({ sequence = 1, count = 150000, interval = 500, health = 1 } = {}) {
  const data = new DataView(new ArrayBuffer(54));
  data.setUint8(0, 1);
  data.setUint16(1, sequence, true);
  data.setUint32(3, interval, true);
  data.setUint8(7, health);
  data.setUint8(8, 2);
  for (let i = 0; i < 4; i++) data.setUint32(9 + i * 4, count, true);
  data.setUint32(25, 115200, true);
  data.setUint8(29, 8);
  data.setUint8(30, 1);
  data.setUint8(32, 0);
  data.setUint8(33, 1);
  return data;
}

class FakeDevice extends EventTarget {
  vendorId = DEVICE_FILTER.vendorId;
  productId = DEVICE_FILTER.productId;
  productName = "PicoUart";
  collections = [{ usagePage: 0xff00, usage: 1 }];
  opened = false;
  reads = [];
  writes = [];
  info = information();
  result = control();
  led = 0;
  channels = Array.from({ length: 6 }, () => channel());
  async open() { this.opened = true; }
  async close() { this.opened = false; }
  async receiveFeatureReport(id) {
    this.reads.push(id);
    if (id === 1) return this.info;
    if (id === 2) return this.result;
    if (id === 3) return telemetry(this.led);
    if (id >= 10 && id <= 15) {
      const snapshot = this.channels[id - 10], data = feature(id, 54);
      new Uint8Array(data.buffer, 1).set(new Uint8Array(snapshot.buffer));
      return data;
    }
    throw new Error(`Unexpected report ${id}`);
  }
  async sendFeatureReport(id, data) {
    this.writes.push([id, [...data]]);
    assert.equal(id, 2);
    assert.equal(data.length, 24);
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
    const command = view.getUint8(1);
    if (command <= 2) this.led = command === 2 ? 1 : 0;
    this.result = control({ command, flags: view.getUint8(2), id: view.getUint32(4, true), status: 2,
      profile: command === 5 ? view.getUint32(16, true) : 0xffffffff });
  }
  input(id, data) {
    const event = new Event("inputreport");
    Object.assign(event, { device: this, reportId: id, data });
    this.dispatchEvent(event);
  }
}

class FakeHid extends EventTarget {
  devices = [new FakeDevice()];
  async requestDevice() { return this.devices; }
}

test("static identity includes firmware, capabilities and total RAM with strict masks", () => {
  const info = decodeDeviceInformation(information());
  assert.equal(info.product_name, "PicoUart");
  assert.equal(info.serial_number, "0123456789ABCDEF");
  assert.equal(info.firmware_version, "1.2.3");
  assert.equal(info.ram_total_bytes, 532480);
  assert.equal(decodeDeviceInformation(information({ mcu: 7 })).mcu, "Unknown MCU (7)");
  for (const [offset, value] of [[1, 2], [54, 5], [55, 7], [56, 64], [58, 1], [59, 0]]) {
    const data = information();
    data.setUint8(offset, value);
    assert.throws(() => decodeDeviceInformation(data));
  }
  assert.throws(() => decodeDeviceInformation(feature(7, 63)), /ID or size/);
});

test("control validates rejected echoes and configuration independently", () => {
  assert.equal(decodeControlReport(control()).configuration.reason, "erased");
  assert.equal(decodeControlReport(control({ command: 127, flags: 1, status: 4, id: 1, error: 5 })).flags, 1);
  assert.equal(decodeControlReport(control({ command: 127, flags: 128, status: 3, id: 1, error: 1 })).flags, 128);
  assert.throws(() => decodeControlReport(control({ command: 2, flags: 128, status: 4, id: 1, error: 5 })));
  assert.throws(() => decodeControlReport(control({ command: 5, status: 2, id: 1 })));
  assert.throws(() => decodeControlReport(control({ command: 2, status: 3, id: 1, error: 7 })));
  const invalid = control();
  invalid.setUint8(24, 1);
  assert.throws(() => decodeControlReport(invalid));
});

test("board telemetry uses static total RAM and LED availability", () => {
  const info = decodeDeviceInformation(information());
  const decoded = decodeTelemetryReport(telemetry(1), info);
  assert.equal(decoded.temperature_celsius, 25);
  assert.equal(decoded.runtime.uptime_ms, 4294967296);
  assert.deepEqual(decoded.runtime.cpu_usage_basis_points, [10000, null]);
  assert.equal(decoded.runtime.ram_total_bytes, 532480);
  assert.deepEqual(decoded.led, { manual_on: true, output_on: true });
  assert.throws(() => decodeTelemetryReport(telemetry(2), info));
  const unavailable = decodeDeviceInformation(information({ capabilities: 6 }));
  assert.equal(decodeTelemetryReport(telemetry(0), unavailable).led.output_on, null);
  assert.throws(() => decodeTelemetryReport(telemetry(1), unavailable));
});

test("channel snapshots retain 32-bit deltas and compact shared fault time", () => {
  const data = channel({ sequence: 65535, count: 4000000000, health: 65 });
  data.setUint32(42, 3, true);
  data.setUint32(50, 123, true);
  const decoded = decodeChannelSnapshot(data, 10);
  assert.equal(decoded.sequence, 65535);
  assert.equal(decoded.traffic.uart_tx, 4000000000);
  assert.deepEqual(decoded.faults.rx_overrun, { count: 3 });
  assert.equal(decoded.last_fault_uptime_s, 123);
  assert.equal(decoded.parity, "N");
  assert.throws(() => decodeChannelSnapshot(data, 9));
  assert.throws(() => decodeChannelSnapshot(new DataView(new ArrayBuffer(59)), 10));
  for (const [offset, value] of [[0, 2], [29, 4], [30, 3], [31, 3], [32, 48]]) {
    const invalid = channel();
    invalid.setUint8(offset, value);
    assert.throws(() => decodeChannelSnapshot(invalid, 10));
  }
  assert.throws(() => decodeChannelSnapshot(channel({ health: 65 }), 10), /fault/);
});

test("connection reads only consolidated reports and caches static data", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  assert.ok(matchesPicoUart(device));
  await client.connect();
  assert.deepEqual(device.reads, [1, 3, 2, 10, 11, 12, 13, 14, 15]);
  assert.equal(client.state.dashboard.identity.serial_number, "0123456789ABCDEF");
  assert.equal(client.state.dashboard.channel_diagnostics[0].baud_rate, 115200);
  assert.equal(client.state.dashboard.channels[0].sequence, null);
  await client.refresh();
  assert.deepEqual(device.reads.slice(9), [3]);
  await client.disconnect();
  assert.equal(client.state.phase, "disconnected");
});

test("Input baselines, sequence wrap, measured intervals and gaps are channel-local", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  await client.connect();
  device.input(10, channel({ sequence: 65535, count: 100 }));
  assert.equal(client.state.dashboard.channels[0].totals.uart_tx, 0);
  device.input(10, channel({ sequence: 0, count: 200, interval: 750 }));
  assert.equal(client.state.dashboard.channels[0].totals.uart_tx, 200);
  assert.equal(client.state.dashboard.channels[0].observed_interval_ms, 750);
  assert.equal(client.state.dashboard.channels[0].traffic_gaps, 0);
  device.input(11, channel());
  device.input(11, channel({ sequence: 2 }));
  const previousTotal = client.state.dashboard.channels[1].totals.uart_tx;
  device.input(11, channel({ sequence: 2 }));
  assert.equal(client.state.dashboard.channels[1].totals.uart_tx, previousTotal);
  const otherGaps = client.state.dashboard.channels[1].traffic_gaps;
  device.input(10, channel({ sequence: 2 }));
  assert.equal(client.state.dashboard.channels[0].traffic_gaps, 1);
  assert.equal(client.state.dashboard.channels[1].traffic_gaps, otherGaps);
  device.input(10, channel({ sequence: 3, count: 0xffffffff }));
  assert.equal(client.state.dashboard.channels[0].traffic_gaps, 2);
  await client.disconnect();
});

test("channel failures clear only that channel and remain visible until it recovers", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  await client.connect();
  device.input(10, new DataView(new ArrayBuffer(1)));
  device.input(11, channel());
  assert.equal(client.state.dashboard.channel_diagnostics[0], null);
  assert.equal(client.state.dashboard.overflow_counts[0], null);
  assert.equal(client.state.dashboard.channels[0].health, null);
  assert.match(client.state.dashboard.diagnostics_error, /size/);
  assert.ok(client.state.dashboard.channel_diagnostics[1]);
  device.input(10, channel());
  assert.equal(client.state.dashboard.diagnostics_error, null);
  await client.disconnect();
});

test("telemetry failure preserves static identity and channel fault data", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  await client.connect();
  const receive = device.receiveFeatureReport.bind(device);
  device.receiveFeatureReport = id => id === 3 ? Promise.reject(new Error("Telemetry unavailable")) : receive(id);
  await client.refresh();
  assert.equal(client.state.dashboard.runtime, null);
  assert.equal(client.state.dashboard.led, null);
  assert.equal(client.state.dashboard.identity.product_name, "PicoUart");
  assert.equal(client.state.dashboard.channel_diagnostics[0].baud_rate, 115200);
  assert.deepEqual(client.state.dashboard.overflow_counts, Array(6).fill(0));
  device.receiveFeatureReport = receive;
  await client.refresh();
  assert.equal(client.state.dashboard.runtime_error, null);
  await client.disconnect();
});

test("LED uses explicit state and distinct correlated control requests", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  await client.connect();
  await client.toggleLed();
  assert.equal(device.writes[0][1][1], 2);
  assert.equal(client.state.dashboard.led.output_on, true);
  await client.toggleLed();
  assert.equal(device.writes[1][1][1], 1);
  assert.equal(client.state.dashboard.led.output_on, false);
  assert.equal(device.writes[0][1][4], 1);
  assert.equal(device.writes[1][1][4], 2);
  await client.disconnect();
});

test("save validates acknowledgement, capability mask and matching verified result", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  await client.connect();
  await client.saveConfiguration(2, false);
  assert.equal(device.writes.length, 0);
  await client.saveConfiguration(2, true);
  assert.equal(device.writes[0][1][1], 5);
  assert.equal(device.writes[0][1][2], 1);
  assert.equal(device.writes[0][1][16], 2);
  assert.match(client.state.configurationSaveMessage, /Saved/);
  await client.disconnect();
});

test("mismatched command results are unconfirmed and never retried", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  await client.connect();
  device.sendFeatureReport = async (id, data) => {
    device.writes.push([id, [...data]]);
    device.result = control({ command: 2, status: 2, id: 99 });
  };
  await client.toggleLed();
  assert.match(client.state.error, /does not match/);
  assert.equal(device.writes.length, 1);
  await client.disconnect();
});

test("static failure prevents commands and unsupported LED remains unavailable", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid), device = hid.devices[0];
  device.info = information({ capabilities: 6 });
  await client.connect();
  assert.equal(client.state.dashboard.led.output_on, null);
  await client.toggleLed();
  assert.equal(device.writes.length, 0);
  await client.disconnect();
  device.info = information();
  device.info.setUint8(1, 2);
  await client.connect();
  await client.toggleLed();
  assert.equal(device.writes.length, 0);
  await client.disconnect();
});

test("application deadline blocks overlapping transfers and ignores late completion", async () => {
  const hid = new FakeHid(), client = new PicoUartWebHid(hid);
  await client.connect();
  let complete;
  await assert.rejects(client.transfer(() => new Promise(resolve => { complete = resolve; }), 5), /timed out/);
  complete(1);
  await assert.rejects(client.transfer(async () => 2), /disconnect/);
  await client.disconnect();
  await client.connect();
  assert.equal(client.ioBlocked, false);
  await client.disconnect();
});
