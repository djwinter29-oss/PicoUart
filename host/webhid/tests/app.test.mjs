import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

function fixture(secure = true, hid = {}) {
  const html = readFileSync(new URL("../index.html", import.meta.url), "utf8");
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [
    `#${id}`, { textContent: "", dataset: {}, parentElement: { dataset: {} },
      addEventListener(event, handler) { this[event] = handler; } },
  ]));
  const document = { querySelector(selector) {
    assert.ok(elements.has(selector), `Missing element ${selector}`);
    return elements.get(selector);
  } };
  let client;
  class FakeClient {
    constructor(hid, render) {
      client = this;
      this.render = render;
      this.calls = [];
      this.state = { phase: hid ? "disconnected" : "unsupported", dashboard: {},
        message: "No device selected", error: null };
    }
    connect() { this.calls.push("connect"); }
    disconnect() { this.calls.push("disconnect"); }
    refresh() { this.calls.push("refresh"); }
    toggleLed() { this.calls.push("toggle-led"); }
    phase(phase, error = null) {
      this.state = { ...this.state, phase, error };
      this.render(this.state);
    }
  }
  const source = readFileSync(new URL("../js/app.mjs", import.meta.url), "utf8")
    .replace(/^import .*;\n/gm, "");
  let polling;
  let agePolling;
  vm.runInNewContext(source, { document, navigator: { hid }, isSecureContext: secure,
    PicoUartWebHid: FakeClient, renderDashboard() {},
    updateReportAge(data) { assert.equal(data, client.state.dashboard); },
    DEVICE_FILTER: { vendorId: 0xcafe, productId: 0x4010 },
    setInterval(callback, interval) {
      if (interval === 2000) polling = { callback, interval };
      else agePolling = { callback, interval };
    } });
  return { html, client, polling, agePolling, get: id => document.querySelector(`#${id}`) };
}

test("one connection button belongs to Device identity, with no separate refresh or disconnect", () => {
  const { html } = fixture();
  const identity = html.match(/<section aria-labelledby="identity-title">([\s\S]*?)<\/section>/)[1];
  const channels = html.match(/<section class="channels-section"[\s\S]*?<\/section>/)[0];
  assert.ok(identity.includes('id="connect"'));
  assert.ok(!channels.includes('id="connect"'));
  assert.equal([...html.matchAll(/id="connect"/g)].length, 1);
  assert.ok(!html.includes('id="disconnect"'));
  assert.ok(!html.includes('id="refresh"'));
});

test("metadata continues refreshing automatically every two seconds", () => {
  const { client, polling, agePolling } = fixture();
  assert.equal(polling.interval, 2000);
  polling.callback();
  assert.deepEqual(client.calls, ["refresh"]);
  assert.equal(agePolling.interval, 1000);
  agePolling.callback();
});

test("connection button reflects lifecycle, routes clicks and recovers after failure or unplug", () => {
  const { client, get } = fixture();
  const button = get("connect");
  assert.equal(button.textContent, "Connect");
  assert.equal(button.disabled, false);
  button.click();
  assert.deepEqual(client.calls, ["connect"]);
  for (const [phase, text, disabled] of [
    ["connecting", "Connecting...", true], ["connected", "Disconnect", false],
    ["reading", "Disconnect", false], ["writing", "Disconnect", true],
    ["disconnecting", "Disconnecting...", true], ["disconnected", "Connect", false],
  ]) {
    client.phase(phase);
    assert.equal(button.textContent, text);
    assert.equal(button.disabled, disabled);
  }
  client.phase("connected", "Disconnect failed");
  assert.equal(button.disabled, false);
  assert.equal(button.textContent, "Disconnect");
  assert.match(button.className, /button-danger/);
  button.click();
  get("toggle-led").click();
  assert.deepEqual(client.calls, ["connect", "disconnect", "toggle-led"]);
  client.phase("reading");
  button.click();
  assert.equal(client.calls.at(-1), "disconnect");
  client.phase("disconnected", "Device unplugged");
  assert.equal(button.textContent, "Connect");
  assert.match(button.className, /button-secondary/);
  button.click();
  assert.equal(client.calls.at(-1), "connect");
});

test("unsupported WebHID and insecure contexts leave connection disabled", () => {
  for (const [secure, hid] of [[false, {}], [true, undefined]]) {
    const { client, get } = fixture(secure, hid ?? null);
    assert.equal(client.state.phase, "unsupported");
    assert.equal(get("connect").disabled, true);
  }
});
