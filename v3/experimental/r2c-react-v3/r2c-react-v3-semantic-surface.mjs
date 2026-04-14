#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';

const HOST_FEATURE_TO_LABEL = {
  local_storage: ['StorageHost', 'localStorage'],
  fetch: ['NetHost', 'fetch'],
  broadcast_channel: ['NetHost', 'BroadcastChannel'],
  worker: ['NetHost', 'Worker'],
  webassembly: ['NetHost', 'WebAssembly'],
  rtc: ['RtcHost', 'RTCPeerConnection'],
  media: ['MediaHost', 'getUserMedia'],
  capacitor: ['PlatformHost', 'Capacitor'],
  custom_event: ['PlatformHost', 'CustomEvent'],
  resize_observer: ['PlatformHost', 'ResizeObserver'],
  mutation_observer: ['PlatformHost', 'MutationObserver'],
  react_three: ['SceneHost', 'react-three'],
};

function parseArgs(argv) {
  const out = {
    repo: '',
    tsxAstPath: '',
    outDir: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--tsx-ast') out.tsxAstPath = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-semantic-surface.mjs --repo <path> [--tsx-ast <file>] [--out-dir <dir>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.repo) throw new Error('missing --repo');
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

function stableNode(nodes, nodeId, kind, label, capability, sourcePath, sourceSpan) {
  nodes.set(nodeId, {
    id: nodeId,
    kind,
    label,
    capability,
    sourcePath,
    sourceSpan,
  });
}

function stableEdge(edges, fromId, toId, kind, label) {
  edges.add(JSON.stringify({ fromId, toId, kind, label }));
}

function resolveEntryModule(modules) {
  const preferred = ['app/App.tsx', 'app/main.tsx', 'app/main.ts', 'app/index.tsx', 'app/index.ts'];
  const modulePaths = new Set(modules.map((module) => String(module.path || '')));
  for (const candidate of preferred) {
    if (modulePaths.has(candidate)) return candidate;
  }
  return modules[0]?.path || '';
}

function buildRsg(repo, modules) {
  const nodes = new Map();
  const edges = new Set();
  for (const module of modules) {
    const sourcePath = String(module.path || '');
    const featureCounts = module.feature_counts || {};
    const moduleId = `module:${sourcePath}`;
    stableNode(nodes, moduleId, 'rsgModule', sourcePath, module.generated ? 'generated' : 'module', sourcePath, 'module');

    for (const importPath of module.static_imports || []) {
      const importId = `import:${sourcePath}:${importPath}`;
      stableNode(nodes, importId, 'rsgImport', importPath, 'module', sourcePath, 'import');
      stableEdge(edges, moduleId, importId, 'rsgDependsOn', 'static_import');
    }
    for (const importPath of module.dynamic_imports || []) {
      const lazyId = `lazy:${sourcePath}:${importPath}`;
      stableNode(nodes, lazyId, 'rsgLazyModule', importPath, 'async', sourcePath, 'dynamic_import');
      stableEdge(edges, moduleId, lazyId, 'rsgSchedules', 'dynamic_import');
    }
    for (const component of module.components || []) {
      const componentId = `component:${component.kind}:${sourcePath}:${component.name}`;
      const componentKind = component.kind === 'class' ? 'rsgComponentClass' : 'rsgComponentFunction';
      stableNode(nodes, componentId, componentKind, String(component.name || ''), 'react_component', sourcePath, `line:${Number(component.line || 0)}`);
      stableEdge(edges, moduleId, componentId, 'rsgOwns', 'component');
    }

    const hookKinds = [
      ['use_state', 'hook:state', 'rsgHookSlot', 'useState', 'state', 'rsgOwns', 'hook'],
      ['use_ref', 'hook:ref', 'rsgHookSlot', 'useRef', 'ref', 'rsgOwns', 'hook'],
      ['use_memo', 'hook:memo', 'rsgHookSlot', 'useMemo', 'memo', 'rsgOwns', 'hook'],
      ['use_callback', 'hook:callback', 'rsgHookSlot', 'useCallback', 'callback', 'rsgCaptures', 'hook'],
      ['use_effect', 'effect', 'rsgEffect', 'useEffect', 'effect', 'rsgSchedules', 'effect'],
      ['use_context', 'context', 'rsgContext', 'context', 'context', 'rsgReads', 'context'],
      ['suspense', 'suspense', 'rsgSuspenseBoundary', 'Suspense', 'async', 'rsgSchedules', 'suspense'],
      ['error_boundary', 'boundary', 'rsgErrorBoundary', 'ErrorBoundary', 'error', 'rsgThrowsTo', 'error_boundary'],
    ];
    for (const [featureName, prefix, nodeKind, labelBase, capability, edgeKind, edgeLabel] of hookKinds) {
      const count = Number(featureCounts[featureName] || 0);
      for (let idx = 0; idx < count; idx += 1) {
        const nodeId = `${prefix}:${sourcePath}:${idx + 1}`;
        const label = `${labelBase}#${idx + 1}`;
        stableNode(nodes, nodeId, nodeKind, label, capability, sourcePath, 'surface');
        stableEdge(edges, moduleId, nodeId, edgeKind, edgeLabel);
      }
    }

    const tailwindCount = Number(module.tailwind_class_token_count || 0);
    if (tailwindCount > 0) {
      const nodeId = `style:${sourcePath}`;
      stableNode(nodes, nodeId, 'rsgStyleBlock', `className[${tailwindCount}]`, 'style', sourcePath, 'surface');
      stableEdge(edges, moduleId, nodeId, 'rsgStyles', 'tailwind');
    }
    if (module.jsx_like) {
      const nodeId = `layout:${sourcePath}`;
      stableNode(nodes, nodeId, 'rsgLayoutNode', 'jsx_surface', 'layout', sourcePath, 'surface');
      stableEdge(edges, moduleId, nodeId, 'rsgLayouts', 'jsx');
    }
    const customEventCount = Number(featureCounts.custom_event || 0);
    if (customEventCount > 0) {
      const nodeId = `event:${sourcePath}:custom`;
      stableNode(nodes, nodeId, 'rsgEventHandler', `CustomEvent[${customEventCount}]`, 'event', sourcePath, 'surface');
      stableEdge(edges, moduleId, nodeId, 'rsgHandles', 'custom_event');
    }
    for (const feature of Object.keys(HOST_FEATURE_TO_LABEL).sort()) {
      const count = Number(featureCounts[feature] || 0);
      if (count <= 0) continue;
      const [group, label] = HOST_FEATURE_TO_LABEL[feature];
      const nodeId = `host:${sourcePath}:${feature}`;
      stableNode(nodes, nodeId, 'rsgHostCall', `${label}[${count}]`, group, sourcePath, 'surface');
      stableEdge(edges, moduleId, nodeId, 'rsgCalls', feature);
    }
    const asyncCount = ['fetch', 'worker', 'webassembly', 'rtc', 'media']
      .map((name) => Number(featureCounts[name] || 0))
      .reduce((sum, value) => sum + value, 0);
    if (asyncCount > 0) {
      const nodeId = `async:${sourcePath}`;
      stableNode(nodes, nodeId, 'rsgAsyncTask', `async[${asyncCount}]`, 'async', sourcePath, 'surface');
      stableEdge(edges, moduleId, nodeId, 'rsgSchedules', 'async');
    }
  }
  const edgeRows = [...edges]
    .map((row) => JSON.parse(row))
    .sort((a, b) =>
      a.fromId.localeCompare(b.fromId)
      || a.toId.localeCompare(b.toId)
      || a.kind.localeCompare(b.kind)
      || a.label.localeCompare(b.label));
  return {
    format: 'rsg_v1',
    repo_root: repo,
    entry_module: resolveEntryModule(modules),
    nodes: [...nodes.keys()].sort().map((key) => nodes.get(key)),
    edges: edgeRows,
  };
}

function buildBlockerReport(repo, runtimeRoots, modules) {
  const blockers = [];
  for (const module of modules) {
    for (const blocker of module.blockers || []) {
      blockers.push(blocker);
    }
  }
  blockers.sort((a, b) =>
    String(a.file || '').localeCompare(String(b.file || ''))
    || Number(a.line || 0) - Number(b.line || 0)
    || Number(a.column || 0) - Number(b.column || 0)
    || String(a.code || '').localeCompare(String(b.code || '')));
  return {
    format: 'compile_blockers_v1',
    repo_root: repo,
    source_roots: runtimeRoots,
    blockers,
  };
}

function main() {
  const args = parseArgs(process.argv);
  const repo = path.resolve(args.repo);
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_v3_cheng'));
  const tsxAstPath = path.resolve(args.tsxAstPath || path.join(outDir, 'tsx_ast_v1.json'));
  const doc = readJson(tsxAstPath);
  if (String(doc.format || '') !== 'tsx_ast_v1') {
    throw new Error(`unexpected tsx ast format: ${String(doc.format || '')}`);
  }
  const modules = Array.isArray(doc.modules) ? doc.modules : [];
  const runtimeRoots = Array.isArray(doc.source_roots) ? doc.source_roots.map((item) => String(item)) : [];
  const rsg = buildRsg(repo, modules);
  const blockerReport = buildBlockerReport(repo, runtimeRoots, modules);
  const rsgPath = path.join(outDir, 'rsg_v1.json');
  const blockerPath = path.join(outDir, 'compile_blockers_v1.json');
  const reportPath = path.join(outDir, 'semantic_surface_report_v1.json');
  writeJson(rsgPath, rsg);
  writeJson(blockerPath, blockerReport);
  writeJson(reportPath, {
    format: 'semantic_surface_report_v1',
    repo_root: repo,
    out_dir: outDir,
    tsx_ast_path: tsxAstPath,
    rsg_path: rsgPath,
    blocker_report_path: blockerPath,
    module_count: modules.length,
    rsg_node_count: rsg.nodes.length,
    rsg_edge_count: rsg.edges.length,
    blocker_count: blockerReport.blockers.length,
    blocked: blockerReport.blockers.length > 0,
    typescript_version: String(doc.typescript_version || ''),
  });
  writeSummary(args.summaryOut, {
    module_count: modules.length,
    rsg_node_count: rsg.nodes.length,
    rsg_edge_count: rsg.edges.length,
    blocker_count: blockerReport.blockers.length,
    blocked: blockerReport.blockers.length > 0,
    typescript_version: String(doc.typescript_version || ''),
    entry_module: String(rsg.entry_module || ''),
    rsg_path: rsgPath,
    blocker_report_path: blockerPath,
  });
  console.log(JSON.stringify({
    ok: true,
    repo_root: repo,
    out_dir: outDir,
    tsx_ast_path: tsxAstPath,
    module_count: modules.length,
    rsg_node_count: rsg.nodes.length,
    rsg_edge_count: rsg.edges.length,
    blocker_count: blockerReport.blockers.length,
    blocked: blockerReport.blockers.length > 0,
  }, null, 2));
}

main();
