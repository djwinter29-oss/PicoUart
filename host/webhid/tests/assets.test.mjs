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
      assert.match(html, /<dialog class="configuration-dialog"[^>]*>[\s\S]*?id="configuration-acknowledgement" hidden/);
      assert.match(html, /<th scope="col"(?: title="[^"]+")?>Traffic<\/th>[\s\S]*?<th scope="col">Diagnostics<\/th>/);
      assert.match(html, /<dialog class="diagnostics-dialog"[^>]*id="channel-diagnostics-dialog"/);
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
      assert.match(styles, /#configuration-profile\s*\{[^}]*width:\s*100%;/);
      assert.match(sharedStyles, /#toggle-led\s*\{[^}]*background:\s*var\(--teal\);[^}]*color:\s*#fff;/);
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