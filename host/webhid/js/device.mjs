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
    if (report.getUint8(index) !== 0) throw new Error("Unexpected feature report padding");
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

export function decodeConfigurationReport(report) {
  const payload = featurePayload(report, 16, 12);
  const source = payload.getUint8(1), reason = payload.getUint8(2);
  const stored = payload.getUint32(4, true), active = payload.getUint32(8, true);
  const sources = ["defaults", "stored", "recovery"];
  const reasons = ["none", "erased", "invalid_header", "crc_failed", "unsupported_version",
    "invalid_profile", "watchdog_recovery", "clock_unavailable"];
  if (payload.getUint8(0) !== 1) throw new Error("Unsupported configuration layout version");
  if (source >= sources.length || reason >= reasons.length || payload.getUint8(3) !== 0) {
    throw new Error("Invalid configuration source, reason, or reserved field");
  }
  if (![0, 1, 2, 3, 4, 0xffffffff].includes(stored) || active > 4) {
    throw new Error("Invalid configuration profile");
  }
  if ((source === 1 && (reason !== 0 || ![0, 1, 2, 3, 4].includes(stored) || active !== stored))
    || (source === 2 && reason === 0)) throw new Error("Inconsistent configuration values");
  return { source: sources[source], reason: reasons[reason],
    stored_profile: stored === 0xffffffff ? null : stored, active_profile: active };
}

export function decodeConfigurationSaveReport(report) {
  const payload = featurePayload(report, 17, 8);
  const status = payload.getUint8(1);
  const profile = payload.getUint32(4, true);
  if (payload.getUint8(0) !== 1) throw new Error("Unsupported configuration-save layout version");
  if (status > 5 || payload.getUint8(2) !== 0 || payload.getUint8(3) !== 0) {
    throw new Error("Invalid configuration-save status or reserved field");
  }
  if (status === 0) {
    if (profile !== 0xffffffff) throw new Error("Invalid idle configuration-save profile");
    return { status, profile: null };
  }
  if (status === 3) {
    if (profile !== 0xffffffff) throw new Error("Invalid rejected configuration-save profile");
    return { status, profile: null };
  }
  if (![0, 1, 2, 3, 4].includes(profile)) {
    throw new Error("Invalid configuration-save profile");
  }
  return { status, profile };
}

export function decodeBoardReport(report) {
  const payload = featurePayload(report, 3, 8);
  if (payload.getUint8(0) !== 1) throw new Error("Unsupported board-status layout version");
  if ((payload.getUint8(1) & ~1) !== 0 || payload.getUint8(7) !== 0) {
    throw new Error("Unknown board-status flags");
  }
  return { temperature_celsius: payload.getInt16(2, true) / 100,
    firmware_version: `${payload.getUint8(4)}.${payload.getUint8(5)}.${payload.getUint8(6)}`,
    hid_reset_enabled: Boolean(payload.getUint8(1) & 1) };
}

export function decodeIdentityReport(report) {
  const payload = featurePayload(report, 7, 49);
  if (payload.getUint8(0) !== 1) throw new Error("Unsupported identity layout version");
  const bytes = new Uint8Array(payload.buffer, payload.byteOffset, payload.byteLength);
  const product = bytes.subarray(1, 33);
  const end = product.indexOf(0);
  const serial = String.fromCharCode(...bytes.subarray(33, 49));
  if (end <= 0 || product.subarray(0, end).some(byte => byte < 32 || byte > 126)
    || product.subarray(end).some(byte => byte !== 0) || !/^[0-9a-f]{16}$/i.test(serial)) {
    throw new Error("Invalid HID product or serial identity");
  }
  return { product_name: String.fromCharCode(...product.subarray(0, end)), serial_number: serial };
}

export function decodeRuntimeReport(report) {
  const payload = featurePayload(report, 8, 21);
  const version = payload.getUint8(0);
  if (version !== 1) throw new Error("Unsupported runtime layout version");
  const uptime = payload.getBigUint64(1, true);
  const cpu = [payload.getUint16(9, true), payload.getUint16(11, true)];
  const reserved = payload.getUint32(13, true);
  const total = payload.getUint32(17, true);
  if (uptime > BigInt(Number.MAX_SAFE_INTEGER) || cpu.some(value => value > 10000 && value !== 65535)
    || !total || reserved > total) throw new Error("Invalid runtime uptime, CPU or RAM values");
  return { uptime_ms: Number(uptime),
    cpu_usage_basis_points: cpu.map(value => value === 65535 ? null : value),
    ram_reserved_bytes: reserved, ram_total_bytes: total };
}

export function decodeOverflowReport(report) {
  const payload = featurePayload(report, 5, 25);
  if (payload.getUint8(0) !== 1) throw new Error("Unsupported overflow-count layout version");
  return Array.from({ length: 6 }, (_, index) => payload.getUint32(1 + index * 4, true));
}

const TRAFFIC_FIELDS = ["uart_tx", "uart_rx", "usb_tx", "usb_rx"];
const HEALTH_LABELS = ["ready", "init_failed", "control_error", "control_pending",
  "cdc_open", "pio", "rx_overrun", "rx_error"];

export function decodeChannelDiagnostics(report, reportId) {
  const data = featurePayload(report, reportId, 59);
  if (data.getUint8(0) !== 1) throw new Error("Unsupported channel diagnostics version");
  const baud = data.getUint32(2, true);
  const bits = data.getUint8(6), stop = data.getUint8(7), parity = data.getUint8(8);
  const tx = data.getUint8(9), rx = data.getUint8(10);
  if (baud && (bits < 5 || bits > 8 || ![1, 2].includes(stop) || parity > 2)) {
    throw new Error("Invalid applied UART framing");
  }
  if ([tx, rx].some(pin => pin > 47 && pin !== 255)) throw new Error("Invalid UART GPIO");
  const faults = Object.fromEntries(["init_failed", "control_error", "rx_overrun", "rx_error"].map((name, index) => {
    const count = data.getUint32(11 + index * 12, true);
    const seen = data.getBigUint64(15 + index * 12, true);
    if (seen > BigInt(Number.MAX_SAFE_INTEGER) || (!count && seen !== 0n)) {
      throw new Error("Invalid channel fault timestamp");
    }
    return [name, { count, last_seen_ms: count ? Number(seen) : null }];
  }));
  return { health: data.getUint8(1), baud_rate: baud, data_bits: bits, stop_bits: stop,
    parity: baud ? "NOE"[parity] : null, tx_pin: tx === 255 ? null : tx, rx_pin: rx === 255 ? null : rx, faults };
}

export function decodeStatusReport(data) {
  if (!(data instanceof DataView) || data.byteLength !== 63 || data.getUint8(0) !== 0x50) {
    throw new Error("Unexpected status report signature or size");
  }
  if (data.getUint8(1) !== 1) throw new Error("Unsupported status layout version");
  return { sequence: data.getUint8(2), channels: Array.from({ length: 6 }, (_, index) => {
    const offset = 3 + index * 10;
    const health = data.getUint8(offset);
    return { id: index, health,
      health_labels: HEALTH_LABELS.filter((_, bit) => health & (1 << bit)),
      state: health & 0xc6 ? "attention" : health & 1 ? "ready" : "initializing",
      cdc_open: Boolean(health & 0x10),
      ring_high_watermark: data.getUint8(offset + 1) * 16,
      traffic: Object.fromEntries(TRAFFIC_FIELDS.map((field, direction) =>
        [field, data.getUint16(offset + 2 + direction * 2, true)])) };
  }) };
}

export function decodeLedReport(value) {
  const payload = featurePayload(value, 9, 2);
  const flags = payload.getUint8(1);
  if (payload.getUint8(0) !== 1 || (flags & ~7)) throw new Error("Unsupported LED version or flags");
  if ((!(flags & 1) && flags !== 0) || ((flags & 2) && !(flags & 4))) {
    throw new Error("Inconsistent LED state");
  }
  return { manual_on: flags & 1 ? Boolean(flags & 2) : null,
    output_on: flags & 1 ? Boolean(flags & 4) : null };
}

function emptyDashboard() {
  return { connected: false, error: null, metadata_error: null, hardware_error: null, hardware: null,
    configuration: null, configuration_error: null,
    configuration_save: null, configuration_save_error: null,
    identity: null, identity_error: null, runtime: null, runtime_error: null,
    led: null, led_error: null, channel_diagnostics: null, diagnostics_error: null, traffic_gaps: 0,
    board: null, overflow_counts: Array(6).fill(null), sequence: null, updated_at: null, traffic_incomplete: false,
    channels: Array.from({ length: 6 }, (_, id) => ({ id, health: null, health_labels: [], state: "unknown",
      cdc_open: false, ring_high_watermark: 0,
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
      configurationSaveMessage: "",
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
        let gap = previous.sequence !== null && status.sequence !== (previous.sequence + 1) % 256;
        const channels = status.channels.map(channel => {
          if (Object.values(channel.traffic).includes(65535)) gap = true;
          return { ...channel, totals: Object.fromEntries(TRAFFIC_FIELDS.map(field =>
            [field, previous.channels[channel.id].totals[field] + channel.traffic[field]])) };
        });
        this.update({ dashboard: { ...previous, connected: true, error: null, channels,
          sequence: status.sequence, updated_at: new Date().toISOString(),
          traffic_incomplete: previous.traffic_incomplete || gap, traffic_gaps: previous.traffic_gaps + Number(gap) } });
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
        deviceName: null, updatedAt: null, error: null, configurationSaveMessage: "",
        dashboard: emptyDashboard() });
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
    this.update({ phase: "connecting", message: "Selecting device", error: null,
      configurationSaveMessage: "" });
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
      const configuration = decodeConfigurationReport(await device.receiveFeatureReport(16));
      if (generation !== this.generation || device !== this.device) return;
      this.update({ dashboard: { ...this.state.dashboard, configuration, configuration_error: null } });
    } catch (error) {
      if (generation !== this.generation || device !== this.device) return;
      this.update({ dashboard: { ...this.state.dashboard, configuration: null,
        configuration_error: error.message } });
    }
    try {
      const configurationSave = decodeConfigurationSaveReport(await device.receiveFeatureReport(17));
      if (generation !== this.generation || device !== this.device) return;
      this.update({ dashboard: { ...this.state.dashboard, configuration_save: configurationSave,
        configuration_save_error: null } });
    } catch (error) {
      if (generation !== this.generation || device !== this.device) return;
      this.update({ dashboard: { ...this.state.dashboard, configuration_save: null,
        configuration_save_error: error.message } });
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
    for (const [field, id, decode] of [
      ["identity", 7, decodeIdentityReport], ["runtime", 8, decodeRuntimeReport],
      ["led", 9, decodeLedReport],
    ]) {
      try {
        const value = decode(await device.receiveFeatureReport(id));
        if (generation !== this.generation || device !== this.device) return;
        this.update({ dashboard: { ...this.state.dashboard, [field]: value, [`${field}_error`]: null } });
      } catch (error) {
        if (generation !== this.generation || device !== this.device) return;
        this.update({ dashboard: { ...this.state.dashboard, [field]: null, [`${field}_error`]: error.message } });
      }
    }
    try {
      const diagnostics = [];
      for (let index = 0; index < 6; index++) {
        diagnostics.push(decodeChannelDiagnostics(await device.receiveFeatureReport(10 + index), 10 + index));
        if (generation !== this.generation || device !== this.device) return;
      }
      this.update({ dashboard: { ...this.state.dashboard, channel_diagnostics: diagnostics, diagnostics_error: null } });
    } catch (error) {
      if (generation !== this.generation || device !== this.device) return;
      this.update({ dashboard: { ...this.state.dashboard, channel_diagnostics: null, diagnostics_error: error.message } });
    }
    this.update({ phase: "connected", message: "Diagnostics received",
      error: this.state.dashboard.hardware_error ?? this.state.dashboard.metadata_error
        ?? this.state.dashboard.configuration_error ?? this.state.dashboard.identity_error ?? this.state.dashboard.runtime_error ?? this.state.dashboard.led_error });
  }

  async toggleLed() {
    const device = this.device;
    if (!device || this.state.phase !== "connected") return;
    const generation = this.generation;
    this.update({ phase: "writing", message: "Toggling board LED", error: null });
    try {
      await device.sendFeatureReport(4, Uint8Array.of(1));
      if (generation !== this.generation || device !== this.device) return;
      this.update({ phase: "connected" });
      await this.refresh();
    } catch (error) {
      if (generation !== this.generation || device !== this.device) return;
      this.update({ phase: "connected", message: "LED toggle failed", error: error.message,
        dashboard: { ...this.state.dashboard, led: null, led_error: error.message } });
    }
  }

  clearConfigurationSaveMessage() {
    if (this.state.configurationSaveMessage) this.update({ configurationSaveMessage: "" });
  }

  async saveConfiguration(profile, unsafeOverclock) {
    const device = this.device;
    const hardware = this.state.dashboard.hardware;
    if (!device || this.state.phase !== "connected") return;
    if (this.state.dashboard.configuration_save_error || !this.state.dashboard.configuration_save) {
      this.update({ configurationSaveMessage: "Persistent configuration save is unavailable on this firmware." });
      return;
    }
    if (this.state.dashboard.configuration_save.status === 1) {
      this.update({ configurationSaveMessage: "A configuration save is already pending." });
      return;
    }
    if (hardware?.mcu_id !== 1 && hardware?.mcu_id !== 2) {
      this.update({ configurationSaveMessage: "Cannot save a clock profile for an unknown microcontroller." });
      return;
    }
    const expectedProfile = hardware.mcu_id === 1 ? 1 : 2;
    if (![0, expectedProfile, 3, 4].includes(profile) || typeof unsafeOverclock !== "boolean"
      || (profile === 0 && unsafeOverclock) || (profile !== 0 && !unsafeOverclock)) {
      this.update({ configurationSaveMessage: "Select a valid profile and explicitly acknowledge overclocking." });
      return;
    }
    if (this.state.dashboard.channels.length !== 6
      || this.state.dashboard.channels.some(channel => channel.cdc_open || channel.health === null)) {
      this.update({ configurationSaveMessage: "Close all CDC ports and stop UART traffic; "
        + "wait for every channel to report closed." });
      return;
    }
    const generation = this.generation;
    this.update({ phase: "writing", message: "Saving persistent configuration", error: null,
      configurationSaveMessage: "Saving configuration..." });
    try {
      const request = new Uint8Array(8);
      const requestView = new DataView(request.buffer);
      requestView.setUint8(0, 1);
      requestView.setUint8(1, unsafeOverclock ? 1 : 0);
      requestView.setUint32(4, profile, true);
      await device.sendFeatureReport(17, request);
      if (generation !== this.generation || device !== this.device) return;
      const deadline = Date.now() + 3000;
      let result;
      do {
        result = decodeConfigurationSaveReport(await device.receiveFeatureReport(17));
        if (generation !== this.generation || device !== this.device) return;
        if (result.status !== 1) break;
        if (Date.now() >= deadline) throw new Error("Timed out waiting for configuration save.");
        await new Promise(resolve => globalThis.setTimeout(resolve, 50));
        if (generation !== this.generation || device !== this.device) return;
      } while (Date.now() < deadline);
      if (result.status === 1) throw new Error("Timed out waiting for configuration save.");
      if (result.status !== 2) {
        const messages = {
          0: "Configuration save did not start.",
          3: "Firmware rejected the configuration request.",
          4: "Close all CDC ports and stop UART traffic before saving.",
          5: "Firmware could not save configuration to flash.",
        };
        throw new Error(messages[result.status] ?? "Unexpected configuration-save status.");
      }
      if (result.profile !== profile) throw new Error("Firmware saved a different configuration profile.");
      this.update({ phase: "connected", message: "Configuration saved",
        configurationSaveMessage: "Saved. Pico is rebooting; USB will reconnect shortly.",
        dashboard: { ...this.state.dashboard, configuration_save: result } });
    } catch (error) {
      if (generation !== this.generation || device !== this.device) return;
      this.update({ phase: "connected", message: "Configuration save failed", error: null,
        configurationSaveMessage: error.message });
    }
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
        deviceName: null, updatedAt: null, error: null, configurationSaveMessage: "",
        dashboard: emptyDashboard() });
    } catch (error) {
      if (generation !== this.generation) return;
      this.device = device;
      this.pendingDevice = null;
      device?.addEventListener("inputreport", this.onInputReport);
      this.update({ phase: "connected", message: "Disconnect failed", error: error.message });
    }
  }
}