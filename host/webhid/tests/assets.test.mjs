// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import test from "node:test";

const root = fileURLToPath(new URL("../", import.meta.url));

test("HTML references only WebHID-owned assets", () => {
  const html = readFileSync(join(root, "index.html"), "utf8");
  for (const match of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
    assert.ok(!match[1].startsWith(".."), match[1]);
    assert.ok(existsSync(join(root, match[1].split("?")[0])), match[1]);
  }
  assert.match(html, /href="css\/dashboard\.css(?:\?v=\d+)?"/);
  assert.match(html, /href="favicon\.svg"/);
  assert.ok(!html.includes("shared/"));
});

for (const frontend of ["web", "webhid"]) {
  test(`${frontend} keeps board configuration in a compact summary with a heading action`, () => {
    const html = readFileSync(new URL(`../../${frontend}/index.html`, import.meta.url), "utf8");
    const section = html.match(/<section aria-labelledby="configuration-title">([\s\S]*?)<\/section>/)[1];
    if (frontend === "webhid") {
      const identity = html.match(/<section aria-labelledby="identity-title">([\s\S]*?)<\/section>/)[1];
      assert.match(section, /<h2 id="configuration-title">Board configuration<\/h2>/);
      assert.match(section, /id="reboot-board"/);
      assert.match(section, /class="button button-secondary" id="reboot-board"/);
      assert.doesNotMatch(identity, /id="reboot-board"/);
      assert.match(section, /class="section-heading">[\s\S]*?class="actions"[\s\S]*?id="save-configuration"[^>]*>Save &amp; Reboot<\/button>[\s\S]*?<\/div>\s*<\/div>/);
      assert.match(section, /class="summary configuration-summary">[\s\S]*?<label for="configuration-profile">Stored boot profile<\/label>\s*<select/);
      assert.match(html, /<dialog class="confirmation-dialog"[^>]*>[\s\S]*?id="configuration-acknowledgement" hidden/);
      assert.match(html, /<dialog class="confirmation-dialog"[^>]*id="reboot-dialog"[\s\S]*?id="confirm-reboot"/);
      assert.match(html, /<th class="traffic-heading" scope="col" title="[^"]+">\s*<span class="traffic-heading-title">Traffic<\/span>\s*<span class="traffic-heading-groups" aria-hidden="true">UART &middot; USB<\/span>\s*<\/th>[\s\S]*?<th scope="col">Diagnostics<\/th>/);
      assert.match(html, /<dialog class="diagnostics-dialog"[^>]*id="channel-diagnostics-dialog"/);
      assert.match(html, /<button class="button button-secondary" id="close-channel-diagnostics"/);
      const channels = html.match(/<section class="channels-section"[\s\S]*?<\/section>/)[0];
      assert.match(channels, /<th scope="col">Channel status<\/th>\s*<th scope="col">UART configuration<\/th>/);
      const diagnosticsDialog = html.match(/<dialog class="diagnostics-dialog"[^>]*>([\s\S]*?)<\/dialog>/)[1];
      assert.doesNotMatch(channels, /refresh-diagnostics/);
      assert.match(diagnosticsDialog, /id="refresh-diagnostics"[^>]*disabled>\s*Refresh diagnostics/);
      assert.match(html, /<details class="pin-map-disclosure">[\s\S]*?id="board-pins"/);
      assert.doesNotMatch(html, /id="traffic-note"/);
      assert.match(section, /id="configuration-message" role="status" aria-live="polite"/);
      assert.match(html, /title="Rates ramp from a one-second to a five-second average as samples arrive\./);
    } else {
      assert.match(section, /<h2 id="configuration-title">Board configuration<\/h2>/);
      assert.match(section, /id="save-configuration"/);
      assert.match(section, /class="button button-secondary" id="reset-board"/);
      assert.match(section, /<label for="clock-profile">Stored boot profile<\/label>/);
    }
    assert.doesNotMatch(section, /configuration-panel|configuration-note|configuration-status|configuration-detail/);
    const styles = readFileSync(new URL(`../../${frontend}/css/${frontend === "web" ? "dashboard" : "webhid"}.css`,
      import.meta.url), "utf8");
    if (frontend === "webhid") {
      const sharedStyles = readFileSync(new URL("../css/dashboard.css", import.meta.url), "utf8");
      const channels = html.match(/<section class="channels-section"[\s\S]*?<\/section>/)[0];
      assert.match(channels, /<th scope="col">Channel status<\/th>\s*<th scope="col">UART configuration<\/th>/);
      assert.match(styles, /#configuration-profile\s*\{[^}]*width:\s*100%;/);
      assert.match(styles, /#configuration-profile\s*\{[^}]*min-height:\s*38px;[^}]*border-radius:\s*7px;[^}]*font:\s*600 14px var\(--mono\);/);
      assert.match(sharedStyles, /\.diagnostics-dialog\s*\{[^}]*border-top:\s*3px solid var\(--teal\);/);
      assert.match(sharedStyles, /\.diagnostics-content dl\s*\{[^}]*display:\s*grid;/);
      assert.match(sharedStyles, /#toggle-led\[data-state="off"\]\s*\{[^}]*background:\s*var\(--paper\);/);
      assert.match(sharedStyles, /#toggle-led\[data-state="on"\]\s*\{[^}]*background:\s*var\(--teal\);[^}]*color:\s*#fff;/);
      assert.match(sharedStyles, /#toggle-led, #connect\s*\{[^}]*width:\s*136px;[^}]*min-width:\s*136px;[^}]*display:\s*inline-flex;/);
      assert.match(sharedStyles, /@media \(max-width: 720px\)[\s\S]*#toggle-led, #connect\s*\{\s*width:\s*auto; min-width:\s*0;/);
      assert.match(sharedStyles, /col\.port-column \{ width: 10%; \}/);
      assert.match(sharedStyles, /col\.health-column \{ width: 14%; \}/);
      assert.match(sharedStyles, /col\.configuration-column \{ width: 12%; \}/);
      assert.match(sharedStyles, /col\.throughput-column \{ width: 54%; \}/);
      assert.match(sharedStyles, /col\.diagnostics-column \{ width: 10%; \}/);
      assert.match(sharedStyles, /\.traffic-metrics > span \{[^}]*grid-template-columns: max-content 11ch 16ch;/);
      assert.match(sharedStyles, /\.channels-section tbody td:last-child \{ padding-inline: 6px; \}/);
      assert.match(sharedStyles, /\.summary\.configuration-summary\s*\{\s*grid-template-columns:\s*repeat\(6, minmax\(0, 1fr\)\);\s*\}/);
      assert.match(sharedStyles, /\.traffic-metrics\s*\{[^}]*grid-auto-flow:\s*column;/);
    }
  });
}

test("WebHID module imports stay within its own codebase", () => {
  for (const name of ["app.mjs", "device.mjs", "view.mjs"]) {
    const file = join(root, "js", name);
    const source = readFileSync(file, "utf8");
    for (const match of source.matchAll(/from "([^"]+)"/g)) {
      const target = resolve(dirname(file), match[1].split("?")[0]);
      assert.ok(!relative(root, target).startsWith(".."), match[1]);
      assert.ok(existsSync(target), match[1]);
    }
  }
  assert.ok(!existsSync(join(root, "prepare.mjs")));
  assert.ok(!existsSync(join(root, "shared", "js", "view.js")));
});