import assert from "node:assert/strict";
import test from "node:test";
import { DEVICE_FILTER, decodeHardwareReport, decodeBoardReport, decodeOverflowReport, decodeStatusReport,
  decodeIdentityReport, decodeRuntimeReport, decodeLedReport, PicoUartWebHid } from "../js/device.mjs";

function report(mcu = 2, clock = 150000000, size = 7, offset = 0) {
  const view = new DataView(new ArrayBuffer(offset + size), offset, size);
  view.setUint8(0, 6);
  view.setUint8(1, 1);
  view.setUint8(2, mcu);
  view.setUint32(3, clock, true);
  return view;
}

function boardReport() {
  return new DataView(Uint8Array.from([3, 15, 0, 0xc4, 0x09, 1, 2, 3, 0]).buffer);
}

function overflowReport() {
  const view = new DataView(new ArrayBuffer(26));
  view.setUint8(0, 5);
  view.setUint8(1, 15);
  for (let index = 0; index < 6; index++) view.setUint32(2 + index * 4, index, true);
  return view;
}

function identityReport() {
  const view = new DataView(new ArrayBuffer(50));
  view.setUint8(0, 7);
  view.setUint8(1, 1);
  new Uint8Array(view.buffer, 2).set(new TextEncoder().encode("PicoUart"));
  new Uint8Array(view.buffer, 34).set(new TextEncoder().encode("0123456789ABCDEF"));
  return view;
}

function runtimeReport() {
  const view = new DataView(new ArrayBuffer(22));
  view.setUint8(0, 8);
  view.setUint8(1, 2);
  view.setBigUint64(2, 4294967296n, true);
  view.setUint16(10, 10000, true);
  view.setUint16(12, 65535, true);
  view.setUint32(14, 65536, true);
  view.setUint32(18, 270336, true);
  return view;
}

function statusReport(sequence = 255, count = 10) {
  const view = new DataView(new ArrayBuffer(63));
  view.setUint8(0, 0x50);
  view.setUint8(1, 15);
  view.setUint8(2, sequence);
  for (let index = 0; index < 6; index++) {
    view.setUint8(3 + index * 10, 0x31);
    view.setUint8(4 + index * 10, 2);
    for (let direction = 0; direction < 4; direction++) {
      view.setUint16(5 + index * 10 + direction * 2, count + direction + index, true);
    }
  }
  return view;
}

class FakeDevice extends EventTarget {
  vendorId = 0xcafe;
  productId = 0x4010;
  productName = "PicoUart";
  collections = [{ usagePage: 0xff00, usage: 1 }];
  opened = false;
  reads = [];
  closes = 0;
  ledFlags = 1;
  writes = [];
  async sendFeatureReport(id, data) {
    this.writes.push([id, [...data]]);
    this.ledFlags = this.ledFlags === 1 ? 7 : 1;
  }
  async open() { this.opened = true; }
  async close() { this.opened = false; this.closes++; }
  async receiveFeatureReport(id) {
    this.reads.push(id);
    if (id === 3) return boardReport();
    if (id === 5) return overflowReport();
    if (id === 7) return identityReport();
    if (id === 8) return runtimeReport();
    if (id === 9) return new DataView(Uint8Array.of(9, 1, this.ledFlags).buffer);
    return report();
  }
  input(data, reportId = 1) {
    this.dispatchEvent(Object.assign(new Event("inputreport"), { device: this, reportId, data }));
  }
}

class FakeHid extends EventTarget {
  constructor(devices = [new FakeDevice()]) { super(); this.devices = devices; }
  async requestDevice(options) { this.options = options; return this.devices; }
  unplug(device) { this.dispatchEvent(Object.assign(new Event("disconnect"), { device })); }
}

function deferred() {
  let resolve;
  const promise = new Promise(resolver => { resolve = resolver; });
  return { promise, resolve };
}

for (const [mcu, clock, label] of [
  [1, 125000000, "RP2040"], [1, 200000000, "RP2040"], [2, 150000000, "RP2350"],
  [3, 200000000, "Unknown MCU (3)"], [255, 0xffffffff, "Unknown MCU (255)"],
]) {
  test(`decode MCU ${mcu} at ${clock} Hz`, () => {
    assert.deepEqual(decodeHardwareReport(report(mcu, clock)),
      { mcu: label, mcu_id: mcu, system_clock_hz: clock });
  });
}

test("honor DataView offsets and Windows zero padding", () => {
  assert.equal(decodeHardwareReport(report(2, 150000000, 26, 5)).system_clock_hz, 150000000);
});

test("reject truncated and unprefixed feature reports", () => {
  for (const size of [0, 1, 6]) assert.throws(() => decodeHardwareReport(new DataView(new ArrayBuffer(size))));
  assert.throws(() => decodeHardwareReport(new Uint8Array(7)));
  const value = report();
  value.setUint8(0, 5);
  assert.throws(() => decodeHardwareReport(value), /report ID or size/);
});

test("reject unsupported layouts, zero clocks, and nonzero padding", () => {
  const value = report();
  value.setUint8(1, 2);
  assert.throws(() => decodeHardwareReport(value), /layout version/);
  assert.throws(() => decodeHardwareReport(report(3, 0)), /system clock/);
  const padded = report(2, 150000000, 26);
  padded.setUint8(7, 1);
  assert.throws(() => decodeHardwareReport(padded), /padding/);
});

test("select only the PicoUart collection and read report 6", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  assert.deepEqual(hid.options, { filters: [{ ...DEVICE_FILTER }] });
  assert.equal(client.state.phase, "connected");
  assert.equal(client.state.hardware.mcu, "RP2350");
  assert.deepEqual(hid.devices[0].reads, [6, 3, 5, 7, 8, 9]);
  assert.equal(client.state.dashboard.identity.serial_number, "0123456789ABCDEF");
  assert.equal(client.state.dashboard.runtime.uptime_ms, 4294967296);
  assert.equal(client.state.dashboard.board.firmware_version, "1.2.3");
  assert.ok(client.state.updatedAt);
  await client.disconnect();
  assert.equal(hid.devices[0].opened, false);
  assert.equal(client.state.hardware, null);
});

test("handle unavailable WebHID and cancelled selection", async () => {
  const unsupported = new PicoUartWebHid(undefined);
  await unsupported.connect();
  await unsupported.disconnect();
  assert.equal(unsupported.state.phase, "unsupported");
  const client = new PicoUartWebHid(new FakeHid([]));
  await client.connect();
  assert.equal(client.state.phase, "disconnected");
  assert.equal(client.state.error, null);
  assert.match(client.state.message, /computer running this browser; no password/);
});

test("reject wrong device usage and ambiguous selections", async () => {
  for (const devices of [[Object.assign(new FakeDevice(), { collections: [] })],
    [new FakeDevice(), new FakeDevice()]]) {
    const client = new PicoUartWebHid(new FakeHid(devices));
    await client.connect();
    assert.equal(client.device, null);
    assert.match(client.state.error, /one PicoUart/);
    assert.ok(devices.every(device => !device.opened));
  }
});

test("permission and open failures do not leave a connection", async () => {
  const hid = new FakeHid();
  hid.requestDevice = async () => { throw new Error("Permission denied"); };
  const client = new PicoUartWebHid(hid);
  await client.connect();
  assert.equal(client.state.error, "Permission denied");
  hid.requestDevice = async () => hid.devices;
  hid.devices[0].open = async () => { throw new Error("Device busy"); };
  await client.connect();
  assert.equal(client.state.error, "Device busy");
  assert.equal(client.state.phase, "disconnected");
});

test("read failure clears stale hardware and refresh can recover", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  hid.devices[0].receiveFeatureReport = async () => { throw new Error("Report unavailable"); };
  await client.refresh();
  assert.equal(client.state.hardware, null);
  assert.equal(client.state.phase, "connected");
  assert.equal(client.state.error, "Report unavailable");
  hid.devices[0].receiveFeatureReport = async id => id === 6 ? report(3, 200000000)
    : FakeDevice.prototype.receiveFeatureReport.call(hid.devices[0], id);
  await client.refresh();
  assert.equal(client.state.hardware.mcu, "Unknown MCU (3)");
  assert.equal(client.state.error, null);
});

test("unplug during a read cannot restore stale hardware", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  const pending = deferred();
  hid.devices[0].receiveFeatureReport = () => pending.promise;
  const reading = client.refresh();
  hid.unplug(hid.devices[0]);
  pending.resolve(report());
  await reading;
  assert.equal(client.state.phase, "disconnected");
  assert.equal(client.state.hardware, null);
  assert.equal(client.state.updatedAt, null);
});

test("unplug while opening closes a late connection", async () => {
  const hid = new FakeHid();
  const pending = deferred();
  hid.devices[0].open = async () => { await pending.promise; hid.devices[0].opened = true; };
  const client = new PicoUartWebHid(hid);
  const connecting = client.connect();
  await Promise.resolve();
  hid.unplug(hid.devices[0]);
  pending.resolve();
  await connecting;
  assert.equal(hid.devices[0].opened, false);
  assert.equal(client.state.phase, "disconnected");
});

test("ignore other devices and prevent duplicate connection requests", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await Promise.all([client.connect(), client.connect()]);
  assert.deepEqual(hid.devices[0].reads, [6, 3, 5, 7, 8, 9]);
  hid.unplug(new FakeDevice());
  assert.equal(client.state.phase, "connected");
});

test("failed close preserves ownership for retry", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  hid.devices[0].close = async () => { throw new Error("Close failed"); };
  await client.disconnect();
  assert.equal(client.device, hid.devices[0]);
  assert.equal(client.state.phase, "connected");
  assert.equal(client.state.error, "Close failed");
});

test("manual disconnect ignores a late read and reconnect gets a fresh sample", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  const pending = deferred();
  hid.devices[0].receiveFeatureReport = () => pending.promise;
  const reading = client.refresh();
  await client.disconnect();
  pending.resolve(report());
  await reading;
  assert.equal(client.state.phase, "disconnected");
  assert.equal(client.state.hardware, null);
  hid.devices[0].receiveFeatureReport = async id => id === 6 ? report(1, 200000000)
    : FakeDevice.prototype.receiveFeatureReport.call(hid.devices[0], id);
  await client.connect();
  assert.equal(client.state.hardware.mcu, "RP2040");
  assert.equal(client.state.hardware.system_clock_hz, 200000000);
});

test("decode board, overflow, and payload-only status reports", () => {
  assert.equal(decodeBoardReport(boardReport()).temperature_celsius, 25);
  const overflow = overflowReport();
  overflow.setUint32(22, 0xffffffff, true);
  assert.deepEqual(decodeOverflowReport(overflow), [0, 1, 2, 3, 4, 0xffffffff]);
  const status = decodeStatusReport(statusReport());
  assert.equal(status.channels.length, 6);
  assert.equal(status.channels[5].traffic.usb_rx, 18);
  assert.equal(status.channels[0].ring_high_watermark, 32);
  assert.equal(status.channels[0].backend, "PIO");
  assert.equal(status.channels[0].cdc_open, true);
});

test("reject malformed metadata and status", () => {
  const board = boardReport();
  board.setUint8(2, 2);
  assert.throws(() => decodeBoardReport(board), /flags/);
  const overflow = overflowReport();
  overflow.setUint8(1, 14);
  assert.throws(() => decodeOverflowReport(overflow), /layout/);
  const data = statusReport();
  data.setUint8(0, 0);
  assert.throws(() => decodeStatusReport(data), /signature/);
  assert.throws(() => decodeStatusReport(new DataView(new ArrayBuffer(64))), /size/);
});

test("live reports track totals, wrap, gaps, saturation, and detach on unplug", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  hid.devices[0].input(statusReport(255));
  hid.devices[0].input(statusReport(0));
  assert.equal(client.state.dashboard.channels[0].totals.uart_tx, 20);
  assert.equal(client.state.dashboard.traffic_incomplete, false);
  hid.devices[0].input(statusReport(2, 65532));
  assert.equal(client.state.dashboard.traffic_incomplete, true);
  assert.equal(client.state.dashboard.sequence, 2);
  hid.unplug(hid.devices[0]);
  hid.devices[0].input(statusReport(3));
  assert.equal(client.state.dashboard.sequence, null);
  assert.equal(client.state.dashboard.channels[0].totals.uart_tx, 0);
});

test("unsupported hardware info preserves other diagnostics and malformed input recovers", async () => {
  const hid = new FakeHid();
  const originalRead = hid.devices[0].receiveFeatureReport.bind(hid.devices[0]);
  hid.devices[0].receiveFeatureReport = async id => {
    if (id === 6) throw new Error("Hardware info unsupported");
    return originalRead(id);
  };
  const client = new PicoUartWebHid(hid);
  await client.connect();
  assert.equal(client.state.hardware, null);
  assert.equal(client.state.dashboard.board.firmware_version, "1.2.3");
  assert.deepEqual(client.state.dashboard.overflow_counts, [0, 1, 2, 3, 4, 5]);
  hid.devices[0].input(new DataView(new ArrayBuffer(63)));
  assert.match(client.state.dashboard.error, /signature/);
  hid.devices[0].input(statusReport());
  assert.equal(client.state.dashboard.error, null);
  assert.equal(client.state.dashboard.hardware_error, "Hardware info unsupported");
});

test("metadata read failure preserves hardware info and unplug during close wins", async () => {
  const hid = new FakeHid();
  const originalRead = hid.devices[0].receiveFeatureReport.bind(hid.devices[0]);
  hid.devices[0].receiveFeatureReport = async id => {
    if (id === 3) throw new Error("Board metadata unavailable");
    return originalRead(id);
  };
  const client = new PicoUartWebHid(hid);
  await client.connect();
  assert.equal(client.state.hardware.mcu, "RP2350");
  assert.equal(client.state.dashboard.board, null);
  hid.devices[0].close = async () => { hid.unplug(hid.devices[0]); throw new Error("Gone"); };
  await client.disconnect();
  assert.equal(client.state.phase, "disconnected");
  assert.equal(client.device, null);
});

test("identity and runtime validate version, ranges, padding and unsigned uptime", () => {
  assert.deepEqual(decodeIdentityReport(identityReport()),
    { product_name: "PicoUart", serial_number: "0123456789ABCDEF" });
  assert.deepEqual(decodeRuntimeReport(runtimeReport()), {
    uptime_ms: 4294967296, cpu_usage_basis_points: [10000, null],
    ram_reserved_bytes: 65536, ram_total_bytes: 270336,
  });
  for (const [id, makeReport, decode] of [
    [7, identityReport, decodeIdentityReport], [8, runtimeReport, decodeRuntimeReport],
  ]) {
    const value = makeReport();
    const padded = new Uint8Array(value.byteLength + 5);
    padded.set(new Uint8Array(value.buffer));
    assert.deepEqual(decode(new DataView(padded.buffer)), decode(value));
    padded[padded.length - 1] = 1;
    assert.throws(() => decode(new DataView(padded.buffer)), /padding/);
    assert.throws(() => decode(new DataView(new ArrayBuffer(1))), /size/);
    value.setUint8(1, 3);
    assert.throws(() => decode(value), /version/);
    value.setUint8(0, id + 1);
    assert.throws(() => decode(value), /ID/);
  }
  const identity = identityReport();
  identity.setUint8(34, 0x47);
  assert.throws(() => decodeIdentityReport(identity), /identity/);
  const runtime = runtimeReport();
  runtime.setUint16(10, 10001, true);
  assert.throws(() => decodeRuntimeReport(runtime), /CPU/);
  runtime.setUint16(10, 0, true);
  runtime.setUint32(18, 0, true);
  assert.throws(() => decodeRuntimeReport(runtime), /RAM/);
});

test("unsupported new reports retain legacy telemetry and clear stale identity/runtime", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  const device = hid.devices[0];
  const original = device.receiveFeatureReport.bind(device);
  device.receiveFeatureReport = async id => {
    if (id === 7 || id === 8) throw new Error("Firmware upgrade required");
    return original(id);
  };
  await client.refresh();
  assert.equal(client.state.dashboard.identity, null);
  assert.equal(client.state.dashboard.runtime, null);
  assert.equal(client.state.dashboard.identity_error, "Firmware upgrade required");
  assert.equal(client.state.dashboard.runtime_error, "Firmware upgrade required");
  assert.equal(client.state.dashboard.board.firmware_version, "1.2.3");
  assert.equal(client.state.dashboard.hardware.mcu, "RP2350");
  device.receiveFeatureReport = original;
  await client.refresh();
  assert.equal(client.state.dashboard.runtime_error, null);
  await client.disconnect();
  assert.equal(client.state.dashboard.identity, null);
  assert.equal(client.state.dashboard.runtime, null);
});

test("legacy runtime preserves counters without presenting fixed CPU values as service load", () => {
  const report = runtimeReport();
  report.setUint8(1, 1);
  const runtime = decodeRuntimeReport(report);
  assert.deepEqual(runtime.cpu_usage_basis_points, [null, null]);
  assert.equal(runtime.uptime_ms, 4294967296);
  assert.equal(runtime.ram_reserved_bytes, 65536);
});

test("LED decoder distinguishes physical activity from manual state and rejects invalid flags", () => {
  for (const [flags, manual_on, output_on] of [[0, null, null], [1, false, false],
    [5, false, true], [7, true, true]]) {
    assert.deepEqual(decodeLedReport(new DataView(Uint8Array.of(9, 1, flags).buffer)), { manual_on, output_on });
  }
  for (const flags of [2, 3, 4, 8, 255]) {
    assert.throws(() => decodeLedReport(new DataView(Uint8Array.of(9, 1, flags).buffer)));
  }
  assert.throws(() => decodeLedReport(new DataView(Uint8Array.of(9, 2, 1).buffer)));
});

test("LED toggle writes the command and refreshes actual output in both directions", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  assert.equal(client.state.dashboard.led.output_on, false);
  await client.toggleLed();
  assert.deepEqual(hid.devices[0].writes, [[4, [1]]]);
  assert.equal(client.state.dashboard.led.output_on, true);
  await client.toggleLed();
  assert.equal(client.state.dashboard.led.output_on, false);
});

test("LED write failure surfaces an error and unplug during write cannot restore stale state", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  hid.devices[0].sendFeatureReport = async () => { throw new Error("Write denied"); };
  await client.toggleLed();
  assert.equal(client.state.error, "Write denied");
  assert.equal(client.state.dashboard.led, null);
  const wait = deferred();
  hid.devices[0].sendFeatureReport = () => wait.promise;
  const toggling = client.toggleLed();
  hid.unplug(hid.devices[0]);
  wait.resolve();
  await toggling;
  assert.equal(client.state.phase, "disconnected");
  assert.equal(client.state.dashboard.led, null);
});

test("unplug during a runtime read cannot repopulate a disconnected dashboard", async () => {
  const hid = new FakeHid();
  const client = new PicoUartWebHid(hid);
  await client.connect();
  const device = hid.devices[0];
  const original = device.receiveFeatureReport.bind(device);
  const pending = deferred();
  const started = deferred();
  device.receiveFeatureReport = async id => {
    if (id !== 8) return original(id);
    started.resolve();
    return pending.promise;
  };
  const reading = client.refresh();
  await started.promise;
  hid.unplug(device);
  pending.resolve(runtimeReport());
  await reading;
  assert.equal(client.state.phase, "disconnected");
  assert.equal(client.state.dashboard.identity, null);
  assert.equal(client.state.dashboard.runtime, null);
});