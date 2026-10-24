import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const dashboard = await readFile(new URL("../js/dashboard.js", import.meta.url), "utf8");
const stylesheet = await readFile(new URL("../css/dashboard.css", import.meta.url), "utf8");
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
    "active_profile",
  ]) assert.ok(!dashboard.includes(legacy), `legacy field remains: ${legacy}`);
});

test("diagnostics are requested on demand rather than at snapshot cadence", () => {
  assert.match(dashboard, /fetch\(`\/api\/diagnostics\?board=/);
  assert.match(dashboard, /diagnosticsDialog\.showModal\(\)[\s\S]*?loadDiagnostics\(\)/);
  assert.match(dashboard, /#refresh-diagnostics[\s\S]*?await loadDiagnostics\(\)/);
  const refresh = dashboard.match(/async function refresh\(\) \{([\s\S]*?)\n\}/)?.[1] ?? "";
  assert.ok(refresh.includes("/api/status"));
  assert.ok(!refresh.includes("/api/diagnostics"));
});

test("dashboard presents lifecycle, static configuration, and honest service-load labels", () => {
  const identity = markup.match(/<div class="summary identity-summary">([\s\S]*?)<\/div>\s*<\/section>/)?.[1] ?? "";
  const identitySection = markup.match(/<section aria-labelledby="identity-title">([\s\S]*?)<\/section>/)?.[1] ?? "";
  const configuration = markup.match(/<div class="summary configuration-summary">([\s\S]*?)<\/div>\s*<details class="pin-map-disclosure">/)?.[1] ?? "";
  const configurationSection = markup.match(/<section aria-labelledby="configuration-title">([\s\S]*?)<\/section>/)?.[1] ?? "";
  const configurationDialog = markup.match(/<dialog class="configuration-dialog" id="configuration-dialog"[^>]*>([\s\S]*?)<\/dialog>/)?.[1] ?? "";
  assert.equal((identity.match(/class="summary-item"/g) ?? []).length, 6);
  assert.doesNotMatch(identity, /Microcontroller/);
  assert.match(identity, /Product[\s\S]*?Board model[\s\S]*?Serial \/ UID[\s\S]*?System clock[\s\S]*?Firmware RAM reserved \/ total[\s\S]*?Firmware version/);
  assert.equal((configuration.match(/class="summary-item"/g) ?? []).length, 6);
  assert.match(identitySection, /id="rescan-boards"/);
  assert.doesNotMatch(identitySection, /clock-profile|save-configuration/);
  for (const id of ["clock-profile", "save-configuration", "overclock-ack"]) {
    assert.equal((markup.match(new RegExp(`id="${id}"`, "g")) ?? []).length, 1, `${id} must appear once`);
  }
  assert.match(markup, /<th scope="col">Lifecycle<\/th>/);
  assert.match(markup, /<th scope="col">Diagnostics<\/th>/);
  assert.match(configuration, /<select id="clock-profile" disabled>/);
  assert.doesNotMatch(configuration, /overclock-ack/);
  assert.match(configurationSection, /<button class="button button-secondary" id="save-configuration" type="button" hidden disabled>/);
  assert.match(configurationDialog, /id="overclock-ack" type="checkbox" disabled/);
  assert.match(configurationDialog, /id="cancel-configuration"/);
  assert.match(configurationDialog, /id="confirm-configuration"/);
  assert.match(dashboard, /fetch\(`\/api\/configuration\?board=/);
  assert.match(dashboard, /JSON\.stringify\(\{ profile, unsafe_overclock: unsafeOverclock \}\)/);
  assert.match(dashboard, /configurationDialog\.showModal\(\)/);
  assert.match(dashboard, /saveConfigurationButton\.addEventListener\("click", openConfigurationConfirmation\)/);
  assert.match(dashboard, /confirmConfigurationButton\.addEventListener\("click", saveConfiguration\)/);
  assert.match(dashboard, /configurationSavePending = true/);
  assert.match(dashboard, /configurationSavePending \|\| !available/);
  assert.match(markup, /<th scope="col"(?: title="[^"]+")?>Traffic<\/th>[\s\S]*?<th scope="col">Diagnostics<\/th>/);
  assert.match(markup, /title="Rates are averaged over approximately the last five seconds\./);
  assert.match(markup, /<dialog class="diagnostics-dialog" id="diagnostics-dialog"/);
  assert.match(markup, /Stored boot profile/);
  assert.match(markup, /Reset cause/);
  assert.match(markup, /Service load \(USB core 0 \/ UART core 1\)/);
  assert.doesNotMatch(markup, /CPU load|unsafe overclock/);
  assert.match(markup, /<details class="pin-map-disclosure">\s*<summary>Channel pin mapping<\/summary>\s*<div class="pin-map" id="pin-map"/);
  assert.match(markup, /Uptime[\s\S]*?Board temperature[\s\S]*?Service load \(USB core 0 \/ UART core 1\)/);
  assert.match(dashboard, /total\.textContent = formatBytes\(channel\.totals\?\.\[key\]\)/);
  assert.match(dashboard, /\(\(current - previous\) >>> 0\) \* 1000 \/ elapsed/);
  assert.match(dashboard, /boardIdentityName\([\s\S]*?data\.identity\?\.mcu_id \?\? data\.identity\?\.mcu/);
  assert.match(dashboard, /measurement\.textContent = stale \? "Waiting for sample"/);
  assert.match(dashboard, /configuration\.append\(settings\)/);
  assert.doesNotMatch(dashboard, /pins\.textContent/);
  assert.match(stylesheet, /\.health-flags \{[^}]*height: 2\.8em;[^}]*overflow: hidden;/);
  assert.match(dashboard, /\(view\.rates\[key\] \/ 1000\)\.toFixed\(1\)/);
  assert.match(dashboard, /\["UART TX", "uart_tx", true\],[\s\S]*?\["UART RX", "uart_rx", true\],[\s\S]*?\["USB RX", "usb_rx", false\],[\s\S]*?\["USB TX", "usb_tx", false\]/);
  assert.match(stylesheet, /\.traffic-metrics \{[^}]*grid-template-columns: max-content max-content;[^}]*grid-template-rows: repeat\(2, auto\);[^}]*grid-auto-flow: column;/);
  assert.match(stylesheet, /\.traffic-metrics > span \{[^}]*grid-template-columns: max-content 11ch 15ch;/);
  assert.match(stylesheet, /\.traffic b \{[^}]*text-align: right;/);
  assert.match(stylesheet, /\.traffic-total \{[^}]*text-align: right;/);
  assert.match(stylesheet, /\.traffic b \{[^}]*font-size: 10px;/);
  assert.match(stylesheet, /\.summary\.identity-summary \{ grid-template-columns: repeat\(6, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.identity-summary \{ grid-template-columns: repeat\(3, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.identity-summary \{ grid-template-columns: repeat\(2, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.configuration-summary \{ grid-template-columns: repeat\(6, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.configuration-summary \{ grid-template-columns: repeat\(3, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.configuration-summary \{ grid-template-columns: repeat\(2, minmax\(0, 1fr\)\); \}/);
});
