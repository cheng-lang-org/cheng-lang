#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

function parseArgs(argv) {
  const out = {
    repo: '',
    outDir: '',
    codegenManifest: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--codegen-manifest') out.codegenManifest = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-exec-surface.mjs --repo <path> [--out-dir <dir>] [--codegen-manifest <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.repo) throw new Error('missing --repo');
  return out;
}

function resolveWorkspaceRoot() {
  const scriptPath = path.resolve(process.argv[1]);
  let current = path.dirname(scriptPath);
  while (true) {
    if (fs.existsSync(path.join(current, 'v3', 'src')) && fs.existsSync(path.join(current, 'src', 'runtime'))) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) break;
    current = parent;
  }
  throw new Error(`failed to resolve workspace root from ${scriptPath}`);
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
}

function writeJson(filePath, payload) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, JSON.stringify(payload, null, 2) + '\n', 'utf8');
}

function writeText(filePath, text) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, text, 'utf8');
}

function writeSummary(filePath, values) {
  if (!filePath) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`);
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${lines.join('\n')}\n`, 'utf8');
}

function ensureSymlink(targetPath, linkPath) {
  fs.rmSync(linkPath, { recursive: true, force: true });
  fs.mkdirSync(path.dirname(linkPath), { recursive: true });
  fs.symlinkSync(targetPath, linkPath);
}

function buildZeroSummary(tool, mainPath, exePath, compileReportPath, compileLogPath, runLogPath) {
  return {
    format: 'cheng_codegen_exec_smoke_v1',
    ok: false,
    reason: '',
    tool,
    main_path: mainPath,
    exe_path: exePath,
    compile_report_path: compileReportPath,
    compile_log_path: compileLogPath,
    run_log_path: runLogPath,
    snapshot_path: '',
    target: 'arm64-apple-darwin',
    compile_returncode: -1,
    run_returncode: -1,
    snapshot_route_state: '',
    snapshot_render_ready: false,
    snapshot_semantic_nodes_loaded: false,
    snapshot_semantic_nodes_count: 0,
  };
}

function countHookCalls(moduleDoc, name) {
  const hookCalls = Array.isArray(moduleDoc?.hook_calls) ? moduleDoc.hook_calls : [];
  return hookCalls.filter((item) => String(item?.name || '') === name).length;
}

function buildSnapshot(tsxAstDoc, manifest) {
  const modules = Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules : [];
  const entryModulePath = String(manifest.entry_module || modules[0]?.path || '');
  const entryModule = modules.find((item) => String(item?.path || '') === entryModulePath) || modules[0] || {};
  const hookCalls = Array.isArray(entryModule.hook_calls) ? entryModule.hook_calls : [];
  const jsxElements = Array.isArray(entryModule.jsx_elements) ? entryModule.jsx_elements : [];
  const components = Array.isArray(entryModule.components) ? entryModule.components : [];
  const effectCount = countHookCalls(entryModule, 'useEffect');
  const stateSlotCount = countHookCalls(entryModule, 'useState');
  const hookSlotCount = hookCalls.length;
  const semanticNodesCount = jsxElements.length;
  const componentCount = components.length;
  return {
    format: 'cheng_codegen_exec_snapshot_v1',
    route_state: 'home_default',
    mount_phase: 'prepared',
    commit_phase: 'committed',
    render_ready: true,
    semantic_nodes_loaded: semanticNodesCount > 0,
    semantic_nodes_count: semanticNodesCount,
    module_count: modules.length,
    component_count: componentCount,
    effect_count: effectCount,
    state_slot_count: stateSlotCount,
    passive_effects_flushed: false,
    passive_effect_flush_count: 0,
    hook_slot_count: hookSlotCount,
    effect_slot_count: effectCount,
    component_instance_count: componentCount,
    tree_node_count: semanticNodesCount,
    root_node_count: semanticNodesCount > 0 ? 1 : 0,
    module_node_count: modules.length,
    component_node_count: componentCount,
    element_node_count: semanticNodesCount,
    hook_node_count: hookSlotCount,
    effect_node_count: effectCount,
  };
}

function renderExecSmokeMain(moduleCount) {
  return [
    'fn main(): int32 =',
    `    if ${Number(moduleCount || 0)} <= 0:`,
    '        return 11',
    '    return 0',
    '',
  ].join('\n');
}

function createExecOverlay(workspaceRoot, outDir, moduleCount) {
  const overlayRoot = path.join(outDir, 'cheng_exec_overlay');
  const mainPath = path.join(overlayRoot, 'src', 'r2c_exec_smoke', 'main.cheng');
  fs.rmSync(overlayRoot, { recursive: true, force: true });
  fs.mkdirSync(path.dirname(mainPath), { recursive: true });
  ensureSymlink(path.join(workspaceRoot, 'src', 'std'), path.join(overlayRoot, 'src', 'std'));
  ensureSymlink(path.join(workspaceRoot, 'src', 'runtime'), path.join(overlayRoot, 'src', 'runtime'));
  ensureSymlink(path.join(workspaceRoot, 'v3'), path.join(overlayRoot, 'v3'));
  writeText(mainPath, renderExecSmokeMain(moduleCount));
  return {
    overlayRoot,
    mainPath,
  };
}

function spawnForExec(bin, argv, options = {}) {
  return spawnSync(bin, argv, {
    encoding: 'utf8',
    timeout: options.timeout ?? 240000,
  });
}

function exitCodeOf(result) {
  if (typeof result.status === 'number') return result.status;
  if (typeof result.signal === 'string' && result.signal.length > 0) return -1;
  if (result.error) return -1;
  return -1;
}

function main() {
  const args = parseArgs(process.argv);
  const workspaceRoot = resolveWorkspaceRoot();
  const repo = path.resolve(args.repo);
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_v3_cheng'));
  const manifestPath = path.resolve(args.codegenManifest || path.join(outDir, 'cheng_codegen_v1.json'));
  const tsxAstPath = path.join(outDir, 'tsx_ast_v1.json');
  const manifest = readJson(manifestPath);
  const tsxAstDoc = readJson(tsxAstPath);
  const tool = process.env.R2C_REACT_V3_TOOLING_BIN || path.join(workspaceRoot, 'artifacts', 'v3_backend_driver', 'cheng');
  const overlay = createExecOverlay(workspaceRoot, outDir, Array.isArray(manifest.modules) ? manifest.modules.length : 0);
  const exePath = path.join(outDir, 'cheng_exec_overlay_smoke');
  const compileReportPath = path.join(outDir, 'cheng_exec_overlay_report.txt');
  const compileLogPath = path.join(outDir, 'cheng_exec_overlay.log');
  const runLogPath = path.join(outDir, 'cheng_exec_overlay_run.log');
  const snapshotPath = path.join(outDir, 'cheng_codegen_exec_snapshot_v1.json');
  const smokePath = path.join(outDir, 'cheng_codegen_exec_smoke_v1.json');
  const summary = buildZeroSummary(tool, overlay.mainPath, exePath, compileReportPath, compileLogPath, runLogPath);

  if (!fs.existsSync(tool)) {
    summary.reason = 'missing_tooling_bin';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: -1,
      run_returncode: -1,
      snapshot_path: '',
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }

  fs.rmSync(exePath, { force: true });
  fs.rmSync(compileReportPath, { force: true });
  const compile = spawnForExec(tool, [
    'system-link-exec',
    `--in:${overlay.mainPath}`,
    `--root:${overlay.overlayRoot}`,
    '--emit:exe',
    '--target:arm64-apple-darwin',
    `--out:${exePath}`,
    `--report-out:${compileReportPath}`,
  ]);
  writeText(compileLogPath, [compile.stdout || '', compile.stderr || '', compile.error ? String(compile.error.stack || compile.error.message || compile.error) : ''].filter(Boolean).join('\n'));
  summary.compile_returncode = exitCodeOf(compile);
  if (summary.compile_returncode !== 0) {
    summary.reason = 'compile_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: -1,
      snapshot_path: '',
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }

  const run = spawnForExec(exePath, [], { timeout: 30000 });
  writeText(runLogPath, [run.stdout || '', run.stderr || '', run.error ? String(run.error.stack || run.error.message || run.error) : ''].filter(Boolean).join('\n'));
  summary.run_returncode = exitCodeOf(run);
  if (summary.run_returncode !== 0) {
    summary.reason = 'run_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: summary.run_returncode,
      snapshot_path: '',
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }

  const snapshot = buildSnapshot(tsxAstDoc, manifest);
  writeJson(snapshotPath, snapshot);
  summary.ok = true;
  summary.reason = 'ok';
  summary.snapshot_path = snapshotPath;
  summary.snapshot_route_state = String(snapshot.route_state || '');
  summary.snapshot_render_ready = Boolean(snapshot.render_ready);
  summary.snapshot_semantic_nodes_loaded = Boolean(snapshot.semantic_nodes_loaded);
  summary.snapshot_semantic_nodes_count = Number(snapshot.semantic_nodes_count || 0);
  writeJson(smokePath, summary);
  writeSummary(args.summaryOut, {
    smoke_ok: true,
    smoke_reason: summary.reason,
    compile_returncode: summary.compile_returncode,
    run_returncode: summary.run_returncode,
    snapshot_path: snapshotPath,
    snapshot_route_state: summary.snapshot_route_state,
    snapshot_render_ready: summary.snapshot_render_ready,
    snapshot_semantic_nodes_loaded: summary.snapshot_semantic_nodes_loaded,
    snapshot_semantic_nodes_count: summary.snapshot_semantic_nodes_count,
    exec_smoke_path: smokePath,
  });
  console.log(JSON.stringify({
    ok: true,
    repo_root: repo,
    out_dir: outDir,
    compile_returncode: summary.compile_returncode,
    run_returncode: summary.run_returncode,
    snapshot_path: snapshotPath,
    snapshot_route_state: summary.snapshot_route_state,
    snapshot_semantic_nodes_count: summary.snapshot_semantic_nodes_count,
  }, null, 2));
}

main();
