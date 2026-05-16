#!/usr/bin/env node
import { mkdir, writeFile } from "node:fs/promises";
import path from "node:path";

import { emitChengCsgV2FromTs } from "./cheng-csg-v2.js";
import { emitCsgCoreFromTs } from "./csg-core.js";
import { extractTsCsg } from "./extractor.js";
import type { ExtractOptions } from "./schema.js";
import { stableJson } from "./stable-json.js";

async function main(): Promise<void> {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    process.stdout.write(helpText());
    return;
  }

  if (options.emit === "cheng-csg-v2") {
    const result = emitChengCsgV2FromTs(options);
    if (result.diagnostics.length > 0 || result.unsupported.length > 0) {
      for (const diagnostic of result.diagnostics) {
        process.stderr.write(`error: ${diagnostic}\n`);
      }
      for (const unsupported of result.unsupported) {
        process.stderr.write(`unsupported: ${unsupported}\n`);
      }
      process.exitCode = 1;
      return;
    }
    await writeOutput(options, result.text);
    return;
  }

  if (options.emit === "csg-core") {
    const result = emitCsgCoreFromTs(options);
    if (options.reportOut) {
      await writeReport(options, stableJson(result.report, true) + "\n");
    }
    if (result.diagnostics.length > 0) {
      for (const diagnostic of result.diagnostics) {
        process.stderr.write(`error: ${diagnostic}\n`);
      }
      process.exitCode = 1;
      return;
    }
    await writeOutput(options, result.text);
    return;
  }

  const result = extractTsCsg(options);
  if (result.diagnostics.length > 0 || result.unsupported.length > 0) {
    for (const diagnostic of result.diagnostics) {
      process.stderr.write(`error: ${diagnostic}\n`);
    }
    for (const unsupported of result.unsupported) {
      const loc = unsupported.loc ? `${unsupported.loc.file}:${unsupported.loc.line}:${unsupported.loc.column}` : "<unknown>";
      process.stderr.write(`unsupported: ${loc} ${unsupported.code}: ${unsupported.message}\n`);
    }
    process.exitCode = 1;
    return;
  }

  const text = result.facts.map((fact) => stableJson(fact, options.pretty)).join("\n") + "\n";
  await writeOutput(options, text);
}

async function writeOutput(options: CliOptions, text: string): Promise<void> {
  if (options.out) {
    const out = path.resolve(options.rootDir ?? process.cwd(), options.out);
    await mkdir(path.dirname(out), { recursive: true });
    await writeFile(out, text, "utf8");
    return;
  }

  process.stdout.write(text);
}

async function writeReport(options: CliOptions, text: string): Promise<void> {
  if (!options.reportOut) return;
  const out = path.resolve(options.rootDir ?? process.cwd(), options.reportOut);
  await mkdir(path.dirname(out), { recursive: true });
  await writeFile(out, text, "utf8");
}

type CliOptions = ExtractOptions & {
  emit?: "ts-csg" | "csg-core" | "cheng-csg-v2";
  entry?: string;
  entryRoots?: string[];
  help?: boolean;
  reportOut?: string;
  runtime?: string[];
  target?: string;
};

function parseArgs(args: string[]): CliOptions {
  const options: CliOptions = { files: [] };

  for (let index = 0; index < args.length; index += 1) {
    const arg = args[index];
    if (!arg) continue;

    if (arg === "--help" || arg === "-h") {
      options.help = true;
    } else if (arg === "--project" || arg === "-p") {
      options.project = requireValue(args, ++index, arg);
    } else if (arg.startsWith("--project=")) {
      options.project = arg.slice("--project=".length);
    } else if (arg === "--file" || arg === "-f") {
      options.files?.push(requireValue(args, ++index, arg));
    } else if (arg.startsWith("--file=")) {
      options.files?.push(arg.slice("--file=".length));
    } else if (arg === "--out" || arg === "-o") {
      options.out = requireValue(args, ++index, arg);
    } else if (arg.startsWith("--out=")) {
      options.out = arg.slice("--out=".length);
    } else if (arg === "--emit") {
      options.emit = parseEmit(requireValue(args, ++index, arg));
    } else if (arg.startsWith("--emit=")) {
      options.emit = parseEmit(arg.slice("--emit=".length));
    } else if (arg === "--runtime") {
      options.runtime = parseRuntime(requireValue(args, ++index, arg));
    } else if (arg.startsWith("--runtime=")) {
      options.runtime = parseRuntime(arg.slice("--runtime=".length));
    } else if (arg === "--report-out") {
      options.reportOut = requireValue(args, ++index, arg);
    } else if (arg.startsWith("--report-out=")) {
      options.reportOut = arg.slice("--report-out=".length);
    } else if (arg === "--target") {
      options.target = requireValue(args, ++index, arg);
    } else if (arg.startsWith("--target=")) {
      options.target = arg.slice("--target=".length);
    } else if (arg === "--entry") {
      options.entry = requireValue(args, ++index, arg);
    } else if (arg.startsWith("--entry=")) {
      options.entry = arg.slice("--entry=".length);
    } else if (arg === "--entry-root") {
      pushEntryRoot(options, requireValue(args, ++index, arg));
    } else if (arg.startsWith("--entry-root=")) {
      pushEntryRoot(options, arg.slice("--entry-root=".length));
    } else if (arg === "--root") {
      options.rootDir = requireValue(args, ++index, arg);
    } else if (arg.startsWith("--root=")) {
      options.rootDir = arg.slice("--root=".length);
    } else if (arg === "--pretty") {
      options.pretty = true;
    } else if (arg.startsWith("-")) {
      throw new Error(`unknown argument: ${arg}`);
    } else {
      options.files?.push(arg);
    }
  }

  if (options.files && options.files.length === 0) {
    delete options.files;
  }
  return options;
}

function requireValue(args: string[], index: number, flag: string): string {
  const value = args[index];
  if (!value || value.startsWith("-")) {
    throw new Error(`${flag} requires a value`);
  }
  return value;
}

function parseEmit(value: string): "ts-csg" | "csg-core" | "cheng-csg-v2" {
  if (value === "ts-csg" || value === "csg-core" || value === "cheng-csg-v2") return value;
  throw new Error(`unknown --emit value: ${value}`);
}

function parseRuntime(value: string): string[] {
  return value.split(",").map((item) => item.trim()).filter((item) => item.length > 0);
}

function pushEntryRoot(options: CliOptions, value: string): void {
  options.entryRoots ??= [];
  options.entryRoots.push(value);
}

function helpText(): string {
  return `ts-csg: TypeScript -> Cheng Semantic Graph facts

Usage:
  ts-csg --project tsconfig.json --out out.csg.jsonl
  ts-csg --emit csg-core --project tsconfig.json --runtime node,browser --out out.csgcore --report-out report.json
  ts-csg --emit cheng-csg-v2 --project tsconfig.json --out out.csgv2
  ts-csg --file src/main.ts --out out.csg.jsonl

Options:
  --emit <kind>        ts-csg (default), csg-core, or cheng-csg-v2
  --project, -p <path>  TypeScript project file
  --file, -f <path>     Source file, repeatable
  --runtime <list>     Runtime surface for csg-core: node,browser
  --report-out <path>  JSON coverage/runtime/unsupported report for csg-core
  --entry-root <path>  Expected project entry root for csg-core, repeatable
  --target <triple>    Target for cheng-csg-v2, default arm64-apple-darwin
  --entry <name>       Entry function for cheng-csg-v2, default main
  --root <path>         Root used for relative paths
  --out, -o <path>      Output JSONL facts
  --pretty             Pretty-print each JSON fact
  --help, -h           Show this help
`;
}

main().catch((error: unknown) => {
  const message = error instanceof Error ? error.message : String(error);
  process.stderr.write(`error: ${message}\n`);
  process.exitCode = 1;
});
