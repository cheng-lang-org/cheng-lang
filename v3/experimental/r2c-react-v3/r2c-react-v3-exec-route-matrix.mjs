#!/usr/bin/env node
import path from 'node:path';
import {
  buildCodegenExecRouteMatrix,
  loadTruthRoutesDoc,
  loadTruthTrace,
  normalizeTruthRoutes,
  readJson,
  writeJson,
  writeSummary,
} from './r2c-react-v3-route-matrix-shared.mjs';

function parseArgs(argv) {
  const out = {
    tsxAstPath: '',
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
    else if (arg === '--exec-snapshot') out.execSnapshot = String(argv[++i] || '');
    else if (arg === '--truth-routes-file') out.truthRoutesFile = String(argv[++i] || '');
    else if (arg === '--codegen-manifest') out.codegenManifest = String(argv[++i] || '');
    else if (arg === '--truth-trace') out.truthTrace = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-exec-route-matrix.mjs --tsx-ast <file> --exec-snapshot <file> --truth-routes-file <file> [--codegen-manifest <file>] [--truth-trace <file>] [--out <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.tsxAstPath) throw new Error('missing --tsx-ast');
  if (!out.execSnapshot) throw new Error('missing --exec-snapshot');
  if (!out.truthRoutesFile) throw new Error('missing --truth-routes-file');
  return out;
}

function main() {
  const args = parseArgs(process.argv);
  const tsxAstPath = path.resolve(args.tsxAstPath);
  const execSnapshotPath = path.resolve(args.execSnapshot);
  const truthRoutesPath = path.resolve(args.truthRoutesFile);
  const codegenManifestPath = args.codegenManifest ? path.resolve(args.codegenManifest) : '';
  const outPath = path.resolve(args.outPath || path.join(path.dirname(execSnapshotPath), 'cheng_codegen_exec_route_matrix_v1.json'));
  const tsxAstDoc = readJson(tsxAstPath);
  if (String(tsxAstDoc.format || '') !== 'tsx_ast_v1') {
    throw new Error('unsupported tsx ast format');
  }
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
  const modules = Array.isArray(tsxAstDoc.modules) ? tsxAstDoc.modules : [];
  const matrix = buildCodegenExecRouteMatrix(
    execSnapshot,
    execSnapshotPath,
    routeIds,
    runnerMode,
    modules,
    truthDoc,
    String(tsxAstDoc.repo_root || ''),
  );
  matrix.outPath = outPath;
  writeJson(outPath, matrix);
  const ok = Number(matrix.unsupportedCount || 0) === 0;
  writeSummary(args.summaryOut, {
    route_matrix_ok: ok,
    route_matrix_reason: ok ? 'ok' : 'unsupported_exec_routes',
    route_matrix_path: outPath,
    runner_mode: String(matrix.runnerMode || ''),
    entry_route_state: String(matrix.entryRouteState || ''),
    route_count: Number(matrix.routeCount || 0),
    supported_count: Number(matrix.supportedCount || 0),
    unsupported_count: Number(matrix.unsupportedCount || 0),
  });
  console.log(JSON.stringify({
    ok: true,
    out_path: outPath,
    runner_mode: String(matrix.runnerMode || ''),
    route_count: Number(matrix.routeCount || 0),
    supported_count: Number(matrix.supportedCount || 0),
    unsupported_count: Number(matrix.unsupportedCount || 0),
  }, null, 2));
}

main();
