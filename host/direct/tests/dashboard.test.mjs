// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const html = readFileSync(new URL("../../web/src/index.html", import.meta.url), "utf8");
const script = readFileSync(new URL("../../web/src/js/dashboard.js", import.meta.url), "utf8");
test("host versions belong to the wordmark rather than the page heading", () => {
  for (const path of ["../../web/src/index.html", "../src/index.html"]) {
    const page = readFileSync(new URL(path, import.meta.url), "utf8");
    assert.match(page, /class="wordmark"[\s\S]*?id="host-version"[\s\S]*?<\/a>/);
    assert.match(page, /<strong>PicoUart<\/strong>\s*<span class="brand-meta"><small>[^<]+<\/small>\s*<span class="host-version"/);
    assert.doesNotMatch(page.split("<main>")[1], /id="host-version"/);
  }
});

const catalog = {
  boards: [
    { id: "A", product_name: "PicoUart", serial_number: "first" },
    { id: "B", product_name: "<board>", serial_number: "second" },
  ],
  default_board_id: "A",
};

test("board controls put Rescan before the accessible selector and LED button without a visible label", () => {
  assert.match(html, /id="rescan-boards"[\s\S]*?<select id="board-select"[\s\S]*?id="toggle-led"/);
  assert.match(html, /<select id="board-select" aria-label="Board attached to this server"/);
  assert.doesNotMatch(html, /<label[^>]*for="board-select"/);
});

class Element {
  constructor() {
    this.children = [];
    this.dataset = {};
    this.events = {};
    this.parentElement = { dataset: {} };
    this.text = "";
    this.value = "";
    this.checked = false;
    this.disabled = false;
    this.open = false;
  }
  set textContent(value) { this.text = value; this.children = []; }
  get textContent() { return this.text + this.children.map(child => child.textContent).join(""); }
  get options() { return this.children; }
  get selectedOptions() { return this.children.filter(child => child.value === this.value); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  close() { this.open = false; }
  showModal() { this.open = true; }
  querySelector() { return this.heading ??= new Element(); }
  addEventListener(event, callback) { this.events[event] = callback; }
}

function snapshot(serial, mcuId = 2, storedProfile = 2) {
  return { connected: true, identity: { product_name: "PicoUart", serial_number: serial,
    capabilities: 7, clock_profile_mask: mcuId === 1 ? 15 : 31, mcu_id: mcuId,
    board_model_id: mcuId === 1 ? 1 : 3, firmware_version: "1.2.3", ram_total_bytes: 270336,
    ram_reserved_bytes: 1024 },
    updated_at: "2026-01-01T00:00:00Z", traffic_incomplete: false,
    hardware: { mcu: mcuId === 1 ? "RP2040" : "RP2350", mcu_id: mcuId, system_clock_hz: 150000000 },
    runtime: { uptime_ms: 3723000, service_load_basis_points: [2500, 500], system_clock_hz: 150000000 },
    configuration: { stored_profile: storedProfile, reset_cause: 1,
      uart_tx_ring_capacity_bytes: 2048, uart_rx_ring_capacity_bytes: 2048,
      cdc_tx_fifo_capacity_bytes: 1024, cdc_rx_fifo_capacity_bytes: 1024,
      channel_pins: Array.from({ length: 6 }, () => ({ tx_pin: 0, rx_pin: 1, rts_pin: 255, cts_pin: 255 })) },
    channels: Array.from({ length: 6 }, (_, id) => ({ id, state: "ready", uart_measurement_state: 1,
      cdc_open: true, baud_rate: 115200, data_bits: 8, parity: "none", stop_bits: 1,
      sequence: 1, interval_ms: 500, observed_interval_ms: 0, traffic_gaps: 0, traffic_incomplete: false,
      totals: { uart_tx: 100 + id, uart_rx: 200 + id, usb_tx: 300 + id, usb_rx: 400 + id },
      traffic: { uart_tx: null, uart_rx: null, usb_tx: null, usb_rx: null } })),
    led: { manual_on: false, output_on: false } };
}

function page() {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [`#${id}`, new Element()]));
  ["SDK default", "150 MHz", "200 MHz", "250 MHz", "280 MHz"].forEach((label, value) => {
    const option = new Element();
    option.value = String(value);
    option.textContent = label;
    elements.get("#clock-profile").append(option);
  });
  const requests = [];
  let interval;
  let intervalMs;
  let ageInterval;
  let intervalCalls = 0;
  let timeoutId = 0;
  const timeouts = new Map();
  const context = vm.createContext({
    document: {
      querySelector: selector => selector.startsWith("meta") ? { content: "csrf" } : elements.get(selector),
      querySelectorAll: () => ["#rescan-boards", "#toggle-led", "#reset-board"].map(id => elements.get(id)),
      createElement: () => new Element(),
      createTextNode: text => { const element = new Element(); element.textContent = text; return element; },
    },
    window: {
      setInterval(callback, delay) {
        if (intervalCalls++ === 1) {
          ageInterval = callback;
          return 2;
        }
        interval = callback;
        intervalMs = delay;
        return 1;
      },
      clearInterval() {},
      setTimeout(callback, delay) {
        const id = ++timeoutId;
        timeouts.set(id, { callback, delay });
        return id;
      },
      clearTimeout(id) {
        timeouts.delete(id);
      },
      confirm: () => true,
    },
    AbortController,
    fetch: (url, options) => new Promise((resolve, reject) => {
      const request = { url, options, resolve };
      requests.push(request);
      options?.signal?.addEventListener("abort", () => {
        const index = requests.indexOf(request);
        if (index >= 0) requests.splice(index, 1);
        reject(new DOMException("Aborted", "AbortError"));
      }, { once: true });
    }),
  });
  vm.runInContext(script, context);
  return {
    context, requests, get intervalMs() { return intervalMs; }, element: id => elements.get(`#${id}`),
    tick: () => interval(),
    tickAge: () => ageInterval(),
    expireTimeouts(delay) {
      for (const [id, timeout] of timeouts) {
        if (timeout.delay !== delay) continue;
        timeouts.delete(id);
        timeout.callback();
      }
    },
    async respond(data, status = 200) {
      const request = requests.shift();
      assert.ok(request, "Expected a pending request");
      request.resolve({ ok: status < 400, status, json: async () => data });
      await new Promise(resolve => setImmediate(resolve));
      return request;
    },
    change(id) {
      const select = elements.get("#board-select");
      select.value = id;
      select.events.change();
    },
  };
}

async function connect(view) {
  assert.equal(view.requests[0].url, "/api/boards");
  await view.respond(catalog);
  assert.ok(view.requests[0], `Status request missing: ${view.element("status-message").textContent}`);
  assert.equal(view.requests[0].url, "/api/status?board=A");
  await view.respond(snapshot("first"));
}

test("Python and .NET display all assigned reset causes and preserve unknown codes", async () => {
  const view = page();
  await connect(view);
  const labels = ["Unknown (not distinguished)", "Power-on", "Watchdog reset", "Software-requested reset",
    "HardFault recovery", "Power-on or brownout", "Brownout", "RUN-pin reset", "Debugger reset",
    "User-requested reboot", "Configuration-save reboot"];
  for (const [cause, label] of [...labels.entries(), [255, "Unknown (255)"]]) {
    view.tick();
    const data = snapshot("first");
    data.configuration.reset_cause = cause;
    await view.respond(data);
    assert.equal(view.element("reset-cause").textContent, label);
  }
});

test("hosted profile selection supports unset defaults and preserves unsaved edits", async () => {
  const view = page();
  await connect(view);
  view.tick();
  await view.respond(snapshot("first", 2, null));
  const profile = view.element("clock-profile");
  assert.equal(profile.disabled, false);
  assert.equal(profile.value, "0");
  assert.equal(view.element("save-configuration").disabled, true);
  profile.value = "1";
  profile.events.change();
  assert.equal(view.element("save-configuration").disabled, false);
  view.tick();
  await view.respond(snapshot("first", 2, null));
  assert.equal(profile.value, "1");
  const data = snapshot("first", 2, null);
  data.configuration = null;
  view.tick();
  await view.respond(data);
  assert.equal(profile.disabled, false);
  assert.equal(view.element("save-configuration").disabled, true);
  view.tick();
  await view.respond({ ...snapshot("first"), connected: false });
  assert.equal(profile.disabled, true);
});

test("hosted pages select independently and render board labels as text", async () => {
  const alice = page(), bob = page();
  assert.equal(alice.intervalMs, 500);
  assert.equal(bob.intervalMs, 500);
  await connect(alice);
  await connect(bob);
  assert.equal(alice.element("board-select").value, "A");
  assert.equal(alice.element("board-select").children[1].textContent, "<board> / second");
  bob.change("B");
  assert.equal(bob.element("serial").textContent, "--");
  assert.equal(bob.requests[0].url, "/api/status?board=B");
  await bob.respond(snapshot("second", 1, 1));
  assert.ok(["SECOND", "--"].includes(bob.element("serial").textContent));
  assert.ok(["FIRST", "--"].includes(alice.element("serial").textContent));
});

test("renders live v1 telemetry instead of leaving the startup waiting message", async () => {
  const view = page();
  await connect(view);
  assert.equal(view.element("status-message").textContent, "HID telemetry is live");
  assert.equal(view.element("serial").textContent, "FIRST");
  assert.equal(view.element("uptime").textContent, "1h 2m 3s");
  assert.equal(view.element("board-model").textContent, "Pico 2 (RP2350)");
  assert.match(view.element("channel-rows").children[0].textContent, /115,200 8N1/);
  assert.match(view.element("channel-rows").children[0].textContent, /100 B/);
  assert.match(view.element("channel-rows").children[0].textContent, /400 B/);
});

test("Python dashboard rates use device intervals and reset across gaps or invalid UART samples", () => {
  const view = page();
  vm.runInContext("channelHistory.clear()", view.context);
  const rateData = { connected: true, identity: { serial_number: "A" }, runtime: { uptime_ms: 1000 } };
  const makeChannel = (sequence, total, observed_interval_ms, {
    interval_ms = 500, traffic_gaps = 0, uart_measurement_state = 1,
  } = {}) => ({ id: 0, sequence, interval_ms, observed_interval_ms, traffic_gaps, uart_measurement_state,
    totals: { uart_tx: total, uart_rx: total, usb_tx: total, usb_rx: total } });
  view.context.rateData = rateData;
  view.context.rateChannel = makeChannel(10, 100, 0);
  view.context.rateNow = 1000;
  const initialRates = vm.runInContext("currentRates(rateChannel, rateNow, rateData).rates", view.context);
  for (const direction of ["uart_tx", "uart_rx", "usb_tx", "usb_rx"]) {
    assert.equal(initialRates[direction], null);
  }

  const updateRates = (channel, now) => {
    view.context.rateChannel = channel;
    view.context.rateNow = now;
    view.context.rateData = { ...rateData, runtime: { uptime_ms: now + 1000 } };
    return vm.runInContext("currentRates(rateChannel, rateNow, rateData).rates", view.context);
  };
  updateRates(makeChannel(11, 1100, 500), 3000);
  let rates = updateRates(makeChannel(12, 2100, 1000), 5000);
  assert.equal(rates.uart_tx, 2000);
  assert.equal(rates.usb_tx, 2000);

  rates = updateRates(makeChannel(14, 3100, 1000, { traffic_gaps: 1 }), 5500);
  assert.equal(rates.uart_tx, null);
  assert.equal(rates.usb_tx, null);
  updateRates(makeChannel(15, 4100, 1500, { traffic_gaps: 1 }), 6000);
  rates = updateRates(makeChannel(16, 5100, 2000, { traffic_gaps: 1 }), 6500);
  assert.equal(rates.uart_tx, 2000);
  assert.equal(rates.usb_tx, 2000);

  rates = updateRates(makeChannel(17, 6100, 2500, {
    traffic_gaps: 1, uart_measurement_state: 2,
  }), 7000);
  assert.equal(rates.uart_tx, null);
  assert.equal(rates.usb_tx, 2000);
  rates = updateRates(makeChannel(18, 7100, 3000, { traffic_gaps: 1 }), 7500);
  assert.equal(rates.uart_tx, null);
  updateRates(makeChannel(19, 8100, 3500, { traffic_gaps: 1 }), 8000);
  rates = updateRates(makeChannel(20, 9100, 4000, { traffic_gaps: 1 }), 8500);
  assert.equal(rates.uart_tx, 2000);

  vm.runInContext("channelHistory.clear()", view.context);
  updateRates(makeChannel(1, 100, 0), 10000);
  rates = updateRates(makeChannel(2, 1100, 500, { interval_ms: 65535 }), 10500);
  assert.equal(rates.usb_tx, null);
  updateRates(makeChannel(3, 2100, 1000), 11000);
  rates = updateRates(makeChannel(4, 3100, 1500), 11500);
  assert.equal(rates.usb_tx, 2000);

  vm.runInContext("channelHistory.clear()", view.context);
  updateRates(makeChannel(10, 100, 0), 20000);
  rates = updateRates(makeChannel(12, 2100, 1000), 20500);
  assert.equal(rates.usb_tx, 2000);
  assert.equal(rates.uart_tx, null);

  vm.runInContext("channelHistory.clear()", view.context);
  updateRates(makeChannel(10, 100, 0), 30000);
  updateRates(makeChannel(11, 600, 500), 30500);
  rates = updateRates(makeChannel(12, 1100, 1000), 31000);
  assert.equal(rates.usb_tx, 1000);
  updateRates(makeChannel(1, 1100, 0), 31500);
  updateRates(makeChannel(2, 1600, 500), 32000);
  rates = updateRates(makeChannel(3, 2100, 1000), 32500);
  assert.equal(rates.usb_tx, 1000);
});

test("Unavailable UART measurements do not render placeholder counters as zero", async () => {
  const view = page();
  await connect(view);
  view.tick();
  const data = snapshot("first");
  data.channels = data.channels.map(channel => ({ ...channel, sequence: 2, uart_measurement_state: 0,
    totals: { uart_tx: 0, uart_rx: 0, usb_tx: 1500, usb_rx: 1500 } }));
  await view.respond(data);

  const metrics = view.element("channel-rows").children[0].children[3].children[0].children;
  assert.equal(metrics[0].children[2].textContent, "--");
  assert.equal(metrics[1].children[2].textContent, "--");
  assert.equal(metrics[2].children[2].textContent, "1,500 B");
  assert.equal(metrics[3].children[2].textContent, "1,500 B");
});

test("Unavailable UART diagnostics do not render zero placeholders as measurements", async () => {
  const view = page();
  await connect(view);
  const firstButton = view.element("channel-rows").children[0].children[4].children[0];
  const opening = firstButton.events.click();
  const unavailable = {
    uart_measurement_state: 0, uart_sample_age_100ms: 255, rx_overrun_count: 0, rx_error_count: 0,
    last_control_error_reason: 0, last_fault_uptime_s: 0, uart_tx_ring_peak_bytes: 0,
    uart_rx_ring_peak_bytes: 0, cdc_tx_fifo_peak_bytes: 123, cdc_rx_fifo_peak_bytes: 456,
  };
  await view.respond({ channel_diagnostics: Array.from({ length: 6 }, (_, id) => ({
    id, ...unavailable,
    ...(id === 1 ? { last_control_error_reason: 3, last_fault_uptime_s: 42 } : {}),
  })) });
  await opening;
  const diagnosticFields = () => view.element("diagnostics-content").children[1].children;
  const value = label => {
    const fields = diagnosticFields();
    const index = fields.findIndex((field, position) => position % 2 === 0 && field.textContent === label);
    return fields[index + 1].textContent;
  };
  assert.equal(value("Known RX bytes dropped"), "Unavailable");
  assert.equal(value("RX controller error observations"), "Unavailable");
  assert.equal(value("Last fault observed"), "Unavailable");
  assert.equal(value("UART TX ring observed peak since boot"), "Unavailable");
  assert.equal(value("UART RX ring observed peak since boot"), "Unavailable");
  assert.match(value("CDC TX FIFO observed peak since boot"), /^123 B/);
  assert.match(value("CDC RX FIFO observed peak since boot"), /^456 B/);

  const secondButton = view.element("channel-rows").children[1].children[4].children[0];
  secondButton.events.click();
  assert.equal(value("Last configuration error"), "Backend configuration failed");
  assert.equal(value("Last fault observed"), "42 s uptime");
});

test("USB totals remain unknown until the first Input sample", async () => {
  const view = page();
  await view.respond({ boards: [], default_board_id: null });
  const empty = { connected: false, channels: Array.from({ length: 6 }, (_, id) => ({
    id, sequence: null, updated_at: null, uart_measurement_state: null,
    totals: { uart_tx: 0, uart_rx: 0, usb_tx: 0, usb_rx: 0 },
  })) };
  await view.respond(empty);

  const metrics = view.element("channel-rows").children[0].children[3].children[0].children;
  assert.deepEqual([...metrics].map(metric => metric.children[2].textContent), ["--", "--", "--", "--"]);

  view.change("A");
  await view.respond(snapshot("first"));
  const connectedMetrics = view.element("channel-rows").children[0].children[3].children[0].children;
  assert.deepEqual([...connectedMetrics].map(metric => metric.children[2].textContent),
    ["100 B", "200 B", "400 B", "300 B"]);
});

test("traffic continuity loss remains visible after current rates recover", async () => {
  const view = page();
  await connect(view);
  const row = view.element("channel-rows").children[0];
  assert.equal(row.children[1].children[2].hidden, true);

  view.tick();
  const gap = snapshot("first");
  gap.channels[0].traffic_incomplete = true;
  gap.channels[0].traffic_gaps = 1;
  await view.respond(gap);
  assert.equal(row.children[1].children[2].hidden, false);
  assert.equal(row.children[1].children[2].textContent, "History incomplete");

  view.tick();
  const recovered = snapshot("first");
  recovered.channels[0].sequence = 2;
  recovered.channels[0].observed_interval_ms = 500;
  recovered.channels[0].traffic_incomplete = true;
  recovered.channels[0].traffic_gaps = 1;
  await view.respond(recovered);
  assert.equal(row.children[1].children[0].textContent, "ready");
  assert.equal(row.children[1].children[2].hidden, false);
});

test("time-driven refresh marks channels and rates stale while status fetch is pending", async () => {
  let now = Date.now();
  const view = page();
  view.context.Date = class extends Date { static now() { return now; } };
  await connect(view);

  now += 500;
  view.tick();
  const updated = snapshot("first");
  updated.channels = updated.channels.map(channel => ({ ...channel, sequence: 2,
    observed_interval_ms: 500,
    totals: Object.fromEntries(Object.entries(channel.totals).map(([key, value]) => [key, value + 1000])) }));
  await view.respond(updated);
  assert.equal(view.element("channel-rows").children[0].children[3]
    .children[0].children[0].children[1].textContent, "-- KB/s");

  now += 500;
  view.tick();
  const secondUpdate = snapshot("first");
  secondUpdate.channels = secondUpdate.channels.map(channel => ({ ...channel, sequence: 3,
    observed_interval_ms: 1000,
    totals: Object.fromEntries(Object.entries(channel.totals).map(([key, value]) => [key, value + 2000])) }));
  await view.respond(secondUpdate);
  assert.match(view.element("channel-rows").children[0].textContent, /2\.0 KB\/s/);

  view.tick();
  assert.equal(view.requests.length, 1);
  now += 5000;
  view.expireTimeouts(5000);
  await new Promise(resolve => setImmediate(resolve));
  assert.equal(view.requests.length, 0);
  view.tickAge();

  const row = view.element("channel-rows").children[0];
  assert.equal(row.children[1].children[0].hidden, true);
  assert.equal(row.children[1].children[1].textContent, "— Unavailable");
  assert.equal(row.children[3].children[0].children[0].children[1].textContent, "-- KB/s");
  assert.equal(view.element("sample-time").hidden, false);
  assert.match(view.element("sample-time").textContent, /Telemetry stale/);
});

test("status timeout covers a stalled response body and releases the next poll", async () => {
  const view = page();
  await connect(view);
  const pending = view.tick();
  const request = view.requests.shift();
  let releaseBody;
  request.resolve({ ok: true, status: 200, json: () => new Promise(resolve => { releaseBody = resolve; }) });
  await new Promise(resolve => setImmediate(resolve));
  view.expireTimeouts(5000);
  await pending;

  assert.equal(request.options.signal.aborted, true);
  assert.equal(vm.runInContext("refreshing", view.context), null);
  assert.match(view.element("status-message").textContent, /timed out/);
  const next = view.tick();
  assert.equal(view.requests.length, 1);
  await view.respond(snapshot("first"));
  await next;
  releaseBody(snapshot("first"));
});

test("board discovery timeout restores the Rescan control", async () => {
  const view = page();
  view.expireTimeouts(10000);
  await new Promise(resolve => setImmediate(resolve));

  assert.equal(vm.runInContext("scanning", view.context), false);
  assert.equal(view.element("rescan-boards").disabled, false);
  assert.match(view.element("status-message").textContent, /Board scan failed: Request timed out/);
});

test("stalled board actions time out, release controls, and remain unconfirmed", async () => {
  const view = page();
  await connect(view);
  const action = view.element("toggle-led").events.click();
  const request = view.requests[0];
  view.expireTimeouts(30000);
  await action;

  assert.equal(request.options.signal.aborted, true);
  assert.equal(view.element("board-select").disabled, false);
  assert.equal(view.element("rescan-boards").disabled, false);
  assert.match(view.element("status-message").textContent, /outcome is unconfirmed/);
  assert.equal(view.element("status-message").parentElement.dataset.error, "true");
});

test("diagnostics timeout renders a visible read error and permits another request", async () => {
  const view = page();
  await connect(view);
  const button = view.element("channel-rows").children[0].children[4].children[0];
  const opening = button.events.click();
  view.expireTimeouts(15000);
  await opening;

  assert.match(view.element("diagnostics-content").textContent, /Diagnostics unavailable: Request timed out/);
  assert.equal(vm.runInContext("diagnosticRequest", view.context), null);
  view.element("refresh-diagnostics").events.click();
  assert.equal(view.requests.at(-1).url, "/api/diagnostics?board=A");
});

test("nullable diagnostics render unavailable data and the diagnostics read age", async () => {
  const view = page();
  await connect(view);
  const button = view.element("channel-rows").children[0].children[4].children[0];
  const opening = button.events.click();
  assert.equal(view.requests[0].url, "/api/diagnostics?board=A");
  await view.respond({ channel_diagnostics: [null, null, null, null, null, null] });
  await opening;

  assert.match(view.element("diagnostics-content").textContent, /Diagnostics unavailable for this channel/);
  assert.match(view.element("diagnostics-content").textContent, /Diagnostics read 0 s ago/);
});

test("an old diagnostics request cannot block the newly selected board", async () => {
  const view = page();
  await connect(view);
  const buttonA = view.element("channel-rows").children[0].children[4].children[0];
  const openingA = buttonA.events.click();
  const oldRequest = view.requests.shift();
  assert.equal(oldRequest.url, "/api/diagnostics?board=A");

  view.change("B");
  await view.respond(snapshot("second"));
  const buttonB = view.element("channel-rows").children[0].children[4].children[0];
  const openingB = buttonB.events.click();
  assert.equal(view.requests[0].url, "/api/diagnostics?board=B");

  oldRequest.resolve({ ok: true, json: async () => ({ channel_diagnostics: [] }) });
  await openingA;
  assert.equal(view.requests[0].url, "/api/diagnostics?board=B");
  await view.respond({ channel_diagnostics: Array.from({ length: 6 }, (_, id) => ({
    id, uart_measurement_state: 1, uart_sample_age_100ms: 0, rx_overrun_count: 7,
    rx_error_count: 0, last_control_error_reason: 0, last_fault_uptime_s: 2,
    uart_tx_ring_peak_bytes: 0, uart_rx_ring_peak_bytes: 0,
    cdc_tx_fifo_peak_bytes: 0, cdc_rx_fifo_peak_bytes: 0,
  })) });
  await openingB;
  assert.equal(view.element("diagnostics-content").textContent.includes("7 B"), true);
});

test("diagnostics refresh after a reboot with unchanged board identity", async () => {
  const view = page();
  await connect(view);
  const button = view.element("channel-rows").children[0].children[4].children[0];
  const opening = button.events.click();
  await view.respond({ channel_diagnostics: Array.from({ length: 6 }, (_, id) => ({
    id, uart_measurement_state: 1, uart_sample_age_100ms: 0, rx_overrun_count: 99,
    rx_error_count: 0, last_control_error_reason: 0, last_fault_uptime_s: 100,
    uart_tx_ring_peak_bytes: 10, uart_rx_ring_peak_bytes: 10,
    cdc_tx_fifo_peak_bytes: 10, cdc_rx_fifo_peak_bytes: 10,
  })) });
  await opening;
  assert.match(view.element("diagnostics-content").textContent, /99 B/);

  view.tick();
  const rebooted = snapshot("first");
  rebooted.runtime.uptime_ms = 1;
  rebooted.channel_diagnostics = null;
  await view.respond(rebooted);
  assert.equal(view.requests[0].url, "/api/diagnostics?board=A");
  await view.respond({ channel_diagnostics: Array.from({ length: 6 }, (_, id) => ({
    id, uart_measurement_state: 1, uart_sample_age_100ms: 0, rx_overrun_count: 0,
    rx_error_count: 0, last_control_error_reason: 0, last_fault_uptime_s: 0,
    uart_tx_ring_peak_bytes: 0, uart_rx_ring_peak_bytes: 0,
    cdc_tx_fifo_peak_bytes: 0, cdc_rx_fifo_peak_bytes: 0,
  })) });
  assert.doesNotMatch(view.element("diagnostics-content").textContent, /99 B/);
  assert.match(view.element("diagnostics-content").textContent, /Diagnostics read 0 s ago/);
});

test("uncertain configuration outcome survives healthy polling and keeps save gated", async () => {
  const view = page();
  await connect(view);
  view.element("clock-profile").value = "1";
  view.element("clock-profile").events.change();
  view.element("save-configuration").events.click();
  view.element("overclock-ack").checked = true;
  view.element("overclock-ack").events.change();
  const saving = view.element("confirm-configuration").events.click();
  await view.respond({ error: "board action outcome is unconfirmed; inspect before retrying" }, 409);
  await saving;
  assert.match(view.element("status-message").textContent, /outcome is unconfirmed/);

  view.tick();
  const healthy = snapshot("first");
  healthy.configuration_save_error = "outcome is unconfirmed; inspect before retrying";
  await view.respond(healthy);
  assert.match(view.element("status-message").textContent, /outcome is unconfirmed/);
  assert.equal(view.element("save-configuration").disabled, true);
});

test("profile selection locks during save and readback completes the submitted profile", async () => {
  const view = page();
  await connect(view);
  view.element("clock-profile").value = "1";
  view.element("clock-profile").events.change();
  view.element("save-configuration").events.click();
  view.element("overclock-ack").checked = true;
  view.element("overclock-ack").events.change();
  const saving = view.element("confirm-configuration").events.click();

  assert.equal(view.element("clock-profile").disabled, true);
  assert.equal(view.element("save-configuration").disabled, true);
  await view.respond({ ok: true });
  await saving;
  view.tick();
  const applied = snapshot("first", 2, 1);
  applied.runtime.uptime_ms = 1;
  await view.respond(applied);

  assert.equal(view.element("clock-profile").value, "1");
  assert.equal(view.element("clock-profile").disabled, false);
  assert.equal(view.element("configuration-recovery").hidden, true);
});

test("matching readback suppresses the retained warning until it clears", async () => {
  const view = page();
  await connect(view);
  view.element("clock-profile").value = "1";
  view.element("clock-profile").events.change();
  view.element("save-configuration").events.click();
  view.element("overclock-ack").checked = true;
  view.element("overclock-ack").events.change();
  const saving = view.element("confirm-configuration").events.click();
  await view.respond({ error: "configuration save outcome is unconfirmed" }, 409);
  await saving;

  const resolved = snapshot("first", 2, 1);
  resolved.configuration_save_error = "configuration save outcome is unconfirmed";
  view.tick();
  await view.respond(resolved);
  assert.equal(view.element("clock-profile").disabled, false);

  view.tick();
  await view.respond(resolved);
  assert.equal(view.element("clock-profile").disabled, false);
  assert.equal(view.element("status-message").textContent, "HID telemetry is live");
});

test("uncertain saves require explicit inspection before another save", async () => {
  const view = page();
  await connect(view);
  view.element("clock-profile").value = "1";
  view.element("clock-profile").events.change();
  view.element("save-configuration").events.click();
  view.element("overclock-ack").checked = true;
  view.element("overclock-ack").events.change();
  const saving = view.element("confirm-configuration").events.click();
  await view.respond({ error: "board action outcome is unconfirmed" }, 409);
  await saving;

  view.tick();
  await view.respond(snapshot("first"));
  assert.equal(view.element("configuration-recovery").hidden, false);
  assert.match(view.element("configuration-recovery-message").textContent, /stored profile: 200 MHz/i);
  assert.equal(view.element("clock-profile").disabled, true);
  view.element("acknowledge-configuration-outcome").events.click();

  assert.equal(view.element("configuration-recovery").hidden, true);
  assert.equal(view.element("clock-profile").value, "2");
  assert.equal(view.element("clock-profile").disabled, false);
  assert.equal(view.element("save-configuration").disabled, true);
  view.element("clock-profile").value = "3";
  view.element("clock-profile").events.change();
  assert.equal(view.element("save-configuration").disabled, false);

  view.tick();
  await view.respond(snapshot("first"));
  const nextUncertainSave = snapshot("first");
  nextUncertainSave.configuration_save_error = "new configuration save outcome is unconfirmed";
  view.tick();
  await view.respond(nextUncertainSave);
  assert.match(view.element("status-message").textContent, /new configuration save outcome is unconfirmed/);
  assert.equal(view.element("clock-profile").disabled, true);
});

test("configuration timeout preserves uncertainty until the user inspects readback", async () => {
  const view = page();
  await connect(view);
  view.element("clock-profile").value = "1";
  view.element("clock-profile").events.change();
  view.element("save-configuration").events.click();
  view.element("overclock-ack").checked = true;
  view.element("overclock-ack").events.change();
  const saving = view.element("confirm-configuration").events.click();
  const request = view.requests[0];
  view.expireTimeouts(30000);
  await saving;

  assert.equal(request.options.signal.aborted, true);
  assert.match(view.element("status-message").textContent, /outcome is unconfirmed/);
  assert.equal(view.element("clock-profile").disabled, true);
  assert.equal(view.element("configuration-recovery").hidden, true);
  view.tick();
  await view.respond(snapshot("first"));
  assert.equal(view.element("configuration-recovery").hidden, false);
  view.element("acknowledge-configuration-outcome").events.click();
  assert.equal(view.element("clock-profile").disabled, false);
});

test("confirmed action retry clears only that action's previous uncertainty", async () => {
  const view = page();
  await connect(view);
  const failed = view.element("toggle-led").events.click();
  await view.respond({ error: "board action outcome is unconfirmed" }, 409);
  await failed;
  assert.match(view.element("status-message").textContent, /outcome is unconfirmed/);

  const retry = view.element("toggle-led").events.click();
  await view.respond({ ok: true });
  const healthy = snapshot("first");
  healthy.led.output_on = true;
  await view.respond(healthy);
  await retry;
  assert.equal(view.element("status-message").textContent, "HID telemetry is live");
});

test("configuration recovery survives board switches until readback or acknowledgement", async () => {
  const view = page();
  await connect(view);
  view.element("clock-profile").value = "1";
  view.element("clock-profile").events.change();
  view.element("save-configuration").events.click();
  view.element("overclock-ack").checked = true;
  view.element("overclock-ack").events.change();
  const saving = view.element("confirm-configuration").events.click();
  await view.respond({ error: "board action outcome is unconfirmed" }, 409);
  await saving;

  view.change("B");
  await view.respond(snapshot("second"));
  view.change("A");
  assert.equal(view.element("clock-profile").disabled, true);
  await view.respond(snapshot("first"));
  assert.equal(view.element("configuration-recovery").hidden, false);
  view.element("acknowledge-configuration-outcome").events.click();
  assert.equal(view.element("clock-profile").disabled, false);
});

test("reloaded dashboard recovers an unconfirmed save from the status contract", async () => {
  const view = page();
  await view.respond(catalog);
  const status = snapshot("first");
  status.configuration_save_error = "command outcome is unconfirmed; do not retry; reconnect and verify board state";
  await view.respond(status);
  view.element("clock-profile").value = "1";
  view.element("clock-profile").events.change();

  assert.equal(view.element("clock-profile").disabled, true);
  assert.equal(view.element("save-configuration").disabled, true);
  assert.equal(view.element("configuration-recovery").hidden, false);
  view.element("acknowledge-configuration-outcome").events.click();
  view.tick();
  await view.respond(status);
  assert.equal(view.element("status-message").textContent, "HID telemetry is live");
  assert.equal(view.element("clock-profile").disabled, false);
  view.element("clock-profile").value = "3";
  view.element("clock-profile").events.change();
  assert.equal(view.element("save-configuration").disabled, false);
});

test("RAM information clears immediately when selecting a board without identity or pin data", async () => {
  const view = page();
  await connect(view);
  assert.notEqual(view.element("ram-usage").textContent, "--");
  view.change("B");
  assert.equal(view.element("serial").textContent, "--");
  assert.equal(view.element("ram-usage").textContent, "--");
  assert.equal(view.element("pin-map").textContent, "Static pin assignments unavailable");
});

test("status polling follows bounded backend HID cadence and resets on board selection or errors", async () => {
  const view = page();
  await connect(view);
  for (const [interval, expected] of [[750, 750], [2000, 1000], [10, 500], [null, 500]]) {
    view.tick();
    await view.respond({ ...snapshot("first"), refresh_interval_ms: interval });
    assert.equal(view.intervalMs, expected);
  }
  view.tick();
  await view.respond({ ...snapshot("first"), refresh_interval_ms: 900 });
  assert.equal(view.intervalMs, 900);
  view.change("B");
  assert.equal(view.intervalMs, 500);
  await view.respond({ ...snapshot("second"), refresh_interval_ms: 800 });
  assert.equal(view.intervalMs, 800);
  view.tick();
  await view.respond({ error: "offline" }, 503);
  assert.equal(view.intervalMs, 500);
});

test("board discovery runs only at startup or on an explicit rescan, never on status ticks", async () => {
  for (const startup of [
    { data: catalog, status: 200, board: "A" },
    { data: { boards: [], default_board_id: null }, status: 200, board: null },
    { data: { error: "enumeration failed" }, status: 503, board: null },
  ]) {
    const view = page();
    assert.equal(view.intervalMs, 500);
    assert.equal(view.requests.length, 1);
    assert.equal(view.requests[0].url, "/api/boards");
    await view.respond(startup.data, startup.status);
    if (startup.status === 200) await view.respond({ ...snapshot(null), connected: false });
    for (let tick = 0; tick < 3; tick++) {
      view.tick();
      assert.equal(view.requests.length, 1);
      assert.equal(view.requests[0].url, startup.board ? `/api/status?board=${startup.board}` : "/api/status");
      await view.respond({ ...snapshot(null), connected: false });
    }
    view.element("rescan-boards").events.click();
    assert.equal(view.requests.length, 1);
    assert.equal(view.requests[0].url, "/api/boards");
    await view.respond(catalog);
    await view.respond(snapshot("first"));
    assert.equal(view.requests.length, 0);
  }
});

test("switching clears history and ignores an earlier board's in-flight response", async () => {
  const view = page();
  await connect(view);
  vm.runInContext("channelHistory.set(0, {}); reportSample = 123", view.context);
  view.tick();
  view.tick();
  assert.equal(view.requests.length, 1, "No overlapping refresh for the same selection");
  view.change("B");
  assert.equal(vm.runInContext("channelHistory.size", view.context), 6);
  assert.equal(vm.runInContext("reportSample", view.context), null);
  assert.equal(view.element("serial").textContent, "--");
  assert.equal(view.element("channel-rows").children.length, 6);
  await view.respond(snapshot("outdated-first"));
  assert.equal(view.element("serial").textContent, "--");
  await view.respond(snapshot("second"));
  assert.ok(["SECOND", "--"].includes(view.element("serial").textContent));
});

test("rescan retains missing selection without silently choosing another board", async () => {
  const view = page();
  await connect(view);
  view.element("rescan-boards").events.click();
  await view.respond({ boards: [catalog.boards[1]], default_board_id: "B" });
  assert.equal(view.element("board-select").value, "A");
  assert.equal(view.element("board-select").children[1].textContent, "PicoUart / first (not found)");
  assert.equal(view.requests[0].url, "/api/status?board=A");
  await view.respond({ ...snapshot("first"), connected: false });
});

test("scan errors remain visible across status refresh until a successful rescan", async () => {
  const view = page();
  await connect(view);
  view.element("rescan-boards").events.click();
  await view.respond({ error: "enumeration failed" }, 503);
  view.tick();
  await view.respond(snapshot("first"));
  assert.match(view.element("status-message").textContent, /Board scan failed: enumeration failed/);
  view.element("rescan-boards").events.click();
  await view.respond(catalog);
  await view.respond(snapshot("first"));
  assert.equal(view.element("status-message").textContent, "HID telemetry is live");
});

test("actions target the selected board and prevent switching until completion", async () => {
  const view = page();
  await connect(view);
  view.change("B");
  await view.respond(snapshot("second"));
  view.element("toggle-led").events.click();
  assert.equal(view.element("board-select").disabled, true);
  assert.equal(view.element("rescan-boards").disabled, true);
  assert.equal(view.requests[0].url, "/api/actions/toggle-led?board=B");
  assert.equal(view.requests[0].options.method, "POST");
  assert.equal(view.requests[0].options.headers["X-CSRF-Token"], "csrf");
  await view.respond({ ok: true });
  assert.equal(view.requests[0].url, "/api/status?board=B");
  await view.respond(snapshot("second"));
  assert.equal(view.element("board-select").disabled, false);
  assert.equal(view.element("rescan-boards").disabled, false);
});

test("empty discovery and unavailable CLI preference do not invent a selection", async () => {
  for (const boards of [[], catalog.boards]) {
    const view = page();
    await view.respond({ boards, default_board_id: null });
    assert.equal(view.element("board-select").value, "");
    assert.equal(view.element("board-select").disabled, !boards.length);
    assert.equal(view.requests[0].url, "/api/status");
    await view.respond({ ...snapshot(null), connected: false });
    view.element("toggle-led").events.click();
    assert.equal(view.requests.length, 0);
  }
});
