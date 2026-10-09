export const DEVICE_FILTER = Object.freeze({
  vendorId: 0xcafe,
  productId: 0x4010,
  usagePage: 0xff00,
  usage: 0x01,
});

function featurePayload(report, reportId, size) {
  if (!(report instanceof DataView) || report.byteLength < size + 1 || report.getUint8(0) !== reportId) {
    throw new Error("Unexpected feature report ID or size");
  }
  for (let index = size + 1; index < report.byteLength; index++) {
    if (report.getUint8(index) !== 0) throw new Error("Unexpected hardware-info report padding");
  }
  return new DataView(report.buffer, report.byteOffset + 1, size);
}

export function decodeHardwareReport(report) {
  const payload = featurePayload(report, 6, 6);
  if (payload.getUint8(0) !== 1) throw new Error("Unsupported hardware-info layout version");
  const mcuId = payload.getUint8(1);
  const clockHz = payload.getUint32(2, true);
  if (clockHz === 0) throw new Error("Invalid hardware-info system clock");
  return {
    mcu: { 1: "RP2040", 2: "RP2350" }[mcuId] ?? `Unknown MCU (${mcuId})`,
    mcu_id: mcuId,
    system_clock_hz: clockHz,
  };
}

export function decodeBoardReport(report) {
  const payload = featurePayload(report, 3, 8);
  if (payload.getUint8(0) !== 15) throw new Error("Unsupported board-status layout version");
  if ((payload.getUint8(1) & ~1) !== 0 || payload.getUint8(7) !== 0) {
    throw new Error("Unknown board-status flags");
  }
  return { temperature_celsius: payload.getInt16(2, true) / 100,
    firmware_version: `${payload.getUint8(4)}.${payload.getUint8(5)}.${payload.getUint8(6)}`,
    hid_reset_enabled: Boolean(payload.getUint8(1) & 1) };
}

export function decodeOverflowReport(report) {
  const payload = featurePayload(report, 5, 25);
  if (payload.getUint8(0) !== 15) throw new Error("Unsupported overflow-count layout version");
  return Array.from({ length: 6 }, (_, index) => payload.getUint32(1 + index * 4, true));
}

const TRAFFIC_FIELDS = ["uart_tx", "uart_rx", "usb_tx", "usb_rx"];
const HEALTH_LABELS = ["ready", "init_failed", "control_error", "control_pending",
  "cdc_open", "pio", "rx_overrun", "rx_error"];

export function decodeStatusReport(data) {
  if (!(data instanceof DataView) || data.byteLength !== 63 || data.getUint8(0) !== 0x50) {
    throw new Error("Unexpected status report signature or size");
  }
  if (data.getUint8(1) !== 15) throw new Error("Unsupported status layout version");
  return { sequence: data.getUint8(2), channels: Array.from({ length: 6 }, (_, index) => {
    const offset = 3 + index * 10;
    const health = data.getUint8(offset);
    return { id: index, health,
      health_labels: HEALTH_LABELS.filter((_, bit) => health & (1 << bit)),
      state: health & 0xc6 ? "attention" : health & 1 ? "ready" : "initializing",
      backend: health & 0x20 ? "PIO" : "Hardware", cdc_open: Boolean(health & 0x10),
      ring_high_watermark: data.getUint8(offset + 1) * 16,
      traffic: Object.fromEntries(TRAFFIC_FIELDS.map((field, direction) =>
        [field, data.getUint16(offset + 2 + direction * 2, true)])) };
  }) };
}

function emptyDashboard() {
  return { connected: false, error: null, metadata_error: null, hardware_error: null, hardware: null,
    board: null, overflow_counts: Array(6).fill(null), sequence: null, updated_at: null, traffic_incomplete: false,
    channels: Array.from({ length: 6 }, (_, id) => ({ id, health: null, health_labels: [], state: "unknown",
      backend: "unknown", cdc_open: false, ring_high_watermark: 0,
      traffic: Object.fromEntries(TRAFFIC_FIELDS.map(field => [field, 0])),
      totals: Object.fromEntries(TRAFFIC_FIELDS.map(field => [field, 0])) })) };
}

export function matchesPicoUart(device) {
  return device.vendorId === DEVICE_FILTER.vendorId && device.productId === DEVICE_FILTER.productId
    && device.collections.some(collection => collection.usagePage === DEVICE_FILTER.usagePage
      && collection.usage === DEVICE_FILTER.usage);
}

export class PicoUartWebHid {
  constructor(hid, onChange = () => {}) {
    this.hid = hid;
    this.onChange = onChange;
    this.device = null;
    this.pendingDevice = null;
    this.generation = 0;
    this.state = {
      phase: hid ? "disconnected" : "unsupported",
      message: hid ? "No device selected" : "WebHID is unavailable in this browser or context",
      error: null,
      hardware: null,
      deviceName: null,
      updatedAt: null,
      dashboard: emptyDashboard(),
    };
    this.onInputReport = event => {
      if (event.device !== this.device || event.reportId !== 1) return;
      try {
        const status = decodeStatusReport(event.data);
        const previous = this.state.dashboard;
        let incomplete = previous.traffic_incomplete
          || (previous.sequence !== null && status.sequence !== (previous.sequence + 1) % 256);
        const channels = status.channels.map(channel => {
          if (Object.values(channel.traffic).includes(65535)) incomplete = true;
          return { ...channel, totals: Object.fromEntries(TRAFFIC_FIELDS.map(field =>
            [field, previous.channels[channel.id].totals[field] + channel.traffic[field]])) };
        });
        this.update({ dashboard: { ...previous, connected: true, error: null, channels,
          sequence: status.sequence, updated_at: new Date().toISOString(), traffic_incomplete: incomplete } });
      } catch (error) {
        this.update({ dashboard: { ...this.state.dashboard, error: error.message } });
      }
    };
    this.onDisconnect = event => {
      if (event.device !== this.device && event.device !== this.pendingDevice) return;
      this.generation++;
      this.device = null;
      this.pendingDevice = null;
      event.device.removeEventListener("inputreport", this.onInputReport);
      this.update({ phase: "disconnected", message: "Device unplugged", hardware: null,
        deviceName: null, updatedAt: null, error: null, dashboard: emptyDashboard() });
    };
    hid?.addEventListener("disconnect", this.onDisconnect);
  }

  update(changes) {
    this.state = { ...this.state, ...changes };
    this.onChange(this.state);
  }

  async connect() {
    if (!this.hid || this.state.phase !== "disconnected") return;
    const generation = ++this.generation;
    this.update({ phase: "connecting", message: "Selecting device", error: null });
    let selected;
    try {
      const devices = await this.hid.requestDevice({ filters: [{ ...DEVICE_FILTER }] });
      if (generation !== this.generation) return;
      if (devices.length === 0) {
        this.update({ phase: "disconnected",
          message: "No device selected. Connect PicoUart USB to the computer running this browser; no password is required." });
        return;
      }
      if (devices.length !== 1 || !matchesPicoUart(devices[0])) {
        throw new Error("Select one PicoUart vendor HID interface");
      }
      selected = devices[0];
      this.pendingDevice = selected;
      if (!selected.opened) await selected.open();
      if (generation !== this.generation) {
        if (selected.opened) await selected.close();
        return;
      }
      this.pendingDevice = null;
      this.device = selected;
      selected.addEventListener("inputreport", this.onInputReport);
      this.update({ phase: "connected", deviceName: selected.productName || "PicoUart",
        message: "Device connected", dashboard: { ...emptyDashboard(), connected: true } });
      await this.refresh();
    } catch (error) {
      if (selected?.opened) {
        selected.removeEventListener("inputreport", this.onInputReport);
        try { await selected.close(); } catch { }
      }
      if (generation !== this.generation) return;
      this.device = null;
      this.pendingDevice = null;
      this.update({ phase: "disconnected", message: "Connection failed", error: error.message,
        hardware: null, deviceName: null, updatedAt: null, dashboard: emptyDashboard() });
    }
  }

  async refresh() {
    const device = this.device;
    if (!device || this.state.phase !== "connected") return;
    const generation = this.generation;
    this.update({ phase: "reading", message: "Reading diagnostics", error: null });
    try {
      const hardware = decodeHardwareReport(await device.receiveFeatureReport(6));
      if (generation !== this.generation || device !== this.device) return;
      this.update({ hardware, updatedAt: new Date().toISOString(),
        dashboard: { ...this.state.dashboard, hardware, hardware_error: null } });
    } catch (error) {
      if (generation !== this.generation || device !== this.device) return;
      this.update({ hardware: null, updatedAt: null,
        dashboard: { ...this.state.dashboard, hardware: null, hardware_error: error.message } });
    }
    try {
      const board = decodeBoardReport(await device.receiveFeatureReport(3));
      if (generation !== this.generation || device !== this.device) return;
      const overflows = decodeOverflowReport(await device.receiveFeatureReport(5));
      if (generation !== this.generation || device !== this.device) return;
      this.update({ dashboard: { ...this.state.dashboard, board, overflow_counts: overflows, metadata_error: null } });
    } catch (error) {
      if (generation !== this.generation || device !== this.device) return;
      this.update({ dashboard: { ...this.state.dashboard, board: null, overflow_counts: Array(6).fill(null),
        metadata_error: error.message } });
    }
    this.update({ phase: "connected", message: "Diagnostics received",
      error: this.state.dashboard.hardware_error ?? this.state.dashboard.metadata_error });
  }

  async disconnect() {
    if (!this.hid) return;
    const device = this.device ?? this.pendingDevice;
    const generation = ++this.generation;
    this.device = null;
    this.pendingDevice = device;
    device?.removeEventListener("inputreport", this.onInputReport);
    this.update({ phase: "disconnecting", message: "Closing device" });
    try {
      if (device?.opened) await device.close();
      if (generation !== this.generation) return;
      this.pendingDevice = null;
      this.update({ phase: "disconnected", message: "Device disconnected", hardware: null,
        deviceName: null, updatedAt: null, error: null, dashboard: emptyDashboard() });
    } catch (error) {
      if (generation !== this.generation) return;
      this.device = device;
      this.pendingDevice = null;
      device?.addEventListener("inputreport", this.onInputReport);
      this.update({ phase: "connected", message: "Disconnect failed", error: error.message });
    }
  }
}