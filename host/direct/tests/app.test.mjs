// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

function fixture(secure = true, hid = {}) {
  const html = readFileSync(new URL("../src/index.html", import.meta.url), "utf8");
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [
    `#${id}`, { textContent: "", dataset: {}, parentElement: { dataset: {} },
      children: [], value: "", checked: false, disabled: false, open: false,
      append(...children) { this.children.push(...children); },
      replaceChildren(...children) { this.children = children; },
      showModal() { this.open = true; },
      close() { this.open = false; },
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
      this.generation = 0;
      this.calls = [];
      this.state = { phase: hid ? "disconnected" : "unsupported", dashboard: {},
      configurationSaveMessage: "",
      message: "No device selected", error: null };
    }
    connect() { this.calls.push("connect"); }
    disconnect() { this.calls.push("disconnect"); }
    refresh() { this.calls.push("refresh"); }
    toggleLed() { this.calls.push("toggle-led"); }
    setLed(on) { this.calls.push("toggle-led"); this.state.dashboard.led = { output_on: on, manual_on: on }; }
    saveConfiguration(profile, unsafeOverclock) {
      this.calls.push(["save-configuration", profile, unsafeOverclock]);
      return this.saveConfigurationResult;
    }
    acknowledgeConfigurationRecovery() {
      this.calls.push("acknowledge-configuration-recovery");
      this.state.dashboard.configuration_save_unconfirmed = false;
      this.state.dashboard.configuration_recovery_ready = false;
      this.render(this.state);
    }
    reboot(confirmed) { this.calls.push(["reboot", confirmed]); }
    clearConfigurationSaveMessage() { this.state.configurationSaveMessage = ""; }
    phase(phase, error = null) {
      this.state = { ...this.state, phase, error };
      this.render(this.state);
    }
  }
  const source = readFileSync(new URL("../src/js/app.mjs", import.meta.url), "utf8")
    .replace(/^import .*;\n/gm, "");
  let polling;
  let agePolling;
  const freshnessUpdates = [];
  vm.runInNewContext(source, { document, navigator: { hid }, isSecureContext: secure,
    PicoUartWebHid: FakeClient, renderDashboard() {},
    updateReportAge(data) { assert.equal(data, client.state.dashboard); },
    updateChannelFreshness(data) { freshnessUpdates.push(data); },
    updateDiagnosticsAge() {},
    setInterval(callback, interval) {
      if (!polling) polling = { callback, interval };
      else agePolling = { callback, interval };
    } });
  return { html, client, polling, agePolling, freshnessUpdates,
    get: id => document.querySelector(`#${id}`) };
}

test("Direct Dashboard distinguishes all assigned reset causes and preserves unknown codes", () => {
  const view = fixture();
  const labels = ["Unknown (not distinguished)", "Power-on", "Watchdog reset", "Software-requested reset",
    "HardFault recovery", "Power-on or brownout", "Brownout", "RUN-pin reset", "Debugger reset",
    "User-requested reboot", "Configuration-save reboot"];
  for (const [reset_cause, label] of [...labels.entries(), [255, "Unknown (255)"]]) {
    view.client.state.dashboard.configuration = { stored_profile: 0, reset_cause };
    view.client.phase("connected");
    assert.equal(view.get("reset-cause").textContent, label);
  }
  view.client.state.dashboard.configuration = null;
  view.client.phase("disconnected");
  assert.equal(view.get("reset-cause").textContent, "--");
});

test("one connection button belongs to Device overview, with no separate refresh or disconnect", () => {
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
  assert.match(configuration, /<h2 id="configuration-title">Hardware &amp; boot<\/h2>/);
  assert.match(configuration, /id="configuration-profile"/);
  assert.doesNotMatch(configuration, /configuration-unsafe-ack/);
  assert.match(html, /<dialog class="confirmation-dialog"[^>]*>[\s\S]*?id="configuration-unsafe-ack"/);
  assert.match(configuration, /role="status" aria-live="polite"/);
  assert.match(configuration, /class="section-heading">[\s\S]*?class="actions"[\s\S]*?id="save-configuration"[\s\S]*?<\/div>\s*<\/div>/);
  assert.match(configuration, /class="summary configuration-summary">[\s\S]*?class="summary-item">[\s\S]*?<label for="configuration-profile">Board clock profile<\/label>\s*<select/);
  assert.match(configuration, /id="uart-tx-capacity"[\s\S]*?id="uart-rx-capacity"[\s\S]*?id="cdc-tx-capacity"[\s\S]*?id="cdc-rx-capacity"/);
  assert.match(html, /<details class="pin-map-disclosure">[\s\S]*?id="board-pins"/);
  assert.doesNotMatch(configuration, /configuration-note|configuration-detail|configuration-status/);
});

test("persistent configuration uses target profiles and gates unsafe saves", async () => {
  const { client, get } = fixture();
  client.saveConfigurationResult = { action: 4, status: 2 };
  client.state.dashboard = { connected: true, hardware: { mcu_id: 2 },
    identity: { capabilities: 7, clock_profile_mask: 31 },
    configuration: { stored_profile: 2 }, configuration_save: { status: 0, profile: 2 },
    channels: Array.from({ length: 6 }, () => ({ channel_state: 0, input_updated_at: new Date().toISOString() })) };
  client.state.phase = "connected";
  client.render(client.state);
  const profile = get("configuration-profile");
  assert.deepEqual(profile.children.map(option => [option.value, option.textContent]), [
    ["0", "SDK default"], ["1", "150 MHz"], ["2", "200 MHz"], ["3", "250 MHz"], ["4", "280 MHz"],
  ]);
  assert.equal(profile.value, "2");
  assert.equal(profile.disabled, false);
  assert.equal(get("save-configuration").disabled, true);
  profile.value = "1";
  profile.change();
  assert.equal(get("save-configuration").disabled, false);
  get("save-configuration").click();
  assert.equal(get("configuration-dialog").open, true);
  assert.equal(get("configuration-acknowledgement").hidden, false);
  assert.equal(get("confirm-configuration").disabled, true);
  get("configuration-unsafe-ack").checked = true;
  get("configuration-unsafe-ack").change();
  assert.equal(get("confirm-configuration").disabled, false);
  await get("confirm-configuration").click();
  assert.deepEqual(client.calls.at(-1), ["save-configuration", 1, true]);
  assert.match(get("configuration-message").textContent, /^Saved 150 MHz/);
  assert.equal(get("configuration-dialog").open, false);
  profile.value = "0";
  profile.change();
  get("save-configuration").click();
  assert.equal(get("configuration-acknowledgement").hidden, true);
  assert.equal(get("confirm-configuration").disabled, false);
  get("cancel-configuration").click();
  client.state.dashboard.channels[3].channel_state = 2;
  client.render(client.state);
  assert.equal(profile.disabled, false);
  assert.equal(get("configuration-unsafe-ack").disabled, true);
  assert.equal(get("save-configuration").disabled, true);
  client.state.dashboard.channels = [];
  client.render(client.state);
  assert.equal(profile.disabled, false);
  client.phase("disconnected");
  assert.equal(profile.disabled, true);
});

test("Save & Reboot stays disabled until the selected profile differs from its baseline", () => {
  const { client, get } = fixture();
  client.state.dashboard = { identity: { capabilities: 4, clock_profile_mask: 31 },
    configuration: { stored_profile: null },
    channels: Array.from({ length: 6 }, () => ({ channel_state: 0, input_updated_at: new Date().toISOString() })) };
  client.state.phase = "connected";
  client.render(client.state);
  const profile = get("configuration-profile");
  const saveButton = get("save-configuration");
  assert.equal(profile.value, "0");
  assert.equal(saveButton.disabled, true);
  profile.value = "1";
  profile.change();
  assert.equal(saveButton.disabled, false);
  profile.value = "0";
  profile.change();
  assert.equal(saveButton.disabled, true);
});

test("profile hydration waits for configuration readback and preserves user edits", () => {
  const { client, get } = fixture();
  client.state.dashboard = {
    identity: { capabilities: 4, clock_profile_mask: 31 },
    configuration: null,
    channels: Array.from({ length: 6 }, () => ({
      channel_state: 0,
      input_updated_at: new Date().toISOString(),
    })),
  };
  client.state.phase = "connected";
  client.render(client.state);

  const profile = get("configuration-profile");
  const saveButton = get("save-configuration");
  assert.equal(profile.value, "0");
  assert.equal(saveButton.disabled, true);

  client.state.dashboard.configuration = { stored_profile: 2 };
  client.render(client.state);
  assert.equal(profile.value, "2");
  assert.equal(saveButton.disabled, true);

  profile.value = "1";
  profile.change();
  assert.equal(saveButton.disabled, false);
  client.state.dashboard.configuration = { stored_profile: 3 };
  client.render(client.state);
  assert.equal(profile.value, "1");
  assert.equal(saveButton.disabled, false);
});

test("unavailable recovery storage keeps monitoring visible and gates configuration saves", () => {
  const { client, get } = fixture();
  client.state.dashboard = {
    identity: { capabilities: 4, clock_profile_mask: 31 },
    configuration: { stored_profile: 2 },
    configuration_save_storage_error: "Browser storage is unavailable",
    channels: Array.from({ length: 6 }, () => ({
      channel_state: 0,
      input_updated_at: new Date().toISOString(),
    })),
  };
  client.state.phase = "connected";
  client.render(client.state);

  assert.equal(get("configuration-profile").disabled, false);
  assert.equal(get("save-configuration").disabled, true);
  assert.equal(get("configuration-message").textContent, "Browser storage is unavailable");
  assert.equal(get("configuration-message").dataset.error, "true");
});

test("Save & Reboot never reports success without a confirmed completion result", async () => {
  const { client, get } = fixture();
  client.state.dashboard = {
    identity: { capabilities: 4, clock_profile_mask: 31 },
    configuration: { stored_profile: 2 },
    channels: Array.from({ length: 6 }, () => ({
      channel_state: 0,
      input_updated_at: new Date().toISOString(),
    })),
  };
  client.state.phase = "connected";
  client.render(client.state);
  get("configuration-profile").value = "1";
  get("configuration-profile").change();
  get("save-configuration").click();
  get("configuration-unsafe-ack").checked = true;
  get("configuration-unsafe-ack").change();

  await get("confirm-configuration").click();

  assert.match(get("configuration-message").textContent, /confirmed completion; outcome is unconfirmed/);
  assert.doesNotMatch(get("configuration-message").textContent, /^Saved/);
  assert.equal(get("configuration-message").dataset.error, "true");
});

test("uncertain saves require a fresh readback and explicit acknowledgement before retry", () => {
  const { client, get } = fixture();
  client.state.dashboard = {
    identity: { capabilities: 4, clock_profile_mask: 31, serial_number: "0123456789ABCDEF" },
    configuration: { stored_profile: 2 },
    configuration_save_unconfirmed: true,
    configuration_save_profile: 1,
    configuration_recovery_ready: false,
    channels: Array.from({ length: 6 }, () => ({
      channel_state: 0,
      input_updated_at: new Date().toISOString(),
    })),
  };
  client.state.phase = "connected";
  client.render(client.state);

  const profile = get("configuration-profile");
  profile.value = "1";
  profile.change();
  assert.equal(get("save-configuration").disabled, true);
  assert.equal(get("configuration-recovery").hidden, false);
  assert.equal(get("configuration-recovery-message").textContent,
    "Previous save for 150 MHz is unconfirmed. Inspect the current stored profile (200 MHz).");
  assert.equal(get("acknowledge-configuration-recovery").disabled, true);

  client.state.dashboard.configuration_recovery_ready = true;
  client.render(client.state);
  const acknowledge = get("acknowledge-configuration-recovery");
  assert.equal(acknowledge.disabled, false);
  acknowledge.click();
  assert.deepEqual(client.calls, ["acknowledge-configuration-recovery"]);
  assert.equal(get("configuration-recovery").hidden, true);
  assert.equal(get("save-configuration").disabled, false);
});

test("configuration read failures disable Save and remain visible without the removed summary", () => {
  const { client, get } = fixture();
  client.state.dashboard = { connected: true, hardware: { mcu_id: 1 },
    configuration: { stored_profile: null }, configuration_save: null, configuration_save_error: "Unsupported report",
    channels: Array.from({ length: 6 }, () => ({ cdc_open: false })) };
  client.state.dashboard.configuration_error = "Unsupported report";
  client.phase("connected");
  assert.equal(get("save-configuration").disabled, true);
  assert.equal(get("configuration-dialog").open, false);
  assert.equal(get("configuration-message").textContent, "Unsupported report");
  assert.equal(get("configuration-message").dataset.error, "true");
});

test("board reboot uses the shared confirmation modal and requires explicit confirmation", () => {
  const { client, get } = fixture();
  client.state.phase = "connected";
  client.state.dashboard.identity = { capabilities: 2 };
  client.render(client.state);
  const rebootButton = get("reboot-board");
  assert.equal(rebootButton.hidden, false);
  assert.equal(rebootButton.disabled, false);
  rebootButton.click();
  assert.equal(get("reboot-dialog").open, true);
  assert.deepEqual(client.calls, []);
  get("cancel-reboot").click();
  assert.equal(get("reboot-dialog").open, false);
  assert.deepEqual(client.calls, []);
  rebootButton.click();
  get("confirm-reboot").click();
  assert.equal(get("reboot-dialog").open, false);
  assert.deepEqual(client.calls, [["reboot", true]]);
});

test("metadata continues refreshing automatically every second", () => {
  const { client, polling, agePolling, freshnessUpdates } = fixture();
  assert.equal(polling.interval, 1000);
  polling.callback();
  assert.deepEqual(client.calls, ["refresh"]);
  assert.equal(agePolling.interval, 1000);
  agePolling.callback();
  assert.deepEqual(freshnessUpdates, [client.state.dashboard]);
});

test("unconfirmed action outcomes remain visible after disconnect without attaching to the next connection", async () => {
  const { client, get } = fixture();
  client.generation = 3;
  client.state.phase = "connected";
  client.state.dashboard.identity = { capabilities: 2 };
  let rejectReboot;
  client.reboot = confirmed => {
    client.calls.push(["reboot", confirmed]);
    return new Promise((_, reject) => { rejectReboot = reject; });
  };
  client.render(client.state);
  get("reboot-board").click();
  const pending = get("confirm-reboot").click();
  client.generation++;
  client.phase("disconnected", "Device unplugged");
  rejectReboot(new Error("Board action submission failed; outcome is unconfirmed."));
  await pending;

  assert.match(get("status-message").textContent, /^Previous connection board action: .*outcome is unconfirmed/);
  assert.equal(get("status-message").parentElement.dataset.error, "true");
  get("connect").click();
  client.generation++;
  client.phase("connecting");
  assert.doesNotMatch(get("status-message").textContent, /outcome is unconfirmed/);
});

test("late configuration-save outcomes are shown as belonging to the previous connection", async () => {
  const { client, get } = fixture();
  client.state.dashboard = {
    identity: { capabilities: 4, clock_profile_mask: 31 },
    configuration: { stored_profile: 2 },
    channels: Array.from({ length: 6 }, () => ({
      channel_state: 0,
      input_updated_at: new Date().toISOString(),
    })),
  };
  client.state.phase = "connected";
  client.render(client.state);
  let rejectSave;
  client.saveConfiguration = (...args) => {
    client.calls.push(["save-configuration", ...args]);
    return new Promise((_, reject) => { rejectSave = reject; });
  };
  get("configuration-profile").value = "1";
  get("configuration-profile").change();
  get("save-configuration").click();
  get("configuration-unsafe-ack").checked = true;
  get("configuration-unsafe-ack").change();
  const pendingSave = get("confirm-configuration").click();

  client.generation++;
  client.phase("disconnected");
  client.state.dashboard = {
    identity: { capabilities: 4, clock_profile_mask: 31 },
    configuration: { stored_profile: 0 },
    channels: Array.from({ length: 6 }, () => ({
      channel_state: 0,
      input_updated_at: new Date().toISOString(),
    })),
  };
  client.phase("connected");
  rejectSave(new Error("Board action submission failed; outcome is unconfirmed."));
  await pendingSave;

  assert.equal(get("configuration-message").textContent, "");
  assert.match(get("status-message").textContent,
    /^Previous connection configuration save: .*outcome is unconfirmed/);
  assert.equal(get("status-message").parentElement.dataset.error, "true");
  assert.deepEqual(client.calls, [["save-configuration", 1, true, true]]);
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
  client.state.dashboard.identity = { capabilities: 1 };
  client.state.dashboard.telemetry = { led: { output_on: false } };
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
