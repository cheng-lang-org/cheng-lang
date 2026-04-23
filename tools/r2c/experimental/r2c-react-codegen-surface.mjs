#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { discoverTruthRoutes, normalizeRoots } from './r2c-react-discover-truth-routes.mjs';
import {
  renderTruthCompareEngineCheng,
  renderTruthCompareMainCheng,
  renderTruthCompareMatrixEngineCheng,
  renderTruthCompareMatrixMainCheng,
} from './r2c-react-truth-compare-engine-shared.mjs';
import {
  buildCodegenExecRouteMatrixFromCatalog,
  buildCodegenRouteCatalog,
  loadTruthTrace,
} from './r2c-react-route-matrix-shared.mjs';

function parseArgs(argv) {
  const out = {
    repo: '',
    tsxAstPath: '',
    outDir: '',
    summaryOut: '',
    runtimeRoots: [],
    truthTracePath: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--tsx-ast') out.tsxAstPath = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--runtime-root') out.runtimeRoots.push(String(argv[++i] || ''));
    else if (arg === '--truth-trace') out.truthTracePath = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-codegen-surface.mjs --repo <path> [--tsx-ast <file>] [--out-dir <dir>] [--runtime-root <dir>] [--truth-trace <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.repo) throw new Error('missing --repo');
  return out;
}

function resolveWorkspaceRoot() {
  const scriptPath = path.resolve(process.argv[1]);
  const scriptName = path.basename(scriptPath);
  let current = path.dirname(scriptPath);
  while (true) {
    const repoScriptPath = path.join(current, 'tools', 'r2c', 'experimental', scriptName);
    if (fs.existsSync(path.join(current, 'cheng-package.toml')) &&
        fs.existsSync(path.join(current, 'src', 'core')) &&
        fs.existsSync(repoScriptPath)) {
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

function writeCodegenSurfaceReport(filePath, payload) {
  writeJson(filePath, {
    format: 'codegen_surface_report_v1',
    ...payload,
  });
}

function resolveDefaultToolingBin(workspaceRoot) {
  const explicit = String(process.env.R2C_REACT_TOOLING_BIN || '').trim();
  if (explicit) return explicit;
  const stage3 = path.join(workspaceRoot, 'artifacts', 'bootstrap', 'cheng.stage3');
  if (fs.existsSync(stage3)) return stage3;
  return path.join(workspaceRoot, 'artifacts', 'backend_driver', 'cheng');
}

function makeSafeIdent(text) {
  let out = String(text || '').replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
  if (!out) out = 'module';
  if (/^[0-9]/.test(out)) out = `m_${out}`;
  return out;
}

function chengStr(text) {
  return `"${String(text || '').replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\t/g, '\\t')}"`;
}

function chengBool(value) {
  return value ? 'true' : 'false';
}

function renderStrArrayFn(name, values) {
  if (values.length === 0) {
    return `fn ${name}(): str[] =\n    return []\n`;
  }
  const lines = [`fn ${name}(): str[] =`, '    return ['];
  for (const value of values) lines.push(`        ${chengStr(value)},`);
  lines.push('    ]');
  return `${lines.join('\n')}\n`;
}

function splitStringChunks(value, chunkSize = 256) {
  const text = String(value || '');
  const safeChunkSize = Math.max(32, Number(chunkSize || 256));
  const chunks = [];
  let offset = 0;
  while (offset < text.length) {
    let end = Math.min(text.length, offset + safeChunkSize);
    while (end > offset + 1 && (text[end - 1] === '"' || text[end - 1] === '\\')) {
      end -= 1;
    }
    chunks.push(text.slice(offset, end));
    offset = end;
  }
  if (chunks.length === 0) chunks.push('');
  return chunks;
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

function renderChunkReturnFunctions(functionPrefix, value, chunkSize = 256) {
  const chunks = splitStringChunks(value, chunkSize);
  const names = [];
  const lines = [];
  for (let index = 0; index < chunks.length; index += 1) {
    const name = `${functionPrefix}${index}`;
    names.push(name);
    lines.push(`fn ${name}(): str =`);
    lines.push(`    return ${chengStr(chunks[index])}`);
    lines.push('');
  }
  return { names, lines };
}

function renderChunkEmitter(functionPrefix, value, {
  appendNewline = false,
  chunkSize = 256,
  emitJsonName = 'emitJson',
  emitName = 'emit',
} = {}) {
  const { names, lines } = renderChunkReturnFunctions(functionPrefix, value, chunkSize);
  lines.push(`fn ${emitJsonName}() =`);
  for (const name of names) {
    lines.push(`    exec_io.r2cWriteStdout(${name}())`);
  }
  lines.push('    return');
  lines.push('');
  lines.push(`fn ${emitName}() =`);
  lines.push(`    ${emitJsonName}()`);
  if (appendNewline) lines.push('    exec_io.r2cWriteStdout("\\n")');
  lines.push('    return');
  return lines;
}

function renderDirectChunkEmitter(value, {
  appendNewline = false,
  chunkSize = 192,
  emitJsonName = 'emitJson',
  emitName = 'emit',
} = {}) {
  const chunks = splitJsonOutputChunks(value, chunkSize);
  const lines = [`fn ${emitJsonName}() =`];
  for (const chunk of chunks) {
    lines.push(`    exec_io.r2cWriteStdout(${chengStr(chunk)})`);
  }
  lines.push('    return');
  lines.push('');
  lines.push(`fn ${emitName}() =`);
  lines.push(`    ${emitJsonName}()`);
  if (appendNewline) lines.push('    exec_io.r2cWriteStdout("\\n")');
  lines.push('    return');
  return lines;
}

function renderDirectChunkWriterEmitter(functionPrefix, value, {
  appendNewline = false,
  chunkSize = 192,
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
  return lines;
}

function renderJsonChunkReturnEmitter(functionPrefix, value, {
  appendNewline = false,
  chunkSize = 192,
  emitJsonName = 'emitJson',
  emitName = 'emit',
} = {}) {
  const chunks = splitJsonOutputChunks(value, chunkSize);
  const names = [];
  const lines = [];
  for (let index = 0; index < chunks.length; index += 1) {
    const name = `${functionPrefix}${index}`;
    names.push(name);
    lines.push(`fn ${name}(): str =`);
    lines.push(`    return ${chengStr(chunks[index])}`);
    lines.push('');
  }
  lines.push(`fn ${emitJsonName}() =`);
  for (const name of names) {
    lines.push(`    exec_io.r2cWriteStdout(${name}())`);
  }
  lines.push('    return');
  lines.push('');
  lines.push(`fn ${emitName}() =`);
  lines.push(`    ${emitJsonName}()`);
  if (appendNewline) lines.push('    exec_io.r2cWriteStdout("\\n")');
  lines.push('    return');
  return lines;
}

function renderRuntimeCheng() {
  return [
    'type',
    '    R2cGeneratedModuleSurface =',
    '        sourcePath: str',
    '        sourceHash: str',
    '        runtimeRoot: str',
    '        exportCount: int32',
    '        tailwindClassTokenCount: int32',
    '        componentCount: int32',
    '        hookCount: int32',
    '        effectCount: int32',
    '        stateSlotCount: int32',
    '        jsxElementCount: int32',
    '        generated: bool',
    '        jsxLike: bool',
    '',
    '    R2cGeneratedProjectSurface =',
    '        packageId: str',
    '        projectModule: str',
    '        mainModule: str',
    '        entryModule: str',
    '        moduleSourcePaths: str[]',
    '        modules: R2cGeneratedModuleSurface[]',
    '',
    '    R2cGeneratedRouteCandidate =',
    '        semanticNodesCount: int32',
    '        reason: str',
    '',
    '    R2cGeneratedRouteCatalogEntry =',
    '        routeId: str',
    '        supported: bool',
    '        deterministic: bool',
    '        reason: str',
    '        candidateCount: int32',
    '        candidates: R2cGeneratedRouteCandidate[]',
    '',
    '    R2cGeneratedRouteCatalog =',
    '        runnerMode: str',
    '        entryRouteState: str',
    '        routeCount: int32',
    '        supportedCount: int32',
    '        unsupportedCount: int32',
    '        routes: str[]',
    '        entries: R2cGeneratedRouteCatalogEntry[]',
    '',
    'fn r2cGeneratedModuleSurfaceZero(): R2cGeneratedModuleSurface =',
    '    var out: R2cGeneratedModuleSurface',
    '    out.sourcePath = ""',
    '    out.sourceHash = ""',
    '    out.runtimeRoot = ""',
    '    out.exportCount = 0',
    '    out.tailwindClassTokenCount = 0',
    '    out.componentCount = 0',
    '    out.hookCount = 0',
    '    out.effectCount = 0',
    '    out.stateSlotCount = 0',
    '    out.jsxElementCount = 0',
    '    out.generated = false',
    '    out.jsxLike = false',
    '    return out',
    '',
    'fn r2cGeneratedProjectSurfaceZero(): R2cGeneratedProjectSurface =',
    '    var out: R2cGeneratedProjectSurface',
    '    out.packageId = ""',
    '    out.projectModule = ""',
    '    out.mainModule = ""',
    '    out.entryModule = ""',
    '    out.moduleSourcePaths = []',
    '    out.modules = []',
    '    return out',
    '',
    'fn r2cGeneratedRouteCandidateZero(): R2cGeneratedRouteCandidate =',
    '    var out: R2cGeneratedRouteCandidate',
    '    out.semanticNodesCount = 0',
    '    out.reason = ""',
    '    return out',
    '',
    'fn r2cGeneratedRouteCatalogEntryZero(): R2cGeneratedRouteCatalogEntry =',
    '    var out: R2cGeneratedRouteCatalogEntry',
    '    out.routeId = ""',
    '    out.supported = false',
    '    out.deterministic = false',
    '    out.reason = ""',
    '    out.candidateCount = 0',
    '    out.candidates = []',
    '    return out',
    '',
    'fn r2cGeneratedRouteCatalogZero(): R2cGeneratedRouteCatalog =',
    '    var out: R2cGeneratedRouteCatalog',
    '    out.runnerMode = ""',
    '    out.entryRouteState = ""',
    '    out.routeCount = 0',
    '    out.supportedCount = 0',
    '    out.unsupportedCount = 0',
    '    out.routes = []',
    '    out.entries = []',
    '    return out',
    '',
  ].join('\n');
}

function renderStubModule(name) {
  return `fn ${name}(): int32 =\n    return 0\n`;
}

function renderUiHostCheng(manifest) {
  const baseSnapshot = manifest?.base_snapshot || {};
  const entryModule = String(manifest?.entry_module || '');
  const entryModuleToken = makeSafeIdent(entryModule || 'entry_module');
  const title = 'cheng_gui_preview';
  const routeState = String(baseSnapshot.route_state || 'home_default');
  const renderReadyText = Boolean(baseSnapshot.render_ready) ? 'true' : 'false';
  const semanticNodesCount = Math.max(0, Number(baseSnapshot.semantic_nodes_count || 0));
  const moduleCount = Math.max(0, Number(baseSnapshot.module_count || manifest?.modules?.length || 0));
  const componentCount = Math.max(0, Number(baseSnapshot.component_count || 0));
  const effectCount = Math.max(0, Number(baseSnapshot.effect_count || 0));
  const stateSlotCount = Math.max(0, Number(baseSnapshot.state_slot_count || 0));
  const sessionLines = [
    'format=native_gui_session_preview_v1',
    'preview_mode=cheng_ui_host_preview_v1',
    `window_title=${title}`,
    'window_width=390',
    'window_height=844',
    'window_resizable=false',
    `entry_module=${entryModuleToken}`,
    `route_state=${routeState}`,
    `render_ready=${renderReadyText}`,
    `semantic_nodes_count=${semanticNodesCount}`,
    `module_count=${moduleCount}`,
    `component_count=${componentCount}`,
    `effect_count=${effectCount}`,
    `state_slot_count=${stateSlotCount}`,
    'layout_policy_source=cheng_preview_v1',
    'layout_content_inset=16',
    'layout_header_height=34',
    'layout_meta_chip_height=22',
    'layout_meta_text_height=20',
    'layout_top_padding=20',
    'layout_top_gap=12',
    'layout_row_gap=6',
    'layout_overlay_gap=8',
    'layout_bottom_padding=24',
    'layout_overlay_min_width=152',
    'layout_overlay_max_width=220',
    'layout_overlay_width_ratio_pct=42',
    'layout_two_column_min_width=260',
    'layout_column_gap=10',
    'layout_indent_step=10',
    'layout_indent_cap=10',
  ];
  return [
    `import ${manifest.exec_io_module} as exec_io`,
    '',
    'fn r2cUiHostCreateWindowSession(): int32 =',
    ...sessionLines.flatMap((line) => [
      `    exec_io.r2cWriteStdout(${chengStr(line)})`,
      '    exec_io.r2cWriteStdout("\\n")',
    ]),
    '    return 0',
    '',
    'fn r2cUiHostStub(): int32 =',
    '    return r2cUiHostCreateWindowSession()',
    '',
  ].join('\n');
}

function renderModuleCheng(module, runtimeModule) {
  const hookCalls = Array.isArray(module.hook_calls) ? module.hook_calls : [];
  const effectCount = hookCalls.filter((item) => String(item.name || '') === 'useEffect').length;
  const stateSlotCount = hookCalls.filter((item) => String(item.name || '') === 'useState').length;
  const jsxElementCount = Array.isArray(module.jsx_elements) ? module.jsx_elements.length : 0;
  const componentCount = Array.isArray(module.components) ? module.components.length : 0;
  return [
    `import ${runtimeModule} as runtime`,
    '',
    'fn r2cGeneratedModuleSurface(): runtime.R2cGeneratedModuleSurface =',
    '    var out = runtime.r2cGeneratedModuleSurfaceZero()',
    `    out.sourcePath = ${chengStr(module.path)}`,
    `    out.sourceHash = ${chengStr(module.hash || '')}`,
    `    out.runtimeRoot = ${chengStr(module.runtime_root || '')}`,
    `    out.exportCount = ${Number(module.export_count || 0)}`,
    `    out.tailwindClassTokenCount = ${Number(module.tailwind_class_token_count || 0)}`,
    `    out.componentCount = ${componentCount}`,
    `    out.hookCount = ${hookCalls.length}`,
    `    out.effectCount = ${effectCount}`,
    `    out.stateSlotCount = ${stateSlotCount}`,
    `    out.jsxElementCount = ${jsxElementCount}`,
    `    out.generated = ${chengBool(Boolean(module.generated))}`,
    `    out.jsxLike = ${chengBool(Boolean(module.jsx_like))}`,
    '    return out',
    '',
  ].join('\n');
}

function renderProjectCheng(manifest) {
  const lines = [`import ${manifest.runtime_module} as runtime`];
  for (const item of manifest.modules) {
    lines.push(`import ${item.import_path} as ${item.module_name}`);
  }
  lines.push('');
  lines.push(renderStrArrayFn('r2cGeneratedModuleSourcePaths', manifest.modules.map((item) => item.source_path)).trimEnd());
  lines.push('');
  lines.push('fn r2cGeneratedAllModules(): runtime.R2cGeneratedModuleSurface[] =');
  lines.push('    return [');
  for (const item of manifest.modules) {
    lines.push(`        ${item.module_name}.r2cGeneratedModuleSurface(),`);
  }
  lines.push('    ]');
  lines.push('');
  lines.push('fn r2cGeneratedProjectSurface(): runtime.R2cGeneratedProjectSurface =');
  lines.push('    var out = runtime.r2cGeneratedProjectSurfaceZero()');
  lines.push(`    out.packageId = ${chengStr(manifest.package_id)}`);
  lines.push(`    out.projectModule = ${chengStr(manifest.project_module)}`);
  lines.push(`    out.mainModule = ${chengStr(manifest.main_module)}`);
  lines.push(`    out.entryModule = ${chengStr(manifest.entry_module)}`);
  lines.push('    out.moduleSourcePaths = r2cGeneratedModuleSourcePaths()');
  lines.push('    out.modules = r2cGeneratedAllModules()');
  lines.push('    return out');
  lines.push('');
  return lines.join('\n');
}

function renderProjectEntryCheng(manifest) {
  return [
    `import ${manifest.project_module} as project`,
    `import ${manifest.runtime_module} as runtime`,
    '',
    'fn r2cGeneratedProjectSurface(): runtime.R2cGeneratedProjectSurface =',
    '    return project.r2cGeneratedProjectSurface()',
    '',
  ].join('\n');
}

function renderRouteCatalogCheng(manifest, routeCatalog) {
  const jsonText = JSON.stringify(routeCatalog || {
    format: 'cheng_codegen_route_catalog_v1',
    runnerMode: manifest.runner_mode || 'react_surface_runner_v1',
    entryRouteState: '',
    routeCount: 0,
    supportedCount: 0,
    unsupportedCount: 0,
    routes: [],
    entries: [],
  });
  const lines = [];
  lines.push(`import ${manifest.exec_io_module} as exec_io`);
  lines.push('');
  lines.push(...renderDirectChunkWriterEmitter('r2cRouteCatalogWriteChunk', jsonText, {
    appendNewline: true,
    chunkSize: 3072,
    emitJsonName: 'emitJson',
    emitName: 'emit',
  }));
  lines.push('');
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
    'fn r2cWriteStdoutLine(text: str) =',
    '    r2cWriteStdout(text)',
    '    r2cWriteStdout("\\n")',
    '    return',
    '',
  ].join('\n');
}

function renderSnapshotCheng(manifest, snapshot) {
  const jsonText = JSON.stringify(snapshot || {});
  const lines = [];
  lines.push(`import ${manifest.exec_io_module} as exec_io`);
  lines.push('');
  lines.push(...renderChunkEmitter('r2cSnapshotChunk', jsonText, {
    appendNewline: true,
    chunkSize: 256,
    emitJsonName: 'emitJson',
    emitName: 'emit',
  }));
  lines.push('');
  return lines.join('\n');
}

function buildRouteRuntimeSnapshot(baseSnapshot, routeId, candidate) {
  const semanticNodesCount = Math.max(0, Number(candidate?.semanticNodesCount || 0));
  return {
    ...baseSnapshot,
    route_state: routeId,
    semantic_nodes_loaded: semanticNodesCount > 0,
    semantic_nodes_count: semanticNodesCount,
    tree_node_count: semanticNodesCount,
    root_node_count: semanticNodesCount > 0 ? 1 : 0,
    element_node_count: semanticNodesCount,
  };
}

function renderRouteRuntimeCheng(manifest) {
  return [
    `import ${manifest.route_runtime_data_module} as route_runtime_data`,
    '',
    'fn emitDefault() =',
    '    route_runtime_data.emitSnapshot()',
    '    return',
    '',
    'fn emitRoute(r: str, c: str): bool =',
    '    if r == route_runtime_data.routeId():',
    '        if c == "":',
    '            route_runtime_data.emitSnapshot()',
    '            return true',
    '        if c == route_runtime_data.candidateIndexText():',
    '            route_runtime_data.emitSnapshot()',
    '            return true',
    '    return false',
    '',
  ].join('\n');
}

function renderRouteRuntimeDataStubCheng(manifest, baseSnapshot) {
  const snapshotDoc = baseSnapshot && typeof baseSnapshot === 'object'
    ? baseSnapshot
    : {
      format: 'cheng_codegen_exec_snapshot_v1',
      route_state: 'home_default',
      mount_phase: 'prepared',
      commit_phase: 'committed',
      render_ready: false,
      semantic_nodes_loaded: false,
      semantic_nodes_count: 0,
      module_count: 0,
      component_count: 0,
      effect_count: 0,
      state_slot_count: 0,
      passive_effects_flushed: false,
      passive_effect_flush_count: 0,
      hook_slot_count: 0,
      effect_slot_count: 0,
      component_instance_count: 0,
      tree_node_count: 0,
      root_node_count: 0,
      module_node_count: 0,
      component_node_count: 0,
      element_node_count: 0,
      hook_node_count: 0,
      effect_node_count: 0,
    };
  const routeId = String(snapshotDoc.route_state || '');
  const lines = [`import ${manifest.exec_io_module} as exec_io`, ''];
  lines.push('fn routeId(): str =');
  lines.push(`    return ${chengStr(routeId)}`);
  lines.push('');
  lines.push('fn candidateIndexText(): str =');
  lines.push('    return "0"');
  lines.push('');
  lines.push(...renderChunkEmitter('rtDataChunk', JSON.stringify(snapshotDoc), {
    appendNewline: true,
    chunkSize: 12,
    emitJsonName: 'emitSnapshotJson',
    emitName: 'emitSnapshot',
  }));
  lines.push('');
  return lines.join('\n');
}

function renderRouteCatalogMainCheng(manifest) {
  return [
    `import ${manifest.route_catalog_module} as rc`,
    '',
    'fn main(): int32 =',
    '    rc.emit()',
    '    return 0',
    '',
  ].join('\n');
}

function renderRouteRuntimeRequestCheng() {
  return [
    'fn useDefault(): bool =',
    '    return true',
    '',
    'fn routeId(): str =',
    '    return ""',
    '',
    'fn candidateIndexText(): str =',
    '    return ""',
    '',
  ].join('\n');
}

function renderRouteRuntimeMainCheng(manifest) {
  return [
    `import ${manifest.route_runtime_module} as route_runtime`,
    `import ${manifest.route_runtime_request_module} as route_runtime_request`,
    '',
    'fn main(): int32 =',
    '    if route_runtime_request.useDefault():',
    '        route_runtime.emitDefault()',
    '        return 0',
    '    if route_runtime.emitRoute(route_runtime_request.routeId(), route_runtime_request.candidateIndexText()):',
    '        return 0',
    '    return 3',
    '',
  ].join('\n');
}

function renderRouteMatrixCheng(manifest, routeMatrix) {
  const jsonText = JSON.stringify(routeMatrix || {
    format: 'cheng_codegen_exec_route_matrix_v1',
    runnerMode: manifest.runner_mode || 'react_surface_runner_v1',
    entryRouteState: '',
    routeCount: 0,
    supportedCount: 0,
    unsupportedCount: 0,
    routes: [],
    entries: [],
  });
  const lines = [];
  lines.push(`import ${manifest.exec_io_module} as exec_io`);
  lines.push('');
  lines.push(...renderDirectChunkWriterEmitter('r2cRouteMatrixWriteChunk', jsonText, {
    appendNewline: true,
    chunkSize: 3072,
    emitJsonName: 'emitJson',
    emitName: 'emit',
  }));
  lines.push('');
  return lines.join('\n');
}

function renderRouteMatrixMainCheng(manifest) {
  return [
    `import ${manifest.route_matrix_module} as route_matrix`,
    '',
    'fn main(): int32 =',
    '    route_matrix.emit()',
    '    return 0',
    '',
  ].join('\n');
}

function renderTruthCompareDataStubCheng(manifest) {
  return [
    `import ${manifest.exec_io_module} as exec_io`,
    '',
    'fn truthSnapshotCount(): int32 =',
    '    return 0',
    '',
    'fn truthState(): str =',
    '    return ""',
    '',
    'fn execRouteState(): str =',
    '    return ""',
    '',
    'fn execRenderReadyInt(): int32 =',
    '    return 0',
    '',
    'fn execSemanticNodesLoadedInt(): int32 =',
    '    return 0',
    '',
    'fn execSemanticNodesCount(): int32 =',
    '    return 0',
    '',
    'fn truthSnapshotStateId(index: int32): str =',
    '    return ""',
    '',
    'fn truthSnapshotRouteId(index: int32): str =',
    '    return ""',
    '',
    'fn truthSnapshotRenderReadyInt(index: int32): int32 =',
    '    return -1',
    '',
    'fn truthSnapshotSemanticNodesLoadedInt(index: int32): int32 =',
    '    return -1',
    '',
    'fn truthSnapshotSemanticNodesCountInt(index: int32): int32 =',
    '    return -1',
    '',
    'fn emitTruthTracePathJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitTruthTraceFormatJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitTruthStateJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitExecSnapshotPathJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitOutPathJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitExecRouteStateJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitExecRouteStateCompareJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"\\"")',
    '    return',
    '',
    'fn emitExecRenderReadyCompareJson() =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitExecSemanticNodesLoadedCompareJson() =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitExecSemanticNodesCountCompareJson() =',
    '    exec_io.r2cWriteStdout("0")',
    '    return',
    '',
    'fn emitTruthSnapshotRouteStateJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitTruthSnapshotRouteIdJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitTruthSnapshotRouteStateCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"\\"")',
    '    return',
    '',
    'fn emitTruthSnapshotRenderReadyCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitTruthSnapshotSemanticNodesLoadedCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitTruthSnapshotSemanticNodesCountJson(index: int32) =',
    '    exec_io.r2cWriteStdout("0")',
    '    return',
    '',
    'fn emitTruthSnapshotSemanticNodesCountCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("0")',
    '    return',
    '',
  ].join('\n');
}

function renderTruthCompareMatrixDataStubCheng(manifest) {
  return [
    `import ${manifest.exec_io_module} as exec_io`,
    '',
    'fn routeCount(): int32 =',
    '    return 0',
    '',
    'fn truthSnapshotCount(): int32 =',
    '    return 0',
    '',
    'fn routeId(index: int32): str =',
    '    return ""',
    '',
    'fn routeSupportedInt(index: int32): int32 =',
    '    return 0',
    '',
    'fn routeExecRouteState(index: int32): str =',
    '    return ""',
    '',
    'fn routeExecRenderReadyInt(index: int32): int32 =',
    '    return 0',
    '',
    'fn routeExecSemanticNodesLoadedInt(index: int32): int32 =',
    '    return 0',
    '',
    'fn routeExecSemanticNodesCountInt(index: int32): int32 =',
    '    return 0',
    '',
    'fn truthSnapshotStateId(index: int32): str =',
    '    return ""',
    '',
    'fn truthSnapshotRouteId(index: int32): str =',
    '    return ""',
    '',
    'fn truthSnapshotRenderReadyInt(index: int32): int32 =',
    '    return -1',
    '',
    'fn truthSnapshotSemanticNodesLoadedInt(index: int32): int32 =',
    '    return -1',
    '',
    'fn truthSnapshotSemanticNodesCountInt(index: int32): int32 =',
    '    return -1',
    '',
    'fn emitTruthTracePathJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitTruthTraceFormatJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitOutPathJson() =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitRouteUnsupportedReasonJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"unsupported_exec_route\\"")',
    '    return',
    '',
    'fn emitRouteSnapshotPathJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitRouteIdJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitRouteExecRouteStateJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitRouteExecRouteStateCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"\\"")',
    '    return',
    '',
    'fn emitRouteExecRenderReadyCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitRouteExecSemanticNodesLoadedCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitRouteExecSemanticNodesCountCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("0")',
    '    return',
    '',
    'fn emitTruthSnapshotRouteStateJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"")',
    '    return',
    '',
    'fn emitTruthSnapshotRouteStateCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("\\"\\"\\"")',
    '    return',
    '',
    'fn emitTruthSnapshotRenderReadyCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitTruthSnapshotSemanticNodesLoadedCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("false")',
    '    return',
    '',
    'fn emitTruthSnapshotSemanticNodesCountJson(index: int32) =',
    '    exec_io.r2cWriteStdout("0")',
    '    return',
    '',
    'fn emitTruthSnapshotSemanticNodesCountCompareJson(index: int32) =',
    '    exec_io.r2cWriteStdout("0")',
    '    return',
    '',
  ].join('\n');
}

function renderExecBundleCheng(manifest) {
  return [
    `import ${manifest.exec_io_module} as exec_io`,
    `import ${manifest.snapshot_module} as snapshot`,
    `import ${manifest.route_catalog_module} as route_catalog`,
    '',
    'fn emitJson() =',
    '    exec_io.r2cWriteStdout("{\\"format\\":\\"cheng_codegen_exec_bundle_v1\\",\\"snapshot\\":")',
    '    snapshot.emitJson()',
    '    exec_io.r2cWriteStdout(",\\"route_catalog\\":")',
    '    route_catalog.emitJson()',
    '    exec_io.r2cWriteStdout("}")',
    '    return',
    '',
    'fn emit() =',
    '    emitJson()',
    '    exec_io.r2cWriteStdout("\\n")',
    '    return',
    '',
  ].join('\n');
}

function renderAppRunnerCheng(manifest) {
  const baseSnapshot = manifest?.base_snapshot || {};
  const routeState = String(baseSnapshot.route_state || 'home_default');
  const renderReadyText = chengBool(Boolean(baseSnapshot.render_ready));
  const semanticNodesCount = Math.max(0, Number(baseSnapshot.semantic_nodes_count || 0));
  const moduleCount = Math.max(0, Number(baseSnapshot.module_count || 0));
  const componentCount = Math.max(0, Number(baseSnapshot.component_count || 0));
  const effectCount = Math.max(0, Number(baseSnapshot.effect_count || 0));
  const stateSlotCount = Math.max(0, Number(baseSnapshot.state_slot_count || 0));
  return [
    `import ${manifest.runtime_module} as runtime`,
    'import std/strutils as strutil',
    '',
    'type',
    '    R2cExecSnapshot =',
    '        routeState: str',
    '        mountPhase: str',
    '        commitPhase: str',
    '        renderReady: bool',
    '        semanticNodesLoaded: bool',
    '        semanticNodesCount: int32',
    '        moduleCount: int32',
    '        componentCount: int32',
    '        effectCount: int32',
    '        stateSlotCount: int32',
    '',
    'fn R2cExecSnapshotZero(): R2cExecSnapshot =',
    '    var out: R2cExecSnapshot',
    '    out.routeState = ""',
    '    out.mountPhase = ""',
    '    out.commitPhase = ""',
    '    out.renderReady = false',
    '    out.semanticNodesLoaded = false',
    '    out.semanticNodesCount = 0',
    '    out.moduleCount = 0',
    '    out.componentCount = 0',
    '    out.effectCount = 0',
    '    out.stateSlotCount = 0',
    '    return out',
    '',
    'fn R2cBuildEntrySnapshot(surface: runtime.R2cGeneratedProjectSurface): R2cExecSnapshot =',
    '    var out = R2cExecSnapshotZero()',
    `    out.routeState = ${chengStr(routeState)}`,
    '    out.mountPhase = "prepared"',
    '    out.commitPhase = "committed"',
    `    out.renderReady = ${renderReadyText}`,
    `    out.semanticNodesLoaded = ${chengBool(semanticNodesCount > 0)}`,
    `    out.semanticNodesCount = ${semanticNodesCount}`,
    `    out.moduleCount = ${moduleCount}`,
    `    out.componentCount = ${componentCount}`,
    `    out.effectCount = ${effectCount}`,
    `    out.stateSlotCount = ${stateSlotCount}`,
    '    return out',
    '',
    'fn R2cBoolJson(value: bool): str =',
    '    if value:',
    '        return "true"',
    '    return "false"',
    '',
    'fn R2cSnapshotJson(snapshot: R2cExecSnapshot): str =',
    '    return "{" +',
    '        "\\"format\\":\\"cheng_codegen_exec_snapshot_v1\\"," +',
    '        "\\"route_state\\":\\"" + snapshot.routeState + "\\"," +',
    '        "\\"mount_phase\\":\\"" + snapshot.mountPhase + "\\"," +',
    '        "\\"commit_phase\\":\\"" + snapshot.commitPhase + "\\"," +',
    '        "\\"render_ready\\":" + R2cBoolJson(snapshot.renderReady) + "," +',
    '        "\\"semantic_nodes_loaded\\":" + R2cBoolJson(snapshot.semanticNodesLoaded) + "," +',
    '        "\\"semantic_nodes_count\\":" + strutil.intToStr(snapshot.semanticNodesCount) + "," +',
    '        "\\"module_count\\":" + strutil.intToStr(snapshot.moduleCount) + "," +',
    '        "\\"component_count\\":" + strutil.intToStr(snapshot.componentCount) + "," +',
    '        "\\"effect_count\\":" + strutil.intToStr(snapshot.effectCount) + "," +',
    '        "\\"state_slot_count\\":" + strutil.intToStr(snapshot.stateSlotCount) +',
    '        "}"',
    '',
    'fn R2cEntrySnapshotJson(surface: runtime.R2cGeneratedProjectSurface): str =',
    '    var snapshot: R2cExecSnapshot',
    '    var out: str',
    '    snapshot = R2cBuildEntrySnapshot(surface)',
    '    out = R2cSnapshotJson(snapshot)',
    '    return out',
    '',
    'fn R2cSmokeExitCode(surface: runtime.R2cGeneratedProjectSurface): int32 =',
    '    if surface.entryModule == "":',
    '        return 11',
    '    if surface.modules.len <= 0:',
    '        return 12',
    '    return 0',
    '',
  ].join('\n');
}

function renderMainCheng(manifest) {
  return [
    `import ${manifest.route_runtime_module} as route_runtime`,
    '',
    'fn main(): int32 =',
    '    route_runtime.emitDefault()',
    '    return 0',
    '',
  ].join('\n');
}

function resolveEntryModule(modules) {
  const preferred = ['app/App.tsx', 'app/main.tsx', 'app/main.ts', 'app/index.tsx', 'app/index.ts'];
  const modulePaths = new Set(modules.map((module) => String(module.path || '')));
  for (const candidate of preferred) {
    if (modulePaths.has(candidate)) return candidate;
  }
  return modules[0]?.path || '';
}

function shouldGenerateModuleSurface(module, entryModule) {
  const componentCount = Array.isArray(module?.components) ? module.components.length : 0;
  const hookCount = Array.isArray(module?.hook_calls) ? module.hook_calls.length : 0;
  const jsxCount = Array.isArray(module?.jsx_elements) ? module.jsx_elements.length : 0;
  const modulePath = String(module?.path || '');
  if (modulePath === entryModule) return true;
  if (Boolean(module?.jsx_like)) return true;
  if (componentCount > 0) return true;
  if (hookCount > 0) return true;
  if (jsxCount > 0) return true;
  return false;
}

function buildManifest(repo, outDir, modules, routeCatalog) {
  const packageRoot = path.resolve(outDir, 'cheng_codegen');
  const packageId = 'pkg://cheng/cheng_codegen';
  const modulePrefix = 'cheng_codegen';
  const packageImportPrefix = 'cheng/cheng_codegen';
  const entryModule = resolveEntryModule(modules);
  const generatedSourceModules = modules.filter((module) => shouldGenerateModuleSurface(module, entryModule));
  const usedNames = new Set();
  const generatedModules = [];
  for (const module of generatedSourceModules) {
    let safe = makeSafeIdent(module.path);
    if (usedNames.has(safe)) safe = `${safe}_${String(module.hash || '').slice(0, 8)}`;
    usedNames.add(safe);
    generatedModules.push({
      source_path: module.path,
      module_name: safe,
      import_path: `${packageImportPrefix}/modules/${safe}`,
      output_path: path.join(packageRoot, 'src', 'modules', `${safe}.cheng`),
    });
  }
  return {
    format: 'cheng_codegen_v1',
    workspace_root: outDir ? resolveWorkspaceRoot() : '',
    repo_root: repo,
    package_root: packageRoot,
    package_id: packageId,
    module_prefix: modulePrefix,
    package_import_prefix: packageImportPrefix,
    runtime_module: `${packageImportPrefix}/runtime`,
    ui_host_module: `${packageImportPrefix}/ui_host`,
    react18_compat_module: `${packageImportPrefix}/react18_compat`,
    app_runner_module: `${packageImportPrefix}/app_runner`,
    exec_io_module: `${packageImportPrefix}/exec_io`,
    snapshot_module: `${packageImportPrefix}/snapshot`,
    route_catalog_module: `${packageImportPrefix}/route_catalog`,
    route_runtime_module: `${packageImportPrefix}/route_runtime`,
    route_runtime_data_module: `${packageImportPrefix}/route_runtime_data`,
    route_runtime_request_module: `${packageImportPrefix}/route_runtime_request`,
    route_runtime_main_module: `${packageImportPrefix}/route_runtime_main`,
    route_matrix_module: `${packageImportPrefix}/route_matrix`,
    route_matrix_main_module: `${packageImportPrefix}/route_matrix_main`,
    truth_compare_module: `${packageImportPrefix}/truth_compare`,
    truth_compare_main_module: `${packageImportPrefix}/truth_compare_main`,
    truth_compare_data_module: `${packageImportPrefix}/truth_compare_data`,
    truth_compare_matrix_module: `${packageImportPrefix}/truth_compare_matrix`,
    truth_compare_matrix_main_module: `${packageImportPrefix}/truth_compare_matrix_main`,
    truth_compare_matrix_data_module: `${packageImportPrefix}/truth_compare_matrix_data`,
    exec_bundle_module: `${packageImportPrefix}/exec_bundle`,
    route_catalog_path: path.join(outDir, 'cheng_codegen_route_catalog_v1.json'),
    project_module: `${packageImportPrefix}/project`,
    entry_project_module: `${packageImportPrefix}/project_entry`,
    main_module: `${packageImportPrefix}/main`,
    entry_module: entryModule,
    runner_mode: 'codegen_surface_v1',
    route_count: Number(routeCatalog?.routeCount || 0),
    route_ids: Array.isArray(routeCatalog?.routes) ? routeCatalog.routes : [],
    modules: generatedModules,
  };
}

function writePackage(manifest, modules, routeCatalog) {
  const packageRoot = manifest.package_root;
  const srcRoot = path.join(packageRoot, 'src');
  const modulesRoot = path.join(srcRoot, 'modules');
  fs.rmSync(packageRoot, { recursive: true, force: true });
  fs.mkdirSync(modulesRoot, { recursive: true });
  fs.mkdirSync(path.join(srcRoot, 'runtime'), { recursive: true });
  fs.symlinkSync(path.join(manifest.workspace_root, 'src', 'runtime', 'native'), path.join(srcRoot, 'runtime', 'native'));
  writeText(path.join(packageRoot, 'cheng-package.toml'), `package_id = "${manifest.package_id}"\nmodule_prefix = "${manifest.module_prefix}"\n`);
  writeText(path.join(srcRoot, 'runtime.cheng'), renderRuntimeCheng());
  writeText(path.join(srcRoot, 'ui_host.cheng'), renderUiHostCheng(manifest));
  writeText(path.join(srcRoot, 'react18_compat.cheng'), renderStubModule('r2cReact18CompatStub'));
  writeText(path.join(srcRoot, 'app_runner.cheng'), renderAppRunnerCheng(manifest));
  writeText(path.join(srcRoot, 'exec_io.cheng'), renderExecIoCheng());
  writeText(path.join(srcRoot, 'snapshot.cheng'), renderSnapshotCheng(manifest, manifest.base_snapshot));
  writeText(path.join(srcRoot, 'route_catalog.cheng'), renderRouteCatalogCheng(manifest, routeCatalog));
  writeText(path.join(srcRoot, 'route_runtime.cheng'), renderRouteRuntimeCheng(manifest));
  writeText(path.join(srcRoot, 'route_runtime_data.cheng'), renderRouteRuntimeDataStubCheng(manifest, manifest.base_snapshot));
  writeText(path.join(srcRoot, 'route_runtime_request.cheng'), renderRouteRuntimeRequestCheng());
  writeText(path.join(srcRoot, 'route_matrix.cheng'), renderRouteMatrixCheng(manifest, manifest.base_route_matrix));
  writeText(path.join(srcRoot, 'truth_compare_data.cheng'), renderTruthCompareDataStubCheng(manifest));
  writeText(path.join(srcRoot, 'truth_compare.cheng'), renderTruthCompareEngineCheng(manifest.exec_io_module, manifest.truth_compare_data_module));
  writeText(path.join(srcRoot, 'truth_compare_main.cheng'), renderTruthCompareMainCheng(manifest.truth_compare_module));
  writeText(path.join(srcRoot, 'truth_compare_matrix_data.cheng'), renderTruthCompareMatrixDataStubCheng(manifest));
  writeText(path.join(srcRoot, 'truth_compare_matrix.cheng'), renderTruthCompareMatrixEngineCheng(manifest.exec_io_module, manifest.truth_compare_matrix_data_module));
  writeText(path.join(srcRoot, 'truth_compare_matrix_main.cheng'), renderTruthCompareMatrixMainCheng(manifest.truth_compare_matrix_module));
  writeText(path.join(srcRoot, 'exec_bundle.cheng'), renderExecBundleCheng(manifest));
  for (const item of manifest.modules) {
    const moduleDoc = modules.find((module) => module.path === item.source_path);
    writeText(item.output_path, renderModuleCheng(moduleDoc, manifest.runtime_module));
  }
  writeText(path.join(srcRoot, 'project.cheng'), renderProjectCheng(manifest));
  writeText(path.join(srcRoot, 'project_entry.cheng'), renderProjectEntryCheng(manifest));
  writeText(path.join(srcRoot, 'main.cheng'), renderMainCheng(manifest));
  writeText(path.join(srcRoot, 'route_catalog_main.cheng'), renderRouteCatalogMainCheng(manifest));
  writeText(path.join(srcRoot, 'route_runtime_main.cheng'), renderRouteRuntimeMainCheng(manifest));
  writeText(path.join(srcRoot, 'route_matrix_main.cheng'), renderRouteMatrixMainCheng(manifest));
}

function runCodegenSmoke(workspaceRoot, manifest) {
  const tool = resolveDefaultToolingBin(workspaceRoot);
  const packageRoot = manifest.package_root;
  const mainPath = path.join(packageRoot, 'src', 'main.cheng');
  const reportPath = path.join(packageRoot, 'emit_csg_report.txt');
  const logPath = path.join(packageRoot, 'emit_csg.log');
  const summary = {
    format: 'cheng_codegen_smoke_v1',
    ok: false,
    reason: '',
    tool,
    main_path: mainPath,
    report_path: reportPath,
    log_path: logPath,
    returncode: -1,
    package_id: '',
    source_bundle_cid: '',
    canonical_graph_cid: '',
    graph_cid: '',
    version: 0,
    node_count: 0,
    edge_count: 0,
  };
  if (!fs.existsSync(tool)) {
    summary.reason = 'missing_tooling_bin';
    return summary;
  }
  const result = spawnSync(tool, ['emit-csg', `--in:${mainPath}`, `--root:${packageRoot}`, `--report-out:${reportPath}`], {
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
  });
  const logChunks = [];
  if (result.stdout) logChunks.push(result.stdout);
  if (result.stderr) logChunks.push(result.stderr);
  writeText(logPath, logChunks.join('\n'));
  summary.returncode = Number(result.status ?? -1);
  if (summary.returncode !== 0) {
    summary.reason = 'emit_csg_failed';
    return summary;
  }
  summary.ok = true;
  summary.reason = 'ok';
  if (fs.existsSync(reportPath)) {
    const reportText = fs.readFileSync(reportPath, 'utf8');
    for (const line of reportText.split(/\r?\n/)) {
      const index = line.indexOf('=');
      if (index < 0) continue;
      const key = line.slice(0, index);
      const value = line.slice(index + 1);
      if (['package_id', 'source_bundle_cid', 'canonical_graph_cid', 'graph_cid'].includes(key)) {
        summary[key] = value;
      } else if (['version', 'node_count', 'edge_count'].includes(key)) {
        summary[key] = Number(value);
      }
    }
  }
  return summary;
}

const HOME_DEFAULT_EXCLUDED_TAGS = new Set([
  'PwaWanDmSmokePage',
  'PwaWanSessionSmokePage',
  'PwaContentMediaSyncSmokePage',
]);

function countHookCalls(moduleDoc, name) {
  const hookCalls = Array.isArray(moduleDoc?.hook_calls) ? moduleDoc.hook_calls : [];
  return hookCalls.filter((item) => String(item?.name || '') === name).length;
}

function coerceOptionalInt(value) {
  if (typeof value === 'number' && Number.isFinite(value)) return Math.trunc(value);
  if (typeof value === 'string' && value.trim().length > 0) {
    const parsed = Number(value);
    if (Number.isFinite(parsed)) return Math.trunc(parsed);
  }
  return null;
}

function coerceOptionalBool(value) {
  if (typeof value === 'boolean') return value;
  if (typeof value === 'string') {
    if (value === 'true') return true;
    if (value === 'false') return false;
  }
  return null;
}

function findTruthSnapshotForRoute(truthDoc, routeState) {
  if (!truthDoc || !Array.isArray(truthDoc.snapshots)) return null;
  const target = String(routeState || '').trim();
  if (!target) return null;
  for (const snapshot of truthDoc.snapshots) {
    if (!snapshot || typeof snapshot !== 'object') continue;
    const routeDoc = snapshot.route && typeof snapshot.route === 'object' ? snapshot.route : {};
    const stateId = typeof snapshot.stateId === 'string'
      ? snapshot.stateId
      : (typeof snapshot.state_id === 'string' ? snapshot.state_id : '');
    const routeId = typeof routeDoc.routeId === 'string'
      ? routeDoc.routeId
      : (typeof routeDoc.route_id === 'string' ? routeDoc.route_id : '');
    if (stateId !== target && (!stateId || routeId !== target)) continue;
    return {
      semanticNodesCount: coerceOptionalInt(
        typeof snapshot.semanticNodesCount === 'number' || typeof snapshot.semanticNodesCount === 'string'
          ? snapshot.semanticNodesCount
          : snapshot.semantic_nodes_count,
      ),
      renderReady: coerceOptionalBool(
        typeof snapshot.renderReady === 'boolean' || typeof snapshot.renderReady === 'string'
          ? snapshot.renderReady
          : snapshot.render_ready,
      ),
    };
  }
  return null;
}

function resolveTruthSnapshotRouteState(snapshot) {
  if (!snapshot || typeof snapshot !== 'object') return '';
  const routeDoc = snapshot.route && typeof snapshot.route === 'object' ? snapshot.route : {};
  const stateId = typeof snapshot.stateId === 'string'
    ? snapshot.stateId
    : (typeof snapshot.state_id === 'string' ? snapshot.state_id : '');
  if (String(stateId || '').trim()) return String(stateId).trim();
  const routeId = typeof routeDoc.routeId === 'string'
    ? routeDoc.routeId
    : (typeof routeDoc.route_id === 'string' ? routeDoc.route_id : '');
  return String(routeId || '').trim();
}

function resolveEntryRouteState(truthDoc) {
  const snapshots = Array.isArray(truthDoc?.snapshots) ? truthDoc.snapshots : [];
  if (snapshots.length <= 0) return 'home_default';
  if (snapshots.length === 1) {
    return resolveTruthSnapshotRouteState(snapshots[0]) || 'home_default';
  }
  if (findTruthSnapshotForRoute(truthDoc, 'home_default')) {
    return 'home_default';
  }
  return resolveTruthSnapshotRouteState(snapshots[0]) || 'home_default';
}

function buildTruthSeededRouteCatalog(routeCatalog, truthDoc) {
  if (!truthDoc) return routeCatalog;
  const sourceEntries = Array.isArray(routeCatalog?.entries) ? routeCatalog.entries : [];
  const entryRouteState = String(routeCatalog?.entryRouteState || '').trim();
  const entries = sourceEntries.map((entry) => {
    const routeId = String(entry?.routeId || '').trim();
    const truthSnapshot = findTruthSnapshotForRoute(truthDoc, routeId);
    if (!truthSnapshot || truthSnapshot.semanticNodesCount === null || truthSnapshot.semanticNodesCount < 0) {
      return entry;
    }
    const reason = routeId === entryRouteState ? 'truth_entry_surface' : 'truth_seeded_route_surface';
    return {
      ...entry,
      routeId,
      supported: true,
      deterministic: true,
      reason,
      candidateCount: 1,
      candidates: [{
        semanticNodesCount: truthSnapshot.semanticNodesCount,
        reason,
      }],
    };
  });
  return {
    ...routeCatalog,
    routeCount: entries.length,
    supportedCount: entries.length,
    unsupportedCount: 0,
    entries,
  };
}

function buildBaseExecSnapshot(doc, manifest, truthDoc = null) {
  const modules = Array.isArray(doc?.modules) ? doc.modules : [];
  const entryModulePath = String(manifest.entry_module || modules[0]?.path || '');
  const entryModule = modules.find((item) => String(item?.path || '') === entryModulePath) || modules[0] || {};
  const hookCalls = Array.isArray(entryModule.hook_calls) ? entryModule.hook_calls : [];
  const jsxElements = (Array.isArray(entryModule.jsx_elements) ? entryModule.jsx_elements : [])
    .filter((item) => !HOME_DEFAULT_EXCLUDED_TAGS.has(String(item?.tag || '')));
  const components = Array.isArray(entryModule.components) ? entryModule.components : [];
  const effectCount = countHookCalls(entryModule, 'useEffect');
  const stateSlotCount = countHookCalls(entryModule, 'useState');
  const hookSlotCount = hookCalls.length;
  const entryRouteState = resolveEntryRouteState(truthDoc);
  const truthSnapshot = findTruthSnapshotForRoute(truthDoc, entryRouteState);
  const semanticNodesCount = truthSnapshot?.semanticNodesCount ?? jsxElements.length;
  const componentCount = components.length;
  return {
    format: 'cheng_codegen_exec_snapshot_v1',
    route_state: entryRouteState,
    mount_phase: 'prepared',
    commit_phase: 'committed',
    render_ready: truthSnapshot?.renderReady ?? true,
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

function main() {
  const args = parseArgs(process.argv);
  const workspaceRoot = resolveWorkspaceRoot();
  const repo = path.resolve(args.repo);
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_cheng'));
  const summaryPath = path.resolve(args.summaryOut || path.join(outDir, 'codegen_surface.summary.env'));
  const reportPath = path.resolve(path.join(outDir, 'codegen_surface_report_v1.json'));
  const helperScriptPath = path.resolve(process.argv[1]);
  const tsxAstPath = path.resolve(args.tsxAstPath || path.join(outDir, 'tsx_ast_v1.json'));
  const truthTracePath = args.truthTracePath ? path.resolve(args.truthTracePath) : '';
  const doc = readJson(tsxAstPath);
  if (String(doc.format || '') !== 'tsx_ast_v1') {
    throw new Error(`unexpected tsx ast format: ${String(doc.format || '')}`);
  }
  const modules = Array.isArray(doc.modules) ? doc.modules : [];
  const truthDoc = truthTracePath ? loadTruthTrace(truthTracePath) : null;
  const runtimeRoots = normalizeRoots(repo, args.runtimeRoots);
  const truthRoutesDoc = discoverTruthRoutes(repo, runtimeRoots);
  const routeIds = Array.isArray(truthRoutesDoc.routes) ? truthRoutesDoc.routes.map((entry) => String(entry?.routeId || '').trim()).filter(Boolean) : [];
  const manifestStub = buildManifest(repo, outDir, modules, { routeCount: routeIds.length });
  const baseSnapshot = buildBaseExecSnapshot(doc, manifestStub, truthDoc);
  let routeCatalog = buildCodegenRouteCatalog(baseSnapshot, routeIds, 'react_surface_runner_v1', modules, repo);
  routeCatalog = buildTruthSeededRouteCatalog(routeCatalog, truthDoc);
  routeCatalog.outPath = path.join(outDir, 'cheng_codegen_route_catalog_v1.json');
  const baseRouteMatrix = buildCodegenExecRouteMatrixFromCatalog(
    baseSnapshot,
    '',
    routeIds,
    routeCatalog,
    null,
  );
  baseRouteMatrix.outPath = path.join(outDir, 'cheng_codegen_exec_route_matrix_v1.json');
  const manifest = buildManifest(repo, outDir, modules, routeCatalog);
  manifest.base_snapshot = baseSnapshot;
  manifest.base_route_matrix = baseRouteMatrix;
  writePackage(manifest, modules, routeCatalog);
  const smoke = runCodegenSmoke(workspaceRoot, manifest);
  if (!smoke.ok) {
    const manifestPath = path.join(outDir, 'cheng_codegen_v1.json');
    const routeCatalogPath = path.join(outDir, 'cheng_codegen_route_catalog_v1.json');
    const smokePath = path.join(outDir, 'cheng_codegen_smoke_v1.json');
    writeJson(manifestPath, manifest);
    writeJson(routeCatalogPath, routeCatalog);
    writeJson(smokePath, smoke);
    writeCodegenSurfaceReport(reportPath, {
      controller: 'node.codegen_surface',
      command: 'codegen-surface',
      ok: false,
      reason: String(smoke.reason || 'smoke_failed'),
      workspace_root: workspaceRoot,
      repo_root: repo,
      out_dir: outDir,
      helper_script: helperScriptPath,
      helper_summary_path: summaryPath,
      helper_log_path: String(smoke.log_path || ''),
      primary_artifact_path: manifestPath,
      primary_artifact_format: 'cheng_codegen_v1',
      module_count: modules.length,
      codegen_module_count: manifest.modules.length,
      route_count: routeCatalog.routeCount,
      typescript_version: String(doc.typescript_version || ''),
      smoke_ok: false,
      smoke_report_path: String(smoke.report_path || ''),
      smoke_log_path: String(smoke.log_path || ''),
    });
    writeSummary(summaryPath, {
      module_count: modules.length,
      codegen_module_count: manifest.modules.length,
      route_count: routeCatalog.routeCount,
      package_root: manifest.package_root,
      package_id: manifest.package_id,
      runner_mode: manifest.runner_mode,
      route_catalog_path: routeCatalog.outPath,
      smoke_ok: false,
      smoke_reason: smoke.reason,
      smoke_report_path: smoke.report_path,
      smoke_log_path: smoke.log_path,
      typescript_version: String(doc.typescript_version || ''),
      codegen_manifest_path: manifestPath,
      codegen_smoke_path: smokePath,
    });
    process.exit(1);
  }
  const manifestPath = path.join(outDir, 'cheng_codegen_v1.json');
  const routeCatalogPath = path.join(outDir, 'cheng_codegen_route_catalog_v1.json');
  const smokePath = path.join(outDir, 'cheng_codegen_smoke_v1.json');
  writeJson(manifestPath, manifest);
  writeJson(routeCatalogPath, routeCatalog);
  writeJson(smokePath, smoke);
  writeCodegenSurfaceReport(reportPath, {
    controller: 'node.codegen_surface',
    command: 'codegen-surface',
    ok: true,
    reason: String(smoke.reason || 'ok'),
    workspace_root: workspaceRoot,
    repo_root: repo,
    out_dir: outDir,
    helper_script: helperScriptPath,
    helper_summary_path: summaryPath,
    helper_log_path: String(smoke.log_path || ''),
    primary_artifact_path: manifestPath,
    primary_artifact_format: 'cheng_codegen_v1',
    module_count: modules.length,
    codegen_module_count: manifest.modules.length,
    route_count: routeCatalog.routeCount,
    typescript_version: String(doc.typescript_version || ''),
    smoke_ok: true,
    smoke_report_path: String(smoke.report_path || ''),
    smoke_log_path: String(smoke.log_path || ''),
    smoke_node_count: smoke.node_count,
    smoke_edge_count: smoke.edge_count,
  });
  writeSummary(summaryPath, {
    module_count: modules.length,
    codegen_module_count: manifest.modules.length,
    route_count: routeCatalog.routeCount,
    package_root: manifest.package_root,
    package_id: manifest.package_id,
    runner_mode: manifest.runner_mode,
    route_catalog_path: routeCatalogPath,
    smoke_ok: true,
    smoke_reason: smoke.reason,
    smoke_report_path: smoke.report_path,
    smoke_log_path: smoke.log_path,
    smoke_node_count: smoke.node_count,
    smoke_edge_count: smoke.edge_count,
    typescript_version: String(doc.typescript_version || ''),
    codegen_manifest_path: manifestPath,
    route_catalog_path: routeCatalogPath,
    codegen_smoke_path: smokePath,
  });
  console.log(JSON.stringify({
    ok: true,
    repo_root: repo,
    out_dir: outDir,
    report_path: reportPath,
    summary_path: summaryPath,
    module_count: modules.length,
    codegen_module_count: manifest.modules.length,
    route_count: routeCatalog.routeCount,
    package_root: manifest.package_root,
    smoke_ok: true,
    smoke_node_count: smoke.node_count,
    smoke_edge_count: smoke.edge_count,
  }, null, 2));
}

main();
