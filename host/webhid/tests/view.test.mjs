import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";
import { renderDashboard, channelPresentation, updateReportAge } from "../js/view.mjs";

class Element {
  constructor(tag = "") {
    this.tag = tag;
    this.children = [];
    this.dataset = {};
    this.events = {};
    this.parentElement = { dataset: {} };
    this.text = "";
  }
  set textContent(value) { this.text = value; this.children = []; }
  get textContent() { return this.text + this.children.map(child => child.textContent).join(""); }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
  addEventListener(type, callback) { this.events[type] = callback; }
}

function fixture(html) {
  const elements = new Map([...html.matchAll(/id="([^"]+)"/g)].map(([, id]) => [
    `#${id}`, new Element(),
  ]));
  return {
    querySelector: selector => {
      if (selector.startsWith("meta")) return { content: "token" };
      if (selector === "#reset-board" && !elements.has(selector)) return null;
      assert.ok(elements.has(selector), `Missing HTML element ${selector}`);
      return elements.get(selector);
    },
    querySelectorAll: () => [],
    createElement: tag => new Element(tag),
    createTextNode: text => { const node = new Element(); node.textContent = text; return node; },
  };
}

function descendants(element) {
  return [element, ...element.children.flatMap(descendants)];
}

for (const frontend of ["webhid", "web"]) {
  test(`${frontend} shows relative report age beside connection and detects stale reports`, () => {
    const html = readFileSync(new URL(`../../${frontend}/index.html`, import.meta.url), "utf8");
    assert.match(html, /id="connection"[\s\S]*?id="sample-time"[\s\S]*?<\/header>/);
    const document = fixture(html);
    let update;
    if (frontend === "webhid") {
      globalThis.document = document;
      update = updateReportAge;
    } else {
      const context = vm.createContext({ document, fetch: () => new Promise(() => {}),
        window: { setInterval() {} } });
      vm.runInContext(readFileSync(new URL("../../web/js/dashboard.js", import.meta.url), "utf8"), context);
      update = (data, now) => {
        context.data = data;
        context.now = now;
        vm.runInContext("updateReportAge(data, now)", context);
      };
    }
    try {
      const data = { connected: true, updated_at: "2026-01-01T00:00:00.000Z" };
      const age = document.querySelector("#sample-time");
      update({ connected: false }, 10000);
      assert.equal(age.hidden, true);
      update(data, 10000);
      assert.equal(age.textContent, "Report 0 s ago");
      update(data, 12000);
      assert.equal(age.textContent, "Report 2 s ago");
      assert.equal(age.dataset.stale, "false");
      update(data, 14000);
      assert.equal(age.textContent, "Report 4 s ago (stale)");
      assert.equal(age.dataset.stale, "true");
      update({ ...data, updated_at: "2026-01-01T00:00:01.000Z" }, 14000);
      assert.equal(age.textContent, "Report 0 s ago");
      update({ connected: true, updated_at: null }, 14000);
      assert.equal(age.textContent, "Waiting for report");
      update({ connected: false }, 14000);
      assert.equal(age.hidden, true);
    } finally {
      delete globalThis.document;
    }
  });

  function setup(document) {
    if (frontend === "webhid") {
      globalThis.document = document;
      return { render: renderDashboard, present: channelPresentation };
    }
    const context = vm.createContext({ document, fetch: () => new Promise(() => {}),
      window: { setInterval() {} } });
    vm.runInContext(readFileSync(new URL("../../web/js/dashboard.js", import.meta.url), "utf8"), context);
    return {
      render: data => { context.data = data; vm.runInContext("render(data)", context); },
      present: (data, channel, now) => {
        Object.assign(context, { data, channel, now });
        return vm.runInContext("channelPresentation(data, channel, now)", context);
      },
    };
  }

  test(`${frontend} measures rates, suppresses gaps/staleness and resets on reboot`, () => {
    const document = fixture(readFileSync(new URL(`../../${frontend}/index.html`, import.meta.url), "utf8"));
    const { present } = setup(document);
    const channel = { id: 0, health: 1, health_labels: ["ready"], cdc_open: false,
      totals: { uart_tx: 0, uart_rx: 0, usb_tx: 0, usb_rx: 0 } };
    const data = { connected: true, identity: { serial_number: "rate-test" }, traffic_gaps: 0,
      updated_at: new Date(100000).toISOString(), runtime: { uptime_ms: 5000 } };
    try {
      assert.equal(present({ ...data, updated_at: null }, channel, 99999).rates, null);
      channel.totals.uart_tx = 100;
      assert.equal(present(data, channel, 100000).rates, null);
      assert.equal(present(data, channel, 100000).state, "idle");
      data.updated_at = new Date(100999).toISOString();
      assert.equal(present(data, channel, 100999).rates, null);
      data.updated_at = new Date(101000).toISOString();
      channel.totals = { uart_tx: 1100, uart_rx: 2000, usb_tx: 3000, usb_rx: 4000 };
      let result = present(data, channel, 101000);
      assert.deepEqual({ ...result.rates }, { uart_tx: 1000, uart_rx: 2000, usb_tx: 3000, usb_rx: 4000 });
      assert.equal(result.state, "ready");
      assert.equal(present(data, channel, 104001).rates, null);
      data.traffic_gaps++;
      data.updated_at = new Date(102000).toISOString();
      assert.equal(present(data, channel, 102000).rates, null);
      data.updated_at = new Date(103000).toISOString();
      result = present(data, channel, 103000);
      assert.equal(result.rates.uart_tx, 0);
      assert.equal(result.state, "idle");
      data.runtime.uptime_ms = 1;
      assert.equal(present(data, channel, 103000).rates, null);
      data.connected = false;
      assert.equal(present(data, channel, 103000).state, "unknown");
    } finally {
      delete globalThis.document;
    }
  });

  test(`${frontend} renders settings and simple read-only fault flags`, () => {
    const html = readFileSync(new URL(`../../${frontend}/index.html`, import.meta.url), "utf8");
    assert.ok(!html.includes("Backend</th>") && !html.includes("Ring peak"));
    assert.ok(html.includes("UART configuration") && html.includes("Throughput"));
    assert.ok(!html.includes('id="configuration-status"') && !html.includes('id="configuration-detail"'));
    assert.deepEqual([...html.matchAll(/<th scope="col"(?: class="[^"]+")?>([^<]+)<\/th>/g)].map(match => match[1]),
      ["Port", "Host session", "UART configuration", "Health", "Throughput", "RX overflow"]);
    const document = fixture(html);
    const { render } = setup(document);
    const diagnostic = { baud_rate: 115200, data_bits: 8, stop_bits: 1, parity: "N", tx_pin: 0, rx_pin: 1,
      faults: { rx_error: { count: 3, last_seen_ms: 5000 } } };
    const channel = { id: 0, health: 0x81, state: "attention", health_labels: ["ready", "rx_error"], cdc_open: false,
      totals: { uart_tx: 0, uart_rx: 0, usb_tx: 0, usb_rx: 0 } };
    const data = { connected: true, identity: { serial_number: "fault-test" }, updated_at: new Date().toISOString(),
      traffic_gaps: 0, channels: [channel], channel_diagnostics: [diagnostic],
      configuration: { source: "defaults", reason: "crc_failed", stored_profile: null, active_profile: 1 },
      overflow_counts: [0, 0, 0, 0, 0, 0] };
    try {
      render(data);
      let rows = document.querySelector("#channel-rows");
      assert.equal(rows.children[0].children.length, 6);
      assert.equal(rows.children[0].children[1].textContent, "Closed");
      assert.equal(rows.children[0].children[2].className, "uart-configuration");
      assert.equal(rows.children[0].children[4].className, "traffic");
      assert.equal(rows.children[0].children[5].className, "overflow");
      assert.match(rows.textContent, /115200 8N1TX GP0 \/ RX GP1/);
      assert.equal(descendants(rows).find(node => node.className === "health-flags").textContent, "Recorded: rx_error");
      assert.ok(descendants(rows).some(node =>
        node.title === "Receive error recorded; check baud rate, framing and wiring."));
      assert.ok(!/last detected|events|expand|rx_error \(/.test(rows.textContent));
      assert.equal(descendants(rows).filter(node => ["details", "summary", "button"].includes(node.tag)).length, 0);
      assert.match(rows.textContent, /attention/);
      assert.ok(!rows.textContent.includes(">="));
      assert.ok(descendants(rows).some(node => node.title?.includes("0 B observed total")));
      assert.equal(descendants(rows).filter(node => node.tag === "button").length, 0);
      assert.ok(!rows.textContent.includes("(acknowledged)"));
      channel.health_labels = ["ready"];
      render(data);
      assert.match(rows.textContent, /attention/);
      assert.match(rows.textContent, /rx_error/);
      diagnostic.faults.rx_error = { count: 4, last_seen_ms: 6000 };
      render(data);
      assert.match(rows.textContent, /attention/);
      assert.equal(descendants(rows).find(node => node.className === "health-flags").textContent, "Recorded: rx_error");
      diagnostic.faults = {};
      render(data);
      assert.match(rows.textContent, /idle/);
      assert.equal(descendants(rows).find(node => node.className === "health-flags").textContent, "Receiver enabled");
      channel.health_labels.push("control_error");
      render(data);
      assert.match(rows.textContent, /attention/);
      channel.health_labels.push("rx_error", "control_pending");
      diagnostic.faults.rx_error = { count: 4, last_seen_ms: 6000 };
      render(data);
      assert.equal(descendants(rows).find(node => node.className === "health-flags").textContent,
        "Current: control_error; Recorded: rx_error");
      diagnostic.faults.control_error = { count: 1, last_seen_ms: 6000 };
      channel.health_labels = ["ready"];
      render(data);
      assert.equal(descendants(rows).find(node => node.className === "health-flags").textContent,
        "Recorded: control_error; Recorded: rx_error");
      render({ ...data, updated_at: null });
      assert.equal(descendants(rows).find(node => node.className === "health-flags").textContent,
        "Last reported: control_error; Last reported: rx_error");
      const metrics = descendants(rows).find(node => node.className === "traffic-metrics");
      assert.deepEqual(metrics.children.map(metric => metric.children[0].textContent),
        ["UART TX", "UART RX", "USB TX", "USB RX"]);
      assert.ok(metrics.children.every(metric => metric.children[1].tag === "b"));
      channel.health_labels = ["ready", "control_error", "rx_error"];
      render({ ...data, channel_diagnostics: null, diagnostics_error: "Firmware upgrade required" });
      assert.match(rows.textContent, /Firmware diagnostics unavailable/);
      assert.equal(descendants(rows).find(node => node.className === "health-flags").textContent,
        "Current: control_error; Recorded: rx_error");
      assert.ok(descendants(rows).some(node => node.title === "Firmware upgrade required"));
      assert.equal(descendants(rows).filter(node => node.textContent === "Clear flags").length, 0);
    } finally {
      delete globalThis.document;
    }
  });

  test(`${frontend} separates identity/runtime and renders real board fields and unavailable states`, () => {
    const html = readFileSync(new URL(`../../${frontend}/index.html`, import.meta.url), "utf8");
    const identity = html.match(/<section aria-labelledby="identity-title">([\s\S]*?)<\/section>/)[1];
    assert.ok(html.includes(frontend === "webhid"
      ? "<title>PicoUart Diagnostics | WebHID</title>"
      : "<title>PicoUart Diagnostics | {{ dashboard_type }}</title>"));
    const runtime = html.match(/<section aria-labelledby="runtime-title">([\s\S]*?)<\/section>/)[1];
    assert.ok(runtime.includes("CPU load (core 0 / core 1)"));
    assert.ok(runtime.includes("RAM (reserved / total)"));
    assert.ok(!html.includes('id="sequence"'));
    assert.ok(identity.includes('id="toggle-led"'));
    assert.ok(!html.includes('id="led-state"'));
    assert.ok(!runtime.includes('id="toggle-led"'));
    assert.ok(!html.includes("LIVE REPORT") && !html.includes("LIVE HEALTH AND TRAFFIC"));
    const css = readFileSync(new URL(`../../${frontend}/css/dashboard.css`, import.meta.url), "utf8");
    assert.match(css, /\.button-led\[data-state="on"\].*border-color: var\(--teal\)/);
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
      board: { firmware_version: "1.2.3", temperature_celsius: 22.92 },
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
      for (const [uptime_ms, expected] of [[0, "0s"], [5000, "5s"], [305000, "5m 5s"],
        [3600000, "1h 0s"], [86400000, "1d 0s"]]) {
        render({ ...data, runtime: { ...data.runtime, uptime_ms } });
        assert.equal(text("uptime"), expected);
      }
      render(data);
      assert.equal(text("cpu-usage"), "100.0% / --");
      assert.equal(text("ram-usage"), "64.0 / 512 KiB (12.5%)");
      assert.equal(text("temperature"), "22.9 \u00b0C");
      assert.equal(text("overflow-total"), "0 B");
      assert.equal(document.querySelector("#traffic-note").hidden, true);
      assert.equal(text("traffic-note"), "");
      render({ ...data, traffic_incomplete: true });
      assert.equal(document.querySelector("#traffic-note").hidden, false);
      assert.equal(text("traffic-note"), "Incomplete traffic samples detected; totals may undercount.");
      render(data);
      assert.equal(document.querySelector("#traffic-note").hidden, true);
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
        assert.equal(document.querySelector("#toggle-led").disabled, true);
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
