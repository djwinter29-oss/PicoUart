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
  get selectedOptions() { return this.children.filter(child => child.value === this.value); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  addEventListener(event, callback) { this.events[event] = callback; }
}

function snapshot(serial, mcuId = 2, storedProfile = 2) {
  return { connected: true, identity: { product_name: "PicoUart", serial_number: serial },
    updated_at: "2026-01-01T00:00:00Z", overflow_counts: Array(6).fill(0),
    hardware: { mcu: mcuId === 1 ? "RP2040" : "RP2350", mcu_id: mcuId, system_clock_hz: 150000000 },
    configuration: { source: "stored", reason: "none", stored_profile: storedProfile, active_profile: storedProfile },
    configuration_save: { status: 0, profile: null },
    channels: Array.from({ length: 6 }, (_, id) => ({ id, health: null, health_labels: [], cdc_open: false,
      totals: { uart_tx: 0, uart_rx: 0, usb_tx: 0, usb_rx: 0 } })),
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
    window: { setInterval(callback, delay) { interval = callback; intervalMs = delay; }, confirm: () => true },
    fetch: (url, options) => new Promise(resolve => requests.push({ url, options, resolve })),
  });
  vm.runInContext(script, context);
  return {
    context, requests, intervalMs, element: id => elements.get(`#${id}`),
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
    changeProfile(value) {
      const select = elements.get("#configuration-profile");
      select.value = String(value);
      select.events.change();
    },
  };
}

async function connect(view) {
  assert.equal(view.requests[0].url, "/api/boards");
  await view.respond(catalog);
  assert.equal(view.requests[0].url, "/api/status?board=A");
  await view.respond(snapshot("first"));
}

test("hosted pages select independently and render board labels as text", async () => {
  const alice = page(), bob = page();
  assert.equal(alice.intervalMs, 1000);
  assert.equal(bob.intervalMs, 1000);
  await connect(alice);
  await connect(bob);
  assert.equal(alice.element("board-select").value, "A");
  assert.equal(alice.element("board-select").children[1].textContent, "<board> / second");
  bob.change("B");
  assert.equal(bob.element("serial").textContent, "--");
  assert.equal(bob.requests[0].url, "/api/status?board=B");
  await bob.respond(snapshot("second", 1, 1));
  assert.equal(bob.element("serial").textContent, "second");
  assert.deepEqual(bob.element("configuration-profile").children.map(option => [option.value, option.textContent]), [
    ["0", "SDK default (125 MHz)"], ["1", "150 MHz"], ["2", "200 MHz"], ["3", "250 MHz (RP2040)"],
  ]);
  assert.deepEqual(alice.element("configuration-profile").children.map(option => [option.value, option.textContent]), [
    ["0", "SDK default (150 MHz)"], ["1", "150 MHz"], ["2", "200 MHz"], ["4", "280 MHz (RP2350)"],
  ]);
  assert.equal(bob.element("configuration-profile").value, "1");
  assert.equal(bob.element("configuration-unsafe-ack").checked, false);
  assert.equal(alice.element("serial").textContent, "first");
  assert.equal(alice.element("configuration-profile").value, "2");
});

test("persistent configuration saves only on explicit POST and preserves the draft across polling", async () => {
  const view = page();
  await connect(view);
  assert.equal(view.element("configuration-profile").disabled, false);
  assert.equal(view.element("save-configuration").disabled, true);
  assert.match(html, /Saving reboots the Pico and briefly disconnects USB/);
  view.changeProfile(0);
  assert.equal(view.element("configuration-acknowledgement").hidden, true);
  assert.equal(view.element("save-configuration").disabled, false);
  view.element("save-configuration").events.click();
  assert.equal(view.requests[0].url, "/api/configuration?board=A");
  assert.equal(view.requests[0].options.method, "POST");
  assert.equal(view.requests[0].options.headers["X-CSRF-Token"], "csrf");
  assert.deepEqual(JSON.parse(view.requests[0].options.body), { profile: 0, unsafe_overclock: false });
  await view.respond({ ok: true });
  assert.equal(view.element("configuration-message").textContent,
    "Saved. Pico is rebooting; USB will reconnect shortly.");
  view.tick();
  await view.respond(snapshot("first"));
  assert.equal(view.element("configuration-message").textContent,
    "Saved. Pico is rebooting; USB will reconnect shortly.");
  view.changeProfile(2);
  assert.equal(view.element("configuration-acknowledgement").hidden, false);
  assert.equal(view.element("configuration-profile").value, "2");
  assert.equal(view.element("save-configuration").disabled, true);
  view.element("configuration-unsafe-ack").checked = true;
  view.element("configuration-unsafe-ack").events.change();
  assert.equal(view.element("save-configuration").disabled, false);
  view.tick();
  await view.respond(snapshot("first"));
  assert.equal(view.element("configuration-profile").value, "2");
  assert.equal(view.element("configuration-unsafe-ack").checked, true);
});

test("configuration save is disabled for unknown MCU, missing capability, or an open CDC port", async () => {
  const view = page();
  await connect(view);
  const profile = view.element("configuration-profile");
  const button = view.element("save-configuration");
  view.tick();
  await view.respond({ ...snapshot("first"), hardware: { mcu_id: 3 }, configuration_save: null });
  assert.equal(profile.disabled, true);
  view.tick();
  await view.respond({ ...snapshot("first"), configuration_save_error: "unsupported" });
  assert.equal(profile.disabled, true);
  assert.equal(view.element("configuration-message").textContent, "unsupported");
  assert.equal(view.element("configuration-message").dataset.error, "true");
  view.tick();
  await view.respond({ ...snapshot("first"), channels: snapshot("first").channels.map(channel =>
    ({ ...channel, cdc_open: true })) });
  assert.equal(profile.disabled, true);
  assert.equal(button.disabled, true);
});

test("board discovery runs only at startup or on an explicit rescan, never on status ticks", async () => {
  for (const startup of [
    { data: catalog, status: 200, board: "A" },
    { data: { boards: [], default_board_id: null }, status: 200, board: null },
    { data: { error: "enumeration failed" }, status: 503, board: null },
  ]) {
    const view = page();
    assert.equal(view.intervalMs, 1000);
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
  assert.equal(view.element("serial").textContent, "second");
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
