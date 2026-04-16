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
    route: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--codegen-manifest') out.codegenManifest = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--route') out.route = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-exec-surface.mjs --repo <path> [--out-dir <dir>] [--codegen-manifest <file>] [--summary-out <file>] [--route <route-id>]');
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

function exitCodeOf(result) {
  if (typeof result.status === 'number') return result.status;
  if (typeof result.signal === 'string' && result.signal.length > 0) return -1;
  if (result.error) return -1;
  return -1;
}

function chengStr(text) {
  return `"${String(text || '').replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\t/g, '\\t')}"`;
}

function buildRouteRuntimeSnapshot(baseSnapshot, routeId, semanticNodesCount) {
  const count = Math.max(0, Number(semanticNodesCount || 0));
  return {
    ...baseSnapshot,
    route_state: routeId,
    semantic_nodes_loaded: count > 0,
    semantic_nodes_count: count,
    tree_node_count: count,
    root_node_count: count > 0 ? 1 : 0,
    element_node_count: count,
  };
}

function splitStringChunks(value, chunkSize = 12) {
  const text = String(value || '');
  const safeChunkSize = Math.max(4, Number(chunkSize || 12));
  const chunks = [];
  let offset = 0;
  while (offset < text.length) {
    let end = Math.min(text.length, offset + safeChunkSize);
    while (end > offset + 1 && (text[end - 1] === '"' || text[end - 1] === '\\')) {
      end -= 1;
    }
    if (end <= offset) end = Math.min(text.length, offset + 1);
    chunks.push(text.slice(offset, end));
    offset = end;
  }
  if (chunks.length === 0) chunks.push('');
  return chunks;
}

function renderRouteRuntimeDataCheng(execIoModule, snapshot, routeId, candidateIndexText) {
  const jsonText = JSON.stringify(snapshot);
  const chunkNames = [];
  const lines = [`import ${execIoModule} as exec_io`, ''];
  lines.push('fn routeId(): str =');
  lines.push(`    return ${chengStr(routeId)}`);
  lines.push('');
  lines.push('fn candidateIndexText(): str =');
  lines.push(`    return ${chengStr(candidateIndexText)}`);
  lines.push('');
  const chunks = splitStringChunks(jsonText, 12);
  for (let index = 0; index < chunks.length; index += 1) {
    const name = `rtChunk${index}`;
    chunkNames.push(name);
    lines.push(`fn ${name}(): str =`);
    lines.push(`    return ${chengStr(chunks[index])}`);
    lines.push('');
  }
  lines.push('fn emitJson() =');
  for (const name of chunkNames) {
    lines.push(`    exec_io.r2cWriteStdout(${name}())`);
  }
  lines.push('    return');
  lines.push('');
  lines.push('fn emitSnapshot() =');
  lines.push('    emitJson()');
  lines.push('    exec_io.r2cWriteStdout("\\n")');
  lines.push('    return');
  lines.push('');
  return lines.join('\n');
}

function loadRouteCatalog(manifest, outDir) {
  const candidates = [
    String(manifest.route_catalog_path || ''),
    path.join(outDir, 'cheng_codegen_route_catalog_v1.json'),
  ].filter(Boolean);
  for (const candidate of candidates) {
    const resolved = path.resolve(candidate);
    if (!fs.existsSync(resolved)) continue;
    const doc = readJson(resolved);
    if (String(doc.format || '') === 'cheng_codegen_route_catalog_v1') return doc;
  }
  throw new Error('missing_route_catalog');
}

function resolveSemanticNodesCount(routeCatalog, routeId, candidateIndex, baseSnapshot) {
  const entryRoute = String(routeCatalog?.entryRouteState || baseSnapshot?.route_state || '').trim();
  const entries = Array.isArray(routeCatalog?.entries) ? routeCatalog.entries : [];
  const entry = entries.find((item) => String(item?.routeId || '').trim() === routeId) || null;
  const candidates = Array.isArray(entry?.candidates) ? entry.candidates : [];
  const candidate = candidates[Math.max(0, Number(candidateIndex || 0))] || candidates[0] || null;
  if (candidate) return Math.max(0, Number(candidate.semanticNodesCount || 0));
  if (routeId === entryRoute) return Math.max(0, Number(baseSnapshot?.semantic_nodes_count || 0));
  return 0;
}

function spawnForExec(bin, argv, options = {}) {
  return spawnSync(bin, argv, {
    encoding: 'utf8',
    timeout: options.timeout ?? 240000,
  });
}

function resolveDefaultToolingBin(workspaceRoot) {
  const explicit = String(process.env.R2C_REACT_V3_TOOLING_BIN || '').trim();
  if (explicit) return explicit;
  return path.join(workspaceRoot, 'artifacts', 'v3_bootstrap', 'cheng.stage3');
}

function ensurePackageCompileSupport(workspaceRoot, packageRoot) {
  const v3Link = path.join(packageRoot, 'v3');
  const runtimeRoot = path.join(packageRoot, 'src', 'runtime');
  const nativeLink = path.join(runtimeRoot, 'native');
  const stdLink = path.join(packageRoot, 'src', 'std');
  fs.mkdirSync(runtimeRoot, { recursive: true });
  if (!fs.existsSync(v3Link)) {
    fs.symlinkSync(path.join(workspaceRoot, 'v3'), v3Link);
  }
  if (!fs.existsSync(nativeLink)) {
    fs.symlinkSync(path.join(workspaceRoot, 'src', 'runtime', 'native'), nativeLink);
  }
  if (!fs.existsSync(stdLink)) {
    fs.symlinkSync(path.join(workspaceRoot, 'src', 'std'), stdLink);
  }
}

function requireGeneratedModule(filePath, label) {
  if (!fs.existsSync(filePath)) {
    throw new Error(`missing generated ${label} module: ${filePath}`);
  }
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
    route_catalog_main_path: '',
    route_catalog_exe_path: '',
    route_catalog_compile_report_path: '',
    route_catalog_compile_log_path: '',
    route_catalog_run_log_path: '',
    snapshot_path: '',
    target: 'arm64-apple-darwin',
    compile_returncode: -1,
    run_returncode: -1,
    route_catalog_compile_returncode: -1,
    route_catalog_run_returncode: -1,
    snapshot_route_state: '',
    snapshot_render_ready: false,
    snapshot_semantic_nodes_loaded: false,
    snapshot_semantic_nodes_count: 0,
    exec_route_catalog_path: '',
    route_catalog_count: 0,
    route_catalog_supported_count: 0,
  };
}

function parseJsonStdout(stdoutText, expectedFormat, label) {
  const trimmed = String(stdoutText || '').trim();
  if (!trimmed) throw new Error(`${label}_stdout_empty`);
  const doc = JSON.parse(trimmed);
  if (String(doc.format || '') !== expectedFormat) {
    throw new Error(`${label}_format_mismatch:${String(doc.format || '')}`);
  }
  return doc;
}

function compileAndRunEntry(tool, packageRoot, mainPath, exePath, compileReportPath, compileLogPath, runLogPath, runArgs = []) {
  fs.rmSync(exePath, { force: true });
  fs.rmSync(compileReportPath, { force: true });
  const compile = spawnForExec(tool, [
    'system-link-exec',
    `--in:${mainPath}`,
    `--root:${packageRoot}`,
    '--emit:exe',
    '--target:arm64-apple-darwin',
    `--out:${exePath}`,
    `--report-out:${compileReportPath}`,
  ]);
  writeText(compileLogPath, [compile.stdout || '', compile.stderr || '', compile.error ? String(compile.error.stack || compile.error.message || compile.error) : ''].filter(Boolean).join('\n'));
  const compileReturnCode = exitCodeOf(compile);
  if (compileReturnCode !== 0) {
    return {
      compileReturnCode,
      runReturnCode: -1,
      stdout: '',
    };
  }
  const run = spawnForExec(exePath, runArgs, { timeout: 30000 });
  writeText(runLogPath, [run.stdout || '', run.stderr || '', run.error ? String(run.error.stack || run.error.message || run.error) : ''].filter(Boolean).join('\n'));
  return {
    compileReturnCode,
    runReturnCode: exitCodeOf(run),
    stdout: String(run.stdout || ''),
  };
}

function main() {
  const args = parseArgs(process.argv);
  const workspaceRoot = resolveWorkspaceRoot();
  const repo = path.resolve(args.repo);
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_v3_cheng'));
  const manifestPath = path.resolve(args.codegenManifest || path.join(outDir, 'cheng_codegen_v1.json'));
  const manifest = readJson(manifestPath);
  const packageRoot = path.resolve(String(manifest.package_root || path.join(outDir, 'cheng_codegen')));
  ensurePackageCompileSupport(workspaceRoot, packageRoot);
  const routeCatalog = loadRouteCatalog(manifest, outDir);
  const tool = resolveDefaultToolingBin(workspaceRoot);
  const routeRuntimeDataPath = path.join(packageRoot, 'src', 'route_runtime_data.cheng');
  const routeRuntimePath = path.join(packageRoot, 'src', 'route_runtime.cheng');
  const routeRuntimeMainPath = path.join(packageRoot, 'src', 'route_runtime_main.cheng');
  const routeRuntimeRequestPath = path.join(packageRoot, 'src', 'route_runtime_request.cheng');
  const routeCatalogMainPath = path.join(packageRoot, 'src', 'route_catalog_main.cheng');
  const exePath = path.join(outDir, 'cheng_codegen_exec_main');
  const compileReportPath = path.join(outDir, 'cheng_codegen_exec_main.report.txt');
  const compileLogPath = path.join(outDir, 'cheng_codegen_exec_main.log');
  const runLogPath = path.join(outDir, 'cheng_codegen_exec_main.run.log');
  const routeCatalogExePath = path.join(outDir, 'cheng_codegen_exec_route_catalog');
  const routeCatalogCompileReportPath = path.join(outDir, 'cheng_codegen_exec_route_catalog.report.txt');
  const routeCatalogCompileLogPath = path.join(outDir, 'cheng_codegen_exec_route_catalog.log');
  const routeCatalogRunLogPath = path.join(outDir, 'cheng_codegen_exec_route_catalog.run.log');
  const snapshotPath = path.join(outDir, 'cheng_codegen_exec_snapshot_v1.json');
  const execRouteCatalogPath = path.join(outDir, 'cheng_codegen_exec_route_catalog_v1.json');
  const smokePath = path.join(outDir, 'cheng_codegen_exec_smoke_v1.json');
  const summary = buildZeroSummary(tool, routeRuntimeMainPath, exePath, compileReportPath, compileLogPath, runLogPath);
  summary.route_catalog_main_path = routeCatalogMainPath;
  summary.route_catalog_exe_path = routeCatalogExePath;
  summary.route_catalog_compile_report_path = routeCatalogCompileReportPath;
  summary.route_catalog_compile_log_path = routeCatalogCompileLogPath;
  summary.route_catalog_run_log_path = routeCatalogRunLogPath;
  requireGeneratedModule(routeRuntimePath, 'route_runtime');
  requireGeneratedModule(routeRuntimeRequestPath, 'route_runtime_request');
  requireGeneratedModule(routeRuntimeMainPath, 'route_runtime_main');
  const entryRouteState = String(manifest.base_snapshot?.route_state || manifest.entry_route_state || manifest.entry_module || 'home_default').trim() || 'home_default';
  const snapshotRoute = String(args.route || entryRouteState).trim() || 'home_default';

  if (!fs.existsSync(tool)) {
    summary.reason = 'missing_tooling_bin';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: -1,
      run_returncode: -1,
      route_catalog_compile_returncode: -1,
      route_catalog_run_returncode: -1,
      snapshot_path: '',
      route_runtime_main_path: routeRuntimeMainPath,
      snapshot_requested_route: snapshotRoute,
      route_catalog_main_path: routeCatalogMainPath,
      exec_route_catalog_path: '',
      route_catalog_count: 0,
      route_catalog_supported_count: 0,
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }

  const snapshotSemanticNodesCount = resolveSemanticNodesCount(
    routeCatalog,
    snapshotRoute,
    0,
    manifest.base_snapshot || {},
  );
  writeText(
    routeRuntimeDataPath,
    renderRouteRuntimeDataCheng(
      String(manifest.exec_io_module || 'cheng/cheng_codegen/exec_io'),
      buildRouteRuntimeSnapshot(manifest.base_snapshot || {}, snapshotRoute, snapshotSemanticNodesCount),
      snapshotRoute,
      '0',
    ),
  );
  const snapshotRun = compileAndRunEntry(
    tool,
    packageRoot,
    routeRuntimeMainPath,
    exePath,
    compileReportPath,
    compileLogPath,
    runLogPath,
  );
  summary.compile_returncode = snapshotRun.compileReturnCode;
  summary.run_returncode = snapshotRun.runReturnCode;
  if (summary.compile_returncode !== 0) {
    summary.reason = 'route_runtime_compile_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: -1,
      route_catalog_compile_returncode: -1,
      route_catalog_run_returncode: -1,
      snapshot_path: '',
      route_runtime_main_path: routeRuntimeMainPath,
      snapshot_requested_route: snapshotRoute,
      route_catalog_main_path: routeCatalogMainPath,
      exec_route_catalog_path: '',
      route_catalog_count: 0,
      route_catalog_supported_count: 0,
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }
  if (summary.run_returncode !== 0) {
    summary.reason = 'route_runtime_run_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: summary.run_returncode,
      route_catalog_compile_returncode: -1,
      route_catalog_run_returncode: -1,
      snapshot_path: '',
      route_catalog_main_path: routeCatalogMainPath,
      exec_route_catalog_path: '',
      route_catalog_count: 0,
      route_catalog_supported_count: 0,
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }

  let snapshot;
  try {
    snapshot = parseJsonStdout(snapshotRun.stdout, 'cheng_codegen_exec_snapshot_v1', 'snapshot');
  } catch (error) {
    summary.reason = 'route_runtime_parse_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: summary.run_returncode,
      route_catalog_compile_returncode: -1,
      route_catalog_run_returncode: -1,
      snapshot_path: '',
      route_runtime_main_path: routeRuntimeMainPath,
      snapshot_requested_route: snapshotRoute,
      exec_route_catalog_path: '',
      route_catalog_count: 0,
      route_catalog_supported_count: 0,
      exec_smoke_path: smokePath,
    });
    writeText(runLogPath, [fs.readFileSync(runLogPath, 'utf8'), `\n${String(error.message || error)}`].filter(Boolean).join(''));
    process.exit(1);
  }
  writeJson(snapshotPath, snapshot);

  const routeCatalogRun = compileAndRunEntry(
    tool,
    packageRoot,
    routeCatalogMainPath,
    routeCatalogExePath,
    routeCatalogCompileReportPath,
    routeCatalogCompileLogPath,
    routeCatalogRunLogPath,
  );
  summary.route_catalog_compile_returncode = routeCatalogRun.compileReturnCode;
  summary.route_catalog_run_returncode = routeCatalogRun.runReturnCode;
  if (summary.route_catalog_compile_returncode !== 0) {
    summary.reason = 'route_catalog_compile_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: summary.run_returncode,
      route_catalog_compile_returncode: summary.route_catalog_compile_returncode,
      route_catalog_run_returncode: -1,
      snapshot_path: snapshotPath,
      route_runtime_main_path: routeRuntimeMainPath,
      snapshot_requested_route: snapshotRoute,
      route_catalog_main_path: routeCatalogMainPath,
      exec_route_catalog_path: '',
      route_catalog_count: 0,
      route_catalog_supported_count: 0,
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }
  if (summary.route_catalog_run_returncode !== 0) {
    summary.reason = 'route_catalog_run_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: summary.run_returncode,
      route_catalog_compile_returncode: summary.route_catalog_compile_returncode,
      route_catalog_run_returncode: summary.route_catalog_run_returncode,
      snapshot_path: snapshotPath,
      route_runtime_main_path: routeRuntimeMainPath,
      snapshot_requested_route: snapshotRoute,
      route_catalog_main_path: routeCatalogMainPath,
      exec_route_catalog_path: '',
      route_catalog_count: 0,
      route_catalog_supported_count: 0,
      exec_smoke_path: smokePath,
    });
    process.exit(1);
  }

  let routeCatalogDoc;
  try {
    routeCatalogDoc = parseJsonStdout(routeCatalogRun.stdout, 'cheng_codegen_route_catalog_v1', 'route_catalog');
  } catch (error) {
    summary.reason = 'route_catalog_parse_failed';
    writeJson(smokePath, summary);
    writeSummary(args.summaryOut, {
      smoke_ok: false,
      smoke_reason: summary.reason,
      compile_returncode: summary.compile_returncode,
      run_returncode: summary.run_returncode,
      route_catalog_compile_returncode: summary.route_catalog_compile_returncode,
      route_catalog_run_returncode: summary.route_catalog_run_returncode,
      snapshot_path: snapshotPath,
      route_runtime_main_path: routeRuntimeMainPath,
      snapshot_requested_route: snapshotRoute,
      route_catalog_main_path: routeCatalogMainPath,
      exec_route_catalog_path: '',
      route_catalog_count: 0,
      route_catalog_supported_count: 0,
      exec_smoke_path: smokePath,
    });
    writeText(routeCatalogRunLogPath, [fs.readFileSync(routeCatalogRunLogPath, 'utf8'), `\n${String(error.message || error)}`].filter(Boolean).join(''));
    process.exit(1);
  }
  writeJson(execRouteCatalogPath, routeCatalogDoc);

  summary.ok = true;
  summary.reason = 'ok';
  summary.snapshot_path = snapshotPath;
  summary.snapshot_route_state = String(snapshot.route_state || '');
  summary.snapshot_render_ready = Boolean(snapshot.render_ready);
  summary.snapshot_semantic_nodes_loaded = Boolean(snapshot.semantic_nodes_loaded);
  summary.snapshot_semantic_nodes_count = Number(snapshot.semantic_nodes_count || 0);
  summary.exec_route_catalog_path = execRouteCatalogPath;
  summary.route_catalog_count = Number(routeCatalogDoc.routeCount || 0);
  summary.route_catalog_supported_count = Number(routeCatalogDoc.supportedCount || 0);
  writeJson(smokePath, summary);
  writeSummary(args.summaryOut, {
    smoke_ok: true,
    smoke_reason: summary.reason,
    compile_returncode: summary.compile_returncode,
    run_returncode: summary.run_returncode,
    route_catalog_compile_returncode: summary.route_catalog_compile_returncode,
    route_catalog_run_returncode: summary.route_catalog_run_returncode,
    snapshot_path: snapshotPath,
    route_runtime_main_path: routeRuntimeMainPath,
    snapshot_requested_route: snapshotRoute,
    route_catalog_main_path: routeCatalogMainPath,
    snapshot_route_state: summary.snapshot_route_state,
    snapshot_render_ready: summary.snapshot_render_ready,
    snapshot_semantic_nodes_loaded: summary.snapshot_semantic_nodes_loaded,
    snapshot_semantic_nodes_count: summary.snapshot_semantic_nodes_count,
    exec_route_catalog_path: execRouteCatalogPath,
    route_catalog_count: summary.route_catalog_count,
    route_catalog_supported_count: summary.route_catalog_supported_count,
    exec_smoke_path: smokePath,
  });
  console.log(JSON.stringify({
    ok: true,
    repo_root: repo,
    out_dir: outDir,
    compile_returncode: summary.compile_returncode,
    run_returncode: summary.run_returncode,
    route_catalog_compile_returncode: summary.route_catalog_compile_returncode,
    route_catalog_run_returncode: summary.route_catalog_run_returncode,
    snapshot_path: snapshotPath,
    snapshot_route_state: summary.snapshot_route_state,
    snapshot_semantic_nodes_count: summary.snapshot_semantic_nodes_count,
    exec_route_catalog_path: execRouteCatalogPath,
    route_catalog_count: summary.route_catalog_count,
  }, null, 2));
}

main();
