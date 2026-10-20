import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const html = readFileSync(new URL("../../web/index.html", import.meta.url), "utf8");
const script = readFileSync(new URL("../../web/js/dashboard.js", import.meta.url), "utf8");
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
  }
  set textContent(value) { this.text = value; this.children = []; }
  get textContent() { return this.text + this.children.map(child => child.textContent).join(""); }
  get selectedOptions() { return this.children.filter(child => child.value === this.value); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  addEventListener(event, callback) { this.events[event] = callback; }
}

function snapshot(serial) {
  return { connected: true, identity: { product_name: "PicoUart", serial_number: serial },
    updated_at: "2026-01-01T00:00:00Z", overflow_counts: Array(6).fill(0),
    channels: [], led: { manual_on: false, output_on: false } };
}

function page() {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [`#${id}`, new Element()]));
  const requests = [];
  let interval;
  const context = vm.createContext({
    document: {
      querySelector: selector => selector.startsWith("meta") ? { content: "csrf" } : elements.get(selector),
      querySelectorAll: () => ["#rescan-boards", "#toggle-led", "#reset-board"].map(id => elements.get(id)),
      createElement: () => new Element(),
      createTextNode: text => { const element = new Element(); element.textContent = text; return element; },
    },
    window: { setInterval(callback) { interval = callback; }, confirm: () => true },
    fetch: (url, options) => new Promise(resolve => requests.push({ url, options, resolve })),
  });
  vm.runInContext(script, context);
  return {
    context, requests, element: id => elements.get(`#${id}`),
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
  assert.equal(view.requests[0].url, "/api/status?board=A");
  await view.respond(snapshot("first"));
}

test("hosted pages select independently and render board labels as text", async () => {
  const alice = page(), bob = page();
  await connect(alice);
  await connect(bob);
  assert.equal(alice.element("board-select").value, "A");
  assert.equal(alice.element("board-select").children[1].textContent, "<board> / second");
  bob.change("B");
  assert.equal(bob.element("serial").textContent, "--");
  assert.equal(bob.requests[0].url, "/api/status?board=B");
  await bob.respond(snapshot("second"));
  assert.equal(bob.element("serial").textContent, "second");
  assert.equal(alice.element("serial").textContent, "first");
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
