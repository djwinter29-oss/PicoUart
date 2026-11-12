// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const dashboard = await readFile(new URL("../src/js/dashboard.js", import.meta.url), "utf8");
const stylesheet = await readFile(new URL("../src/css/dashboard.css", import.meta.url), "utf8");
const markup = await readFile(new URL("../src/index.html", import.meta.url), "utf8");

test("Python and .NET share the Dashboard page heading", () => {
  assert.match(markup, /<h1>Dashboard<\/h1>/);
});

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
    "active_profile",
  ]) assert.ok(!dashboard.includes(legacy), `legacy field remains: ${legacy}`);
  assert.doesNotMatch(dashboard, /data\.configuration_save\b/);
});

test("diagnostics are requested on demand rather than at snapshot cadence", () => {
  assert.match(dashboard, /fetchJson\(\s*`\/api\/diagnostics\?board=/);
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
  const configurationDialog = markup.match(/<dialog class="confirmation-dialog" id="configuration-dialog"[^>]*>([\s\S]*?)<\/dialog>/)?.[1] ?? "";
  const rebootDialog = markup.match(/<dialog class="confirmation-dialog" id="reboot-dialog"[^>]*>([\s\S]*?)<\/dialog>/)?.[1] ?? "";
  assert.equal((identity.match(/class="summary-item"/g) ?? []).length, 6);
  assert.doesNotMatch(identity, /Microcontroller/);
  assert.match(identity, /Product[\s\S]*?Board model[\s\S]*?Serial \/ UID[\s\S]*?System clock[\s\S]*?Firmware RAM reserved \/ total[\s\S]*?Firmware version/);
  assert.equal((configuration.match(/class="summary-item"/g) ?? []).length, 6);
  assert.doesNotMatch(markup, /Host session/);
  assert.match(markup, /<th scope="col">Port<\/th>\s*<th scope="col">Channel status<\/th>\s*<th scope="col">UART configuration<\/th>/);
  assert.doesNotMatch(markup, /id="traffic-note"/);
  assert.doesNotMatch(dashboard, /querySelector\("#traffic-note"\)/);
  assert.match(identitySection, /id="rescan-boards"/);
  assert.doesNotMatch(identitySection, /clock-profile|save-configuration/);
  for (const id of ["clock-profile", "save-configuration", "overclock-ack"]) {
    assert.equal((markup.match(new RegExp(`id="${id}"`, "g")) ?? []).length, 1, `${id} must appear once`);
  }
  assert.match(markup, /<th scope="col">Channel status<\/th>/);
  assert.match(markup, /<th scope="col">Diagnostics<\/th>/);
  assert.match(configuration, /<select id="clock-profile" title="System clock profile applied after saving and rebooting\." disabled>/);
  assert.doesNotMatch(configuration, /overclock-ack/);
  assert.match(configurationSection, /<button class="button button-secondary" id="save-configuration" type="button" hidden disabled>/);
  assert.match(configurationDialog, /id="overclock-ack" type="checkbox" disabled/);
  assert.match(configurationDialog, /id="cancel-configuration"/);
  assert.match(configurationDialog, /id="confirm-configuration"/);
  assert.match(rebootDialog, /id="cancel-reboot"/);
  assert.match(rebootDialog, /id="confirm-reboot"/);
  assert.match(dashboard, /rebootDialog\.showModal\(\)/);
  assert.match(dashboard, /rebootDialog\.close\(\)[\s\S]*?runAction\("reset"\)/);
  assert.doesNotMatch(dashboard, /window\.confirm\(/);
  assert.match(dashboard, /fetchJson\(\s*`\/api\/configuration\?board=/);
  assert.match(dashboard, /JSON\.stringify\(\{ profile, unsafe_overclock: unsafeOverclock \}\)/);
  assert.match(dashboard, /configurationDialog\.showModal\(\)/);
  assert.match(dashboard, /saveConfigurationButton\.addEventListener\("click", openConfigurationConfirmation\)/);
  assert.match(dashboard, /confirmConfigurationButton\.addEventListener\("click", saveConfiguration\)/);
  assert.match(dashboard, /configurationSavePending = true/);
  assert.match(dashboard, /configurationSavePending \|\| !available/);
  assert.match(markup, /id="configuration-recovery"/);
  assert.match(markup, /id="acknowledge-configuration-outcome"/);
  assert.match(dashboard, /await Promise\.race\(\[request, timeout\]\)/);
  assert.match(dashboard, /view\.history\.textContent = view\.history\.hidden \? "" : "History incomplete"/);
  assert.match(markup, /<th class="traffic-heading" scope="col" title="[^"]+">\s*<span class="traffic-heading-title">Traffic<\/span>\s*<span class="traffic-heading-groups" aria-hidden="true">UART &middot; USB<\/span>\s*<\/th>[\s\S]*?<th scope="col">Diagnostics<\/th>/);
  assert.match(markup, /title="Rates ramp from a one-second to a five-second average as samples arrive\./);
  assert.match(markup, /<dialog class="diagnostics-dialog" id="diagnostics-dialog"/);
  assert.match(markup, /Board clock profile/);
  assert.match(markup, /Reset cause/);
  assert.match(markup, /Service load \(Core 0 \/ Core 1\)/);
  assert.doesNotMatch(markup, /CPU load|unsafe overclock/);
  assert.match(markup, /<details class="pin-map-disclosure">\s*<summary>Pin mapping<\/summary>\s*<div class="pin-map" id="pin-map"/);
  assert.match(markup, /Uptime[\s\S]*?Board temperature[\s\S]*?Service load \(Core 0 \/ Core 1\)/);
  assert.match(dashboard, /const total = sampleAvailable && uartMeasurementAvailable \? formatBytes\(channel\.totals\?\.\[key\]\) : "--"/);
  assert.match(dashboard, /latest\.totals\[key\] - baseline\.totals\[key\]/);
  assert.match(dashboard, /elapsed >= MIN_TRAFFIC_RATE_WINDOW_MS/);
  assert.match(dashboard, /const channelRowViews = new Map\(\)/);
  assert.match(dashboard, /const rates = currentRates\(channel, now, data\)/);
  assert.match(dashboard, /view\.diagnosticsButton\.disabled = !data\.connected/);
  assert.doesNotMatch(dashboard, /channelRows\.replaceChildren\(\)/);
  assert.match(dashboard, /boardIdentityName\([\s\S]*?data\.identity\?\.mcu_id \?\? data\.identity\?\.mcu/);
  assert.match(dashboard, /function measurementStatus\(value\)/);
  assert.match(dashboard, /view\.state\.hidden = stale/);
  assert.match(dashboard, /view\.measurement\.textContent = stale \? "— Unavailable" : measurementStatus\(channel\.uart_measurement_state\)/);
  assert.match(dashboard, /view\.history\.hidden = channel\.traffic_incomplete !== true/);
  assert.match(dashboard, /configuration\.append\(settings\)/);
  assert.doesNotMatch(dashboard, /pins\.textContent/);
  assert.match(stylesheet, /\.health-flags \{[^}]*height: 2\.8em;[^}]*overflow: hidden;/);
  assert.match(dashboard, /\(rates\.rates\[key\] \/ 1000\)\.toFixed\(1\)/);
  assert.match(dashboard, /\["UART TX", "uart_tx", true\],[\s\S]*?\["UART RX", "uart_rx", true\],[\s\S]*?\["USB RX", "usb_rx", false\],[\s\S]*?\["USB TX", "usb_tx", false\]/);
  assert.match(stylesheet, /\.traffic-metrics \{[^}]*grid-template-columns: max-content max-content;[^}]*grid-template-rows: repeat\(2, auto\);[^}]*grid-auto-flow: column;/);
  assert.match(stylesheet, /\.traffic-metrics > span \{[^}]*grid-template-columns: max-content 11ch 16ch;/);
  assert.match(stylesheet, /col\.port-column \{ width: 10%; \}/);
  assert.match(stylesheet, /col\.lifecycle-column \{ width: 14%; \}/);
  assert.match(stylesheet, /col\.configuration-column \{ width: 12%; \}/);
  assert.match(stylesheet, /col\.throughput-column \{ width: 54%; \}/);
  assert.match(stylesheet, /col\.diagnostics-column \{ width: 10%; \}/);
  assert.match(stylesheet, /\.channels-section table \{ min-width: 1080px; \}/);
  assert.match(stylesheet, /@media \(max-width: 1100px\) \{\s*\.channels-section table \{ min-width: 1080px; \}\s*\}/);
  assert.match(stylesheet, /#clock-profile \{[^}]*min-height: 38px;[^}]*border-radius: 7px;[^}]*font: 600 14px var\(--mono\); \}/);
  assert.match(stylesheet, /\.channels-section tbody td:last-child \{ padding-inline: 6px; \}/);
  assert.match(stylesheet, /\.traffic b \{[^}]*text-align: right;/);
  assert.match(stylesheet, /\.traffic-total \{[^}]*text-align: right;/);
  assert.match(stylesheet, /\.traffic b \{[^}]*font-size: 10px;/);
  assert.match(stylesheet, /#toggle-led\[data-state="off"\] \{[^}]*background: var\(--paper\);/);
  assert.match(stylesheet, /#toggle-led\[data-state="on"\] \{[^}]*background: var\(--teal\);[^}]*color: #fff;/);
  assert.match(stylesheet, /#save-configuration, #reset-board \{ min-width: 136px; \}/);
  assert.match(stylesheet, /\.confirmation-dialog \{[^}]*border-top: 3px solid var\(--teal\);/);
  assert.match(stylesheet, /\.summary\.identity-summary \{ grid-template-columns: repeat\(6, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.identity-summary \{ grid-template-columns: repeat\(3, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.identity-summary \{ grid-template-columns: repeat\(2, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.configuration-summary \{ grid-template-columns: repeat\(6, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.configuration-summary \{ grid-template-columns: repeat\(3, minmax\(0, 1fr\)\); \}/);
  assert.match(stylesheet, /\.summary\.configuration-summary \{ grid-template-columns: repeat\(2, minmax\(0, 1fr\)\); \}/);
});
