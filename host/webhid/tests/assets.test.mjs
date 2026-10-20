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
  test(`${frontend} keeps persistent configuration as a compact summary with a heading action`, () => {
    const html = readFileSync(new URL(`../../${frontend}/index.html`, import.meta.url), "utf8");
    const section = html.match(/<section aria-labelledby="configuration-title">([\s\S]*?)<\/section>/)[1];
    assert.match(section, /class="section-heading">[\s\S]*?class="actions"[\s\S]*?id="save-configuration"[^>]*>Save<\/button>[\s\S]*?<\/div>\s*<\/div>/);
    assert.match(section, /class="summary configuration-summary">\s*<div class="summary-item">\s*<label for="configuration-profile">System clock profile<\/label>\s*<select/);
    assert.match(section, /id="configuration-acknowledgement" hidden/);
    assert.doesNotMatch(section, /configuration-panel|configuration-note|configuration-status|configuration-detail/);
    assert.match(section, /id="configuration-message" role="status" aria-live="polite"/);
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