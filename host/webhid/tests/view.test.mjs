import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import { renderDashboard } from "../js/view.mjs";

function fixture(html) {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [
    `#${id}`, { textContent: "", dataset: {}, parentElement: { dataset: {} },
      replaceChildren() {}, addEventListener() {} },
  ]));
  return {
    querySelector: selector => {
      if (selector.startsWith("meta")) return { content: "token" };
      if (selector === "#reset-board" && !elements.has(selector)) return null;
      assert.ok(elements.has(selector), `Missing HTML element ${selector}`);
      return elements.get(selector);
    },
    querySelectorAll: () => [],
  };
}

for (const frontend of ["webhid", "web"]) {
  test(`${frontend} separates identity/runtime and renders real board fields and unavailable states`, () => {
    const html = readFileSync(new URL(`../../${frontend}/index.html`, import.meta.url), "utf8");
    const identity = html.match(/<section aria-labelledby="identity-title">([\s\S]*?)<\/section>/)[1];
    const runtime = html.match(/<section aria-labelledby="runtime-title">([\s\S]*?)<\/section>/)[1];
    assert.ok(runtime.includes("CPU load (core 0 / core 1)"));
    assert.ok(runtime.includes("RAM (reserved/total)"));
    assert.ok(!html.includes('id="sequence"'));
    assert.ok(runtime.includes('id="toggle-led"'));
    assert.ok(!html.includes('id="led-state"'));
    const css = readFileSync(new URL(`../../${frontend}/css/dashboard.css`, import.meta.url), "utf8");
    assert.match(css, /\.button-led\[data-state="on"\].*background: var\(--teal\)/);
    assert.match(css, /\.button-led\[data-state="unknown"\].*border-style: dashed/);
    assert.ok(!html.match(/<section class="channels-section"[\s\S]*?<\/section>/)[0].includes('id="toggle-led"'));
    for (const id of ["product-name", "mcu", "system-clock", "firmware", "serial"]) {
      assert.ok(identity.includes(`id="${id}"`));
      assert.ok(!runtime.includes(`id="${id}"`));
    }
    assert.ok(!html.includes("traffic-summary"));
    assert.equal([...html.matchAll(/id="overflow-total"/g)].length, 1);
    for (const id of ["uptime", "cpu-usage", "ram-usage", "temperature", "overflow-total"]) {
      assert.ok(runtime.includes(`id="${id}"`));
      assert.ok(!identity.includes(`id="${id}"`));
    }
    const document = fixture(html);
    const data = {
      connected: true, identity: { product_name: "PicoUart", serial_number: "0123456789ABCDEF" },
      hardware: { mcu: "RP2350", system_clock_hz: 150000000 },
      board: { firmware_version: "1.2.3", temperature_celsius: 25.3 },
      runtime: { uptime_ms: 90061000, cpu_usage_basis_points: [10000, null],
        ram_reserved_bytes: 65536, ram_total_bytes: 524288 },
      overflow_counts: [0, 0, 0, 0, 0, 0], channels: [],
      led: { manual_on: false, output_on: true },
    };
    let render;
    if (frontend === "webhid") {
      globalThis.document = document;
      render = renderDashboard;
    } else {
      const context = vm.createContext({ document, fetch: () => new Promise(() => {}),
        window: { setInterval() {} } });
      vm.runInContext(readFileSync(new URL("../../web/js/dashboard.js", import.meta.url), "utf8"), context);
      render = value => { context.data = value; vm.runInContext("render(data)", context); };
    }
    try {
      render(data);
      const text = id => document.querySelector(`#${id}`).textContent;
      assert.equal(text("product-name"), "PicoUart");
      assert.equal(text("serial"), "0123456789ABCDEF");
      assert.equal(text("uptime"), "1d 1h 1m 1s");
      assert.equal(text("cpu-usage"), "100.0% / --");
      assert.equal(text("ram-usage"), "64.0 / 512 KiB");
      assert.equal(text("temperature"), "25.30 \u00b0C");
      assert.equal(text("overflow-total"), "0 B");
      render({ ...data, overflow_counts: [1, 2, 3, 4, 5, 6] });
      assert.equal(text("overflow-total"), "21 B");
      render({ ...data, overflow_counts: [null, 0, 0, 0, 0, 0] });
      assert.equal(text("overflow-total"), "--");
      render(data);
      assert.equal(text("toggle-led"), "Board LED: On");
      assert.equal(document.querySelector("#toggle-led").dataset.state, "on");
      render({ ...data, led: { manual_on: false, output_on: false } });
      assert.equal(text("toggle-led"), "Board LED: Off");
      assert.equal(document.querySelector("#toggle-led").dataset.state, "off");
      render({ ...data, led: null, led_error: "LED unsupported" });
      assert.equal(text("toggle-led"), "Board LED: --");
      assert.equal(document.querySelector("#toggle-led").dataset.state, "unknown");
      assert.match(text("status-message"), /LED unsupported/);
      render({ ...data, connected: false });
      assert.equal(text("toggle-led"), "Board LED: --");
      assert.equal(document.querySelector("#toggle-led").dataset.state, "unknown");
      if (frontend === "web") {
        render({ ...data, led: null });
        assert.equal(document.querySelector("#toggle-led").disabled, false);
        render({ ...data, led: { manual_on: null, output_on: null } });
        assert.equal(document.querySelector("#toggle-led").disabled, true);
        render({ ...data, connected: false });
        assert.equal(document.querySelector("#toggle-led").disabled, true);
      }
      render({ ...data, identity: null, runtime: null, runtime_error: "Firmware upgrade required" });
      for (const id of ["product-name", "serial", "uptime", "cpu-usage", "ram-usage"]) {
        assert.equal(text(id), "--");
      }
      assert.match(text("status-message"), /Firmware upgrade required/);
    } finally {
      delete globalThis.document;
    }
  });
}
