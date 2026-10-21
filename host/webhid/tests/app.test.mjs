import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

function fixture(secure = true, hid = {}) {
  const html = readFileSync(new URL("../index.html", import.meta.url), "utf8");
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [
    `#${id}`, { textContent: "", dataset: {}, parentElement: { dataset: {} },
      children: [], value: "", checked: false, disabled: false,
      append(...children) { this.children.push(...children); },
      replaceChildren(...children) { this.children = children; },
      addEventListener(event, handler) { this[event] = handler; } },
  ]));
  const document = { querySelector(selector) {
    assert.ok(elements.has(selector), `Missing element ${selector}`);
    return elements.get(selector);
  }, createElement() {
    return { textContent: "", value: "", children: [] };
  } };
  let client;
  class FakeClient {
    constructor(hid, render) {
      client = this;
      this.render = render;
      this.calls = [];
      this.state = { phase: hid ? "disconnected" : "unsupported", dashboard: {},
      configurationSaveMessage: "",
      message: "No device selected", error: null };
    }
    connect() { this.calls.push("connect"); }
    disconnect() { this.calls.push("disconnect"); }
    refresh() { this.calls.push("refresh"); }
    toggleLed() { this.calls.push("toggle-led"); }
    saveConfiguration(profile, unsafeOverclock) { this.calls.push(["save-configuration", profile, unsafeOverclock]); }
    clearConfigurationSaveMessage() { this.state.configurationSaveMessage = ""; }
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
    setInterval(callback, interval) {
      if (!polling) polling = { callback, interval };
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
  assert.ok(!html.includes('id="usb-identity"'));
  assert.ok(!html.includes("USB identity"));
  const configuration = html.match(/<section aria-labelledby="configuration-title">([\s\S]*?)<\/section>/)[1];
  assert.match(html, /id="identity-title"[\s\S]*?<section aria-labelledby="configuration-title">[\s\S]*?<section aria-labelledby="runtime-title">/);
  assert.match(configuration, /id="configuration-profile"/);
  assert.match(configuration, /id="configuration-unsafe-ack"/);
  assert.match(configuration, /role="status" aria-live="polite"/);
  assert.match(configuration, /class="section-heading">[\s\S]*?class="actions"[\s\S]*?id="save-configuration"[\s\S]*?<\/div>\s*<\/div>/);
  assert.match(configuration, /class="summary configuration-summary">[\s\S]*?class="summary-item">[\s\S]*?<label for="configuration-profile">System clock profile<\/label>\s*<select/);
  assert.doesNotMatch(configuration, /configuration-note|configuration-detail|configuration-status/);
});

test("persistent configuration uses target profiles and gates unsafe saves", () => {
  const { client, get } = fixture();
  client.state.dashboard = { connected: true, hardware: { mcu_id: 2 },
    configuration: { stored_profile: 2 }, configuration_save: { status: 0, profile: 2 },
    channels: Array.from({ length: 6 }, () => ({ cdc_open: false })) };
  client.state.phase = "connected";
  client.render(client.state);
  const profile = get("configuration-profile");
  assert.deepEqual(profile.children.map(option => [option.value, option.textContent]), [
    ["0", "SDK default (150 MHz)"], ["1", "150 MHz"], ["2", "200 MHz"], ["3", "250 MHz"], ["4", "280 MHz (RP2350)"],
  ]);
  assert.equal(profile.value, "2");
  assert.equal(profile.disabled, false);
  assert.equal(get("configuration-acknowledgement").hidden, false);
  assert.equal(get("save-configuration").disabled, true);
  get("configuration-unsafe-ack").checked = true;
  get("configuration-unsafe-ack").change();
  assert.equal(get("save-configuration").disabled, false);
  get("save-configuration").click();
  assert.deepEqual(client.calls.at(-1), ["save-configuration", 2, true]);
  profile.value = "0";
  profile.change();
  assert.equal(get("configuration-acknowledgement").hidden, true);
  client.state.dashboard.channels[3].cdc_open = true;
  client.render(client.state);
  assert.equal(profile.disabled, true);
  assert.equal(get("configuration-unsafe-ack").disabled, true);
  assert.equal(get("save-configuration").disabled, true);
});

test("configuration read failures disable Save and remain visible without the removed summary", () => {
  const { client, get } = fixture();
  client.state.dashboard = { connected: true, hardware: { mcu_id: 1 },
    configuration: { stored_profile: null }, configuration_save: null, configuration_save_error: "Unsupported report",
    channels: Array.from({ length: 6 }, () => ({ cdc_open: false })) };
  client.phase("connected");
  assert.equal(get("save-configuration").disabled, true);
  assert.equal(get("configuration-acknowledgement").hidden, true);
  assert.equal(get("configuration-message").textContent, "Unsupported report");
  assert.equal(get("configuration-message").dataset.error, "true");
});

test("metadata continues refreshing automatically every second", () => {
  const { client, polling, agePolling } = fixture();
  assert.equal(polling.interval, 1000);
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
  assert.equal(get("toggle-led").disabled, true);
  client.state.dashboard.led = { manual_on: false, output_on: false };
  client.phase("connected");
  assert.equal(get("toggle-led").disabled, false);
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
