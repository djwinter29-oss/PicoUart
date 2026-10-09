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
    assert.ok(existsSync(join(root, match[1])), match[1]);
  }
  assert.match(html, /href="css\/dashboard\.css"/);
  assert.match(html, /href="favicon\.svg"/);
  assert.ok(!html.includes("shared/"));
});

test("WebHID module imports stay within its own codebase", () => {
  for (const name of ["app.mjs", "device.mjs", "view.mjs"]) {
    const file = join(root, "js", name);
    const source = readFileSync(file, "utf8");
    for (const match of source.matchAll(/from "([^"]+)"/g)) {
      const target = resolve(dirname(file), match[1]);
      assert.ok(!relative(root, target).startsWith(".."), match[1]);
      assert.ok(existsSync(target), match[1]);
    }
  }
  assert.ok(!existsSync(join(root, "prepare.mjs")));
  assert.ok(!existsSync(join(root, "shared", "js", "view.js")));
});