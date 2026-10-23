import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const dashboard = await readFile(new URL("../js/dashboard.js", import.meta.url), "utf8");
const markup = await readFile(new URL("../index.html", import.meta.url), "utf8");

test("dashboard uses the HID snapshot and diagnostic contract fields", () => {
  for (const field of [
    "service_load_basis_points",
    "ram_reserved_bytes",
    "ram_total_bytes",
    "channel_pins",
    "reset_cause",
    "uart_tx_ring_capacity_bytes",
    "uart_rx_ring_capacity_bytes",
    "cdc_tx_fifo_capacity_bytes",
    "cdc_rx_fifo_capacity_bytes",
    "uart_measurement_state",
    "uart_sample_age_100ms",
    "last_control_error_reason",
    "rx_overrun_count",
    "rx_error_count",
    "last_fault_uptime_s",
    "uart_tx_ring_peak_bytes",
    "uart_rx_ring_peak_bytes",
    "cdc_tx_fifo_peak_bytes",
    "cdc_rx_fifo_peak_bytes",
  ]) assert.ok(dashboard.includes(field), `missing ${field}`);

  for (const legacy of [
    "health_labels",
    "init_failed",
    "control_pending",
    "cpu_usage_basis_points",
    "configuration_save",
    "unsafe_overclock",
    "active_profile",
  ]) assert.ok(!dashboard.includes(legacy), `legacy field remains: ${legacy}`);
});

test("diagnostics are requested on demand rather than at snapshot cadence", () => {
  assert.match(dashboard, /fetch\(`\/api\/diagnostics\?board=/);
  assert.match(dashboard, /details\.addEventListener\("toggle",[\s\S]*?if \(!details\.open\) return;[\s\S]*?loadDiagnostics\(\)/);
  const refresh = dashboard.match(/async function refresh\(\) \{([\s\S]*?)\n\}/)?.[1] ?? "";
  assert.ok(refresh.includes("/api/status"));
  assert.ok(!refresh.includes("/api/diagnostics"));
});

test("dashboard presents lifecycle, static configuration, and honest service-load labels", () => {
  assert.match(markup, /<th scope="col">Lifecycle<\/th>/);
  assert.match(markup, /<th scope="col">Diagnostics<\/th>/);
  assert.match(markup, /Stored boot profile/);
  assert.match(markup, /Reset cause/);
  assert.match(markup, /Service load \(USB core 0 \/ UART core 1\)/);
  assert.doesNotMatch(markup, /CPU load|unsafe overclock|Save &amp; Reboot/);
});
