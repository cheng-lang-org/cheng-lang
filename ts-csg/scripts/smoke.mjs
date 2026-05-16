import assert from "node:assert/strict";
import { execFileSync, spawnSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import ts from "typescript";

const root = fileURLToPath(new URL("../", import.meta.url));
const outA = join(root, "tmp/basic-a.csg.jsonl");
const outB = join(root, "tmp/basic-b.csg.jsonl");
const outCsgA = join(root, "tmp/main-a.csgv2");
const outCsgB = join(root, "tmp/main-b.csgv2");
const outCoreA = join(root, "tmp/basic-a.csgcore");
const outCoreB = join(root, "tmp/basic-b.csgcore");
const reportCoreA = join(root, "tmp/basic-a.report.json");
const reportCoreB = join(root, "tmp/basic-b.report.json");
const coldPath = join(root, "tmp/cheng_cold");
const objPath = join(root, "tmp/main.o");
const exePath = join(root, "tmp/main");
const nodeModulePath = join(root, "tmp/main.node.mjs");

mkdirSync(dirname(outA), { recursive: true });

function runCli(args) {
  return execFileSync(process.execPath, ["dist/cli.js", ...args], {
    cwd: root,
    encoding: "utf8",
    stdio: ["ignore", "pipe", "pipe"],
  });
}

runCli(["--project", "fixtures/basic/tsconfig.json", "--out", outA]);
runCli(["--project", "fixtures/basic/tsconfig.json", "--out", outB]);

const textA = readFileSync(outA, "utf8");
const textB = readFileSync(outB, "utf8");
assert.equal(textA, textB, "facts must be deterministic");

const facts = textA.trim().split("\n").map((line) => JSON.parse(line));
const kinds = new Map();
for (const fact of facts) {
  kinds.set(fact.kind, (kinds.get(fact.kind) ?? 0) + 1);
}

assert.equal(kinds.get("csg.schema"), 1);
assert.equal(kinds.get("ts.project"), 1);
assert.equal(kinds.get("ts.source_file"), 2);
assert.ok((kinds.get("ts.module_import") ?? 0) >= 1);
assert.ok((kinds.get("ts.function") ?? 0) >= 2);
assert.ok((kinds.get("ts.class") ?? 0) >= 1);
assert.ok((kinds.get("ts.call") ?? 0) >= 2);
assert.ok((kinds.get("ts.new") ?? 0) >= 1);
assert.ok((kinds.get("ts.control") ?? 0) >= 1);
assert.equal(kinds.get("ts.unsupported"), undefined);

const unsupported = spawnSync(process.execPath, ["dist/cli.js", "--project", "fixtures/unsupported/tsconfig.json"], {
  cwd: root,
  encoding: "utf8",
});
assert.equal(unsupported.status, 1);
assert.match(unsupported.stderr, /type\.any/);

runCli(["--emit", "csg-core", "--project", "fixtures/basic/tsconfig.json", "--runtime", "node,browser", "--out", outCoreA, "--report-out", reportCoreA]);
runCli(["--emit", "csg-core", "--project", "fixtures/basic/tsconfig.json", "--runtime", "node,browser", "--out", outCoreB, "--report-out", reportCoreB]);
const coreA = readFileSync(outCoreA, "utf8");
const coreB = readFileSync(outCoreB, "utf8");
assert.equal(coreA, coreB, "CSG-Core facts must be deterministic");
assert.equal(readFileSync(reportCoreA, "utf8"), readFileSync(reportCoreB, "utf8"), "CSG-Core reports must be deterministic");
const coreFacts = coreA.trim().split("\n").map((line) => JSON.parse(line));
const coreKinds = new Map();
for (const fact of coreFacts) {
  coreKinds.set(fact.kind, (coreKinds.get(fact.kind) ?? 0) + 1);
}
const coreReport = JSON.parse(readFileSync(reportCoreA, "utf8"));
assert.equal(coreReport.schema, "csg-core.report");
assert.equal(coreReport.complete, true);
assert.equal(coreReport.counts.modules, coreKinds.get("csg.module"));
assert.equal(coreReport.counts.functions, coreKinds.get("csg.function"));
assert.equal(coreReport.counts.blocks, coreKinds.get("csg.block"));
assert.equal(coreReport.counts.ops, coreKinds.get("csg.op"));
assert.equal(coreReport.counts.calls, coreKinds.get("csg.call"));
assert.equal(coreReport.counts.unsupported, 0);
assert.ok(coreReport.counts.types > 0);
assert.ok((coreKinds.get("csg.binding") ?? 0) >= 4);
assert.ok(coreFacts.some((fact) => fact.kind === "csg.op" && fact.opKind === "binding_extract"));

const unsupportedCoreOut = join(root, "tmp/unsupported.csgcore");
const unsupportedCoreReport = join(root, "tmp/unsupported.report.json");
runCli(["--emit", "csg-core", "--project", "fixtures/unsupported/tsconfig.json", "--runtime", "node,browser", "--out", unsupportedCoreOut, "--report-out", unsupportedCoreReport]);
const unsupportedCore = JSON.parse(readFileSync(unsupportedCoreReport, "utf8"));
assert.equal(unsupportedCore.complete, false);
assert.equal(unsupportedCore.counts.unsupported, unsupportedCore.unsupported.length);
assert.match(unsupportedCore.unsupported[0].code, /type\.any/);

runCli(["--emit", "cheng-csg-v2", "--project", "fixtures/csg-v2/tsconfig.json", "--out", outCsgA]);
runCli(["--emit", "cheng-csg-v2", "--project", "fixtures/csg-v2/tsconfig.json", "--out", outCsgB]);
const csgA = readFileSync(outCsgA, "utf8");
const csgB = readFileSync(outCsgB, "utf8");
assert.equal(csgA, csgB, "CHENG_CSG_V2 facts must be deterministic");
assert.ok(csgA.startsWith("CHENG_CSG_V2\n"));
assert.match(csgA, /^R0001/m);
assert.match(csgA, /^R0004/m);
assert.match(csgA, /^R0005/m);
assert.match(csgA, /^R0006/m);

const unsupportedCsg = spawnSync(process.execPath, ["dist/cli.js", "--emit", "cheng-csg-v2", "--project", "fixtures/csg-v2-unsupported/tsconfig.json"], {
  cwd: root,
  encoding: "utf8",
});
assert.equal(unsupportedCsg.status, 1);
assert.match(unsupportedCsg.stderr, /unsupported expression|unsupported numeric operator|only direct calls/);

assert.equal(process.platform, "darwin", "CHENG_CSG_V2 execution smoke currently targets arm64-apple-darwin");
execFileSync("cc", ["-std=c11", "-O2", "-o", coldPath, "../bootstrap/cheng_cold.c"], {
  cwd: root,
  stdio: ["ignore", "ignore", "pipe"],
});
execFileSync(coldPath, [
  "system-link-exec",
  `--csg-in:${outCsgA}`,
  "--emit:obj",
  `--out:${objPath}`,
  "--target:arm64-apple-darwin",
], {
  cwd: root,
  stdio: ["ignore", "pipe", "pipe"],
});
execFileSync("cc", [objPath, "-o", exePath], {
  cwd: root,
  stdio: ["ignore", "pipe", "pipe"],
});
const tsSource = readFileSync(join(root, "fixtures/csg-v2/src/main.ts"), "utf8");
const transpiled = ts.transpileModule(tsSource, {
  compilerOptions: {
    module: ts.ModuleKind.ES2022,
    target: ts.ScriptTarget.ES2022,
  },
}).outputText;
writeFileSync(nodeModulePath, transpiled, "utf8");
const nodeResult = await import(`${pathToFileURL(nodeModulePath).href}?t=${Date.now()}`);
const expected = nodeResult.main();
assert.equal(Number.isInteger(expected), true);
const run = spawnSync(exePath, [], {
  cwd: root,
  encoding: "utf8",
});
assert.equal(run.status, expected);

process.stdout.write(`ts-csg smoke ok: ${facts.length} facts, CHENG_CSG_V2 exit ${run.status}\n`);
