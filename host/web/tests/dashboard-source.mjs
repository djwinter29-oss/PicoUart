// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Ji Dong
import { readFileSync } from "node:fs";
import vm from "node:vm";

const entry = new URL("../src/js/dashboard.js", import.meta.url);
const imports = /^import\s*\{([^}]+)\}\s*from\s*"([^"]+)";\n/gm;

export function dashboardSources() {
  const sources = new Map();
  function visit(url) {
    if (sources.has(url.href)) return;
    const source = readFileSync(url, "utf8");
    for (const [, , path] of source.matchAll(imports)) visit(new URL(path, url));
    sources.set(url.href, source);
  }
  visit(entry);
  return sources;
}

export function dashboardScript() {
  return '"use strict";\n' + [...dashboardSources().values()]
    .map(source => source.replace(imports, "").replace(/^export /gm, ""))
    .join("\n");
}

// Keep the DOM/clock fixtures synchronous; production uses native ES modules.
export function runDashboard(context) {
  const modules = new Map();
  for (const [url, source] of dashboardSources()) {
    const dependencies = {};
    const script = source.replace(imports, (_, names, path) => {
      dependencies[path] = modules.get(new URL(path, url).href);
      return `const { ${names} } = dependencies[${JSON.stringify(path)}];\n`;
    }).replace(/^export /gm, "");
    if (url === entry.href) {
      context.dependencies = dependencies;
      vm.runInContext(`"use strict";\n${script}`, context, { filename: url });
    } else {
      const sandbox = { dependencies };
      for (const name of ["document", "window", "fetch", "AbortController", "Date", "performance"]) {
        Object.defineProperty(sandbox, name, { get: () => context[name] ?? globalThis[name] });
      }
      const names = [...source.matchAll(/^export (?:async )?(?:function|const) (\w+)/gm)]
        .map(([, name]) => name);
      // Separate contexts catch accidental reliance on the entry's private state.
      const namespace = vm.runInNewContext(`"use strict";\n${script}\n({ ${names.join(", ")} })`, sandbox,
        { filename: url });
      modules.set(url, namespace);
      Object.assign(context, namespace);
    }
  }
}
