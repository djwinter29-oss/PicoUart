export const DEVICE_FILTER = Object.freeze({
  vendorId: 0xcafe,
  productId: 0x4010,
  usagePage: 0xff00,
  usage: 0x01,
});

const TRAFFIC_FIELDS = ["uart_tx", "uart_rx", "usb_tx", "usb_rx"];
const HEALTH_LABELS = ["ready", "init_failed", "control_error", "control_pending",
  "cdc_open", "pio", "rx_overrun", "rx_error"];
const FAULT_NAMES = ["init_failed", "control_error", "rx_overrun", "rx_error"];
const UNAVAILABLE = 0xffffffff;

function featurePayload(report, reportId, size) {
  if (!(report instanceof DataView) || report.byteLength < size + 1 || report.getUint8(0) !== reportId) {
    throw new Error("Unexpected feature report ID or size");
  }
  for (let index = size + 1; index < report.byteLength; index++) {
    if (report.getUint8(index) !== 0) throw new Error("Unexpected feature report padding");
  }
  return new DataView(report.buffer, report.byteOffset + 1, size);
}

export function decodeDeviceInformation(report) {
  const data = featurePayload(report, 1, 63);
  if (data.getUint8(0) !== 1) throw new Error("Unsupported device-information version");
  const bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  const product = bytes.subarray(1, 33), end = product.indexOf(0);
  const serial = String.fromCharCode(...bytes.subarray(33, 49));
  if (end <= 0 || product.subarray(0, end).some(byte => byte < 32 || byte > 126)
    || product.subarray(end).some(byte => byte !== 0) || !/^[0-9a-f]{16}$/i.test(serial)) {
    throw new Error("Invalid HID product or serial identity");
  }
  const hw = data.getUint8(54), pio = data.getUint8(55), capabilities = data.getUint16(56, true);
  const profiles = data.getUint8(58), ram = data.getUint32(59, true);
  if (data.getUint8(53) !== 6 || (hw & pio) || (hw | pio) !== 63 || (capabilities & ~7)
    || !(profiles & 1) || (profiles & ~31) || !ram) throw new Error("Invalid static device capabilities");
  const mcuId = data.getUint8(52);
  return { product_name: String.fromCharCode(...product.subarray(0, end)), serial_number: serial,
    firmware_version: `${data.getUint8(49)}.${data.getUint8(50)}.${data.getUint8(51)}`,
    mcu_id: mcuId, mcu: { 1: "RP2040", 2: "RP2350" }[mcuId] ?? `Unknown MCU (${mcuId})`,
    channel_count: 6, hardware_channel_mask: hw, pio_channel_mask: pio,
    capabilities, clock_profile_mask: profiles, ram_total_bytes: ram };
}

export function decodeControlReport(report) {
  const data = featurePayload(report, 2, 24);
  if (data.getUint8(0) !== 1) throw new Error("Unsupported board-control version");
  const command = data.getUint8(1), flags = data.getUint8(2), status = data.getUint8(3);
  const requestId = data.getUint32(4, true), stored = data.getUint32(8, true);
  const active = data.getUint32(12, true), profile = data.getUint32(16, true);
  const source = data.getUint8(20), reason = data.getUint8(21), error = data.getUint8(22);
  const sources = ["defaults", "stored", "recovery"];
  const reasons = ["none", "erased", "invalid_header", "crc_failed", "unsupported_version",
    "invalid_profile", "watchdog_recovery", "clock_unavailable"];
  const expectedStatuses = [null, 3, 3, 3, 3, 4, 5, 6, 6, 6];
  if (status > 6 || error > 9 || data.getUint8(23) !== 0 || source > 2 || reason > 7
    || active > 4 || (stored > 4 && stored !== UNAVAILABLE) || (profile > 4 && profile !== UNAVAILABLE)
    || (source === 1 && (reason !== 0 || stored !== active)) || (source === 2 && reason === 0)
    || (status <= 2 ? error !== 0 : expectedStatuses[error] !== status)) {
    throw new Error("Invalid board-control result or boot configuration");
  }
  const unidentified = command === 0 && requestId === 0 && flags === 0 && profile === UNAVAILABLE;
  if (status === 0 ? !unidentified
    : !requestId ? !(unidentified && status === 3 && error === 1)
    : ((command < 1 || command > 5) && ![3, 4].includes(status))
      || ((command < 1 || command > 5) && ![1, 5].includes(error))
      || ((flags & ~1) && !(status === 3 && error === 1))
      || (command !== 5 && flags && ![1, 5].includes(error))
      || (command !== 5 && profile !== UNAVAILABLE)
      || (status <= 2 && command === 5 && (profile === UNAVAILABLE || (profile !== 0 && !(flags & 1))))) {
    throw new Error("Invalid echoed board-control fields");
  }
  return { command, flags, status, request_id: requestId, error_reason: error,
    profile: profile === UNAVAILABLE ? null : profile,
    configuration: { source: sources[source], reason: reasons[reason],
      stored_profile: stored === UNAVAILABLE ? null : stored, active_profile: active } };
}

export function decodeTelemetryReport(report, information) {
  const data = featurePayload(report, 3, 24);
  if (data.getUint8(0) !== 1) throw new Error("Unsupported telemetry version");
  const flags = data.getUint8(1), uptime = data.getBigUint64(4, true);
  const cpu = [data.getUint16(12, true), data.getUint16(14, true)];
  const reserved = data.getUint32(16, true), clock = data.getUint32(20, true);
  if ((flags & ~1) || uptime > BigInt(Number.MAX_SAFE_INTEGER) || !clock
    || cpu.some(value => value > 10000 && value !== 65535)
    || reserved > information.ram_total_bytes || (!(information.capabilities & 1) && flags)) {
    throw new Error("Invalid board telemetry");
  }
  const led = information.capabilities & 1 ? Boolean(flags & 1) : null;
  return { led: { manual_on: led, output_on: led },
    temperature_celsius: data.getInt16(2, true) / 100, system_clock_hz: clock,
    runtime: { uptime_ms: Number(uptime), cpu_usage_basis_points: cpu.map(v => v === 65535 ? null : v),
      ram_reserved_bytes: reserved, ram_total_bytes: information.ram_total_bytes } };
}

export function decodeChannelSnapshot(data, reportId) {
  if (reportId < 10 || reportId > 15 || !(data instanceof DataView) || data.byteLength !== 54) {
    throw new Error("Unexpected channel report ID or size");
  }
  if (data.getUint8(0) !== 1) throw new Error("Unsupported channel snapshot version");
  const health = data.getUint8(7), baud = data.getUint32(25, true);
  const bits = data.getUint8(29), stop = data.getUint8(30), parity = data.getUint8(31);
  const tx = data.getUint8(32), rx = data.getUint8(33);
  if (baud ? (baud < 50 || baud > 3000000 || bits < 5 || bits > 8 || ![1, 2].includes(stop) || parity > 2
    || ((health & 32) && (bits !== 8 || stop !== 1 || parity !== 0))) : (bits || stop || parity)) {
    throw new Error("Invalid applied UART framing");
  }
  if ([tx, rx].some(pin => pin > 47 && pin !== 255)) throw new Error("Invalid UART GPIO");
  const counts = FAULT_NAMES.map((_, index) => data.getUint32(34 + index * 4, true));
  const lastFault = data.getUint32(50, true);
  if ((!counts.some(Boolean) && lastFault) || Boolean(counts[2]) !== Boolean(health & 64)
    || Boolean(counts[3]) !== Boolean(health & 128)) throw new Error("Inconsistent channel fault history");
  return { id: reportId - 10, sequence: data.getUint16(1, true), interval_ms: data.getUint32(3, true),
    health, health_labels: HEALTH_LABELS.filter((_, bit) => health & (1 << bit)),
    cdc_open: Boolean(health & 16), ring_high_watermark: data.getUint8(8) * 16,
    traffic: Object.fromEntries(TRAFFIC_FIELDS.map((field, index) => [field, data.getUint32(9 + index * 4, true)])),
    baud_rate: baud, data_bits: bits, stop_bits: stop, parity: baud ? "NOE"[parity] : null,
    tx_pin: tx === 255 ? null : tx, rx_pin: rx === 255 ? null : rx,
    faults: Object.fromEntries(FAULT_NAMES.map((name, index) => [name, { count: counts[index] }])),
    last_fault_uptime_s: lastFault };
}

function emptyDashboard() {
  return { connected: false, error: null, metadata_error: null, hardware_error: null, hardware: null,
    configuration: null, configuration_error: null, configuration_save: null, configuration_save_error: null,
    identity: null, identity_error: null, runtime: null, runtime_error: null, led: null, led_error: null,
    channel_diagnostics: Array(6).fill(null), diagnostics_error: null, traffic_gaps: 0,
    board: null, overflow_counts: Array(6).fill(null), updated_at: null, traffic_incomplete: false,
    channels: Array.from({ length: 6 }, (_, id) => ({ id, health: null, health_labels: [], cdc_open: false,
      ring_high_watermark: 0, sequence: null, updated_at: null, observed_interval_ms: 0,
      traffic_gaps: 0, traffic_incomplete: false,
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
    this.information = null;
    this.nextRequestId = 1;
    this.ioBlocked = false;
    this.channelErrors = Array(6).fill(null);
    this.state = { phase: hid ? "disconnected" : "unsupported",
      message: hid ? "No device selected" : "WebHID is unavailable in this browser or context",
      error: null, configurationSaveMessage: "", hardware: null, deviceName: null, updatedAt: null,
      dashboard: emptyDashboard() };
    this.onInputReport = event => {
      if (event.device !== this.device || event.reportId < 10 || event.reportId > 15) return;
      try {
        this.applyChannel(decodeChannelSnapshot(event.data, event.reportId), true);
      } catch (error) {
        this.failChannel(event.reportId - 10, error.message);
      }
    };
    this.onDisconnect = event => {
      if (event.device !== this.device && event.device !== this.pendingDevice) return;
      this.generation++;
      event.device.removeEventListener("inputreport", this.onInputReport);
      this.device = null;
      this.pendingDevice = null;
      this.information = null;
      this.update({ phase: "disconnected", message: "Device unplugged", hardware: null, deviceName: null,
        updatedAt: null, error: null, configurationSaveMessage: "", dashboard: emptyDashboard() });
    };
    hid?.addEventListener("disconnect", this.onDisconnect);
  }

  update(changes) {
    this.state = { ...this.state, ...changes };
    this.onChange(this.state);
  }

  async transfer(operation, timeout = 3000) {
    if (this.ioBlocked) throw new Error("HID operation timed out; disconnect before continuing.");
    const generation = this.generation;
    let timer;
    try {
      const result = await Promise.race([operation(), new Promise((_, reject) => {
        timer = globalThis.setTimeout(() => {
          if (generation === this.generation) this.ioBlocked = true;
          reject(new Error("HID operation timed out; outcome is unconfirmed. Disconnect before continuing."));
        }, timeout);
      })]);
      if (generation !== this.generation) throw new Error("HID connection changed during operation");
      return result;
    } finally {
      globalThis.clearTimeout(timer);
    }
  }

  applyChannel(snapshot, input) {
    const dashboard = this.state.dashboard, id = snapshot.id;
    const previous = dashboard.channels[id];
    const channels = [...dashboard.channels], diagnostics = [...dashboard.channel_diagnostics];
    const overflow = [...dashboard.overflow_counts];
    this.channelErrors[id] = null;
    diagnostics[id] = snapshot;
    overflow[id] = snapshot.faults.rx_overrun.count;
    let gap = false;
    if (input) {
      const expired = previous.sequence !== null
        && (!previous.updated_at || Date.now() - Date.parse(previous.updated_at) > 3000);
      const first = previous.sequence === null || expired;
      const advance = first ? 1 : (snapshot.sequence - previous.sequence + 65536) % 65536;
      if (!first && (advance === 0 || advance >= 32768)) {
        channels[id] = { ...previous, traffic_gaps: previous.traffic_gaps + 1, traffic_incomplete: true };
        this.update({ dashboard: { ...dashboard, channels, traffic_gaps: dashboard.traffic_gaps + 1,
          traffic_incomplete: true } });
        return;
      }
      gap = expired || (!first && snapshot.sequence !== (previous.sequence + 1) % 65536);
      gap ||= snapshot.interval_ms === 0 || snapshot.interval_ms === UNAVAILABLE
      const traffic = Object.fromEntries(TRAFFIC_FIELDS.map(field => [field,
        first ? 0 : (snapshot.traffic[field] - previous.totals[field]) >>> 0]));
      channels[id] = { ...snapshot, updated_at: new Date().toISOString(),
        traffic, totals: { ...snapshot.traffic },
        observed_interval_ms: previous.observed_interval_ms + (first || gap ? 0 : snapshot.interval_ms),
        traffic_gaps: previous.traffic_gaps + Number(gap),
        traffic_incomplete: previous.traffic_incomplete || gap };
    } else if (previous.sequence === null) {
      // Feature deltas overlap the Input stream and never establish its sequence or freshness.
      channels[id] = { ...previous, health: snapshot.health, health_labels: snapshot.health_labels,
        cdc_open: snapshot.cdc_open, ring_high_watermark: snapshot.ring_high_watermark };
    }
    this.update({ dashboard: { ...dashboard, channels, channel_diagnostics: diagnostics, overflow_counts: overflow,
      error: null, diagnostics_error: this.channelErrors.find(Boolean) ?? null,
      updated_at: input ? channels[id].updated_at : dashboard.updated_at,
      traffic_gaps: dashboard.traffic_gaps + Number(gap), traffic_incomplete: dashboard.traffic_incomplete || gap } });
  }

  failChannel(id, message) {
    const dashboard = this.state.dashboard, channels = [...dashboard.channels];
    const diagnostics = [...dashboard.channel_diagnostics], overflow = [...dashboard.overflow_counts];
    this.channelErrors[id] = message;
    diagnostics[id] = null;
    overflow[id] = null;
    channels[id] = { ...channels[id], health: null, health_labels: [], updated_at: null,
      traffic_gaps: channels[id].traffic_gaps + 1, traffic_incomplete: true };
    this.update({ dashboard: { ...dashboard, channels, channel_diagnostics: diagnostics, overflow_counts: overflow,
      diagnostics_error: this.channelErrors.find(Boolean), traffic_gaps: dashboard.traffic_gaps + 1,
      traffic_incomplete: true } });
  }

  applyControl(result) {
    this.update({ dashboard: { ...this.state.dashboard, configuration: result.configuration,
      configuration_error: null,
      configuration_save: { status: result.command === 5 ? result.status : 0,
        profile: result.command === 5 ? result.profile : null },
      configuration_save_error: this.information?.capabilities & 4 ? null : "Configuration save unavailable" } });
  }

  async connect() {
    if (!this.hid || this.state.phase !== "disconnected") return;
    const generation = ++this.generation;
    this.update({ phase: "connecting", message: "Selecting device", error: null, configurationSaveMessage: "" });
    let selected;
    try {
      const devices = await this.hid.requestDevice({ filters: [{ ...DEVICE_FILTER }] });
      if (generation !== this.generation) return;
      if (!devices.length) {
        this.update({ phase: "disconnected", message: "No device selected. Connect PicoUart USB to this browser." });
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
      this.information = null;
      this.nextRequestId = 1;
      this.ioBlocked = false;
      this.channelErrors = Array(6).fill(null);
      selected.addEventListener("inputreport", this.onInputReport);
      this.update({ phase: "connected", deviceName: selected.productName || "PicoUart",
        message: "Device connected", dashboard: { ...emptyDashboard(), connected: true } });
      await this.refresh();
    } catch (error) {
      if (selected?.opened) {
        selected.removeEventListener("inputreport", this.onInputReport);
        try {
          await selected.close();
        } catch (closeError) {
          error = new Error(`${error.message}; ${closeError.message}`);
        }
      }
      if (generation !== this.generation) return;
      this.device = null;
      this.pendingDevice = null;
      this.update({ phase: "disconnected", message: "Connection failed", error: error.message,
        hardware: null, deviceName: null, updatedAt: null, dashboard: emptyDashboard() });
    }
  }

  async refresh() {
    const device = this.device, generation = this.generation;
    if (!device || this.state.phase !== "connected" || this.ioBlocked) return;
    this.update({ phase: "reading", message: "Reading diagnostics", error: null });
    const stale = () => generation !== this.generation || device !== this.device;
    try {
      if (!this.information) {
        this.information = decodeDeviceInformation(await this.transfer(() => device.receiveFeatureReport(1)));
        this.update({ dashboard: { ...this.state.dashboard, identity: this.information, identity_error: null } });
      }
    } catch (error) {
      if (stale()) return;
      this.update({ phase: "connected", error: error.message, dashboard: { ...this.state.dashboard,
        identity: null, identity_error: error.message, board: null, led: null, runtime: null } });
      return;
    }
    try {
      const telemetry = decodeTelemetryReport(
        await this.transfer(() => device.receiveFeatureReport(3)), this.information);
      const hardware = { mcu: this.information.mcu, mcu_id: this.information.mcu_id,
        system_clock_hz: telemetry.system_clock_hz, clock_profile_mask: this.information.clock_profile_mask };
      this.update({ hardware, updatedAt: new Date().toISOString(), dashboard: { ...this.state.dashboard,
        hardware, hardware_error: null, metadata_error: null, runtime_error: null, led_error: null,
        led: telemetry.led, runtime: telemetry.runtime,
        board: { firmware_version: this.information.firmware_version,
          temperature_celsius: telemetry.temperature_celsius,
          hid_reset_enabled: Boolean(this.information.capabilities & 2) } } });
    } catch (error) {
      if (stale()) return;
      this.update({ hardware: null, dashboard: { ...this.state.dashboard, hardware: null, hardware_error: error.message,
        board: null, metadata_error: error.message, runtime: null, runtime_error: error.message,
        led: null, led_error: error.message } });
    }
    try {
      if (!this.state.dashboard.configuration || this.state.dashboard.configuration_error) {
        this.applyControl(decodeControlReport(await this.transfer(() => device.receiveFeatureReport(2))));
      }
    } catch (error) {
      if (stale()) return;
      this.update({ dashboard: { ...this.state.dashboard, configuration: null, configuration_error: error.message,
        configuration_save: null, configuration_save_error: error.message } });
    }
    for (let id = 10; id <= 15 && !this.ioBlocked; id++) {
      if (this.state.dashboard.channel_diagnostics[id - 10]) continue;
      try {
        const data = featurePayload(await this.transfer(() => device.receiveFeatureReport(id)), id, 54);
        this.applyChannel(decodeChannelSnapshot(data, id), false);
      } catch (error) {
        if (stale()) return;
        this.failChannel(id - 10, error.message);
      }
    }
    if (stale()) return;
    this.update({ phase: "connected", message: "Diagnostics received",
      error: this.state.dashboard.identity_error ?? this.state.dashboard.metadata_error
        ?? this.state.dashboard.configuration_error ?? this.state.dashboard.diagnostics_error });
  }

  async command(command, profile = 0, flags = 0) {
    const device = this.device, generation = this.generation;
    if (!device || !this.information) throw new Error("Read device capabilities before sending commands.");
    const needed = command <= 2 ? 1 : command <= 4 ? 2 : 4;
    if (!(this.information.capabilities & needed)) throw new Error("Board control capability unavailable.");
    const latest = decodeControlReport(await this.transfer(() => device.receiveFeatureReport(2)));
    this.applyControl(latest);
    if (latest.status === 1 || (latest.status === 2 && [4, 5].includes(latest.command))) {
      throw new Error("A board operation is pending or awaiting reboot.");
    }
    if (this.nextRequestId === latest.request_id) this.nextRequestId++;
    if (this.nextRequestId > UNAVAILABLE) throw new Error("Request IDs exhausted; reconnect before continuing.");
    const id = this.nextRequestId++;
    const request = new Uint8Array(24), view = new DataView(request.buffer);
    view.setUint8(0, 1);
    view.setUint8(1, command);
    view.setUint8(2, flags);
    view.setUint32(4, id, true);
    view.setUint32(16, profile, true);
    await this.transfer(() => device.sendFeatureReport(2, request));
    const deadline = Date.now() + 3000;
    while (Date.now() < deadline) {
      if (generation !== this.generation || device !== this.device) {
        throw new Error("HID connection changed during command; outcome is unconfirmed.");
      }
      const result = decodeControlReport(await this.transfer(() => device.receiveFeatureReport(2),
        Math.max(1, deadline - Date.now())));
      this.applyControl(result);
      if (result.request_id !== id || result.command !== command || result.flags !== flags) {
        throw new Error("Board command result does not match; outcome is unconfirmed.");
      }
      if (result.status === 2) {
        if (command === 5 && result.profile !== profile) throw new Error("Firmware saved a different profile.");
        return result;
      }
      if (result.status !== 1) {
        throw new Error(`Board command rejected: status ${result.status}, reason ${result.error_reason}.`);
      }
      await new Promise(resolve => globalThis.setTimeout(resolve, 50));
    }
    throw new Error("Timed out awaiting board command; outcome is unconfirmed.");
  }

  async toggleLed() {
    if (!this.device || this.state.phase !== "connected") return;
    const generation = this.generation;
    this.update({ phase: "writing", message: "Setting board LED", error: null });
    try {
      if (this.state.dashboard.led?.output_on == null) throw new Error("Read LED state before changing it.");
      await this.command(this.state.dashboard.led.output_on ? 1 : 2);
      this.update({ phase: "connected" });
      await this.refresh();
    } catch (error) {
      if (generation !== this.generation) return;
      this.update({ phase: "connected", message: "LED command failed", error: error.message,
        dashboard: { ...this.state.dashboard, led: null, led_error: error.message } });
    }
  }

  clearConfigurationSaveMessage() {
    if (this.state.configurationSaveMessage) this.update({ configurationSaveMessage: "" });
  }

  async saveConfiguration(profile, unsafeOverclock) {
    if (!this.device || this.state.phase !== "connected") return;
    const generation = this.generation;
    this.update({ phase: "writing", message: "Saving persistent configuration",
      configurationSaveMessage: "Saving configuration...", error: null });
    try {
      if (!Number.isInteger(profile) || profile < 0 || profile > 4
        || !(this.information?.clock_profile_mask & (1 << profile))
        || typeof unsafeOverclock !== "boolean" || (profile !== 0 && !unsafeOverclock)
        || (profile === 0 && unsafeOverclock)) {
        throw new Error("Select a supported profile and acknowledge overclocking.");
      }
      if (this.state.dashboard.channels.some(channel => channel.health === null || channel.cdc_open)) {
        throw new Error("Close all CDC ports and stop UART traffic before saving.");
      }
      await this.command(5, profile, unsafeOverclock ? 1 : 0);
      this.update({ phase: "connected", message: "Configuration saved",
        configurationSaveMessage: "Saved. Pico is rebooting; USB will reconnect shortly." });
    } catch (error) {
      if (generation !== this.generation) return;
      this.update({ phase: "connected", message: "Configuration save failed", error: null,
        configurationSaveMessage: error.message });
    }
  }

  async disconnect() {
    if (!this.hid) return;
    const device = this.device ?? this.pendingDevice, generation = ++this.generation;
    this.device = null;
    this.pendingDevice = device;
    device?.removeEventListener("inputreport", this.onInputReport);
    this.update({ phase: "disconnecting", message: "Closing device" });
    try {
      if (device?.opened) await device.close();
      if (generation !== this.generation) return;
      this.pendingDevice = null;
      this.information = null;
      this.update({ phase: "disconnected", message: "Device disconnected", hardware: null,
        deviceName: null, updatedAt: null, error: null, configurationSaveMessage: "", dashboard: emptyDashboard() });
    } catch (error) {
      if (generation !== this.generation) return;
      this.device = device;
      this.pendingDevice = null;
      device?.addEventListener("inputreport", this.onInputReport);
      this.update({ phase: "connected", message: "Disconnect failed", error: error.message });
    }
  }
}
