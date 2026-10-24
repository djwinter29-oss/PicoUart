import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import { renderDashboard, updateReportAge } from "../js/view.mjs";

class Element {
  constructor(tag = "") {
    this.tag = tag;
    this.children = [];
    this.dataset = {};
    this.parentElement = { dataset: {} };
    this.text = "";
    this.hidden = false;
    this.disabled = false;
    this.className = "";
    this.title = "";
    this.value = "";
    this.events = {};
    this.open = false;
  }
  set textContent(value) { this.text = value; this.children = []; }
  get textContent() { return this.text + this.children.map(child => child.textContent).join(""); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  contains(target) { return this.children.some(child => child === target || child.contains?.(target)); }
  addEventListener(event, callback) { this.events[event] = callback; }
  click() { this.events.click?.(); }
  showModal() { this.open = true; }
  close() { this.open = false; }
}

function fixture(html) {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [`#${id}`, new Element()]));
  return {
    querySelector: selector => {
      if (selector.startsWith("meta")) return { content: "token" };
      assert.ok(elements.has(selector), `Missing HTML element ${selector}`);
      return elements.get(selector);
    },
    querySelectorAll: () => [],
    createElement: tag => new Element(tag),
    createTextNode: text => {
      const node = new Element();
      node.textContent = text;
      return node;
    },
  };
}

test("webhid report age text tracks freshness", () => {
  const html = readFileSync(new URL("../../webhid/index.html", import.meta.url), "utf8");
  const document = fixture(html);
  globalThis.document = document;
  try {
    const age = document.querySelector("#sample-time");
    updateReportAge({ connected: false }, 10000);
    assert.equal(age.hidden, true);
    updateReportAge({ connected: true, updated_at: "2026-01-01T00:00:00.000Z" }, Date.parse("2026-01-01T00:00:00.000Z"));
    assert.equal(age.textContent, "Telemetry 0 s ago");
    updateReportAge({ connected: true, updated_at: "2026-01-01T00:00:00.000Z" }, Date.parse("2026-01-01T00:00:05.000Z"));
    assert.equal(age.textContent, "Telemetry 5 s ago (stale)");
    assert.equal(age.dataset.stale, "true");
    updateReportAge({ connected: true, updated_at: null }, Date.parse("2026-01-01T00:00:05.000Z"));
    assert.equal(age.textContent, "Waiting for telemetry");
  } finally {
    delete globalThis.document;
  }
});

test("webhid render updates live cells without replacing diagnostics buttons", () => {
  const html = readFileSync(new URL("../../webhid/index.html", import.meta.url), "utf8");
  const document = fixture(html);
  globalThis.document = document;
  try {
    const dashboard = {
      connected: true,
      updated_at: new Date().toISOString(),
      identity: {
        product_name: "PicoUart",
        serial_number: "ABC123",
        mcu: "RP2350",
        board_model: "Pico 2",
        firmware_version: "1.2.3",
        ram_reserved_bytes: 65536,
        ram_total_bytes: 524288,
      },
      telemetry: {
        system_clock_hz: 150000000,
        temperature_celsius: 24.1,
        runtime: { uptime_ms: 9000, service_load_basis_points: [1250, 500] },
        led: { output_on: true },
      },
      configuration: {
        stored_profile_name: "150 MHz",
        reset_reason_name: "none",
        capacities: { uart_tx: 4096, uart_rx: 4096, cdc_tx: 2048, cdc_rx: 2048 },
        pins: Array.from({ length: 6 }, () => ({ tx: 0, rx: 1, rts: 255, cts: 255 })),
      },
      channels: Array.from({ length: 6 }, (_, id) => ({
        id,
        channel_state: 2,
        channel_state_name: "Ready",
        uart_measurement_state_name: "Current",
        baud_rate: 115200,
        data_bits: 8,
        parity: "N",
        stop_bits: 1,
        counters: { uart_tx: 0, uart_rx: 0, usb_tx: 0, usb_rx: 0 },
        rates: { uart_tx: 0, uart_rx: 0, usb_tx: 0, usb_rx: 0 },
        input_updated_at: new Date().toISOString(),
        feature_updated_at: null,
      })),
      channel_diagnostics: Array.from({ length: 6 }, () => ({
        updated_at: new Date().toISOString(),
        uart_measurement_state: 1,
        uart_measurement_state_name: "Current",
        uart_sample_age_100ms: 0,
        rx_dropped_bytes: 0,
        rx_error_observations: 0,
        last_configuration_error_reason: 0,
        last_configuration_error: "None recorded",
        last_fault_uptime_s: 0,
        peaks: { uart_tx: 0, uart_rx: 1, cdc_tx: 0, cdc_rx: 0 },
      })),
    };
    renderDashboard(dashboard);
    assert.equal(document.querySelector("#product-name").textContent, "PicoUart");
    assert.equal(document.querySelector("#board-model").textContent, "Pico 2 (RP2350)");
    assert.equal(document.querySelector("#serial").textContent, "ABC123");
    assert.equal(document.querySelector("#firmware").textContent, "1.2.3");
    assert.equal(document.querySelector("#service-load").textContent, "12.5% / 5.0%");
    assert.equal(document.querySelector("#toggle-led").textContent, "Board LED: On");
    assert.equal(document.querySelector("#toggle-led").dataset.state, "on");
    renderDashboard({ ...dashboard, telemetry: { ...dashboard.telemetry, led: { output_on: false } } });
    assert.equal(document.querySelector("#toggle-led").textContent, "Board LED: Off");
    assert.equal(document.querySelector("#toggle-led").dataset.state, "off");
    assert.doesNotMatch(html, /<span>Microcontroller<\/span>/);
    assert.equal(document.querySelector("#channel-rows").children[0].children[2].children.length, 1);
    const lifecycle = document.querySelector("#channel-rows").children[0].children[1];
    assert.equal(lifecycle.children[1].className, "health-flags");
    assert.equal(lifecycle.className, "channel-status");
    assert.equal(lifecycle.children[0].textContent, "Ready");
    assert.equal(lifecycle.children[0].hidden, false);
    assert.equal(lifecycle.children[1].textContent, "LIVE");
    const cachedDashboard = { ...dashboard, channels: dashboard.channels.map(channel =>
      ({ ...channel, uart_measurement_state_name: "Cached" })) };
    renderDashboard(cachedDashboard);
    assert.equal(lifecycle.children[1].textContent, "CACHED");
    const staleDashboard = { ...dashboard, channels: dashboard.channels.map(channel =>
      ({ ...channel, input_updated_at: new Date(Date.now() - 5000).toISOString() })) };
    renderDashboard(staleDashboard);
    assert.equal(lifecycle.children[0].hidden, true);
    assert.equal(lifecycle.children[1].textContent, "— Unavailable");
    renderDashboard(dashboard);
    const traffic = document.querySelector("#channel-rows").children[0].children[3].children[0];
    assert.deepEqual(traffic.children.map(metric => metric.children[0].textContent), [
      "UART TX", "UART RX", "USB RX", "USB TX",
    ]);
    assert.equal(traffic.children[0].children[1].textContent, "0.0 KB/s");
    const rows = document.querySelector("#channel-rows");
    const initialRow = rows.children[0];
    const diagnosticsButton = initialRow.children[4].children[0];
    const pointerUpdate = { ...dashboard, channels: dashboard.channels.map((channel, index) =>
      index === 0 ? { ...channel, baud_rate: 57600 } : channel) };
    renderDashboard(pointerUpdate);
    assert.equal(rows.children[0], initialRow);
    assert.equal(rows.children[0].children[4].children[0], diagnosticsButton);
    assert.equal(rows.children[0].children[2].children[0].textContent,
      `${(57600).toLocaleString()} 8N1`);

    const focusUpdate = { ...pointerUpdate, channels: pointerUpdate.channels.map((channel, index) =>
      index === 0 ? { ...channel, baud_rate: 38400 } : channel) };
    renderDashboard(focusUpdate);
    assert.equal(rows.children[0], initialRow);
    assert.equal(rows.children[0].children[4].children[0], diagnosticsButton);
    assert.equal(rows.children[0].children[2].children[0].textContent,
      `${(38400).toLocaleString()} 8N1`);

    assert.equal(diagnosticsButton.textContent, "Diagnostics");
    diagnosticsButton.click();
    assert.equal(document.querySelector("#channel-diagnostics-dialog").open, true);
    assert.equal(document.querySelector("#channel-diagnostics-title").textContent, "CDC0 diagnostics");
    const diagnostics = document.querySelector("#channel-diagnostics-content").children[0].children;
    assert.equal(diagnostics.length, 20);
    assert.deepEqual(diagnostics.filter((_, index) => index % 2 === 0).map(term => term.textContent), [
      "UART measurement", "UART sample age", "Known RX bytes dropped", "RX controller error observations",
      "Last configuration error", "Last fault observed", "UART TX ring observed peak since boot",
      "UART RX ring observed peak since boot", "CDC TX FIFO observed peak since boot",
      "CDC RX FIFO observed peak since boot",
    ]);
    assert.equal(diagnostics[1].textContent, "Current");
    assert.equal(diagnostics[3].textContent, "0.0 s");
    assert.equal(diagnostics[11].textContent, "None recorded");
    assert.equal(diagnostics[13].textContent, "0 B (0.0%)");
    assert.equal(diagnostics[15].textContent, "1 B (0.0%)");

    renderDashboard({ ...dashboard, connected: false, channels: dashboard.channels.map(channel => ({
      ...channel, input_updated_at: null, uart_measurement_state_name: null,
    })) });
    assert.equal(lifecycle.children[0].hidden, true);
    assert.equal(lifecycle.children[1].textContent, "— Unavailable");
  } finally {
    delete globalThis.document;
  }
});

test("host web runtime section reflects service-load terminology", () => {
  const html = readFileSync(new URL("../../web/index.html", import.meta.url), "utf8");
  const runtime = html.match(/<section aria-labelledby="runtime-title">([\s\S]*?)<\/section>/)[1];
  assert.ok(runtime.includes("Service load (Core 0 / Core 1)"));
  assert.ok(runtime.includes("Board temperature"));
  assert.ok(!runtime.includes("CPU load (core 0 / core 1)"));
});

test("host web updateReportAge keeps telemetry wording", () => {
  const html = readFileSync(new URL("../../web/index.html", import.meta.url), "utf8");
  const document = fixture(html);
  const context = vm.createContext({
    document,
    fetch: () => new Promise(() => {}),
    window: { setInterval() {}, clearInterval() {} },
  });
  vm.runInContext(readFileSync(new URL("../../web/js/dashboard.js", import.meta.url), "utf8"), context);
  context.data = { connected: true, updated_at: "2026-01-01T00:00:00.000Z" };
  context.now = Date.parse("2026-01-01T00:00:00.000Z");
  vm.runInContext("updateReportAge(data, now)", context);
  context.now = Date.parse("2026-01-01T00:00:04.000Z");
  vm.runInContext("updateReportAge(data, now)", context);
  assert.equal(document.querySelector("#sample-time").textContent, "Report 4 s ago (stale)");
});
