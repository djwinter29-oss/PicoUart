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

test("webhid render shows identity and runtime summaries", () => {
  const html = readFileSync(new URL("../../webhid/index.html", import.meta.url), "utf8");
  const document = fixture(html);
  globalThis.document = document;
  try {
    renderDashboard({
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
        capacities: { uart_tx_ring: 4096, uart_rx_ring: 4096, cdc_tx_fifo: 2048, cdc_rx_fifo: 2048 },
        pins: Array.from({ length: 6 }, () => ({ tx: 0, rx: 1, rts: 255, cts: 255 })),
      },
      channels: Array.from({ length: 6 }, (_, id) => ({
        id,
        channel_state: 2,
        channel_state_name: "Ready",
        uart_measurement_state_name: "Valid",
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
        rx_dropped_bytes: 0,
        rx_error_observations: 0,
        last_configuration_error_reason: 0,
        last_configuration_error: "None recorded",
        peaks: { tx_ring: 0, rx_ring: 0, cdc_tx: 0, cdc_rx: 0 },
      })),
    });
    assert.equal(document.querySelector("#product-name").textContent, "PicoUart");
    assert.equal(document.querySelector("#board-model").textContent, "Pico 2 (RP2350)");
    assert.equal(document.querySelector("#serial").textContent, "ABC123");
    assert.equal(document.querySelector("#firmware").textContent, "1.2.3");
    assert.equal(document.querySelector("#service-load").textContent, "12.5% / 5.0%");
    assert.equal(document.querySelector("#toggle-led").textContent, "Board LED: On");
    assert.doesNotMatch(html, /<span>Microcontroller<\/span>/);
    assert.equal(document.querySelector("#channel-rows").children[0].children[2].children.length, 1);
    const lifecycle = document.querySelector("#channel-rows").children[0].children[1];
    assert.equal(lifecycle.children[1].className, "health-flags");
    assert.equal(lifecycle.children[1].textContent, "UART Valid");
    const traffic = document.querySelector("#channel-rows").children[0].children[3].children[0];
    assert.deepEqual(traffic.children.map(metric => metric.children[0].textContent), [
      "UART TX", "UART RX", "USB RX", "USB TX",
    ]);
    assert.equal(traffic.children[0].children[1].textContent, "0.0 KB/s");
    const diagnosticsButton = document.querySelector("#channel-rows").children[0].children[4].children[0];
    assert.equal(diagnosticsButton.textContent, "Diagnostics");
    diagnosticsButton.click();
    assert.equal(document.querySelector("#channel-diagnostics-dialog").open, true);
    assert.equal(document.querySelector("#channel-diagnostics-title").textContent, "CDC0 diagnostics");
    assert.match(document.querySelector("#channel-diagnostics-content").textContent, /UART TX ring peak/);
  } finally {
    delete globalThis.document;
  }
});

test("host web runtime section reflects service-load terminology", () => {
  const html = readFileSync(new URL("../../web/index.html", import.meta.url), "utf8");
  const runtime = html.match(/<section aria-labelledby="runtime-title">([\s\S]*?)<\/section>/)[1];
  assert.ok(runtime.includes("Service load (USB core 0 / UART core 1)"));
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
