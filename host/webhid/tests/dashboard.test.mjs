import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const html = readFileSync(new URL("../../web/index.html", import.meta.url), "utf8");
const script = readFileSync(new URL("../../web/js/dashboard.js", import.meta.url), "utf8");
test("host versions belong to the wordmark rather than the page heading", () => {
  for (const path of ["../../web/index.html", "../index.html"]) {
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
  }
  set textContent(value) { this.text = value; this.children = []; }
  get textContent() { return this.text + this.children.map(child => child.textContent).join(""); }
  get options() { return this.children; }
  get selectedOptions() { return this.children.filter(child => child.value === this.value); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  close() { this.open = false; }
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
      sequence: 1, interval_ms: 500, traffic_gaps: 0, traffic_incomplete: false,
      totals: { uart_tx: 100 + id, uart_rx: 200 + id, usb_tx: 300 + id, usb_rx: 400 + id },
      traffic: { uart_tx: null, uart_rx: null, usb_tx: null, usb_rx: null } })),
    led: { manual_on: false, output_on: false } };
}

function page() {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [`#${id}`, new Element()]));
  const requests = [];
  let interval;
  let intervalMs;
  const context = vm.createContext({
    document: {
      querySelector: selector => selector.startsWith("meta") ? { content: "csrf" } : elements.get(selector),
      querySelectorAll: () => ["#rescan-boards", "#toggle-led", "#reset-board"].map(id => elements.get(id)),
      createElement: () => new Element(),
      createTextNode: text => { const element = new Element(); element.textContent = text; return element; },
    },
    window: {
      setInterval(callback, delay) { interval = callback; intervalMs = delay; return 1; },
      clearInterval() {},
      confirm: () => true,
    },
    fetch: (url, options) => new Promise(resolve => requests.push({ url, options, resolve })),
  });
  vm.runInContext(script, context);
  return {
    context, requests, get intervalMs() { return intervalMs; }, element: id => elements.get(`#${id}`),
    tick: () => interval(),
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

test("Python dashboard averages cumulative byte deltas over five seconds across sequence gaps", () => {
  const view = page();
  vm.runInContext("channelHistory.clear()", view.context);
  const rateData = { connected: true, identity: { serial_number: "A" }, runtime: { uptime_ms: 1000 } };
  const makeChannel = (sequence, totals, interval_ms) => ({ id: 0, sequence, interval_ms,
    uart_measurement_state: 1, totals });
  view.context.rateData = rateData;
  view.context.rateChannel = makeChannel(10,
    { uart_tx: 100, uart_rx: 200, usb_tx: 300, usb_rx: 400 }, 500);
  view.context.rateNow = 1000;
  assert.equal(vm.runInContext("currentRates(rateChannel, rateNow, rateData).rates", view.context), null);

  const samples = [
    [2000, 11, 500, { uart_tx: 2100, uart_rx: 1200, usb_tx: 1300, usb_rx: 900 }],
    [3000, 12, 500, { uart_tx: 2100, uart_rx: 1200, usb_tx: 1300, usb_rx: 900 }],
    [4000, 13, 500, { uart_tx: 6100, uart_rx: 3200, usb_tx: 4300, usb_rx: 2400 }],
    [5000, 14, 500, { uart_tx: 6100, uart_rx: 3200, usb_tx: 4300, usb_rx: 2400 }],
    [6000, 16, 65535, { uart_tx: 7100, uart_rx: 3700, usb_tx: 5300, usb_rx: 2900 }],
  ];
  for (const [time, sequence, interval_ms, totals] of samples) {
    view.context.rateChannel = makeChannel(sequence, totals, interval_ms);
    rateData.runtime.uptime_ms = time;
    view.context.rateNow = time;
    vm.runInContext("currentRates(rateChannel, rateNow, rateData)", view.context);
  }
  const rates = vm.runInContext("currentRates(rateChannel, rateNow, rateData).rates", view.context);
  assert.equal(rates.uart_tx, 1400);
  assert.equal(rates.uart_rx, 700);
  assert.equal(rates.usb_tx, 1000);
  assert.equal(rates.usb_rx, 500);
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
