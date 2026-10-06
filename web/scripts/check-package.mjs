import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { readFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { pathToFileURL } from "node:url";

const metadata = JSON.parse(readFileSync("package.json", "utf8"));
const archive = `${metadata.name.replace("@", "").replace("/", "-")}-${metadata.version}.tgz`;
const files = execFileSync("tar", ["-tzf", archive], { encoding: "utf8" }).trim().split("\n");
for (const path of [
  "dist/vv.js",
  "dist/engine.js",
  "dist/vv-runtime.js",
  "dist/vv.wasm",
  "dist/index.d.ts",
  "dist/engine.d.ts",
  "README.md",
  "THIRD_PARTY_NOTICES.md",
]) {
  assert(files.includes(`package/${path}`), `Missing package asset: ${path}`);
}
assert(
  files.every((path) =>
    /^package\/(dist\/|README\.md$|THIRD_PARTY_NOTICES\.md$|package\.json$)/.test(path),
  ),
  "Unexpected packaged file",
);
const temporary = mkdtempSync(join(tmpdir(), "vv-package-"));
try {
  execFileSync("tar", ["-xzf", archive, "-C", temporary]);
  const { ViewerEngine, Lut } = await import(
    pathToFileURL(join(temporary, "package/dist/engine.js")).href
  );
  assert.equal(typeof ViewerEngine.create, "function");
  assert.equal(Lut.rainbow(0, 1).min, 0);
} finally {
  rmSync(temporary, { recursive: true, force: true });
}
console.log(`Verified ${archive}: runtime, WASM, declarations, notices and standalone entry point`);
