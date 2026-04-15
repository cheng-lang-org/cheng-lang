#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';

function parseArgs(argv) {
  const out = {
    execSnapshot: '',
    truthTrace: '',
    truthState: '',
    outPath: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--exec-snapshot') out.execSnapshot = String(argv[++i] || '');
    else if (arg === '--truth-trace') out.truthTrace = String(argv[++i] || '');
    else if (arg === '--truth-state') out.truthState = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-truth-compare.mjs --exec-snapshot <file> --truth-trace <file> [--truth-state <state>] [--out <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.execSnapshot) throw new Error('missing --exec-snapshot');
  if (!out.truthTrace) throw new Error('missing --truth-trace');
  return out;
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
}

function writeJson(filePath, payload) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, JSON.stringify(payload, null, 2) + '\n', 'utf8');
}

function writeSummary(filePath, values) {
  if (!filePath) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`);
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${lines.join('\n')}\n`, 'utf8');
}

function buildTruthCompareZero() {
  return {
    format: 'truth_compare_v1',
    ok: false,
    reason: '',
    truth_trace_path: '',
    truth_trace_format: '',
    truth_state: '',
    matched_snapshot_index: -1,
    exec_snapshot_path: '',
    out_path: '',
    exec_route_state: '',
    exec_render_ready: false,
    exec_semantic_nodes_loaded: false,
    exec_semantic_nodes_count: 0,
    truth_route_state: '',
    truth_route_id: '',
    truth_render_ready: false,
    truth_semantic_nodes_loaded: false,
    truth_semantic_nodes_count: 0,
    missing_truth_fields: [],
    mismatches: [],
  };
}

function jsonFirst(doc, keys) {
  for (const key of keys) {
    if (Object.prototype.hasOwnProperty.call(doc, key)) return doc[key];
  }
  return undefined;
}

function coerceJsonBool(value) {
  if (typeof value === 'boolean') return value;
  if (typeof value === 'string') {
    const lowered = value.trim().toLowerCase();
    if (lowered === 'true') return true;
    if (lowered === 'false') return false;
  }
  return null;
}

function coerceJsonInt(value) {
  if (typeof value === 'number' && Number.isInteger(value)) return value;
  if (typeof value === 'string' && /^-?[0-9]+$/.test(value.trim())) return Number.parseInt(value.trim(), 10);
  return null;
}

function coerceJsonStr(value) {
  return typeof value === 'string' ? value : null;
}

function normalizeTruthSnapshot(snapshot) {
  const routeDoc = typeof jsonFirst(snapshot, ['route']) === 'object' && jsonFirst(snapshot, ['route']) !== null
    ? jsonFirst(snapshot, ['route'])
    : {};
  const stateId = coerceJsonStr(jsonFirst(snapshot, ['stateId', 'state_id'])) || '';
  const routeId = coerceJsonStr(jsonFirst(routeDoc, ['routeId', 'route_id'])) || '';
  const semanticNodesCount = coerceJsonInt(jsonFirst(snapshot, ['semanticNodesCount', 'semantic_nodes_count']));
  const renderReady = coerceJsonBool(jsonFirst(snapshot, ['renderReady', 'render_ready']));
  const semanticNodesLoaded = semanticNodesCount !== null ? semanticNodesCount > 0 : null;
  return {
    state_id: stateId,
    route_id: routeId,
    render_ready: renderReady,
    semantic_nodes_count: semanticNodesCount,
    semantic_nodes_loaded: semanticNodesLoaded,
  };
}

function loadTruthTrace(filePath) {
  const doc = readJson(filePath);
  const format = String(doc.format || '');
  if (format !== 'truth_trace_v2' && format !== 'r2c-truth-trace-v1') {
    throw new Error(`unsupported truth trace format: ${format}`);
  }
  if (!Array.isArray(doc.snapshots)) {
    throw new Error('truth trace missing snapshots[]');
  }
  return doc;
}

function compareExecSnapshotToTruthDoc(execSnapshot, execSnapshotPath, truthDoc, truthTracePath, truthState = '') {
  const compare = buildTruthCompareZero();
  compare.truth_trace_path = truthTracePath;
  compare.exec_snapshot_path = execSnapshotPath;
  compare.truth_state = truthState || String(execSnapshot.route_state || '');
  compare.truth_trace_format = String(truthDoc.format || '');
  const targetState = String(compare.truth_state);
  let matchedIndex = -1;
  let normalizedTruth = null;
  const snapshots = Array.isArray(truthDoc.snapshots) ? truthDoc.snapshots : [];
  for (let index = 0; index < snapshots.length; index += 1) {
    const rawSnapshot = snapshots[index];
    if (!rawSnapshot || typeof rawSnapshot !== 'object') continue;
    const current = normalizeTruthSnapshot(rawSnapshot);
    if (current.state_id === targetState || (!current.state_id && current.route_id === targetState)) {
      matchedIndex = index;
      normalizedTruth = current;
      break;
    }
  }
  if (!normalizedTruth) {
    compare.reason = 'missing_truth_state';
    return compare;
  }
  compare.matched_snapshot_index = matchedIndex;
  const execFields = {
    route_state: String(execSnapshot.route_state || ''),
    render_ready: Boolean(execSnapshot.render_ready),
    semantic_nodes_loaded: Boolean(execSnapshot.semantic_nodes_loaded),
    semantic_nodes_count: Number(execSnapshot.semantic_nodes_count || 0),
  };
  const truthFields = {
    route_state: String(normalizedTruth.state_id || ''),
    truth_route_id: String(normalizedTruth.route_id || ''),
    render_ready: normalizedTruth.render_ready,
    semantic_nodes_loaded: normalizedTruth.semantic_nodes_loaded,
    semantic_nodes_count: normalizedTruth.semantic_nodes_count,
  };
  compare.exec_route_state = execFields.route_state;
  compare.exec_render_ready = execFields.render_ready;
  compare.exec_semantic_nodes_loaded = execFields.semantic_nodes_loaded;
  compare.exec_semantic_nodes_count = execFields.semantic_nodes_count;
  compare.truth_route_state = truthFields.route_state;
  compare.truth_route_id = truthFields.truth_route_id;
  compare.truth_render_ready = truthFields.render_ready === null ? false : Boolean(truthFields.render_ready);
  compare.truth_semantic_nodes_loaded = truthFields.semantic_nodes_loaded === null ? false : Boolean(truthFields.semantic_nodes_loaded);
  compare.truth_semantic_nodes_count = truthFields.semantic_nodes_count === null ? 0 : Number(truthFields.semantic_nodes_count);
  const missingTruthFields = [];
  const mismatches = [];
  for (const field of ['route_state', 'render_ready', 'semantic_nodes_loaded', 'semantic_nodes_count']) {
    const truthValue = truthFields[field];
    const execValue = execFields[field];
    if (truthValue === null || truthValue === undefined || truthValue === '') {
      missingTruthFields.push(field);
      continue;
    }
    if (execValue !== truthValue) {
      mismatches.push({
        field,
        exec_value: JSON.stringify(execValue),
        truth_value: JSON.stringify(truthValue),
      });
    }
  }
  compare.missing_truth_fields = missingTruthFields;
  compare.mismatches = mismatches;
  if (missingTruthFields.length > 0) {
    compare.reason = 'missing_truth_fields';
    return compare;
  }
  if (mismatches.length > 0) {
    compare.reason = 'mismatch';
    return compare;
  }
  compare.ok = true;
  compare.reason = 'ok';
  return compare;
}

function main() {
  const args = parseArgs(process.argv);
  const execSnapshotPath = path.resolve(args.execSnapshot);
  const truthTracePath = path.resolve(args.truthTrace);
  const outPath = path.resolve(args.outPath || path.join(path.dirname(execSnapshotPath), 'truth_compare_v1.json'));
  const execSnapshot = readJson(execSnapshotPath);
  if (String(execSnapshot.format || '') !== 'cheng_codegen_exec_snapshot_v1') {
    throw new Error('unsupported exec snapshot format');
  }
  const truthDoc = loadTruthTrace(truthTracePath);
  const compare = compareExecSnapshotToTruthDoc(
    execSnapshot,
    execSnapshotPath,
    truthDoc,
    truthTracePath,
    args.truthState || String(execSnapshot.route_state || ''),
  );
  compare.out_path = outPath;
  writeJson(outPath, compare);
  writeSummary(args.summaryOut, {
    truth_compare_ok: Boolean(compare.ok),
    truth_compare_reason: String(compare.reason || ''),
    truth_compare_path: outPath,
    truth_state: String(compare.truth_state || ''),
    matched_snapshot_index: Number(compare.matched_snapshot_index ?? -1),
    mismatch_count: Array.isArray(compare.mismatches) ? compare.mismatches.length : 0,
    missing_truth_fields_count: Array.isArray(compare.missing_truth_fields) ? compare.missing_truth_fields.length : 0,
    exec_semantic_nodes_count: Number(compare.exec_semantic_nodes_count || 0),
    truth_semantic_nodes_count: Number(compare.truth_semantic_nodes_count || 0),
  });
  console.log(JSON.stringify({
    ok: Boolean(compare.ok),
    reason: String(compare.reason || ''),
    out_path: outPath,
    truth_state: String(compare.truth_state || ''),
    mismatch_count: Array.isArray(compare.mismatches) ? compare.mismatches.length : 0,
    missing_truth_fields: Array.isArray(compare.missing_truth_fields) ? compare.missing_truth_fields : [],
  }, null, 2));
  process.exit(Boolean(compare.ok) ? 0 : 2);
}

main();
