#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

function parseArgs(argv) {
  const out = {
    inspectorState: '',
    out: '',
    summaryOut: '',
    openSource: false,
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--inspector-state') out.inspectorState = String(argv[++i] || '');
    else if (arg === '--out') out.out = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--open-source') out.openSource = true;
    else if (arg === '--help' || arg === '-h') {
      console.log('Usage: r2c-react-v3-replay-hit-inspector.mjs --inspector-state <file> [--out <file>] [--summary-out <file>] [--open-source]');
      process.exit(0);
    }
  }
  if (!out.inspectorState) throw new Error('missing --inspector-state');
  return out;
}

function writeText(filePath, text) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, text, 'utf8');
}

function writeJson(filePath, value) {
  writeText(filePath, `${JSON.stringify(value, null, 2)}\n`);
}

function writeSummary(filePath, values) {
  if (!filePath) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`);
  writeText(filePath, `${lines.join('\n')}\n`);
}

function run(bin, argv, options = {}) {
  return spawnSync(bin, argv, {
    encoding: 'utf8',
    timeout: options.timeout ?? 120000,
    maxBuffer: 16 * 1024 * 1024,
    env: options.env || process.env,
  });
}

function shellQuote(text) {
  return `'${String(text).replaceAll("'", "'\\''")}'`;
}

function replayOpenSource(stateDoc) {
  const sourceJump = stateDoc?.source_jump || {};
  let targetPath = String(sourceJump.target_path || '').trim();
  let targetLine = Number(sourceJump.target_line || 0) || 0;
  if (!targetPath) {
    const repoRoot = String(stateDoc?.repo_root || '').trim();
    const modulePath = String(stateDoc?.hit?.source_module_path || '').trim();
    if (repoRoot && modulePath) {
      targetPath = path.resolve(repoRoot, modulePath);
    }
  }
  if (targetLine <= 0) {
    targetLine = Number(stateDoc?.hit?.source_line || 0) || 1;
  }
  const response = {
    requested: false,
    ok: false,
    target_path: targetPath,
    target_line: targetLine,
    command: '',
    error: '',
  };
  if (!targetPath) {
    response.error = 'missing_target_path';
    return response;
  }
  if (!fs.existsSync(targetPath)) {
    response.error = `source_file_missing:${targetPath}`;
    return response;
  }
  response.requested = true;
  const args = ['--background', '--line', String(targetLine), targetPath];
  response.command = `/usr/bin/xed ${args.map(shellQuote).join(' ')}`;
  const result = run('/usr/bin/xed', args);
  if (result.status === 0) {
    response.ok = true;
    return response;
  }
  const stderr = String(result.stderr || '').trim();
  const stdout = String(result.stdout || '').trim();
  response.error = stderr || stdout || `xed_exit_${String(result.status ?? 'unknown')}`;
  return response;
}

function main() {
  const args = parseArgs(process.argv);
  const inspectorStatePath = path.resolve(args.inspectorState);
  const outDir = path.dirname(inspectorStatePath);
  const outPath = path.resolve(args.out || path.join(outDir, 'native_gui_inspector_replay_v1.json'));
  const summaryPath = path.resolve(args.summaryOut || path.join(outDir, 'native_gui_inspector_replay.summary.env'));
  const stateDoc = JSON.parse(fs.readFileSync(inspectorStatePath, 'utf8'));
  if (String(stateDoc?.format || '') !== 'native_gui_inspector_state_v1') {
    throw new Error(`native_gui_inspector_state_format_mismatch:${String(stateDoc?.format || '')}`);
  }

  const replaySourceOpen = args.openSource ? replayOpenSource(stateDoc) : {
    requested: false,
    ok: false,
    target_path: String(stateDoc?.source_jump?.target_path || ''),
    target_line: Number(stateDoc?.source_jump?.target_line || 0),
    command: '',
    error: '',
  };

  const replayDoc = {
    format: 'native_gui_inspector_replay_v1',
    ok: Boolean(stateDoc.ready),
    reason: stateDoc.ready ? 'ok' : String(stateDoc.reason || 'inspector_not_ready'),
    inspector_state_path: inspectorStatePath,
    repo_root: String(stateDoc.repo_root || ''),
    route_state: String(stateDoc.window?.route_state || ''),
    entry_module: String(stateDoc.window?.entry_module || ''),
    hit_item_id: String(stateDoc.hit?.item_id || ''),
    hit_source_module_path: String(stateDoc.hit?.source_module_path || ''),
    hit_source_component_name: String(stateDoc.hit?.source_component_name || ''),
    hit_source_line: Number(stateDoc.hit?.source_line || 0),
    panel_title: String(stateDoc.panel?.title || ''),
    panel_line_count: Array.isArray(stateDoc.panel?.lines) ? stateDoc.panel.lines.length : 0,
    panel_lines: Array.isArray(stateDoc.panel?.lines) ? stateDoc.panel.lines.map((line) => String(line || '')) : [],
    source_jump: {
      enabled: Boolean(stateDoc.source_jump?.enabled),
      attempted: Boolean(stateDoc.source_jump?.attempted),
      ok: Boolean(stateDoc.source_jump?.ok),
      target_path: String(stateDoc.source_jump?.target_path || ''),
      target_line: Number(stateDoc.source_jump?.target_line || 0),
      command: String(stateDoc.source_jump?.command || ''),
      error: String(stateDoc.source_jump?.error || ''),
    },
    replay_source_open: replaySourceOpen,
  };

  writeJson(outPath, replayDoc);
  writeSummary(summaryPath, {
    native_gui_inspector_state_path: inspectorStatePath,
    native_gui_inspector_state_ready: Boolean(stateDoc.ready),
    native_gui_inspector_replay_path: outPath,
    native_gui_inspector_replay_ready: Boolean(replayDoc.ok),
    native_gui_inspector_replay_panel_title: replayDoc.panel_title,
    native_gui_inspector_replay_panel_line_count: replayDoc.panel_line_count,
    native_gui_replay_source_open_requested: replaySourceOpen.requested,
    native_gui_replay_source_open_ok: replaySourceOpen.ok,
    native_gui_replay_source_open_target_path: replaySourceOpen.target_path,
    native_gui_replay_source_open_target_line: replaySourceOpen.target_line,
    native_gui_replay_source_open_command: replaySourceOpen.command,
    native_gui_replay_source_open_error: replaySourceOpen.error,
    native_gui_hit_item_id: replayDoc.hit_item_id,
    native_gui_hit_source_module_path: replayDoc.hit_source_module_path,
    native_gui_hit_source_component_name: replayDoc.hit_source_component_name,
    native_gui_hit_source_line: replayDoc.hit_source_line,
  });

  console.log(JSON.stringify(replayDoc, null, 2));
}

main();
