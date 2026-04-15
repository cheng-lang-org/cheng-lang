#!/usr/bin/env node
import path from 'node:path';
import {
  compareExecRouteMatrixToTruth,
  loadTruthTrace,
  readJson,
  writeJson,
  writeSummary,
} from './r2c-react-v3-route-matrix-shared.mjs';

function parseArgs(argv) {
  const out = {
    execRouteMatrix: '',
    truthTrace: '',
    outPath: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--exec-route-matrix') out.execRouteMatrix = String(argv[++i] || '');
    else if (arg === '--truth-trace') out.truthTrace = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-truth-compare-matrix.mjs --exec-route-matrix <file> --truth-trace <file> [--out <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.execRouteMatrix) throw new Error('missing --exec-route-matrix');
  if (!out.truthTrace) throw new Error('missing --truth-trace');
  return out;
}

function main() {
  const args = parseArgs(process.argv);
  const execRouteMatrixPath = path.resolve(args.execRouteMatrix);
  const truthTracePath = path.resolve(args.truthTrace);
  const outPath = path.resolve(args.outPath || path.join(path.dirname(execRouteMatrixPath), 'truth_compare_matrix_v1.json'));
  const execRouteMatrix = readJson(execRouteMatrixPath);
  if (String(execRouteMatrix.format || '') !== 'cheng_codegen_exec_route_matrix_v1') {
    throw new Error('unsupported exec route matrix format');
  }
  const truthDoc = loadTruthTrace(truthTracePath);
  const compare = compareExecRouteMatrixToTruth(execRouteMatrix, truthDoc, truthTracePath);
  compare.outPath = outPath;
  writeJson(outPath, compare);
  writeSummary(args.summaryOut, {
    truth_compare_matrix_ok: Boolean(compare.ok),
    truth_compare_matrix_reason: String(compare.reason || ''),
    truth_compare_matrix_path: outPath,
    route_count: Number(compare.routeCount || 0),
    ok_count: Number(compare.okCount || 0),
    unsupported_count: Number(compare.unsupportedCount || 0),
    mismatch_count: Number(compare.mismatchCount || 0),
    missing_truth_state_count: Number(compare.missingTruthStateCount || 0),
    missing_truth_field_count: Number(compare.missingTruthFieldCount || 0),
  });
  console.log(JSON.stringify({
    ok: Boolean(compare.ok),
    reason: String(compare.reason || ''),
    out_path: outPath,
    route_count: Number(compare.routeCount || 0),
    ok_count: Number(compare.okCount || 0),
    unsupported_count: Number(compare.unsupportedCount || 0),
    mismatch_count: Number(compare.mismatchCount || 0),
    missing_truth_state_count: Number(compare.missingTruthStateCount || 0),
  }, null, 2));
  process.exit(Boolean(compare.ok) ? 0 : 2);
}

main();
