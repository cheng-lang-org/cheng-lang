#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

function parseArgs(argv) {
  const out = {
    repo: '',
    outDir: '',
    bundlePath: '',
    sessionPath: '',
    summaryOut: '',
    autoCloseMs: '',
    screenshotOut: '',
    click: '',
    resize: '',
    scrollY: '',
    waitAfterClickMs: '',
    focusItemId: '',
    typeText: '',
    sendKey: '',
    routeState: '',
    hostSourcePath: '',
    hostExePath: '',
    inspectorStateOut: '',
    openSourceOnClick: false,
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--bundle-path') out.bundlePath = String(argv[++i] || '');
    else if (arg === '--session-path') out.sessionPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--auto-close-ms') out.autoCloseMs = String(argv[++i] || '');
    else if (arg === '--screenshot-out') out.screenshotOut = String(argv[++i] || '');
    else if (arg === '--click') out.click = String(argv[++i] || '');
    else if (arg === '--resize') out.resize = String(argv[++i] || '');
    else if (arg === '--scroll-y') out.scrollY = String(argv[++i] || '');
    else if (arg === '--wait-after-click-ms') out.waitAfterClickMs = String(argv[++i] || '');
    else if (arg === '--focus-item-id') out.focusItemId = String(argv[++i] || '');
    else if (arg === '--type-text') out.typeText = String(argv[++i] || '');
    else if (arg === '--send-key') out.sendKey = String(argv[++i] || '');
    else if (arg === '--route-state') out.routeState = String(argv[++i] || '');
    else if (arg === '--host-source') out.hostSourcePath = String(argv[++i] || '');
    else if (arg === '--host-exe') out.hostExePath = String(argv[++i] || '');
    else if (arg === '--inspector-state-out') out.inspectorStateOut = String(argv[++i] || '');
    else if (arg === '--open-source-on-click') out.openSourceOnClick = true;
    else if (arg === '--help' || arg === '-h') {
      console.log('Usage: r2c-react-v3-native-gui-run.mjs --repo <path> [--out-dir <dir>] [--bundle-path <file>] [--session-path <file>] [--summary-out <file>] [--auto-close-ms <n>] [--screenshot-out <file>] [--click x,y] [--resize WxH] [--scroll-y <n>] [--wait-after-click-ms <n>] [--focus-item-id <id>] [--type-text <text>] [--send-key <key>] [--route-state <id>] [--inspector-state-out <file>] [--open-source-on-click] [--host-source <file>] [--host-exe <file>]');
      process.exit(0);
    }
  }
  if (!out.repo) throw new Error('missing --repo');
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

function parseSummaryEnv(filePath) {
  const out = {};
  if (!filePath || !fs.existsSync(filePath)) return out;
  for (const line of fs.readFileSync(filePath, 'utf8').split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    const pivot = trimmed.indexOf('=');
    if (pivot <= 0) continue;
    out[trimmed.slice(0, pivot)] = trimmed.slice(pivot + 1);
  }
  return out;
}

function parseKeyValueStdout(stdoutText) {
  const out = {};
  for (const line of String(stdoutText || '').split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed) continue;
    const pivot = trimmed.indexOf('=');
    if (pivot <= 0) continue;
    out[trimmed.slice(0, pivot)] = trimmed.slice(pivot + 1);
  }
  return out;
}

function readJsonIfExists(filePath) {
  if (!filePath || !fs.existsSync(filePath)) return null;
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
}

function parseNumberField(fields, key, fallback = null) {
  const raw = String(fields?.[key] ?? '').trim();
  if (!raw) return fallback;
  const value = Number(raw);
  return Number.isFinite(value) ? value : fallback;
}

function parseJsonText(value, fallback) {
  const text = String(value ?? '').trim();
  if (!text) return fallback;
  try {
    return JSON.parse(text);
  } catch {
    return fallback;
  }
}

function resolveReplaySourceTarget(repo, summary) {
  const explicitPath = String(summary.native_gui_source_jump_target_path || '').trim();
  const explicitLine = Number(summary.native_gui_source_jump_target_line || 0) || Number(summary.native_gui_hit_source_line || 0) || 1;
  if (explicitPath) {
    return {
      targetPath: explicitPath,
      targetLine: explicitLine,
    };
  }
  const modulePath = String(summary.native_gui_hit_source_module_path || '').trim();
  if (!modulePath) {
    return {
      targetPath: '',
      targetLine: explicitLine,
    };
  }
  return {
    targetPath: path.resolve(repo, modulePath),
    targetLine: explicitLine,
  };
}

function cloneLayoutItem(item) {
  if (!item || typeof item !== 'object') return null;
  return {
    id: String(item.id || ''),
    source_node_id: String(item.source_node_id || ''),
    kind: String(item.kind || ''),
    x: Number(item.x || 0),
    y: Number(item.y || 0),
    width: Number(item.width || 0),
    height: Number(item.height || 0),
    text: String(item.text || ''),
    detail_text: String(item.detail_text || ''),
    z_index: Number(item.z_index || 0),
    plan_role: String(item.plan_role || ''),
    layer: String(item.layer || ''),
    column: Number(item.column || 0),
    column_span: Number(item.column_span || 0),
    interactive: Boolean(item.interactive),
    synthetic: Boolean(item.synthetic),
    visible_in_viewport: Boolean(item.visible_in_viewport),
    visual_role: String(item.visual_role || ''),
    density: String(item.density || ''),
    prominence: String(item.prominence || ''),
    accent_tone: String(item.accent_tone || ''),
    source_module_path: String(item.source_module_path || ''),
    source_component_name: String(item.source_component_name || ''),
    source_line: Number(item.source_line || 0),
  };
}

function cloneLayoutSurfaceNode(node) {
  if (!node || typeof node !== 'object') return null;
  return {
    id: String(node.id || ''),
    kind: String(node.kind || ''),
    label: String(node.label || ''),
    detail_text: String(node.detail_text || ''),
    tag: String(node.tag || ''),
    line: Number(node.line || 0),
    depth: Number(node.depth || 0),
    module_path: String(node.module_path || ''),
    component_name: String(node.component_name || ''),
    layout_role: String(node.layout_role || ''),
    style_traits: Array.isArray(node.style_traits) ? node.style_traits.map((item) => String(item || '')) : [],
    class_preview: String(node.class_preview || ''),
    class_tokens: Array.isArray(node.class_tokens) ? node.class_tokens.map((item) => String(item || '')) : [],
    attr_names: Array.isArray(node.attr_names) ? node.attr_names.map((item) => String(item || '')) : [],
  };
}

function cloneStyleLayoutNode(node) {
  if (!node || typeof node !== 'object') return null;
  return {
    node_id: String(node.node_id || ''),
    visual_role: String(node.visual_role || ''),
    density: String(node.density || ''),
    prominence: String(node.prominence || ''),
    accent_tone: String(node.accent_tone || ''),
    badge_text: String(node.badge_text || ''),
    row_height: Number(node.row_height || 0),
    detail_text: String(node.detail_text || ''),
    depth: Number(node.depth || 0),
    column_span_hint: Number(node.column_span_hint || 0),
    layer: String(node.layer || ''),
  };
}

function findByStringId(items, key, id) {
  const target = String(id || '').trim();
  if (!target || !Array.isArray(items)) return null;
  return items.find((item) => String(item?.[key] || '') === target) || null;
}

function buildHitInspectorDoc(sessionDoc, summary) {
  const nativeLayoutPlan = sessionDoc?.native_layout_plan || null;
  const layoutSurface = sessionDoc?.layout_surface || null;
  const styleLayoutSurface = sessionDoc?.style_layout_surface || null;
  const hitItemId = String(summary.native_gui_hit_item_id || '').trim();
  const hitSourceNodeId = String(summary.native_gui_hit_source_node_id || '').trim();
  const layoutItem = findByStringId(nativeLayoutPlan?.items, 'id', hitItemId);
  const sourceNodeId = hitSourceNodeId || String(layoutItem?.source_node_id || '').trim();
  const sourceNode = findByStringId(layoutSurface?.nodes, 'id', sourceNodeId);
  const styleNode = findByStringId(styleLayoutSurface?.nodes, 'node_id', sourceNodeId);
  const ready = Boolean(layoutItem);
  let reason = 'ok';
  if (!hitItemId) reason = 'no_hit_item';
  else if (!layoutItem) reason = 'layout_item_missing';
  return {
    format: 'native_gui_hit_inspector_v1',
    ready,
    reason,
    route_state: String(summary.native_gui_route_state || ''),
    entry_module: String(summary.native_gui_entry_module || ''),
    native_layout_plan_policy_source: String(summary.native_gui_native_layout_plan_policy_source || ''),
    scroll_offset_y: summary.native_gui_scroll_offset_y,
    click_count: Number(summary.native_gui_click_count || 0),
    last_click_x: summary.native_gui_last_click_x,
    last_click_y: summary.native_gui_last_click_y,
    hit_item_id: hitItemId,
    hit_source_node_id: sourceNodeId,
    hit_source_module_path: String(summary.native_gui_hit_source_module_path || ''),
    hit_source_component_name: String(summary.native_gui_hit_source_component_name || ''),
    hit_source_line: Number(summary.native_gui_hit_source_line || 0),
    hit_item_interactive: Boolean(summary.native_gui_hit_item_interactive),
    source_jump: {
      enabled: Boolean(summary.native_gui_source_jump_enabled),
      attempted: Boolean(summary.native_gui_source_jump_attempted),
      ok: Boolean(summary.native_gui_source_jump_ok),
      target_path: String(summary.native_gui_source_jump_target_path || ''),
      target_line: Number(summary.native_gui_source_jump_target_line || 0),
      command: String(summary.native_gui_source_jump_command || ''),
      error: String(summary.native_gui_source_jump_error || ''),
    },
    layout_item: cloneLayoutItem(layoutItem),
    source_node: cloneLayoutSurfaceNode(sourceNode),
    style_node: cloneStyleLayoutNode(styleNode),
  };
}

function buildInspectorStateDoc(repo, bundlePath, sessionPath, screenshotPath, hitInspectorPath, sessionDoc, summary) {
  const replayTarget = resolveReplaySourceTarget(repo, summary);
  return {
    format: 'native_gui_inspector_state_v1',
    ready: Boolean(summary.native_gui_inspector_ready),
    reason: summary.native_gui_inspector_ready ? 'ok' : (summary.native_gui_hit_inspector_ready ? 'panel_missing' : 'hit_missing'),
    repo_root: repo,
    bundle_path: bundlePath,
    session_path: sessionPath,
    screenshot_path: screenshotPath,
    hit_inspector_path: hitInspectorPath,
    window: {
      title: String(summary.native_gui_window_title || ''),
      width: Number(summary.native_gui_window_width || 0),
      height: Number(summary.native_gui_window_height || 0),
      route_state: String(summary.native_gui_route_state || ''),
      entry_module: String(summary.native_gui_entry_module || ''),
      scroll_offset_y: summary.native_gui_scroll_offset_y,
      click_count: Number(summary.native_gui_click_count || 0),
      resize_count: Number(summary.native_gui_resize_count || 0),
      scroll_count: Number(summary.native_gui_scroll_count || 0),
      last_click_x: summary.native_gui_last_click_x,
      last_click_y: summary.native_gui_last_click_y,
    },
    hit: {
      item_id: String(summary.native_gui_hit_item_id || ''),
      source_node_id: String(summary.native_gui_hit_source_node_id || ''),
      source_module_path: String(summary.native_gui_hit_source_module_path || ''),
      source_component_name: String(summary.native_gui_hit_source_component_name || ''),
      source_line: Number(summary.native_gui_hit_source_line || 0),
      item_interactive: Boolean(summary.native_gui_hit_item_interactive),
    },
    panel: {
      ready: Boolean(summary.native_gui_inspector_ready),
      title: String(summary.native_gui_inspector_title || ''),
      frame: {
        x: Number(summary.native_gui_inspector_panel_x || 0),
        y: Number(summary.native_gui_inspector_panel_y || 0),
        width: Number(summary.native_gui_inspector_panel_width || 0),
        height: Number(summary.native_gui_inspector_panel_height || 0),
      },
      lines: Array.isArray(summary.native_gui_inspector_lines) ? summary.native_gui_inspector_lines.map((line) => String(line || '')) : [],
    },
    source_jump: {
      enabled: Boolean(summary.native_gui_source_jump_enabled),
      attempted: Boolean(summary.native_gui_source_jump_attempted),
      ok: Boolean(summary.native_gui_source_jump_ok),
      target_path: String(replayTarget.targetPath || ''),
      target_line: Number(replayTarget.targetLine || 0),
      command: String(summary.native_gui_source_jump_command || ''),
      error: String(summary.native_gui_source_jump_error || ''),
    },
    hit_inspector: readJsonIfExists(hitInspectorPath),
    session_preview_mode: String(sessionDoc?.preview_mode || ''),
  };
}

function run(bin, argv, options = {}) {
  return spawnSync(bin, argv, {
    encoding: 'utf8',
    timeout: options.timeout ?? 240000,
    maxBuffer: 64 * 1024 * 1024,
    env: options.env || process.env,
  });
}

function exitCodeOf(result) {
  if (typeof result.status === 'number') return result.status;
  if (result.signal) return -1;
  if (result.error) return -1;
  return -1;
}

function findWorkspaceRoot(scriptPath) {
  let current = path.dirname(scriptPath);
  while (true) {
    if (fs.existsSync(path.join(current, 'v3', 'experimental', 'r2c-react-v3'))) return current;
    const parent = path.dirname(current);
    if (parent === current) break;
    current = parent;
  }
  throw new Error(`failed to resolve workspace root from ${scriptPath}`);
}

function resolveDefaultToolingBin(workspaceRoot) {
  const explicit = String(process.env.R2C_REACT_V3_TOOLING_BIN || '').trim();
  if (explicit) return explicit;
  const stage3 = path.join(workspaceRoot, 'artifacts', 'v3_bootstrap', 'cheng.stage3');
  if (fs.existsSync(stage3)) return stage3;
  return path.join(workspaceRoot, 'artifacts', 'v3_backend_driver', 'cheng');
}

function resolveClang() {
  const xcrun = spawnSync('xcrun', ['--sdk', 'macosx', '--find', 'clang'], { encoding: 'utf8' });
  if (xcrun.status === 0) {
    const out = String(xcrun.stdout || '').trim();
    if (out) return { bin: 'xcrun', prefix: ['--sdk', 'macosx', 'clang'] };
  }
  return { bin: 'clang', prefix: [] };
}

function sessionMatchesRoute(sessionPath, targetRouteState) {
  if (!targetRouteState || !fs.existsSync(sessionPath)) return true;
  try {
    const sessionDoc = JSON.parse(fs.readFileSync(sessionPath, 'utf8'));
    return String(sessionDoc?.window?.route_state || '') === String(targetRouteState);
  } catch {
    return false;
  }
}

function ensureSession(repo, outDir, bundlePath, sessionPath, bundleSummaryPath, toolPath, routeState) {
  const targetRouteState = String(routeState || '').trim();
  if (fs.existsSync(sessionPath) && fs.existsSync(bundlePath) && sessionMatchesRoute(sessionPath, targetRouteState)) return;
  const result = run(toolPath, [
    'r2c-react-v3',
    'native-gui-bundle',
    '--repo', repo,
    '--out-dir', outDir,
    ...(targetRouteState ? ['--route-state', targetRouteState] : []),
  ]);
  const bundleLogPath = path.join(outDir, 'native_gui_run.bundle.log');
  writeText(bundleLogPath, [result.stdout || '', result.stderr || '', result.error ? String(result.error.stack || result.error.message || result.error) : ''].filter(Boolean).join('\n'));
  if (exitCodeOf(result) !== 0) {
    throw new Error(`native_gui_bundle_helper_failed:${exitCodeOf(result)}`);
  }
  const bundleSummary = parseSummaryEnv(bundleSummaryPath);
  if (bundleSummary.native_gui_bundle_path) bundlePath = String(bundleSummary.native_gui_bundle_path);
  if (bundleSummary.native_gui_session_path) sessionPath = String(bundleSummary.native_gui_session_path);
  if (!fs.existsSync(sessionPath)) {
    throw new Error(`native_gui_session_missing:${sessionPath}`);
  }
}

function main() {
  if (process.platform !== 'darwin') {
    throw new Error(`native_gui_run_platform_unsupported:${process.platform}`);
  }
  const args = parseArgs(process.argv);
  const repo = path.resolve(args.repo);
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_v3_cheng'));
  const workspaceRoot = findWorkspaceRoot(path.resolve(process.argv[1]));
  const toolPath = resolveDefaultToolingBin(workspaceRoot);
  const hostSourcePath = path.resolve(args.hostSourcePath || path.join(workspaceRoot, 'v3', 'experimental', 'r2c-react-v3', 'native_gui_host_macos.m'));
  const bundlePath = path.resolve(args.bundlePath || path.join(outDir, 'native_gui_bundle_v1.json'));
  const bundleSummaryPath = path.join(outDir, 'native_gui_bundle.summary.env');
  const sessionPath = path.resolve(args.sessionPath || path.join(outDir, 'native_gui_session_v1.json'));
  const summaryPath = path.resolve(args.summaryOut || path.join(outDir, 'native_gui_run.summary.env'));
  const screenshotPath = path.resolve(args.screenshotOut || path.join(outDir, 'native_gui_session.png'));
  const hitInspectorPath = path.resolve(path.join(outDir, 'native_gui_hit_inspector_v1.json'));
  const inspectorStatePath = path.resolve(args.inspectorStateOut || path.join(outDir, 'native_gui_inspector_state_v1.json'));
  const renderPlanPath = path.resolve(path.join(outDir, 'native_render_plan_runtime_v1.json'));
  const runtimeStatePath = path.resolve(path.join(outDir, 'native_gui_runtime_state_runtime_v1.json'));
  const hostExePath = path.resolve(args.hostExePath || path.join(outDir, 'native_gui_host_macos'));
  const compileLogPath = path.join(outDir, 'native_gui_host_macos.compile.log');
  const runLogPath = path.join(outDir, 'native_gui_host_macos.run.log');
  const autoCloseMs = String(args.autoCloseMs || '1200').trim() || '1200';

  ensureSession(repo, outDir, bundlePath, sessionPath, bundleSummaryPath, toolPath, args.routeState);
  const sessionDoc = JSON.parse(fs.readFileSync(sessionPath, 'utf8'));
  if (String(sessionDoc?.format || '') !== 'native_gui_session_v1') {
    throw new Error(`unexpected native gui session format: ${String(sessionDoc?.format || '')}`);
  }
  const clang = resolveClang();
  fs.rmSync(hostExePath, { force: true });
  const compile = run(clang.bin, [
    ...clang.prefix,
    '-fobjc-arc',
    '-fmodules',
    '-framework', 'Cocoa',
    hostSourcePath,
    '-o', hostExePath,
  ]);
  writeText(compileLogPath, [compile.stdout || '', compile.stderr || '', compile.error ? String(compile.error.stack || compile.error.message || compile.error) : ''].filter(Boolean).join('\n'));
  const compileReturnCode = exitCodeOf(compile);
  if (compileReturnCode !== 0) {
    throw new Error(`native_gui_host_compile_failed:${compileReturnCode}`);
  }

  const runResult = run(hostExePath, [
    '--session', sessionPath,
    '--repo-root', repo,
    '--auto-close-ms', autoCloseMs,
    '--screenshot-out', screenshotPath,
    '--render-plan-out', renderPlanPath,
    '--runtime-state-out', runtimeStatePath,
    ...(args.click ? ['--click', args.click] : []),
    ...(args.resize ? ['--resize', args.resize] : []),
    ...(args.scrollY ? ['--scroll-y', args.scrollY] : []),
    ...(args.waitAfterClickMs ? ['--wait-after-click-ms', args.waitAfterClickMs] : []),
    ...(args.focusItemId ? ['--focus-item-id', args.focusItemId] : []),
    ...(args.typeText ? ['--type-text', args.typeText] : []),
    ...(args.sendKey ? ['--send-key', args.sendKey] : []),
    ...(args.openSourceOnClick ? ['--open-source-on-click'] : []),
  ], { timeout: Math.max(30000, Number.parseInt(autoCloseMs, 10) + 15000) });
  writeText(runLogPath, [runResult.stdout || '', runResult.stderr || '', runResult.error ? String(runResult.error.stack || runResult.error.message || runResult.error) : ''].filter(Boolean).join('\n'));
  const runReturnCode = exitCodeOf(runResult);
  if (runReturnCode !== 0) {
    throw new Error(`native_gui_host_run_failed:${runReturnCode}`);
  }

  const fields = parseKeyValueStdout(runResult.stdout || '');
  if (String(fields.format || '') !== 'native_gui_run_v1') {
    throw new Error(`native_gui_run_format_mismatch:${String(fields.format || '')}`);
  }
  const screenshotExists = fs.existsSync(screenshotPath);
  const runtimeStateDoc = readJsonIfExists(runtimeStatePath);
  const bundleSummary = parseSummaryEnv(bundleSummaryPath);
  const bundleDoc = readJsonIfExists(bundlePath);
  const bundleLayoutSurface = String(bundleDoc?.format || '') === 'native_gui_bundle_v1'
    ? bundleDoc.layout_surface || null
    : null;
  const bundleNativeLayoutPlan = String(bundleDoc?.format || '') === 'native_gui_bundle_v1'
    ? bundleDoc.native_layout_plan || null
    : null;
  const runtimeReady = String(fields.runtime_ready || '') === 'true';
  const renderPlanReady = String(fields.render_plan_ready || '') === 'true';
  const summary = {
    native_gui_bundle_path: bundlePath,
    native_gui_session_path: sessionPath,
    native_gui_host_source_path: hostSourcePath,
    native_gui_host_exe_path: hostExePath,
    native_gui_host_compile_log_path: compileLogPath,
    native_gui_host_run_log_path: runLogPath,
    native_gui_host_compile_ok: compileReturnCode === 0,
    native_gui_host_run_ok: runReturnCode === 0,
    native_gui_host_compile_returncode: compileReturnCode,
    native_gui_host_run_returncode: runReturnCode,
    native_gui_host_ready: true,
    native_gui_renderer_ready: true,
    native_gui_session_preview_ready: bundleSummary.native_gui_session_preview_ready || 'true',
    native_gui_session_preview_mode: bundleSummary.native_gui_session_preview_mode || String(sessionDoc.preview_mode || ''),
    native_gui_layout_surface_path: bundleSummary.native_gui_layout_surface_path || String(bundleLayoutSurface?.path || ''),
    native_gui_layout_surface_ready: bundleSummary.native_gui_layout_surface_ready || String(Boolean(bundleLayoutSurface?.ready)),
    native_gui_layout_surface_node_count: bundleSummary.native_gui_layout_surface_node_count || String(Number(bundleLayoutSurface?.node_count || 0)),
    native_gui_layout_surface_source_module: bundleSummary.native_gui_layout_surface_source_module || String(bundleLayoutSurface?.source_module_path || ''),
    native_gui_layout_surface_source_component: bundleSummary.native_gui_layout_surface_source_component || String(bundleLayoutSurface?.source_component_name || ''),
    native_gui_layout_surface_strategy: bundleSummary.native_gui_layout_surface_strategy || String(bundleLayoutSurface?.source_strategy || ''),
    native_gui_layout_surface_component_expansion_count: bundleSummary.native_gui_layout_surface_component_expansion_count || '',
    native_gui_layout_surface_parsed_module_count: bundleSummary.native_gui_layout_surface_parsed_module_count || '',
    native_gui_layout_surface_styled_node_count: bundleSummary.native_gui_layout_surface_styled_node_count || '',
    native_gui_layout_surface_interactive_node_count: bundleSummary.native_gui_layout_surface_interactive_node_count || '',
    native_gui_style_layout_surface_path: bundleSummary.native_gui_style_layout_surface_path || '',
    native_gui_style_layout_surface_ready: bundleSummary.native_gui_style_layout_surface_ready || '',
    native_gui_style_layout_surface_node_count: bundleSummary.native_gui_style_layout_surface_node_count || '',
    native_gui_style_layout_overlay_node_count: bundleSummary.native_gui_style_layout_overlay_node_count || '',
    native_gui_style_layout_surface_surface_node_count: bundleSummary.native_gui_style_layout_surface_surface_node_count || '',
    native_gui_style_layout_surface_control_node_count: bundleSummary.native_gui_style_layout_surface_control_node_count || '',
    native_gui_native_layout_plan_path: bundleSummary.native_gui_native_layout_plan_path || String(bundleNativeLayoutPlan?.path || ''),
    native_gui_native_layout_plan_ready: bundleSummary.native_gui_native_layout_plan_ready || String(Boolean(bundleNativeLayoutPlan?.ready)),
    native_gui_native_layout_plan_policy_source: bundleSummary.native_gui_native_layout_plan_policy_source || String(bundleNativeLayoutPlan?.layout_policy_source || ''),
    native_gui_native_layout_plan_item_count: bundleSummary.native_gui_native_layout_plan_item_count || String(Number(bundleNativeLayoutPlan?.item_count || 0)),
    native_gui_native_layout_plan_viewport_item_count: bundleSummary.native_gui_native_layout_plan_viewport_item_count || String(Number(bundleNativeLayoutPlan?.viewport_item_count || 0)),
    native_gui_native_layout_plan_clipped_item_count: bundleSummary.native_gui_native_layout_plan_clipped_item_count || String(Number(bundleNativeLayoutPlan?.clipped_item_count || 0)),
    native_gui_native_layout_plan_interactive_item_count: bundleSummary.native_gui_native_layout_plan_interactive_item_count || String(Number(bundleNativeLayoutPlan?.interactive_item_count || 0)),
    native_gui_native_layout_plan_source_backed_item_count: bundleSummary.native_gui_native_layout_plan_source_backed_item_count || String(Number(bundleNativeLayoutPlan?.source_backed_item_count || 0)),
    native_gui_native_layout_plan_scroll_height: bundleSummary.native_gui_native_layout_plan_scroll_height || String(Number(bundleNativeLayoutPlan?.scroll_height || 0)),
    native_gui_initial_render_plan_path: bundleSummary.native_gui_render_plan_path || '',
    native_gui_initial_render_plan_ready: bundleSummary.native_gui_render_plan_ready || '',
    native_gui_initial_render_plan_command_count: bundleSummary.native_gui_render_plan_command_count || '0',
    native_gui_initial_render_plan_visible_layout_item_count: bundleSummary.native_gui_render_plan_visible_layout_item_count || '0',
    native_gui_render_plan_path: renderPlanPath,
    native_gui_render_plan_ready: renderPlanReady && fs.existsSync(renderPlanPath),
    native_gui_render_plan_command_count: Number.parseInt(String(fields.render_plan_command_count || '0'), 10) || 0,
    native_gui_render_plan_visible_layout_item_count: Number.parseInt(String(fields.visible_layout_item_count || fields.layout_item_count || '0'), 10) || 0,
    native_gui_runtime_path: bundleSummary.native_gui_runtime_path || '',
    native_gui_runtime_mode: bundleSummary.native_gui_runtime_mode || '',
    native_gui_runtime_ready: runtimeReady,
    native_gui_runtime_error: String(fields.runtime_error || ''),
    native_gui_runtime_exe_path: bundleSummary.native_gui_runtime_exe_path || '',
    native_gui_runtime_compiled_exe_path: bundleSummary.native_gui_runtime_compiled_exe_path || '',
    native_gui_runtime_contract_path: bundleSummary.native_gui_runtime_contract_path || '',
    native_gui_route_state_requested: String(args.routeState || ''),
    native_gui_host_abi_feature_hits: String(runtimeStateDoc?.host_abi_feature_hits_csv || bundleSummary.native_gui_host_abi_feature_hits || ''),
    native_gui_host_abi_first_batch_ready: Boolean(runtimeStateDoc?.host_abi_first_batch_ready ?? (String(bundleSummary.native_gui_host_abi_first_batch_ready || '') === 'true')),
    native_gui_host_abi_first_batch_features: bundleSummary.native_gui_host_abi_first_batch_features || '',
    native_gui_host_abi_first_batch_missing_features: String(runtimeStateDoc?.host_abi_missing_features_csv || bundleSummary.native_gui_host_abi_first_batch_missing_features || ''),
    native_gui_storage_host_ready: Boolean(runtimeStateDoc?.storage_host_ready),
    native_gui_fetch_host_ready: Boolean(runtimeStateDoc?.fetch_host_ready),
    native_gui_custom_event_host_ready: Boolean(runtimeStateDoc?.custom_event_host_ready),
    native_gui_resize_observer_host_ready: Boolean(runtimeStateDoc?.resize_observer_host_ready),
    native_gui_initial_runtime_state_path: bundleSummary.native_gui_runtime_state_path || '',
    native_gui_runtime_state_path: runtimeStatePath,
    native_gui_controller_tool_path: toolPath,
    native_gui_window_opened: String(fields.window_opened || '') === 'true',
    native_gui_window_title: String(fields.window_title || ''),
    native_gui_route_state: String(fields.route_state || ''),
    native_gui_entry_module: String(fields.entry_module || ''),
    native_gui_layout_item_count: Number.parseInt(String(fields.layout_item_count || '0'), 10) || 0,
    native_gui_visible_layout_item_count: Number.parseInt(String(fields.visible_layout_item_count || fields.layout_item_count || '0'), 10) || 0,
    native_gui_click_count: Number.parseInt(String(fields.click_count || '0'), 10) || 0,
    native_gui_resize_count: Number.parseInt(String(fields.resize_count || '0'), 10) || 0,
    native_gui_scroll_count: Number.parseInt(String(fields.scroll_count || '0'), 10) || 0,
    native_gui_focus_count: Number.parseInt(String(fields.focus_count || '0'), 10) || 0,
    native_gui_key_count: Number.parseInt(String(fields.key_count || '0'), 10) || 0,
    native_gui_text_count: Number.parseInt(String(fields.text_count || '0'), 10) || 0,
    native_gui_last_click_x: parseNumberField(fields, 'last_click_x'),
    native_gui_last_click_y: parseNumberField(fields, 'last_click_y'),
    native_gui_scroll_offset_y: parseNumberField(fields, 'scroll_offset_y'),
    native_gui_focused_item_id: String(fields.focused_item_id || ''),
    native_gui_typed_text: String(fields.typed_text || ''),
    native_gui_last_key: String(fields.last_key || ''),
    native_gui_hit_item_id: String(fields.hit_item_id || ''),
    native_gui_hit_source_node_id: String(fields.hit_source_node_id || ''),
    native_gui_hit_source_module_path: String(fields.hit_source_module_path || ''),
    native_gui_hit_source_component_name: String(fields.hit_source_component_name || ''),
    native_gui_hit_source_line: Number.parseInt(String(fields.hit_source_line || '0'), 10) || 0,
    native_gui_hit_item_interactive: String(fields.hit_item_interactive || '') === 'true',
    native_gui_inspector_ready: String(fields.inspector_ready || '') === 'true',
    native_gui_inspector_title: String(fields.inspector_title || ''),
    native_gui_inspector_panel_x: parseNumberField(fields, 'inspector_panel_x', 0),
    native_gui_inspector_panel_y: parseNumberField(fields, 'inspector_panel_y', 0),
    native_gui_inspector_panel_width: parseNumberField(fields, 'inspector_panel_width', 0),
    native_gui_inspector_panel_height: parseNumberField(fields, 'inspector_panel_height', 0),
    native_gui_inspector_lines: parseJsonText(fields.inspector_lines_json, []),
    native_gui_source_jump_enabled: String(fields.source_jump_enabled || '') === 'true',
    native_gui_source_jump_attempted: String(fields.source_jump_attempted || '') === 'true',
    native_gui_source_jump_ok: String(fields.source_jump_ok || '') === 'true',
    native_gui_source_jump_target_path: String(fields.source_jump_target_path || ''),
    native_gui_source_jump_target_line: Number.parseInt(String(fields.source_jump_target_line || '0'), 10) || 0,
    native_gui_source_jump_command: String(fields.source_jump_command || ''),
    native_gui_source_jump_error: String(fields.source_jump_error || ''),
    native_gui_window_width: parseNumberField(fields, 'window_width'),
    native_gui_window_height: parseNumberField(fields, 'window_height'),
    native_gui_screenshot_path: screenshotPath,
    native_gui_screenshot_written: String(fields.screenshot_written || '') === 'true' && screenshotExists,
  };
  const hitInspectorDoc = buildHitInspectorDoc(sessionDoc, summary);
  writeJson(hitInspectorPath, hitInspectorDoc);
  summary.native_gui_hit_inspector_path = hitInspectorPath;
  summary.native_gui_hit_inspector_ready = Boolean(hitInspectorDoc.ready);
  const inspectorStateDoc = buildInspectorStateDoc(repo, bundlePath, sessionPath, screenshotPath, hitInspectorPath, sessionDoc, summary);
  writeJson(inspectorStatePath, inspectorStateDoc);
  summary.native_gui_inspector_state_path = inspectorStatePath;
  summary.native_gui_inspector_state_ready = Boolean(inspectorStateDoc.ready);
  writeSummary(summaryPath, summary);
  if (!summary.native_gui_runtime_ready) {
    throw new Error(`native_gui_runtime_unready:${summary.native_gui_runtime_error || 'unknown'}`);
  }
  if (!summary.native_gui_render_plan_ready) {
    throw new Error('native_gui_render_plan_unready');
  }
  if (args.openSourceOnClick && summary.native_gui_click_count > 0 && !summary.native_gui_source_jump_ok) {
    throw new Error(`native_gui_source_jump_failed:${summary.native_gui_source_jump_error || 'unknown'}`);
  }
  console.log(JSON.stringify({
    format: 'native_gui_run_report_v1',
    ok: true,
    reason: 'ok',
    bundle_path: bundlePath,
    session_path: sessionPath,
    host_exe_path: hostExePath,
    screenshot_path: screenshotPath,
    window_opened: summary.native_gui_window_opened,
    screenshot_written: summary.native_gui_screenshot_written,
    layout_item_count: summary.native_gui_layout_item_count,
    layout_surface_path: summary.native_gui_layout_surface_path,
    layout_surface_node_count: Number(summary.native_gui_layout_surface_node_count || 0),
    layout_surface_source_module: summary.native_gui_layout_surface_source_module,
    layout_surface_source_component: summary.native_gui_layout_surface_source_component,
    layout_surface_strategy: summary.native_gui_layout_surface_strategy,
    native_layout_plan_path: summary.native_gui_native_layout_plan_path,
    native_layout_plan_policy_source: summary.native_gui_native_layout_plan_policy_source,
    native_layout_plan_item_count: Number(summary.native_gui_native_layout_plan_item_count || 0),
    native_layout_plan_viewport_item_count: Number(summary.native_gui_native_layout_plan_viewport_item_count || 0),
    native_layout_plan_clipped_item_count: Number(summary.native_gui_native_layout_plan_clipped_item_count || 0),
    native_layout_plan_interactive_item_count: Number(summary.native_gui_native_layout_plan_interactive_item_count || 0),
    native_layout_plan_source_backed_item_count: Number(summary.native_gui_native_layout_plan_source_backed_item_count || 0),
    native_layout_plan_scroll_height: Number(summary.native_gui_native_layout_plan_scroll_height || 0),
    initial_render_plan_path: summary.native_gui_initial_render_plan_path,
    render_plan_path: summary.native_gui_render_plan_path,
    render_plan_ready: summary.native_gui_render_plan_ready,
    render_plan_command_count: summary.native_gui_render_plan_command_count,
    runtime_path: summary.native_gui_runtime_path,
    runtime_mode: summary.native_gui_runtime_mode,
    runtime_state_path: summary.native_gui_runtime_state_path,
    runtime_ready: summary.native_gui_runtime_ready,
    runtime_error: summary.native_gui_runtime_error,
    runtime_exe_path: summary.native_gui_runtime_exe_path,
    runtime_compiled_exe_path: summary.native_gui_runtime_compiled_exe_path,
    runtime_contract_path: summary.native_gui_runtime_contract_path,
    route_state_requested: summary.native_gui_route_state_requested,
    host_abi_first_batch_ready: summary.native_gui_host_abi_first_batch_ready,
    host_abi_first_batch_features: String(summary.native_gui_host_abi_first_batch_features || '').split(',').filter(Boolean),
    host_abi_first_batch_missing_features: String(summary.native_gui_host_abi_first_batch_missing_features || '').split(',').filter(Boolean),
    storage_host_ready: summary.native_gui_storage_host_ready,
    fetch_host_ready: summary.native_gui_fetch_host_ready,
    custom_event_host_ready: summary.native_gui_custom_event_host_ready,
    resize_observer_host_ready: summary.native_gui_resize_observer_host_ready,
    controller_tool_path: summary.native_gui_controller_tool_path,
    click_count: summary.native_gui_click_count,
    resize_count: summary.native_gui_resize_count,
    scroll_count: summary.native_gui_scroll_count,
    focus_count: summary.native_gui_focus_count,
    key_count: summary.native_gui_key_count,
    text_count: summary.native_gui_text_count,
    last_click_x: summary.native_gui_last_click_x,
    last_click_y: summary.native_gui_last_click_y,
    scroll_offset_y: summary.native_gui_scroll_offset_y,
    visible_layout_item_count: summary.native_gui_visible_layout_item_count,
    focused_item_id: summary.native_gui_focused_item_id,
    typed_text: summary.native_gui_typed_text,
    last_key: summary.native_gui_last_key,
    hit_item_id: summary.native_gui_hit_item_id,
    hit_source_node_id: summary.native_gui_hit_source_node_id,
    hit_source_module_path: summary.native_gui_hit_source_module_path,
    hit_source_component_name: summary.native_gui_hit_source_component_name,
    hit_source_line: summary.native_gui_hit_source_line,
    hit_item_interactive: summary.native_gui_hit_item_interactive,
    hit_inspector_path: summary.native_gui_hit_inspector_path,
    hit_inspector_ready: summary.native_gui_hit_inspector_ready,
    inspector_state_path: summary.native_gui_inspector_state_path,
    inspector_state_ready: summary.native_gui_inspector_state_ready,
    source_jump_enabled: summary.native_gui_source_jump_enabled,
    source_jump_attempted: summary.native_gui_source_jump_attempted,
    source_jump_ok: summary.native_gui_source_jump_ok,
    source_jump_target_path: summary.native_gui_source_jump_target_path,
    source_jump_target_line: summary.native_gui_source_jump_target_line,
    source_jump_command: summary.native_gui_source_jump_command,
    source_jump_error: summary.native_gui_source_jump_error,
    window_width: summary.native_gui_window_width,
    window_height: summary.native_gui_window_height,
  }, null, 2));
}

main();
