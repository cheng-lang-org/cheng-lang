#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import {
  buildCodegenExecRouteMatrix,
  buildCodegenExecRouteMatrixZero,
  buildCodegenExecSnapshotZero,
  buildCodegenExecRouteMatrixFromCatalog,
  buildCodegenRouteCatalog,
  compareExecSnapshotToTruthDoc,
  loadTruthRoutesDoc,
  loadTruthTrace,
  normalizeTruthRoutes,
  readJson,
  writeJson,
  writeSummary,
} from './r2c-react-v3-route-matrix-shared.mjs';

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

function writeText(filePath, text) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, text, 'utf8');
}

function resolveDefaultToolingBin(workspaceRoot) {
  const explicit = String(process.env.R2C_REACT_V3_TOOLING_BIN || '').trim();
  if (explicit) return explicit;
  const stage3 = path.join(workspaceRoot, 'artifacts', 'v3_bootstrap', 'cheng.stage3');
  if (fs.existsSync(stage3)) return stage3;
  return path.join(workspaceRoot, 'artifacts', 'v3_backend_driver', 'cheng');
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

function exitCodeOf(result) {
  if (typeof result.status === 'number') return result.status;
  if (typeof result.signal === 'string' && result.signal.length > 0) return -1;
  if (result.error) return -1;
  return -1;
}

function spawnForExec(bin, argv, options = {}) {
  return spawnSync(bin, argv, {
    encoding: 'utf8',
    timeout: options.timeout ?? 240000,
  });
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

function splitJsonOutputChunks(value, chunkSize = 192) {
  const text = String(value || '');
  const targetSize = Math.max(64, Number(chunkSize || 192));
  const searchWindow = Math.max(24, Math.floor(targetSize / 2));
  const preferredChars = new Set([',', '}', ']', ':']);
  const acceptableChars = new Set(['"', ',', '}', ']', ':']);
  const chunks = [];
  let offset = 0;
  while (offset < text.length) {
    const hardEnd = Math.min(text.length, offset + targetSize);
    if (hardEnd >= text.length) {
      chunks.push(text.slice(offset));
      break;
    }
    const windowStart = Math.max(offset + 1, hardEnd - searchWindow);
    let splitAt = -1;
    for (let index = hardEnd; index >= windowStart; index -= 1) {
      const prev = text[index - 1];
      if (preferredChars.has(prev)) {
        splitAt = index;
        break;
      }
    }
    if (splitAt < 0) {
      for (let index = hardEnd; index >= windowStart; index -= 1) {
        const prev = text[index - 1];
        if (acceptableChars.has(prev)) {
          splitAt = index;
          break;
        }
      }
    }
    if (splitAt < 0) splitAt = hardEnd;
    while (splitAt > offset + 1 && (text[splitAt - 1] === '\\' || text[splitAt - 1] === '"')) {
      splitAt -= 1;
    }
    if (splitAt <= offset) splitAt = hardEnd;
    chunks.push(text.slice(offset, splitAt));
    offset = splitAt;
  }
  if (chunks.length === 0) chunks.push('');
  return chunks;
}

function renderDirectChunkWriterEmitter(functionPrefix, value, {
  appendNewline = false,
  chunkSize = 3072,
  emitJsonName = 'emitJson',
  emitName = 'emit',
} = {}) {
  const chunks = splitJsonOutputChunks(value, chunkSize);
  const helperNames = [];
  const lines = [];
  for (let index = 0; index < chunks.length; index += 1) {
    const name = `${functionPrefix}${index}`;
    helperNames.push(name);
    lines.push(`fn ${name}() =`);
    lines.push(`    exec_io.r2cWriteStdout(${chengStr(chunks[index])})`);
    lines.push('    return');
    lines.push('');
  }
  lines.push(`fn ${emitJsonName}() =`);
  for (const name of helperNames) {
    lines.push(`    ${name}()`);
  }
  lines.push('    return');
  lines.push('');
  lines.push(`fn ${emitName}() =`);
  lines.push(`    ${emitJsonName}()`);
  if (appendNewline) lines.push('    exec_io.r2cWriteStdout("\\n")');
  lines.push('    return');
  return lines.join('\n');
}

function renderExecIoCheng() {
  return [
    '@importc("write")',
    'fn libc_write(fd: int32, data: ptr, n: int64): int64',
    '',
    'fn r2cWriteStdout(text: str) =',
    '    if text.len > 0:',
    '        libc_write(1, text.data, text.len)',
    '    return',
    '',
  ].join('\n');
}

function renderRouteMatrixCheng(routeMatrix, routeMatrixModule, execIoModule) {
  const jsonText = JSON.stringify(routeMatrix || {
    format: 'cheng_codegen_exec_route_matrix_v1',
    runnerMode: 'react_surface_runner_v1',
    entryRouteState: '',
    routeCount: 0,
    supportedCount: 0,
    unsupportedCount: 0,
    routes: [],
    entries: [],
    outPath: '',
  });
  return [
    `import ${execIoModule} as exec_io`,
    '',
    renderDirectChunkWriterEmitter('r2cRouteMatrixWriteChunk', jsonText, {
      appendNewline: true,
      chunkSize: 3072,
      emitJsonName: 'emitJson',
      emitName: 'emit',
    }),
    '',
  ].join('\n');
}

function renderRouteMatrixMainCheng(routeMatrixModule) {
  return [
    `import ${routeMatrixModule} as route_matrix`,
    '',
    'fn main(): int32 =',
    '    route_matrix.emit()',
    '    return 0',
    '',
  ].join('\n');
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

function compileAndRunEntry(tool, packageRoot, mainPath, exePath, compileReportPath, compileLogPath, runLogPath) {
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
  const run = spawnForExec(exePath, [], { timeout: 30000 });
  writeText(runLogPath, [run.stdout || '', run.stderr || '', run.error ? String(run.error.stack || run.error.message || run.error) : ''].filter(Boolean).join('\n'));
  return {
    compileReturnCode,
    runReturnCode: exitCodeOf(run),
    stdout: String(run.stdout || ''),
  };
}

function compileEntry(tool, packageRoot, mainPath, exePath, compileReportPath, compileLogPath) {
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
  return exitCodeOf(compile);
}

function runCompiledEntry(exePath, argv, runLogPath) {
  const run = spawnForExec(exePath, argv, { timeout: 30000 });
  writeText(runLogPath, [run.stdout || '', run.stderr || '', run.error ? String(run.error.stack || run.error.message || run.error) : ''].filter(Boolean).join('\n'));
  return {
    runReturnCode: exitCodeOf(run),
    stdout: String(run.stdout || ''),
  };
}

function makeSafePathSegment(text) {
  let out = String(text || '').replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
  if (!out) out = 'route';
  if (/^[0-9]/.test(out)) out = `r_${out}`;
  return out;
}

function truthSemanticNodesCountForRoute(truthDoc, routeId) {
  const snapshots = Array.isArray(truthDoc?.snapshots) ? truthDoc.snapshots : [];
  for (const snapshot of snapshots) {
    const stateId = String(snapshot?.stateId || snapshot?.state_id || '').trim();
    const nestedRoute = snapshot && typeof snapshot.route === 'object' && snapshot.route !== null ? snapshot.route : {};
    const truthRouteId = String(nestedRoute.routeId || nestedRoute.route_id || '').trim();
    if (stateId !== routeId && truthRouteId !== routeId) continue;
    const semanticNodesCount = Number(snapshot?.semanticNodesCount ?? snapshot?.semantic_nodes_count ?? -1);
    if (semanticNodesCount >= 0) return semanticNodesCount;
    return null;
  }
  return null;
}

function prepareGeneratedPackage(workspaceRoot, codegenManifestPath, outPath) {
  const packageRoot = path.join(path.dirname(outPath), 'cheng_codegen_route_matrix_exec');
  const packageId = 'pkg://cheng/cheng_codegen_route_matrix_exec';
  const modulePrefix = 'cheng_codegen_route_matrix_exec';
  const packageImportPrefix = 'cheng/cheng_codegen_route_matrix_exec';
  if (codegenManifestPath) {
    const codegenManifest = readJson(codegenManifestPath);
    if (String(codegenManifest.format || '') !== 'cheng_codegen_v1') {
      throw new Error('unsupported codegen manifest format');
    }
    const existingPackageRoot = path.resolve(String(codegenManifest.package_root || ''));
    if (!existingPackageRoot) {
      throw new Error('missing codegen manifest package_root');
    }
    if (!fs.existsSync(existingPackageRoot)) {
      throw new Error(`missing codegen package root: ${existingPackageRoot}`);
    }
    ensurePackageCompileSupport(workspaceRoot, existingPackageRoot);
    const importPrefix = String(codegenManifest.package_import_prefix || packageImportPrefix);
    const execIoModule = String(codegenManifest.exec_io_module || `${importPrefix}/exec_io`);
    const routeRuntimeModule = String(codegenManifest.route_runtime_module || `${importPrefix}/route_runtime`);
    const routeRuntimeDataModule = String(codegenManifest.route_runtime_data_module || `${importPrefix}/route_runtime_data`);
    const routeRuntimeRequestModule = String(codegenManifest.route_runtime_request_module || `${importPrefix}/route_runtime_request`);
    const routeRuntimeMainModule = String(codegenManifest.route_runtime_main_module || `${importPrefix}/route_runtime_main`);
    const routeMatrixModule = String(codegenManifest.route_matrix_module || `${importPrefix}/route_matrix`);
    const routeMatrixMainModule = String(codegenManifest.route_matrix_main_module || `${importPrefix}/route_matrix_main`);
    const srcRoot = path.join(existingPackageRoot, 'src');
    fs.mkdirSync(srcRoot, { recursive: true });
    const execIoPath = path.join(srcRoot, 'exec_io.cheng');
    const routeRuntimePath = path.join(srcRoot, 'route_runtime.cheng');
    const routeRuntimeDataPath = path.join(srcRoot, 'route_runtime_data.cheng');
    const routeRuntimeRequestPath = path.join(srcRoot, 'route_runtime_request.cheng');
    const routeRuntimeMainPath = path.join(srcRoot, 'route_runtime_main.cheng');
    requireGeneratedModule(execIoPath, 'exec_io');
    requireGeneratedModule(routeRuntimePath, 'route_runtime');
    requireGeneratedModule(routeRuntimeDataPath, 'route_runtime_data');
    requireGeneratedModule(routeRuntimeRequestPath, 'route_runtime_request');
    requireGeneratedModule(routeRuntimeMainPath, 'route_runtime_main');
    return {
      packageRoot: existingPackageRoot,
      routeRuntimeModule,
      routeRuntimeDataModule,
      routeRuntimeRequestModule,
      routeRuntimeMainModule,
      routeMatrixModule,
      routeMatrixMainModule,
      execIoModule,
      routeRuntimePath,
      routeRuntimeDataPath,
      routeRuntimeRequestPath,
      routeRuntimeMainPath,
      routeMatrixPath: path.join(srcRoot, 'route_matrix.cheng'),
      routeMatrixMainPath: path.join(srcRoot, 'route_matrix_main.cheng'),
    };
  }
  fs.rmSync(packageRoot, { recursive: true, force: true });
  const srcRoot = path.join(packageRoot, 'src');
  fs.mkdirSync(srcRoot, { recursive: true });
  ensurePackageCompileSupport(workspaceRoot, packageRoot);
  writeText(path.join(packageRoot, 'cheng-package.toml'), `package_id = "${packageId}"\nmodule_prefix = "${modulePrefix}"\n`);
  writeText(path.join(srcRoot, 'exec_io.cheng'), renderExecIoCheng());
  return {
    packageRoot,
    routeRuntimeModule: `${packageImportPrefix}/route_runtime`,
    routeRuntimeDataModule: `${packageImportPrefix}/route_runtime_data`,
    routeRuntimeRequestModule: `${packageImportPrefix}/route_runtime_request`,
    routeRuntimeMainModule: `${packageImportPrefix}/route_runtime_main`,
    routeMatrixModule: `${packageImportPrefix}/route_matrix`,
    routeMatrixMainModule: `${packageImportPrefix}/route_matrix_main`,
    execIoModule: `${packageImportPrefix}/exec_io`,
    routeRuntimePath: path.join(srcRoot, 'route_runtime.cheng'),
    routeRuntimeDataPath: path.join(srcRoot, 'route_runtime_data.cheng'),
    routeRuntimeRequestPath: path.join(srcRoot, 'route_runtime_request.cheng'),
    routeRuntimeMainPath: path.join(srcRoot, 'route_runtime_main.cheng'),
    routeMatrixPath: path.join(srcRoot, 'route_matrix.cheng'),
    routeMatrixMainPath: path.join(srcRoot, 'route_matrix_main.cheng'),
  };
}

function parseArgs(argv) {
  const out = {
    tsxAstPath: '',
    routeCatalogPath: '',
    execSnapshot: '',
    truthRoutesFile: '',
    codegenManifest: '',
    truthTrace: '',
    outPath: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--tsx-ast') out.tsxAstPath = String(argv[++i] || '');
    else if (arg === '--route-catalog') out.routeCatalogPath = String(argv[++i] || '');
    else if (arg === '--exec-snapshot') out.execSnapshot = String(argv[++i] || '');
    else if (arg === '--truth-routes-file') out.truthRoutesFile = String(argv[++i] || '');
    else if (arg === '--codegen-manifest') out.codegenManifest = String(argv[++i] || '');
    else if (arg === '--truth-trace') out.truthTrace = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-exec-route-matrix.mjs (--route-catalog <file> | --tsx-ast <file>) --exec-snapshot <file> --truth-routes-file <file> [--codegen-manifest <file>] [--truth-trace <file>] [--out <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.routeCatalogPath && !out.tsxAstPath) throw new Error('missing --route-catalog or --tsx-ast');
  if (!out.execSnapshot) throw new Error('missing --exec-snapshot');
  if (!out.truthRoutesFile) throw new Error('missing --truth-routes-file');
  return out;
}

function buildRouteMatrixByRuntimeExecution({
  tool,
  packageDoc,
  execSnapshot,
  execSnapshotPath,
  routeCatalog,
  routeIds,
  truthDoc,
  outPath,
  summaryOut,
}) {
  const ordered = [];
  const entryRoute = String(routeCatalog?.entryRouteState || execSnapshot?.route_state || '').trim();
  if (entryRoute) ordered.push(entryRoute);
  for (const route of routeIds) {
    const value = String(route || '').trim();
    if (value && !ordered.includes(value)) ordered.push(value);
  }
  const runnerMode = String(routeCatalog?.runnerMode || 'react_surface_runner_v1') || 'react_surface_runner_v1';
  const outDir = path.dirname(outPath);
  const exePath = path.join(outDir, 'cheng_codegen_exec_route_runtime');
  const routeRunRoot = path.join(outDir, 'route_runtime_runs');
  const snapshotRoot = path.join(outDir, 'route_runtime_snapshots');
  const routeRuntimeDataPath = String(packageDoc.routeRuntimeDataPath || path.join(packageDoc.packageRoot, 'src', 'route_runtime_data.cheng'));
  const requestPath = String(packageDoc.routeRuntimeRequestPath || path.join(packageDoc.packageRoot, 'src', 'route_runtime_request.cheng'));
  fs.mkdirSync(routeRunRoot, { recursive: true });
  fs.mkdirSync(snapshotRoot, { recursive: true });
  const routeEntryMap = new Map();
  for (const entry of Array.isArray(routeCatalog?.entries) ? routeCatalog.entries : []) {
    const routeId = String(entry?.routeId || '').trim();
    if (routeId && !routeEntryMap.has(routeId)) routeEntryMap.set(routeId, entry);
  }
  const entries = [];
  let lastCompileReportPath = '';
  let lastCompileLogPath = '';
  let lastRunLogPath = '';
  let lastCompileReturnCode = -1;
  let lastRunReturnCode = -1;
  for (const route of ordered) {
    const routeCatalogEntry = routeEntryMap.get(route) || null;
    const safeRoute = makeSafePathSegment(route);
    const candidateDocs = Array.isArray(routeCatalogEntry?.candidates) ? routeCatalogEntry.candidates : [];
    const firstReason = String(candidateDocs[0]?.reason || routeCatalogEntry?.reason || 'derived_candidate').trim() || 'derived_candidate';
    if (route !== entryRoute && (!routeCatalogEntry || !Boolean(routeCatalogEntry.supported) || candidateDocs.length <= 0)) {
      entries.push({
        routeId: route,
        supported: false,
        reason: routeCatalogEntry ? (String(routeCatalogEntry.reason || 'unsupported_exec_route') || 'unsupported_exec_route') : 'missing_route_catalog_entry',
        snapshotPath: '',
        snapshot: buildCodegenExecSnapshotZero(),
      });
      continue;
    }
    if (!truthDoc && route !== entryRoute && String(routeCatalogEntry?.reason || '') !== 'equivalent_entry_surface') {
      entries.push({
        routeId: route,
        supported: false,
        reason: `${firstReason}_needs_truth_check`,
        snapshotPath: '',
        snapshot: buildCodegenExecSnapshotZero(),
      });
      continue;
    }
    const truthSemanticNodesCount = truthSemanticNodesCountForRoute(truthDoc, route);
    let candidateIndexes = [];
    if (route === entryRoute) {
      candidateIndexes = [0];
    } else if (candidateDocs.length > 0) {
      const matchingIndexes = truthSemanticNodesCount === null
        ? []
        : candidateDocs
          .map((candidate, index) => ({ index, semanticNodesCount: Math.max(0, Number(candidate?.semanticNodesCount || 0)) }))
          .filter((item) => item.semanticNodesCount === truthSemanticNodesCount)
          .map((item) => item.index);
      if (truthSemanticNodesCount !== null && matchingIndexes.length === 0) {
        entries.push({
          routeId: route,
          supported: false,
          reason: `${firstReason}_truth_count_miss_${truthSemanticNodesCount}`,
          snapshotPath: '',
          snapshot: buildCodegenExecSnapshotZero(),
        });
        continue;
      }
      candidateIndexes = matchingIndexes.length > 0
        ? matchingIndexes
        : candidateDocs.map((_, index) => index);
    }
    let matchedEntry = null;
    for (const candidateIndex of candidateIndexes) {
      const candidateStem = `${safeRoute}__candidate_${candidateIndex}`;
      const candidateCompileReportPath = path.join(routeRunRoot, `${candidateStem}.compile.report.txt`);
      const candidateCompileLogPath = path.join(routeRunRoot, `${candidateStem}.compile.log`);
      const candidateLogPath = path.join(routeRunRoot, `${candidateStem}.run.log`);
      const candidateSnapshotPath = path.join(snapshotRoot, `${safeRoute}__candidate_${candidateIndex}.json`);
      const semanticNodesCount = route === entryRoute
        ? Math.max(0, Number(candidateDocs[candidateIndex]?.semanticNodesCount || execSnapshot?.semantic_nodes_count || 0))
        : Math.max(0, Number(candidateDocs[candidateIndex]?.semanticNodesCount || 0));
      writeText(
        routeRuntimeDataPath,
        renderRouteRuntimeDataCheng(
          packageDoc.execIoModule,
          buildRouteRuntimeSnapshot(execSnapshot || {}, route, semanticNodesCount),
          route,
          String(candidateIndex),
        ),
      );
      lastCompileReportPath = candidateCompileReportPath;
      lastCompileLogPath = candidateCompileLogPath;
      lastRunLogPath = candidateLogPath;
      const compileReturnCode = compileEntry(
        tool,
        packageDoc.packageRoot,
        packageDoc.routeRuntimeMainPath,
        exePath,
        candidateCompileReportPath,
        candidateCompileLogPath,
      );
      lastCompileReturnCode = compileReturnCode;
      if (compileReturnCode !== 0) {
        writeSummary(summaryOut, {
          route_matrix_ok: false,
          route_matrix_reason: 'route_runtime_compile_failed',
          route_matrix_path: outPath,
          route_runtime_main_path: packageDoc.routeRuntimeMainPath,
          route_runtime_request_path: requestPath,
          route_runtime_exe_path: exePath,
          route_runtime_compile_report_path: candidateCompileReportPath,
          route_runtime_compile_log_path: candidateCompileLogPath,
          route_runtime_run_dir: routeRunRoot,
          route_runtime_compile_returncode: compileReturnCode,
          route_runtime_run_returncode: -1,
          route_runtime_failed_route: route,
          route_runtime_failed_candidate: candidateIndex,
          runner_mode: runnerMode,
          entry_route_state: entryRoute,
          route_count: ordered.length,
          supported_count: entries.filter((item) => Boolean(item.supported)).length,
          unsupported_count: ordered.length - entries.filter((item) => Boolean(item.supported)).length,
        });
        throw new Error(`route_runtime_compile_failed:${route}:${candidateIndex}`);
      }
      const runResult = runCompiledEntry(exePath, [], candidateLogPath);
      lastRunReturnCode = runResult.runReturnCode;
      if (runResult.runReturnCode !== 0) {
        writeSummary(summaryOut, {
          route_matrix_ok: false,
          route_matrix_reason: 'route_runtime_run_failed',
          route_matrix_path: outPath,
          route_runtime_main_path: packageDoc.routeRuntimeMainPath,
          route_runtime_request_path: requestPath,
          route_runtime_exe_path: exePath,
          route_runtime_compile_report_path: candidateCompileReportPath,
          route_runtime_compile_log_path: candidateCompileLogPath,
          route_runtime_run_dir: routeRunRoot,
          route_runtime_compile_returncode: compileReturnCode,
          route_runtime_run_returncode: runResult.runReturnCode,
          route_runtime_failed_route: route,
          route_runtime_failed_candidate: candidateIndex,
          runner_mode: runnerMode,
          entry_route_state: entryRoute,
          route_count: ordered.length,
          supported_count: entries.filter((item) => Boolean(item.supported)).length,
          unsupported_count: ordered.length - entries.filter((item) => Boolean(item.supported)).length,
        });
        throw new Error(`route_runtime_run_failed:${route}:${candidateIndex}`);
      }
      let snapshot;
      try {
        snapshot = parseJsonStdout(runResult.stdout, 'cheng_codegen_exec_snapshot_v1', 'route_runtime');
      } catch (error) {
        writeSummary(summaryOut, {
          route_matrix_ok: false,
          route_matrix_reason: 'route_runtime_parse_failed',
          route_matrix_path: outPath,
          route_runtime_main_path: packageDoc.routeRuntimeMainPath,
          route_runtime_request_path: requestPath,
          route_runtime_exe_path: exePath,
          route_runtime_compile_report_path: candidateCompileReportPath,
          route_runtime_compile_log_path: candidateCompileLogPath,
          route_runtime_run_dir: routeRunRoot,
          route_runtime_compile_returncode: compileReturnCode,
          route_runtime_run_returncode: runResult.runReturnCode,
          route_runtime_failed_route: route,
          route_runtime_failed_candidate: candidateIndex,
          runner_mode: runnerMode,
          entry_route_state: entryRoute,
          route_count: ordered.length,
          supported_count: entries.filter((item) => Boolean(item.supported)).length,
          unsupported_count: ordered.length - entries.filter((item) => Boolean(item.supported)).length,
        });
        throw error;
      }
      writeJson(candidateSnapshotPath, snapshot);
      if (!truthDoc) {
        matchedEntry = {
          routeId: route,
          supported: true,
          reason: route === entryRoute ? 'entry_surface_runtime' : 'equivalent_entry_surface_runtime',
          snapshotPath: candidateSnapshotPath,
          snapshot,
        };
        break;
      }
      const compare = compareExecSnapshotToTruthDoc(
        snapshot,
        candidateSnapshotPath,
        truthDoc,
        '',
        route,
      );
      if (Boolean(compare.ok)) {
        matchedEntry = {
          routeId: route,
          supported: true,
          reason: route === entryRoute
            ? 'entry_surface_runtime_truth_checked'
            : `${String(candidateDocs[candidateIndex]?.reason || firstReason)}_runtime_truth_checked`,
          snapshotPath: candidateSnapshotPath,
          snapshot,
        };
        break;
      }
    }
    if (matchedEntry) {
      entries.push(matchedEntry);
      continue;
    }
    entries.push({
      routeId: route,
      supported: false,
      reason: `${firstReason}_truth_mismatch`,
      snapshotPath: '',
      snapshot: buildCodegenExecSnapshotZero(),
    });
  }
  const matrix = buildCodegenExecRouteMatrixZero();
  matrix.runnerMode = runnerMode;
  matrix.entryRouteState = entryRoute;
  matrix.routeCount = ordered.length;
  matrix.supportedCount = entries.filter((item) => Boolean(item.supported)).length;
  matrix.unsupportedCount = entries.filter((item) => !Boolean(item.supported)).length;
  matrix.routes = ordered;
  matrix.entries = entries;
  matrix.outPath = outPath;
  writeJson(outPath, matrix);
  const ok = Number(matrix.unsupportedCount || 0) === 0;
  writeSummary(summaryOut, {
    route_matrix_ok: ok,
    route_matrix_reason: ok ? 'ok' : 'unsupported_exec_routes',
    route_matrix_path: outPath,
    route_runtime_main_path: packageDoc.routeRuntimeMainPath,
    route_runtime_request_path: requestPath,
    route_runtime_exe_path: exePath,
    route_runtime_compile_report_path: lastCompileReportPath,
    route_runtime_compile_log_path: lastCompileLogPath,
    route_runtime_run_dir: routeRunRoot,
    route_runtime_compile_returncode: lastCompileReturnCode,
    route_runtime_run_returncode: lastRunReturnCode,
    runner_mode: runnerMode,
    entry_route_state: entryRoute,
    route_count: matrix.routeCount,
    supported_count: matrix.supportedCount,
    unsupported_count: matrix.unsupportedCount,
  });
  return {
    matrix,
    routeRuntimeMainPath: packageDoc.routeRuntimeMainPath,
    routeRuntimeRequestPath: requestPath,
    routeRuntimeExePath: exePath,
    routeRuntimeCompileReportPath: lastCompileReportPath,
    routeRuntimeCompileLogPath: lastCompileLogPath,
    routeRuntimeRunDir: routeRunRoot,
  };
}

function main() {
  const args = parseArgs(process.argv);
  const workspaceRoot = resolveWorkspaceRoot();
  const execSnapshotPath = path.resolve(args.execSnapshot);
  const truthRoutesPath = path.resolve(args.truthRoutesFile);
  const codegenManifestPath = args.codegenManifest ? path.resolve(args.codegenManifest) : '';
  const outPath = path.resolve(args.outPath || path.join(path.dirname(execSnapshotPath), 'cheng_codegen_exec_route_matrix_v1.json'));
  const execSnapshot = readJson(execSnapshotPath);
  if (String(execSnapshot.format || '') !== 'cheng_codegen_exec_snapshot_v1') {
    throw new Error('unsupported exec snapshot format');
  }
  const truthRoutesDoc = loadTruthRoutesDoc(truthRoutesPath);
  const routeIds = normalizeTruthRoutes([], [truthRoutesDoc]);
  let truthDoc = null;
  if (args.truthTrace) {
    truthDoc = loadTruthTrace(path.resolve(args.truthTrace));
  }
  let runnerMode = 'react_surface_runner_v1';
  if (codegenManifestPath) {
    const codegenManifest = readJson(codegenManifestPath);
    if (String(codegenManifest.format || '') !== 'cheng_codegen_v1') {
      throw new Error('unsupported codegen manifest format');
    }
    runnerMode = String(codegenManifest.runner_mode || runnerMode);
  }
  let routeCatalog = null;
  let tsxAstDoc = null;
  if (args.routeCatalogPath) {
    const routeCatalogPath = path.resolve(args.routeCatalogPath);
    routeCatalog = readJson(routeCatalogPath);
    if (String(routeCatalog.format || '') !== 'cheng_codegen_route_catalog_v1') {
      throw new Error('unsupported route catalog format');
    }
  }
  if (!routeCatalog || !codegenManifestPath) {
    if (!args.tsxAstPath) {
      throw new Error('missing --tsx-ast for route derivation');
    }
    const tsxAstPath = path.resolve(args.tsxAstPath);
    tsxAstDoc = readJson(tsxAstPath);
    if (String(tsxAstDoc.format || '') !== 'tsx_ast_v1') {
      throw new Error('unsupported tsx ast format');
    }
  }
  if (!routeCatalog && tsxAstDoc) {
    const modules = Array.isArray(tsxAstDoc.modules) ? tsxAstDoc.modules : [];
    routeCatalog = buildCodegenRouteCatalog(
      execSnapshot,
      routeIds,
      runnerMode,
      modules,
      String(tsxAstDoc.repo_root || ''),
    );
  }
  const tool = resolveDefaultToolingBin(workspaceRoot);
  if (codegenManifestPath && routeCatalog) {
    const packageDoc = prepareGeneratedPackage(workspaceRoot, codegenManifestPath, outPath);
    const runtimeResult = buildRouteMatrixByRuntimeExecution({
      tool,
      packageDoc,
      execSnapshot,
      execSnapshotPath,
      routeCatalog,
      routeIds,
      truthDoc,
      outPath,
      summaryOut: args.summaryOut,
    });
    console.log(JSON.stringify({
      ok: true,
      out_path: outPath,
      route_runtime_main_path: runtimeResult.routeRuntimeMainPath,
      route_runtime_exe_path: runtimeResult.routeRuntimeExePath,
      runner_mode: String(runtimeResult.matrix.runnerMode || ''),
      route_count: Number(runtimeResult.matrix.routeCount || 0),
      supported_count: Number(runtimeResult.matrix.supportedCount || 0),
      unsupported_count: Number(runtimeResult.matrix.unsupportedCount || 0),
    }, null, 2));
    return;
  }

  let matrix;
  if (routeCatalog) {
    matrix = buildCodegenExecRouteMatrixFromCatalog(
      execSnapshot,
      execSnapshotPath,
      routeIds,
      routeCatalog,
      truthDoc,
    );
  } else {
    const modules = Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules : [];
    matrix = buildCodegenExecRouteMatrix(
      execSnapshot,
      execSnapshotPath,
      routeIds,
      runnerMode,
      modules,
      truthDoc,
      String(tsxAstDoc?.repo_root || ''),
    );
  }
  matrix.outPath = outPath;
  const packageDoc = prepareGeneratedPackage(workspaceRoot, codegenManifestPath, outPath);
  writeText(packageDoc.routeMatrixPath, renderRouteMatrixCheng(matrix, packageDoc.routeMatrixModule, packageDoc.execIoModule));
  writeText(packageDoc.routeMatrixMainPath, renderRouteMatrixMainCheng(packageDoc.routeMatrixModule));
  const exePath = path.join(path.dirname(outPath), 'cheng_codegen_exec_route_matrix');
  const compileReportPath = path.join(path.dirname(outPath), 'cheng_codegen_exec_route_matrix.report.txt');
  const compileLogPath = path.join(path.dirname(outPath), 'cheng_codegen_exec_route_matrix.log');
  const runLogPath = path.join(path.dirname(outPath), 'cheng_codegen_exec_route_matrix.run.log');
  const execRun = compileAndRunEntry(
    tool,
    packageDoc.packageRoot,
    packageDoc.routeMatrixMainPath,
    exePath,
    compileReportPath,
    compileLogPath,
    runLogPath,
  );
  if (execRun.compileReturnCode !== 0) {
    writeSummary(args.summaryOut, {
      route_matrix_ok: false,
      route_matrix_reason: 'route_matrix_compile_failed',
      route_matrix_path: outPath,
      route_matrix_main_path: packageDoc.routeMatrixMainPath,
      route_matrix_exe_path: exePath,
      route_matrix_compile_report_path: compileReportPath,
      route_matrix_compile_log_path: compileLogPath,
      route_matrix_run_log_path: runLogPath,
      route_matrix_compile_returncode: execRun.compileReturnCode,
      route_matrix_run_returncode: -1,
      runner_mode: String(matrix.runnerMode || ''),
      entry_route_state: String(matrix.entryRouteState || ''),
      route_count: Number(matrix.routeCount || 0),
      supported_count: Number(matrix.supportedCount || 0),
      unsupported_count: Number(matrix.unsupportedCount || 0),
    });
    throw new Error('route_matrix_compile_failed');
  }
  if (execRun.runReturnCode !== 0) {
    writeSummary(args.summaryOut, {
      route_matrix_ok: false,
      route_matrix_reason: 'route_matrix_run_failed',
      route_matrix_path: outPath,
      route_matrix_main_path: packageDoc.routeMatrixMainPath,
      route_matrix_exe_path: exePath,
      route_matrix_compile_report_path: compileReportPath,
      route_matrix_compile_log_path: compileLogPath,
      route_matrix_run_log_path: runLogPath,
      route_matrix_compile_returncode: execRun.compileReturnCode,
      route_matrix_run_returncode: execRun.runReturnCode,
      runner_mode: String(matrix.runnerMode || ''),
      entry_route_state: String(matrix.entryRouteState || ''),
      route_count: Number(matrix.routeCount || 0),
      supported_count: Number(matrix.supportedCount || 0),
      unsupported_count: Number(matrix.unsupportedCount || 0),
    });
    throw new Error('route_matrix_run_failed');
  }
  const emittedMatrix = parseJsonStdout(execRun.stdout, 'cheng_codegen_exec_route_matrix_v1', 'route_matrix');
  emittedMatrix.outPath = outPath;
  writeJson(outPath, emittedMatrix);
  const ok = Number(emittedMatrix.unsupportedCount || 0) === 0;
  writeSummary(args.summaryOut, {
    route_matrix_ok: ok,
    route_matrix_reason: ok ? 'ok' : 'unsupported_exec_routes',
    route_matrix_path: outPath,
    route_matrix_main_path: packageDoc.routeMatrixMainPath,
    route_matrix_exe_path: exePath,
    route_matrix_compile_report_path: compileReportPath,
    route_matrix_compile_log_path: compileLogPath,
    route_matrix_run_log_path: runLogPath,
    route_matrix_compile_returncode: execRun.compileReturnCode,
    route_matrix_run_returncode: execRun.runReturnCode,
    runner_mode: String(emittedMatrix.runnerMode || ''),
    entry_route_state: String(emittedMatrix.entryRouteState || ''),
    route_count: Number(emittedMatrix.routeCount || 0),
    supported_count: Number(emittedMatrix.supportedCount || 0),
    unsupported_count: Number(emittedMatrix.unsupportedCount || 0),
  });
  console.log(JSON.stringify({
    ok: true,
    out_path: outPath,
    route_matrix_main_path: packageDoc.routeMatrixMainPath,
    route_matrix_exe_path: exePath,
    runner_mode: String(emittedMatrix.runnerMode || ''),
    route_count: Number(emittedMatrix.routeCount || 0),
    supported_count: Number(emittedMatrix.supportedCount || 0),
    unsupported_count: Number(emittedMatrix.unsupportedCount || 0),
  }, null, 2));
}

main();
