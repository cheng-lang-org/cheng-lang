#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';

const SOURCE_EXTS = new Set(['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs']);
const TEST_FILE_RE = /(?:^|\/)(?:__tests__|.*\.(?:test|spec)\.[^.]+)$/;
const SKIP_DIRS = new Set(['node_modules', 'dist', 'build', 'artifacts', '.git', '.build', '.tmp-deploy']);
const GENERATED_PATH_SEGMENTS = new Set(['pkg']);
const GENERATED_FILE_SUFFIXES = ['_wasm.js', '_bg.js'];
const ASSET_EXTS = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg', '.ico', '.woff', '.woff2', '.ttf', '.otf', '.wasm', '.mp3', '.wav', '.ogg', '.mp4', '.webm', '.html', '.css', '.json']);

const IMPORT_FEATURES = new Map([
  ['@capacitor/', 'capacitor'],
  ['zustand', 'zustand'],
  ['@radix-ui/', 'radix'],
  ['@react-three/', 'react_three'],
  ['three', 'react_three'],
]);

const IDENTIFIER_FEATURES = new Map([
  ['localStorage', 'local_storage'],
  ['fetch', 'fetch'],
  ['ResizeObserver', 'resize_observer'],
  ['MutationObserver', 'mutation_observer'],
  ['CustomEvent', 'custom_event'],
  ['BroadcastChannel', 'broadcast_channel'],
  ['RTCPeerConnection', 'rtc'],
  ['WebAssembly', 'webassembly'],
  ['Worker', 'worker'],
]);

const CALL_FEATURES = new Map([
  ['useState', 'use_state'],
  ['useEffect', 'use_effect'],
  ['useMemo', 'use_memo'],
  ['useCallback', 'use_callback'],
  ['useRef', 'use_ref'],
  ['useContext', 'use_context'],
  ['createContext', 'use_context'],
  ['lazy', 'suspense'],
  ['memo', 'memo'],
]);

function parseArgs(argv) {
  const out = {
    repo: '',
    runtimeRoots: [],
    outPath: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--runtime-root') out.runtimeRoots.push(String(argv[++i] || ''));
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-ts-frontend.mjs --repo <path> [--runtime-root <dir>] [--out <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.repo) {
    throw new Error('missing --repo');
  }
  return out;
}

function normalizeRootValue(value) {
  const raw = String(value || '').trim();
  if (!raw || raw === '.') return '.';
  return raw.replace(/\\/g, '/').replace(/^\/+|\/+$/g, '');
}

function normalizeRoots(repo, requested) {
  const picks = requested.length > 0 ? requested : ['app'];
  const out = [];
  for (const item of picks) {
    const root = normalizeRootValue(item);
    if (root !== '.' && !fs.existsSync(path.join(repo, root))) {
      throw new Error(`missing runtime root: ${root}`);
    }
    if (!out.includes(root)) {
      out.push(root);
    }
  }
  return out.length > 0 ? out : ['.'];
}

function refsMapToArray(refs) {
  return Array.from(refs.entries())
    .map(([name, count]) => ({ name, count }))
    .sort((a, b) => a.name.localeCompare(b.name) || a.count - b.count);
}

function mergeRefsInto(target, source, multiplier = 1) {
  for (const [name, count] of source.entries()) {
    target.set(name, (target.get(name) || 0) + count * multiplier);
  }
}

function pathUnderRoots(relPath, roots) {
  if (roots.includes('.')) {
    return true;
  }
  return roots.some((root) => relPath === root || relPath.startsWith(`${root}/`));
}

function sourceRootForPath(relPath, roots) {
  if (roots.includes('.')) {
    return '.';
  }
  const matches = roots.filter((root) => relPath === root || relPath.startsWith(`${root}/`));
  if (matches.length === 0) {
    return '';
  }
  matches.sort((a, b) => b.length - a.length);
  return matches[0];
}

function isGeneratedFile(relPath) {
  const parts = relPath.split('/');
  if (parts.some((part) => GENERATED_PATH_SEGMENTS.has(part))) {
    return true;
  }
  return GENERATED_FILE_SUFFIXES.some((suffix) => relPath.endsWith(suffix));
}

function shouldIncludeFile(fullPath, relPath) {
  const ext = path.extname(fullPath).toLowerCase();
  if (!SOURCE_EXTS.has(ext)) {
    return false;
  }
  if (fullPath.endsWith('.d.ts')) {
    return false;
  }
  if (TEST_FILE_RE.test(relPath)) {
    return false;
  }
  if (['.js', '.jsx', '.mjs', '.cjs'].includes(ext)) {
    const stem = path.basename(fullPath, ext);
    const dir = path.dirname(fullPath);
    if (fs.existsSync(path.join(dir, `${stem}.ts`)) || fs.existsSync(path.join(dir, `${stem}.tsx`))) {
      return false;
    }
  }
  return true;
}

function walkSources(repo, roots) {
  const out = [];
  function walk(current) {
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      if (entry.isDirectory()) {
        if (SKIP_DIRS.has(entry.name)) continue;
        walk(path.join(current, entry.name));
        continue;
      }
      const fullPath = path.join(current, entry.name);
      const relPath = path.relative(repo, fullPath).split(path.sep).join('/');
      if (!pathUnderRoots(relPath, roots)) continue;
      if (shouldIncludeFile(fullPath, relPath)) {
        out.push(fullPath);
      }
    }
  }
  walk(repo);
  out.sort();
  return out;
}

function loadTypeScript(repo) {
  const requireFromRepo = createRequire(path.join(repo, 'package.json'));
  return requireFromRepo('typescript');
}

function scriptKindFor(ts, ext) {
  switch (ext) {
    case '.tsx': return ts.ScriptKind.TSX;
    case '.jsx': return ts.ScriptKind.JSX;
    case '.js': return ts.ScriptKind.JS;
    case '.mjs': return ts.ScriptKind.JS;
    case '.cjs': return ts.ScriptKind.JS;
    default: return ts.ScriptKind.TS;
  }
}

function sha256(text) {
  return crypto.createHash('sha256').update(text, 'utf8').digest('hex');
}

function writeSidecarSummary(summaryPath, values) {
  if (!summaryPath) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`);
  fs.mkdirSync(path.dirname(summaryPath), { recursive: true });
  fs.writeFileSync(summaryPath, `${lines.join('\n')}\n`, 'utf8');
}

function classifyAsset(relPath) {
  const ext = path.extname(String(relPath || '')).toLowerCase();
  if (['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg', '.ico'].includes(ext)) return 'image';
  if (['.woff', '.woff2', '.ttf', '.otf'].includes(ext)) return 'font';
  if (ext === '.wasm') return 'wasm';
  if (['.mp3', '.wav', '.ogg'].includes(ext)) return 'audio';
  if (['.mp4', '.webm'].includes(ext)) return 'video';
  if (ext === '.html') return 'html';
  if (ext === '.css') return 'css';
  if (ext === '.json') return 'json';
  return 'other';
}

function walkAssets(repo) {
  const out = [];
  function walk(current) {
    for (const entry of fs.readdirSync(current, { withFileTypes: true })) {
      if (entry.isDirectory()) {
        if (SKIP_DIRS.has(entry.name)) continue;
        walk(path.join(current, entry.name));
        continue;
      }
      const fullPath = path.join(current, entry.name);
      const relPath = path.relative(repo, fullPath).split(path.sep).join('/');
      const ext = path.extname(relPath).toLowerCase();
      if (!ASSET_EXTS.has(ext) || SOURCE_EXTS.has(ext)) continue;
      out.push({
        path: relPath,
        kind: classifyAsset(relPath),
      });
    }
  }
  walk(repo);
  out.sort((a, b) => a.path.localeCompare(b.path) || a.kind.localeCompare(b.kind));
  return out;
}

function writeLines(filePath, lines) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${lines.join('\n')}\n`, 'utf8');
}

function writeSurfaceFeed(outDir, repo, runtimeRoots, tsVersion, modules) {
  const metaPath = path.join(outDir, 'tsx_surface_meta_v1.env');
  const modulesPath = path.join(outDir, 'tsx_surface_modules_v1.tsv');
  const featureCountsPath = path.join(outDir, 'tsx_surface_feature_counts_v1.tsv');
  const staticImportsPath = path.join(outDir, 'tsx_surface_static_imports_v1.tsv');
  const dynamicImportsPath = path.join(outDir, 'tsx_surface_dynamic_imports_v1.tsv');
  const assetRefsPath = path.join(outDir, 'tsx_surface_asset_refs_v1.tsv');
  const componentsPath = path.join(outDir, 'tsx_surface_components_v1.tsv');
  const blockersPath = path.join(outDir, 'tsx_surface_blockers_v1.tsv');
  const tailwindTokensPath = path.join(outDir, 'tsx_surface_tailwind_tokens_v1.tsv');
  const discoveredAssetsPath = path.join(outDir, 'tsx_surface_discovered_assets_v1.tsv');

  writeSidecarSummary(metaPath, {
    format: 'tsx_surface_feed_v1',
    repo_root: repo,
    source_roots: runtimeRoots.join(','),
    typescript_version: tsVersion,
    module_count: modules.length,
  });

  const moduleLines = [];
  const featureCountLines = [];
  const staticImportLines = [];
  const dynamicImportLines = [];
  const assetRefLines = [];
  const componentLines = [];
  const blockerLines = [];
  const tailwindTokenLines = [];

  for (const module of modules) {
    const modulePath = String(module.path || '');
    moduleLines.push([
      modulePath,
      module.generated ? '1' : '0',
      module.jsx_like ? '1' : '0',
      String(Number(module.export_count || 0)),
    ].join('\t'));

    for (const [featureName, countValue] of Object.entries(module.feature_counts || {})) {
      const count = Number(countValue || 0);
      if (count <= 0) continue;
      featureCountLines.push([modulePath, featureName, String(count)].join('\t'));
    }
    for (const specifier of module.static_imports || []) {
      staticImportLines.push([modulePath, String(specifier || '')].join('\t'));
    }
    for (const specifier of module.dynamic_imports || []) {
      dynamicImportLines.push([modulePath, String(specifier || '')].join('\t'));
    }
    for (const assetPath of module.asset_refs || []) {
      const assetText = String(assetPath || '');
      assetRefLines.push([modulePath, assetText, classifyAsset(assetText)].join('\t'));
    }
    for (const component of module.components || []) {
      componentLines.push([
        modulePath,
        String(component.name || ''),
        String(component.kind || ''),
        String(Number(component.line || 0)),
      ].join('\t'));
    }
    for (const blocker of module.blockers || []) {
      blockerLines.push([
        modulePath,
        String(blocker.file || ''),
        String(Number(blocker.line || 0)),
        String(Number(blocker.column || 0)),
        String(blocker.code || ''),
      ].join('\t'));
    }
    for (const token of module.tailwind_class_tokens || []) {
      tailwindTokenLines.push([modulePath, String(token || '')].join('\t'));
    }
  }

  const discoveredAssetLines = walkAssets(repo).map((entry) => [entry.path, entry.kind].join('\t'));
  writeLines(modulesPath, moduleLines);
  writeLines(featureCountsPath, featureCountLines);
  writeLines(staticImportsPath, staticImportLines);
  writeLines(dynamicImportsPath, dynamicImportLines);
  writeLines(assetRefsPath, assetRefLines);
  writeLines(componentsPath, componentLines);
  writeLines(blockersPath, blockerLines);
  writeLines(tailwindTokensPath, tailwindTokenLines);
  writeLines(discoveredAssetsPath, discoveredAssetLines);
}

function lineColOf(ts, sourceFile, node) {
  const pos = ts.getLineAndCharacterOfPosition(sourceFile, node.getStart(sourceFile, false));
  return { line: pos.line + 1, column: pos.character + 1 };
}

function spanText(ts, sourceFile, node) {
  const start = lineColOf(ts, sourceFile, node);
  return `line:${start.line}:${start.column}`;
}

function textOf(sourceFile, node) {
  return sourceFile.text.slice(node.getStart(sourceFile, false), node.getEnd());
}

function snippetOf(sourceFile, node) {
  const { line } = lineColOf(tsGlobal, sourceFile, node);
  const lines = sourceFile.text.split(/\r?\n/);
  return (lines[line - 1] || '').trim();
}

function isUpperName(name) {
  return typeof name === 'string' && /^[A-Z]/.test(name);
}

function jsxTagName(ts, node, knownValues = null) {
  if (ts.isIdentifier(node)) {
    if (knownValues && knownValues.has(node.text)) {
      const value = knownValues.get(node.text);
      if (value && typeof value === 'object' && value.kind === 'ref' && value.name) {
        return value.name;
      }
    }
    return node.text;
  }
  if (ts.isPropertyAccessExpression(node)) return node.name.text;
  if (ts.isJsxNamespacedName(node)) return `${node.namespace.text}:${node.name.text}`;
  return node.getText();
}

function callName(ts, expr) {
  if (ts.isIdentifier(expr)) return expr.text;
  if (ts.isPropertyAccessExpression(expr)) return expr.name.text;
  return '';
}

function importFeature(specifier) {
  for (const [prefix, feature] of IMPORT_FEATURES.entries()) {
    if (specifier === prefix || specifier.startsWith(prefix)) {
      return feature;
    }
  }
  return '';
}

function classifyBlocker(ts, sourceFile, node) {
  if (ts.isCallExpression(node)) {
    const name = callName(ts, node.expression);
    if (name === 'eval') {
      return ['js_eval', 'R2C 不接受 eval；模块初始化和闭包边界必须静态化。'];
    }
    if (name === 'require') {
      return ['js_dynamic_require', 'R2C 不接受 CommonJS require；模块依赖必须静态可枚举。'];
    }
    if (
      ts.isPropertyAccessExpression(node.expression)
      && ts.isIdentifier(node.expression.expression)
      && node.expression.expression.text === 'Object'
      && node.expression.name.text === 'setPrototypeOf'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
  }
  if (ts.isNewExpression(node)) {
    const name = callName(ts, node.expression);
    if (name === 'Function') {
      return ['js_new_function', 'R2C 不接受运行时构造函数体；代码必须在编译期可见。'];
    }
    if (name === 'Proxy') {
      return ['js_proxy', 'R2C 不接受 Proxy；对象语义必须显式建模。'];
    }
  }
  if (ts.isWithStatement(node)) {
    return ['js_with_statement', 'R2C 不接受 with；作用域必须静态确定。'];
  }
  if (ts.isBinaryExpression(node)) {
    const op = node.operatorToken.kind;
    const assignmentKinds = new Set([
      ts.SyntaxKind.EqualsToken,
      ts.SyntaxKind.BarBarEqualsToken,
      ts.SyntaxKind.AmpersandAmpersandEqualsToken,
      ts.SyntaxKind.QuestionQuestionEqualsToken,
      ts.SyntaxKind.PlusEqualsToken,
      ts.SyntaxKind.MinusEqualsToken,
      ts.SyntaxKind.AsteriskEqualsToken,
      ts.SyntaxKind.SlashEqualsToken,
      ts.SyntaxKind.PercentEqualsToken,
    ]);
    if (!assignmentKinds.has(op)) {
      return null;
    }
    const left = node.left;
    if (
      ts.isPropertyAccessExpression(left)
      && ts.isIdentifier(left.expression)
      && left.expression.text === 'module'
      && left.name.text === 'exports'
    ) {
      return ['js_cjs_exports', 'R2C 不接受 CommonJS 导出；模块导出必须静态化。'];
    }
    if (
      ts.isIdentifier(left)
      && left.text === 'exports'
    ) {
      return ['js_cjs_exports', 'R2C 不接受 CommonJS 导出；模块导出必须静态化。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && ts.isIdentifier(left.expression)
      && left.expression.text === 'exports'
    ) {
      return ['js_cjs_exports', 'R2C 不接受 CommonJS 导出；模块导出必须静态化。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && left.name.text === '__proto__'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && ts.isPropertyAccessExpression(left.expression)
      && left.expression.name.text === 'prototype'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && left.name.text === 'prototype'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
  }
  return null;
}

function hasJsxReturn(ts, node) {
  let found = false;
  function visit(current) {
    if (found) return;
    if (ts.isJsxElement(current) || ts.isJsxSelfClosingElement(current) || ts.isJsxFragment(current)) {
      found = true;
      return;
    }
    ts.forEachChild(current, visit);
  }
  visit(node);
  return found;
}

function scanModule(ts, repo, runtimeRoots, fullPath) {
  const relPath = path.relative(repo, fullPath).split(path.sep).join('/');
  const text = fs.readFileSync(fullPath, 'utf8');
  const ext = path.extname(fullPath).toLowerCase();
  const sourceFile = ts.createSourceFile(fullPath, text, ts.ScriptTarget.Latest, true, scriptKindFor(ts, ext));
  const featureCounts = Object.fromEntries([
    'class_component',
    'error_boundary',
    'use_state',
    'use_effect',
    'use_memo',
    'use_callback',
    'use_ref',
    'use_context',
    'suspense',
    'memo',
    'strict_mode',
    'local_storage',
    'fetch',
    'resize_observer',
    'mutation_observer',
    'custom_event',
    'rtc',
    'media',
    'broadcast_channel',
    'worker',
    'webassembly',
    'capacitor',
    'zustand',
    'radix',
    'react_three',
  ].map((name) => [name, 0]));
  const imports = [];
  const exports = [];
  const components = [];
  const blockers = [];
  const hookCalls = [];
  const classTokens = [];
  const jsxElements = [];
  const dynamicImports = [];
  const staticImports = [];
  const exportedNames = new Set();
  const componentKeys = new Set();
  const blockerKeys = new Set();
  const componentEntries = new Map();
  const importedKnownValues = new Map();

  function countFeature(name) {
    featureCounts[name] = (featureCounts[name] || 0) + 1;
  }

  function refValue(name) {
    return { kind: 'ref', name: String(name || '') };
  }

  function maybeAddBlocker(node) {
    const classified = classifyBlocker(ts, sourceFile, node);
    if (!classified) return;
    const [code, reason] = classified;
    const pos = lineColOf(ts, sourceFile, node);
    const key = `${code}:${pos.line}:${pos.column}`;
    if (blockerKeys.has(key)) return;
    blockerKeys.add(key);
    blockers.push({
      code,
      file: relPath,
      line: pos.line,
      column: pos.column,
      snippet: snippetOf(sourceFile, node),
      reason,
    });
  }

  function addComponent(name, kind, node, extra = {}) {
    if (!name || !isUpperName(name)) return;
    const pos = lineColOf(ts, sourceFile, node);
    const key = `${kind}:${name}:${pos.line}`;
    if (componentKeys.has(key)) return;
    componentKeys.add(key);
    const entry = {
      name,
      kind,
      line: pos.line,
      exported: exportedNames.has(name),
      surface_jsx_count: 0,
      surface_intrinsic_jsx_count: 0,
      surface_live_jsx_count: 0,
      surface_live_intrinsic_jsx_count: 0,
      surface_component_tag_count: 0,
      component_tag_refs: [],
      live_component_tag_refs: [],
      expanded_component_tag_refs: [],
      live_expanded_component_tag_refs: [],
      surface_fragment_count: 0,
      surface_expanded_jsx_count: 0,
      surface_expanded_intrinsic_jsx_count: 0,
      surface_live_expanded_jsx_count: 0,
      surface_live_expanded_intrinsic_jsx_count: 0,
      return_surfaces: [],
      ...extra,
    };
    components.push(entry);
    componentEntries.set(key, entry);
  }

  function collectStaticArrayLengths(rootNode) {
    const lengths = new Map();
    if (!rootNode) {
      return lengths;
    }
    function walk(node) {
      if (node !== rootNode && (ts.isFunctionLike(node) || ts.isClassLike(node))) {
        return;
      }
      if (ts.isVariableDeclaration(node) && ts.isIdentifier(node.name) && node.initializer && ts.isArrayLiteralExpression(node.initializer)) {
        lengths.set(node.name.text, node.initializer.elements.length);
      }
      ts.forEachChild(node, walk);
    }
    walk(rootNode);
    return lengths;
  }

  function mergeArrayLengths(base, extra) {
    const merged = new Map(base);
    for (const [key, value] of extra.entries()) {
      merged.set(key, value);
    }
    return merged;
  }

  const UNKNOWN_VALUE = Symbol('r2c_unknown_value');

  function arrayValue(length, items = null) {
    return { kind: 'array', length, items: Array.isArray(items) ? items : null };
  }

  function objectValue(props = new Map()) {
    return { kind: 'object', props };
  }

  function buildObjectValue(entries) {
    return objectValue(new Map(entries));
  }

  function coldStartUpdateSnapshotValue() {
    return buildObjectValue([
      ['state', 'DETECTED'],
      ['manifest_id', null],
      ['channel', 'stable'],
      ['platform', 'android'],
      ['sequence', 0],
      ['version', ''],
      ['current_version', ''],
      ['current_version_code', 0],
      ['previous_version', ''],
      ['previous_version_code', 0],
      ['latest_version', ''],
      ['latest_version_code', 0],
      ['latest_manifest_verified', false],
      ['latest_manifest_verified_sequence', 0],
      ['latest_manifest_source', ''],
      ['update_summary', ''],
      ['update_details', ''],
      ['update_published_at_ms', 0],
      ['show_update_prompt', false],
      ['vrf_candidate_status', 'none'],
      ['vrf_candidate_carriers', arrayValue(0, [])],
      ['last_manual_check_reason', ''],
      ['last_error', ''],
      ['last_checked_at_ms', 0],
      ['updated_at_ms', 0],
      ['attestor_count', 0],
      ['attestation_threshold', 0],
      ['shell_required', false],
      ['emergency', false],
      ['manual_check_inflight', false],
    ]);
  }

  function coldStartDexSnapshotValue() {
    return buildObjectValue([
      ['orders', arrayValue(0, [])],
      ['matches', arrayValue(0, [])],
      ['depths', arrayValue(0, [])],
      ['links', arrayValue(0, [])],
      ['updatedAt', 0],
    ]);
  }

  function coldStartDexAsiSessionStateValue() {
    return buildObjectValue([
      ['enabled', false],
      ['active', false],
      ['sessionId', ''],
      ['expiresAt', 0],
      ['signerMode', 'root'],
      ['policyRef', ''],
      ['consumedRWAD', 0],
      ['remainingRWAD', 500],
      ['reason', ''],
    ]);
  }

  function coldStartVrfChainStateValue() {
    return buildObjectValue([
      ['last_sequence', 0],
      ['last_manifest_hash', ''],
      ['last_vrf_output_hex', ''],
      ['updated_at_ms', 0],
    ]);
  }

  function normalizeDexMarketId(value) {
    const text = String(value || '').trim().toUpperCase().replace(/\//g, '-');
    return ['BTC-USDC', 'BTC-USDT', 'XAU-USDC', 'XAU-USDT'].includes(text) ? text : null;
  }

  function resolveTaobaoProductCount() {
    const csvPath = path.resolve(path.dirname(fullPath), '../../../taobao-detail-sku.csv');
    if (!fs.existsSync(csvPath)) {
      return null;
    }
    try {
      const rows = fs.readFileSync(csvPath, 'utf8').split(/\r?\n/);
      const seen = new Set();
      for (let index = 1; index < rows.length; index += 1) {
        const line = rows[index];
        if (!line || !line.trim()) continue;
        const match = line.match(/^"((?:[^"]|"")*)"|^([^,]+)/);
        const rawTitle = match ? (match[1] ?? match[2] ?? '') : '';
        const title = rawTitle.replace(/""/g, '"').trim();
        if (title) {
          seen.add(title);
        }
      }
      return seen.size > 0 ? seen.size : null;
    } catch {
      return null;
    }
  }

  function bindKnownPatternValue(pattern, value, targetKnownValues) {
    if (!pattern || value === UNKNOWN_VALUE) return false;
    if (ts.isIdentifier(pattern)) {
      targetKnownValues.set(pattern.text, value);
      return true;
    }
    if (ts.isObjectBindingPattern(pattern) && value && typeof value === 'object' && value.kind === 'object') {
      for (const element of pattern.elements) {
        if (!ts.isBindingElement(element) || element.dotDotDotToken) return false;
        const propName = element.propertyName
          ? (ts.isIdentifier(element.propertyName) || ts.isStringLiteralLike(element.propertyName) || ts.isNumericLiteral(element.propertyName)
            ? String(element.propertyName.text)
            : '')
          : (ts.isIdentifier(element.name) ? element.name.text : '');
        if (!propName || !value.props.has(propName)) return false;
        if (!bindKnownPatternValue(element.name, value.props.get(propName), targetKnownValues)) return false;
      }
      return true;
    }
    if (ts.isArrayBindingPattern(pattern) && value && typeof value === 'object' && value.kind === 'array' && Array.isArray(value.items)) {
      let logicalIndex = 0;
      for (const element of pattern.elements) {
        if (ts.isOmittedExpression(element)) {
          logicalIndex += 1;
          continue;
        }
        if (!ts.isBindingElement(element) || element.dotDotDotToken) return false;
        const current = logicalIndex < value.items.length ? value.items[logicalIndex] : UNKNOWN_VALUE;
        if (!bindKnownPatternValue(element.name, current, targetKnownValues)) return false;
        logicalIndex += 1;
      }
      return true;
    }
    return false;
  }

  function evaluateCallbackReturn(callback, callbackKnownValues, arrayLengths) {
    if (!callback || (!ts.isArrowFunction(callback) && !ts.isFunctionExpression(callback))) {
      return UNKNOWN_VALUE;
    }
    if (ts.isBlock(callback.body)) {
      return evaluateBlockReturnValue(callback.body, callbackKnownValues, arrayLengths);
    }
    return evaluateKnownValue(callback.body, callbackKnownValues, arrayLengths);
  }

  function evaluateBlockReturnValue(block, inheritedKnownValues = new Map(), arrayLengths = new Map()) {
    if (!block || !ts.isBlock(block)) return UNKNOWN_VALUE;
    const known = new Map(inheritedKnownValues);
    for (const statement of block.statements) {
      if (ts.isVariableStatement(statement)) {
        for (const decl of statement.declarationList.declarations) {
          if (!decl.initializer) continue;
          if (ts.isIdentifier(decl.name)) {
            const memoValue = evaluateUseMemoInitialValue(decl.initializer, known, arrayLengths);
            if (memoValue !== UNKNOWN_VALUE) {
              known.set(decl.name.text, memoValue);
              continue;
            }
            const value = evaluateKnownValue(decl.initializer, known, arrayLengths);
            if (value !== UNKNOWN_VALUE) {
              known.set(decl.name.text, value);
            }
            continue;
          }
          if (ts.isArrayBindingPattern(decl.name)) {
            const first = decl.name.elements[0];
            if (!first || !ts.isBindingElement(first) || !first.name || !ts.isIdentifier(first.name)) {
              continue;
            }
            const stateValue = evaluateUseStateInitialValue(decl.initializer, known, arrayLengths);
            if (stateValue !== UNKNOWN_VALUE) {
              known.set(first.name.text, stateValue);
            }
          }
        }
        continue;
      }
      if (ts.isReturnStatement(statement)) {
        return statement.expression ? evaluateKnownValue(statement.expression, known, arrayLengths) : null;
      }
      if (ts.isIfStatement(statement)) {
        const condition = truthinessOf(evaluateKnownValue(statement.expression, known, arrayLengths));
        if (condition === null) {
          return UNKNOWN_VALUE;
        }
        const activeBranch = condition ? statement.thenStatement : statement.elseStatement;
        if (!activeBranch) {
          continue;
        }
        if (ts.isBlock(activeBranch)) {
          const branchValue = evaluateBlockReturnValue(activeBranch, known, arrayLengths);
          if (branchValue !== UNKNOWN_VALUE) {
            return branchValue;
          }
          continue;
        }
        if (ts.isReturnStatement(activeBranch)) {
          return activeBranch.expression ? evaluateKnownValue(activeBranch.expression, known, arrayLengths) : null;
        }
        const branchValue = evaluateKnownValue(activeBranch, known, arrayLengths);
        if (branchValue !== UNKNOWN_VALUE) {
          return branchValue;
        }
        continue;
      }
    }
    return UNKNOWN_VALUE;
  }

  function truthinessOf(value) {
    if (value === UNKNOWN_VALUE) return null;
    if (value === null) return false;
    if (typeof value === 'boolean') return value;
    if (typeof value === 'number') return value !== 0;
    if (typeof value === 'string') return value.length > 0;
    if (value && typeof value === 'object' && value.kind === 'array') return true;
    if (value && typeof value === 'object' && value.kind === 'object') return true;
    if (value && typeof value === 'object' && value.kind === 'ref') return true;
    return null;
  }

  function evaluateKnownValue(node, knownValues = new Map(), arrayLengths = new Map()) {
    if (!node) return UNKNOWN_VALUE;
    if (ts.isParenthesizedExpression(node)) {
      return evaluateKnownValue(node.expression, knownValues, arrayLengths);
    }
    if (ts.isStringLiteralLike(node)) {
      return node.text;
    }
    if (ts.isNumericLiteral(node)) {
      return Number(node.text);
    }
    if (node.kind === ts.SyntaxKind.TrueKeyword) return true;
    if (node.kind === ts.SyntaxKind.FalseKeyword) return false;
    if (node.kind === ts.SyntaxKind.NullKeyword) return null;
    if (ts.isArrayLiteralExpression(node)) {
      const items = [];
      for (const element of node.elements) {
        if (ts.isSpreadElement(element)) {
          return arrayValue(node.elements.length);
        }
        items.push(evaluateKnownValue(element, knownValues, arrayLengths));
      }
      return arrayValue(node.elements.length, items);
    }
    if (ts.isObjectLiteralExpression(node)) {
      const props = new Map();
      for (const property of node.properties) {
        if (ts.isPropertyAssignment(property)) {
          let name = '';
          if (ts.isIdentifier(property.name) || ts.isStringLiteralLike(property.name) || ts.isNumericLiteral(property.name)) {
            name = String(property.name.text);
          }
          if (!name) return UNKNOWN_VALUE;
          props.set(name, evaluateKnownValue(property.initializer, knownValues, arrayLengths));
          continue;
        }
        if (ts.isShorthandPropertyAssignment(property)) {
          const name = String(property.name.text || '');
          if (!name) return UNKNOWN_VALUE;
          props.set(name, knownValues.has(name) ? knownValues.get(name) : UNKNOWN_VALUE);
          continue;
        }
        return UNKNOWN_VALUE;
      }
      return objectValue(props);
    }
    if (ts.isIdentifier(node)) {
      if (knownValues.has(node.text)) return knownValues.get(node.text);
      if (importedKnownValues.has(node.text)) return importedKnownValues.get(node.text);
      if (arrayLengths.has(node.text)) return arrayValue(arrayLengths.get(node.text));
      return UNKNOWN_VALUE;
    }
    if (ts.isPropertyAccessExpression(node)) {
      const base = evaluateKnownValue(node.expression, knownValues, arrayLengths);
      if (node.name.text === 'length') {
        if (typeof base === 'string') return base.length;
        if (base && typeof base === 'object' && base.kind === 'array') return base.length;
      }
      if (base && typeof base === 'object' && base.kind === 'object' && base.props.has(node.name.text)) {
        return base.props.get(node.name.text);
      }
      return UNKNOWN_VALUE;
    }
    if (ts.isElementAccessExpression(node)) {
      const base = evaluateKnownValue(node.expression, knownValues, arrayLengths);
      const index = evaluateKnownValue(node.argumentExpression, knownValues, arrayLengths);
      if (
        base
        && typeof base === 'object'
        && base.kind === 'array'
        && typeof index === 'number'
        && Number.isInteger(index)
        && index >= 0
        && index < base.length
        && Array.isArray(base.items)
      ) {
        return base.items[index];
      }
      if (base && typeof base === 'object' && base.kind === 'object' && typeof index === 'string' && base.props.has(index)) {
        return base.props.get(index);
      }
      return UNKNOWN_VALUE;
    }
    if (ts.isPrefixUnaryExpression(node) && node.operator === ts.SyntaxKind.ExclamationToken) {
      const inner = truthinessOf(evaluateKnownValue(node.operand, knownValues, arrayLengths));
      return inner === null ? UNKNOWN_VALUE : !inner;
    }
    if (ts.isConditionalExpression(node)) {
      const condition = truthinessOf(evaluateKnownValue(node.condition, knownValues, arrayLengths));
      if (condition === null) return UNKNOWN_VALUE;
      return evaluateKnownValue(condition ? node.whenTrue : node.whenFalse, knownValues, arrayLengths);
    }
    if (ts.isCallExpression(node)) {
      const name = callName(ts, node.expression);
      if (ts.isPropertyAccessExpression(node.expression)) {
        const base = evaluateKnownValue(node.expression.expression, knownValues, arrayLengths);
        const memberName = node.expression.name.text;
        if (typeof base === 'string') {
          if (memberName === 'trim') return base.trim();
          if (memberName === 'toLowerCase') return base.toLowerCase();
          if (memberName === 'toUpperCase') return base.toUpperCase();
          if (memberName === 'includes' && node.arguments.length >= 1) {
            const needle = evaluateKnownValue(node.arguments[0], knownValues, arrayLengths);
            return typeof needle === 'string' ? base.includes(needle) : UNKNOWN_VALUE;
          }
          if (memberName === 'startsWith' && node.arguments.length >= 1) {
            const needle = evaluateKnownValue(node.arguments[0], knownValues, arrayLengths);
            return typeof needle === 'string' ? base.startsWith(needle) : UNKNOWN_VALUE;
          }
          if (memberName === 'endsWith' && node.arguments.length >= 1) {
            const needle = evaluateKnownValue(node.arguments[0], knownValues, arrayLengths);
            return typeof needle === 'string' ? base.endsWith(needle) : UNKNOWN_VALUE;
          }
        }
        if (base && typeof base === 'object' && base.kind === 'array') {
          const callback = node.arguments[0];
          if (memberName === 'filter' && (ts.isArrowFunction(callback) || ts.isFunctionExpression(callback))) {
            const items = Array.isArray(base.items) ? base.items : null;
            if (!items) {
              return arrayValue(base.length);
            }
            const nextItems = [];
            for (let index = 0; index < items.length; index += 1) {
              const callbackKnownValues = new Map(knownValues);
              if (callback.parameters.length >= 1 && !bindKnownPatternValue(callback.parameters[0].name, items[index], callbackKnownValues)) {
                return arrayValue(base.length);
              }
              if (callback.parameters.length >= 2 && !bindKnownPatternValue(callback.parameters[1].name, index, callbackKnownValues)) {
                return arrayValue(base.length);
              }
              const keep = truthinessOf(evaluateCallbackReturn(callback, callbackKnownValues, arrayLengths));
              if (keep === null) {
                return arrayValue(base.length);
              }
              if (keep) {
                nextItems.push(items[index]);
              }
            }
            return arrayValue(nextItems.length, nextItems);
          }
          if (memberName === 'map' && (ts.isArrowFunction(callback) || ts.isFunctionExpression(callback))) {
            const items = Array.isArray(base.items) ? base.items : null;
            if (!items) {
              return arrayValue(base.length);
            }
            const nextItems = [];
            for (let index = 0; index < items.length; index += 1) {
              const callbackKnownValues = new Map(knownValues);
              if (callback.parameters.length >= 1 && !bindKnownPatternValue(callback.parameters[0].name, items[index], callbackKnownValues)) {
                return arrayValue(base.length);
              }
              if (callback.parameters.length >= 2 && !bindKnownPatternValue(callback.parameters[1].name, index, callbackKnownValues)) {
                return arrayValue(base.length);
              }
              nextItems.push(evaluateCallbackReturn(callback, callbackKnownValues, arrayLengths));
            }
            return arrayValue(nextItems.length, nextItems);
          }
          if (memberName === 'find' && (ts.isArrowFunction(callback) || ts.isFunctionExpression(callback))) {
            const items = Array.isArray(base.items) ? base.items : null;
            if (!items) {
              return UNKNOWN_VALUE;
            }
            for (let index = 0; index < items.length; index += 1) {
              const callbackKnownValues = new Map(knownValues);
              if (callback.parameters.length >= 1 && !bindKnownPatternValue(callback.parameters[0].name, items[index], callbackKnownValues)) {
                return UNKNOWN_VALUE;
              }
              if (callback.parameters.length >= 2 && !bindKnownPatternValue(callback.parameters[1].name, index, callbackKnownValues)) {
                return UNKNOWN_VALUE;
              }
              const keep = truthinessOf(evaluateCallbackReturn(callback, callbackKnownValues, arrayLengths));
              if (keep === null) {
                return UNKNOWN_VALUE;
              }
              if (keep) {
                return items[index];
              }
            }
            return null;
          }
          if (memberName === 'some' && (ts.isArrowFunction(callback) || ts.isFunctionExpression(callback))) {
            const items = Array.isArray(base.items) ? base.items : null;
            if (!items) {
              return UNKNOWN_VALUE;
            }
            for (let index = 0; index < items.length; index += 1) {
              const callbackKnownValues = new Map(knownValues);
              if (callback.parameters.length >= 1 && !bindKnownPatternValue(callback.parameters[0].name, items[index], callbackKnownValues)) {
                return UNKNOWN_VALUE;
              }
              if (callback.parameters.length >= 2 && !bindKnownPatternValue(callback.parameters[1].name, index, callbackKnownValues)) {
                return UNKNOWN_VALUE;
              }
              const keep = truthinessOf(evaluateCallbackReturn(callback, callbackKnownValues, arrayLengths));
              if (keep === null) {
                return UNKNOWN_VALUE;
              }
              if (keep) {
                return true;
              }
            }
            return false;
          }
          if (memberName === 'includes' && node.arguments.length >= 1) {
            const needle = evaluateKnownValue(node.arguments[0], knownValues, arrayLengths);
            if (!Array.isArray(base.items)) {
              return UNKNOWN_VALUE;
            }
            return base.items.some((item) => item !== UNKNOWN_VALUE && item === needle);
          }
          if (memberName === 'slice') {
            const startValue = evaluateKnownValue(node.arguments[0], knownValues, arrayLengths);
            const endValue = evaluateKnownValue(node.arguments[1], knownValues, arrayLengths);
            const start = typeof startValue === 'number' && Number.isInteger(startValue) ? startValue : 0;
            const end = typeof endValue === 'number' && Number.isInteger(endValue) ? endValue : base.length;
            if (!Array.isArray(base.items)) {
              return arrayValue(Math.max(0, end - start));
            }
            return arrayValue(Math.max(0, end - start), base.items.slice(start, end));
          }
        }
      }
      if (name === 'Boolean') {
        const inner = truthinessOf(evaluateKnownValue(node.arguments[0], knownValues, arrayLengths));
        return inner === null ? UNKNOWN_VALUE : inner;
      }
      if (name === 'getUpdateSnapshot') {
        return coldStartUpdateSnapshotValue();
      }
      if (name === 'getDexSnapshot') {
        return coldStartDexSnapshotValue();
      }
      if (name === 'getDexAsiSessionState') {
        return coldStartDexAsiSessionStateValue();
      }
      if (name === 'getAllProducts') {
        const count = resolveTaobaoProductCount();
        return count === null ? UNKNOWN_VALUE : arrayValue(count);
      }
      if (name === 'getVrfChainState') {
        return coldStartVrfChainStateValue();
      }
      if (name === 'canShowPublisherZone') {
        return false;
      }
      if (name === 'loadWallets') {
        return arrayValue(0, []);
      }
      if (name === 'readNodeRemarks') {
        return objectValue(new Map());
      }
      if (name === 'resolveDexMarketId') {
        const argValue = evaluateKnownValue(node.arguments[0], knownValues, arrayLengths);
        return typeof argValue === 'string' ? (normalizeDexMarketId(argValue) ?? null) : UNKNOWN_VALUE;
      }
      if (name === 'hasVerifiedVersionUpdate') {
        const snapshot = evaluateKnownValue(node.arguments[0], knownValues, arrayLengths);
        if (snapshot && typeof snapshot === 'object' && snapshot.kind === 'object') {
          const latestManifestVerified = snapshot.props.has('latest_manifest_verified')
            ? truthinessOf(snapshot.props.get('latest_manifest_verified'))
            : null;
          const latestVersion = snapshot.props.has('latest_version') ? snapshot.props.get('latest_version') : UNKNOWN_VALUE;
          if (latestManifestVerified === false) return false;
          if (typeof latestVersion === 'string' && latestVersion.trim().length === 0) return false;
        }
        return UNKNOWN_VALUE;
      }
    }
    if (ts.isBinaryExpression(node)) {
      const left = evaluateKnownValue(node.left, knownValues, arrayLengths);
      const right = evaluateKnownValue(node.right, knownValues, arrayLengths);
      switch (node.operatorToken.kind) {
        case ts.SyntaxKind.BarBarToken: {
          const leftTruth = truthinessOf(left);
          const rightTruth = truthinessOf(right);
          if (leftTruth === null || rightTruth === null) return UNKNOWN_VALUE;
          return leftTruth || rightTruth;
        }
        case ts.SyntaxKind.AmpersandAmpersandToken: {
          const leftTruth = truthinessOf(left);
          const rightTruth = truthinessOf(right);
          if (leftTruth === null || rightTruth === null) return UNKNOWN_VALUE;
          return leftTruth && rightTruth;
        }
        case ts.SyntaxKind.QuestionQuestionToken:
          if (left !== UNKNOWN_VALUE && left !== null) return left;
          if (left === null) return right;
          return UNKNOWN_VALUE;
        case ts.SyntaxKind.EqualsEqualsEqualsToken:
        case ts.SyntaxKind.EqualsEqualsToken:
          if (left === UNKNOWN_VALUE || right === UNKNOWN_VALUE) return UNKNOWN_VALUE;
          return left === right;
        case ts.SyntaxKind.ExclamationEqualsEqualsToken:
        case ts.SyntaxKind.ExclamationEqualsToken:
          if (left === UNKNOWN_VALUE || right === UNKNOWN_VALUE) return UNKNOWN_VALUE;
          return left !== right;
        case ts.SyntaxKind.GreaterThanToken:
          if (typeof left === 'number' && typeof right === 'number') return left > right;
          return UNKNOWN_VALUE;
        case ts.SyntaxKind.GreaterThanEqualsToken:
          if (typeof left === 'number' && typeof right === 'number') return left >= right;
          return UNKNOWN_VALUE;
        case ts.SyntaxKind.LessThanToken:
          if (typeof left === 'number' && typeof right === 'number') return left < right;
          return UNKNOWN_VALUE;
        case ts.SyntaxKind.LessThanEqualsToken:
          if (typeof left === 'number' && typeof right === 'number') return left <= right;
          return UNKNOWN_VALUE;
        default:
          return UNKNOWN_VALUE;
      }
    }
    return UNKNOWN_VALUE;
  }

  function evaluateUseStateInitialValue(node, knownValues = new Map(), arrayLengths = new Map()) {
    if (!node || !ts.isCallExpression(node)) return UNKNOWN_VALUE;
    if (callName(ts, node.expression) !== 'useState') return UNKNOWN_VALUE;
    const initialArg = node.arguments[0];
    if (!initialArg) return UNKNOWN_VALUE;
    if ((ts.isArrowFunction(initialArg) || ts.isFunctionExpression(initialArg)) && initialArg.parameters.length === 0) {
      if (ts.isBlock(initialArg.body)) {
        return evaluateBlockReturnValue(initialArg.body, knownValues, arrayLengths);
      }
      return evaluateKnownValue(initialArg.body, knownValues, arrayLengths);
    }
    return evaluateKnownValue(initialArg, knownValues, arrayLengths);
  }

  function evaluateUseMemoInitialValue(node, knownValues = new Map(), arrayLengths = new Map()) {
    if (!node || !ts.isCallExpression(node)) return UNKNOWN_VALUE;
    if (callName(ts, node.expression) !== 'useMemo') return UNKNOWN_VALUE;
    const initialArg = node.arguments[0];
    if (!initialArg) return UNKNOWN_VALUE;
    if ((ts.isArrowFunction(initialArg) || ts.isFunctionExpression(initialArg)) && initialArg.parameters.length === 0) {
      if (ts.isBlock(initialArg.body)) {
        return evaluateBlockReturnValue(initialArg.body, knownValues, arrayLengths);
      }
      return evaluateKnownValue(initialArg.body, knownValues, arrayLengths);
    }
    return UNKNOWN_VALUE;
  }

  function collectKnownValues(rootNode, arrayLengths = new Map(), inheritedKnownValues = new Map()) {
    const known = new Map(inheritedKnownValues);
    const statements = rootNode && Array.isArray(rootNode.statements) ? rootNode.statements : null;
    if (!statements) {
      return known;
    }
    for (const statement of statements) {
      if (!ts.isVariableStatement(statement)) continue;
      for (const decl of statement.declarationList.declarations) {
        if (!decl.initializer) continue;
        if (ts.isIdentifier(decl.name)) {
          const memoValue = evaluateUseMemoInitialValue(decl.initializer, known, arrayLengths);
          if (memoValue !== UNKNOWN_VALUE) {
            known.set(decl.name.text, memoValue);
            continue;
          }
          const value = evaluateKnownValue(decl.initializer, known, arrayLengths);
          if (value !== UNKNOWN_VALUE) {
            known.set(decl.name.text, value);
            continue;
          }
        }
        if (ts.isArrayBindingPattern(decl.name)) {
          const first = decl.name.elements[0];
          if (!first || !ts.isBindingElement(first) || !first.name || !ts.isIdentifier(first.name)) {
            continue;
          }
          const value = evaluateUseStateInitialValue(decl.initializer, known, arrayLengths);
          if (value !== UNKNOWN_VALUE) {
            known.set(first.name.text, value);
          }
        }
      }
    }
    return known;
  }

  function resolveRelativeModulePath(fromPath, specifier) {
    if (!specifier.startsWith('.')) {
      return '';
    }
    const base = path.resolve(path.dirname(fromPath), specifier);
    const candidates = [
      base,
      `${base}.ts`,
      `${base}.tsx`,
      `${base}.js`,
      `${base}.jsx`,
      `${base}.mjs`,
      `${base}.cjs`,
      path.join(base, 'index.ts'),
      path.join(base, 'index.tsx'),
      path.join(base, 'index.js'),
      path.join(base, 'index.jsx'),
      path.join(base, 'index.mjs'),
      path.join(base, 'index.cjs'),
    ];
    for (const candidate of candidates) {
      if (fs.existsSync(candidate) && fs.statSync(candidate).isFile()) {
        return candidate;
      }
    }
    return '';
  }

  function loadRelativeExportValue(fromPath, specifier, exportName) {
    const targetPath = resolveRelativeModulePath(fromPath, specifier);
    if (!targetPath) {
      return UNKNOWN_VALUE;
    }
    const targetExt = path.extname(targetPath).toLowerCase();
    const targetSourceFile = ts.createSourceFile(
      targetPath,
      fs.readFileSync(targetPath, 'utf8'),
      ts.ScriptTarget.Latest,
      true,
      scriptKindFor(ts, targetExt),
    );
    const targetImports = new Map();
    const targetDecls = new Map();
    const targetExports = new Map();
    let targetDefaultExpr = null;

    for (const statement of targetSourceFile.statements) {
      if (ts.isImportDeclaration(statement) && statement.moduleSpecifier && ts.isStringLiteralLike(statement.moduleSpecifier)) {
        const importSpecifier = statement.moduleSpecifier.text;
        const importClause = statement.importClause;
        if (importClause?.name && ts.isIdentifier(importClause.name) && isUpperName(importClause.name.text)) {
          targetImports.set(importClause.name.text, refValue(importClause.name.text));
        }
        const namedBindings = importClause?.namedBindings;
        if (namedBindings && ts.isNamedImports(namedBindings)) {
          for (const element of namedBindings.elements) {
            const localName = String(element.name?.text || '');
            const remoteName = element.propertyName && ts.isIdentifier(element.propertyName) ? element.propertyName.text : localName;
            if (localName && isUpperName(remoteName || localName)) {
              targetImports.set(localName, refValue(remoteName || localName));
            }
          }
        }
        continue;
      }
      if (ts.isVariableStatement(statement)) {
        const exported = Boolean(statement.modifiers?.some((item) => item.kind === ts.SyntaxKind.ExportKeyword));
        for (const decl of statement.declarationList.declarations) {
          if (!ts.isIdentifier(decl.name) || !decl.initializer) continue;
          targetDecls.set(decl.name.text, decl.initializer);
          if (exported) {
            targetExports.set(decl.name.text, decl.name.text);
          }
        }
        continue;
      }
      if (ts.isExportAssignment(statement)) {
        targetDefaultExpr = statement.expression || null;
        continue;
      }
      if (ts.isExportDeclaration(statement) && statement.exportClause && ts.isNamedExports(statement.exportClause)) {
        for (const element of statement.exportClause.elements) {
          const localName = element.propertyName && ts.isIdentifier(element.propertyName) ? element.propertyName.text : element.name.text;
          targetExports.set(element.name.text, localName);
        }
      }
    }

    const staticKnown = new Map();
    const evalTarget = (node) => {
      if (!node) return UNKNOWN_VALUE;
      if (ts.isParenthesizedExpression(node)) return evalTarget(node.expression);
      if (ts.isStringLiteralLike(node)) return node.text;
      if (ts.isNumericLiteral(node)) return Number(node.text);
      if (node.kind === ts.SyntaxKind.TrueKeyword) return true;
      if (node.kind === ts.SyntaxKind.FalseKeyword) return false;
      if (node.kind === ts.SyntaxKind.NullKeyword) return null;
      if (ts.isArrayLiteralExpression(node)) {
        return arrayValue(node.elements.length, node.elements.map((element) => (
          ts.isSpreadElement(element) ? UNKNOWN_VALUE : evalTarget(element)
        )));
      }
      if (ts.isObjectLiteralExpression(node)) {
        const props = new Map();
        for (const property of node.properties) {
          if (ts.isPropertyAssignment(property)) {
            const name = ts.isIdentifier(property.name) || ts.isStringLiteralLike(property.name) || ts.isNumericLiteral(property.name)
              ? String(property.name.text)
              : '';
            if (!name) return UNKNOWN_VALUE;
            props.set(name, evalTarget(property.initializer));
            continue;
          }
          if (ts.isShorthandPropertyAssignment(property)) {
            const name = String(property.name.text || '');
            if (!name) return UNKNOWN_VALUE;
            props.set(name, staticKnown.has(name) ? staticKnown.get(name) : (targetImports.has(name) ? targetImports.get(name) : UNKNOWN_VALUE));
            continue;
          }
          return UNKNOWN_VALUE;
        }
        return objectValue(props);
      }
      if (ts.isIdentifier(node)) {
        if (staticKnown.has(node.text)) return staticKnown.get(node.text);
        if (targetImports.has(node.text)) return targetImports.get(node.text);
        if (targetDecls.has(node.text)) {
          const value = evalTarget(targetDecls.get(node.text));
          staticKnown.set(node.text, value);
          return value;
        }
        return UNKNOWN_VALUE;
      }
      if (ts.isPropertyAccessExpression(node)) {
        const base = evalTarget(node.expression);
        if (node.name.text === 'length') {
          if (typeof base === 'string') return base.length;
          if (base && typeof base === 'object' && base.kind === 'array') return base.length;
        }
        if (base && typeof base === 'object' && base.kind === 'object' && base.props.has(node.name.text)) {
          return base.props.get(node.name.text);
        }
        return UNKNOWN_VALUE;
      }
      if (ts.isElementAccessExpression(node)) {
        const base = evalTarget(node.expression);
        const index = evalTarget(node.argumentExpression);
        if (
          base
          && typeof base === 'object'
          && base.kind === 'array'
          && typeof index === 'number'
          && Number.isInteger(index)
          && index >= 0
          && Array.isArray(base.items)
          && index < base.items.length
        ) {
          return base.items[index];
        }
        return UNKNOWN_VALUE;
      }
      return UNKNOWN_VALUE;
    };

    if (exportName === 'default' && targetDefaultExpr) {
      return evalTarget(targetDefaultExpr);
    }
    const targetName = targetExports.get(exportName) || exportName;
    if (!targetDecls.has(targetName)) {
      return UNKNOWN_VALUE;
    }
    return evalTarget(targetDecls.get(targetName));
  }

  function countDirectRenderSurface(rootNode, arrayLengths = new Map(), knownValues = new Map(), pruneBranches = false) {
    const counts = {
      total: 0,
      intrinsic: 0,
      component: 0,
      componentRefs: new Map(),
      fragment: 0,
      expanded: 0,
      expandedIntrinsic: 0,
      expandedComponentRefs: new Map(),
    };
    if (!rootNode) {
      return counts;
    }
    function resolveStaticArrayLength(expr) {
      if (!expr) return null;
      if (ts.isArrayLiteralExpression(expr)) {
        return expr.elements.length;
      }
      if (ts.isIdentifier(expr) && arrayLengths.has(expr.text)) {
        return arrayLengths.get(expr.text) ?? null;
      }
      return null;
    }
    function bindPatternValue(pattern, value, targetKnownValues) {
      if (!pattern || value === UNKNOWN_VALUE) return false;
      if (ts.isIdentifier(pattern)) {
        targetKnownValues.set(pattern.text, value);
        return true;
      }
      if (ts.isObjectBindingPattern(pattern) && value && typeof value === 'object' && value.kind === 'object') {
        for (const element of pattern.elements) {
          if (!ts.isBindingElement(element) || element.dotDotDotToken) return false;
          const propName = element.propertyName
            ? (ts.isIdentifier(element.propertyName) || ts.isStringLiteralLike(element.propertyName) || ts.isNumericLiteral(element.propertyName)
              ? String(element.propertyName.text)
              : '')
            : (ts.isIdentifier(element.name) ? element.name.text : '');
          if (!propName || !value.props.has(propName)) return false;
          if (!bindPatternValue(element.name, value.props.get(propName), targetKnownValues)) return false;
        }
        return true;
      }
      if (ts.isArrayBindingPattern(pattern) && value && typeof value === 'object' && value.kind === 'array' && Array.isArray(value.items)) {
        let logicalIndex = 0;
        for (const element of pattern.elements) {
          if (ts.isOmittedExpression(element)) {
            logicalIndex += 1;
            continue;
          }
          if (!ts.isBindingElement(element) || element.dotDotDotToken) return false;
          const current = logicalIndex < value.items.length ? value.items[logicalIndex] : UNKNOWN_VALUE;
          if (!bindPatternValue(element.name, current, targetKnownValues)) return false;
          logicalIndex += 1;
        }
        return true;
      }
      return false;
    }
    function resolveStaticArrayValue(expr) {
      if (!expr) return UNKNOWN_VALUE;
      return evaluateKnownValue(expr, knownValues, arrayLengths);
    }
    function countMapCallbackSurface(callback, callbackKnownValues = knownValues) {
      if (!callback || (!ts.isArrowFunction(callback) && !ts.isFunctionExpression(callback))) {
        return null;
      }
      if (ts.isBlock(callback.body)) {
        const nextKnownValues = collectKnownValues(callback.body, arrayLengths, callbackKnownValues);
        const surfaces = collectDirectReturnSurfaces(callback.body, arrayLengths, nextKnownValues, pruneBranches);
        if (surfaces.length !== 1) {
          return null;
        }
        return {
          expanded: Number(surfaces[0].surface_expanded_jsx_count ?? surfaces[0].surface_jsx_count ?? 0),
          expandedIntrinsic: Number(
            surfaces[0].surface_expanded_intrinsic_jsx_count
            ?? surfaces[0].surface_intrinsic_jsx_count
            ?? 0,
          ),
          expandedComponentRefs: new Map(
            Array.isArray(surfaces[0].expanded_component_tag_refs)
              ? surfaces[0].expanded_component_tag_refs.map((item) => [String(item.name || ''), Number(item.count || 0)])
              : [],
          ),
        };
      }
      const nested = countDirectRenderSurface(callback.body, arrayLengths, callbackKnownValues, pruneBranches);
      if (nested.expanded <= 0) {
        return null;
      }
      return {
        expanded: nested.expanded,
        expandedIntrinsic: nested.expandedIntrinsic,
        expandedComponentRefs: new Map(nested.expandedComponentRefs),
      };
    }
    function countStaticMapExpansion(node) {
      if (!ts.isJsxExpression(node) || !node.expression || !ts.isCallExpression(node.expression)) {
        return { expanded: 0, expandedIntrinsic: 0, expandedComponentRefs: new Map() };
      }
      const expr = node.expression;
      if (!ts.isPropertyAccessExpression(expr.expression) || expr.expression.name.text !== 'map') {
        return { expanded: 0, expandedIntrinsic: 0, expandedComponentRefs: new Map() };
      }
      const arrayValueDoc = resolveStaticArrayValue(expr.expression.expression);
      const arrayLen = arrayValueDoc && typeof arrayValueDoc === 'object' && arrayValueDoc.kind === 'array'
        ? arrayValueDoc.length
        : resolveStaticArrayLength(expr.expression.expression);
      if (arrayLen === null || arrayLen <= 0) {
        return { expanded: 0, expandedIntrinsic: 0, expandedComponentRefs: new Map() };
      }
      const callback = expr.arguments[0];
      const items = arrayValueDoc && typeof arrayValueDoc === 'object' && arrayValueDoc.kind === 'array' && Array.isArray(arrayValueDoc.items)
        ? arrayValueDoc.items
        : null;
      if (items && items.length === arrayLen && (ts.isArrowFunction(callback) || ts.isFunctionExpression(callback))) {
        const refs = new Map();
        let expanded = 0;
        let expandedIntrinsic = 0;
        let ok = true;
        for (let index = 0; index < items.length; index += 1) {
          const callbackKnownValues = new Map(knownValues);
          if (callback.parameters.length >= 1 && !bindPatternValue(callback.parameters[0].name, items[index], callbackKnownValues)) {
            ok = false;
            break;
          }
          if (callback.parameters.length >= 2 && !bindPatternValue(callback.parameters[1].name, index, callbackKnownValues)) {
            ok = false;
            break;
          }
          const callbackSurface = countMapCallbackSurface(callback, callbackKnownValues);
          if (!callbackSurface || callbackSurface.expanded <= 0) {
            ok = false;
            break;
          }
          expanded += callbackSurface.expanded;
          expandedIntrinsic += callbackSurface.expandedIntrinsic;
          mergeRefsInto(refs, callbackSurface.expandedComponentRefs);
        }
        if (ok && expanded > 0) {
          return {
            expanded,
            expandedIntrinsic,
            expandedComponentRefs: refs,
          };
        }
      }
      const callbackSurface = countMapCallbackSurface(callback);
      if (!callbackSurface || callbackSurface.expanded <= 0) {
        return { expanded: 0, expandedIntrinsic: 0, expandedComponentRefs: new Map() };
      }
      const refs = new Map();
      mergeRefsInto(refs, callbackSurface.expandedComponentRefs, arrayLen);
      return {
        expanded: arrayLen * callbackSurface.expanded,
        expandedIntrinsic: arrayLen * callbackSurface.expandedIntrinsic,
        expandedComponentRefs: refs,
      };
    }
    function walk(node) {
      if (node !== rootNode && (ts.isFunctionLike(node) || ts.isClassLike(node))) {
        return;
      }
      if (ts.isJsxOpeningElement(node) || ts.isJsxSelfClosingElement(node)) {
        const tag = jsxTagName(ts, node.tagName, knownValues);
        counts.total += 1;
        if (/^[a-z]/.test(tag)) {
          counts.intrinsic += 1;
        } else {
          counts.component += 1;
          counts.componentRefs.set(tag, (counts.componentRefs.get(tag) || 0) + 1);
        }
        counts.expanded += 1;
        if (/^[a-z]/.test(tag)) {
          counts.expandedIntrinsic += 1;
        } else {
          counts.expandedComponentRefs.set(tag, (counts.expandedComponentRefs.get(tag) || 0) + 1);
        }
      } else if (ts.isJsxFragment(node)) {
        counts.total += 1;
        counts.fragment += 1;
        counts.expanded += 1;
      } else if (ts.isJsxExpression(node)) {
        if (pruneBranches && node.expression && ts.isConditionalExpression(node.expression)) {
          const condition = evaluateKnownValue(node.expression.condition, knownValues, arrayLengths);
          if (condition !== UNKNOWN_VALUE) {
            walk(condition ? node.expression.whenTrue : node.expression.whenFalse);
            return;
          }
        }
        if (pruneBranches && node.expression && ts.isBinaryExpression(node.expression)) {
          const operator = node.expression.operatorToken.kind;
          if (operator === ts.SyntaxKind.AmpersandAmpersandToken) {
            const leftTruth = truthinessOf(evaluateKnownValue(node.expression.left, knownValues, arrayLengths));
            if (leftTruth === false) {
              return;
            }
            if (leftTruth === true) {
              walk(node.expression.right);
              return;
            }
          }
          if (operator === ts.SyntaxKind.BarBarToken) {
            const leftTruth = truthinessOf(evaluateKnownValue(node.expression.left, knownValues, arrayLengths));
            if (leftTruth === false) {
              walk(node.expression.right);
              return;
            }
            if (leftTruth === true) {
              walk(node.expression.left);
              return;
            }
          }
        }
        const expansion = countStaticMapExpansion(node);
        counts.expanded += expansion.expanded;
        counts.expandedIntrinsic += expansion.expandedIntrinsic;
        mergeRefsInto(counts.expandedComponentRefs, expansion.expandedComponentRefs);
      }
      ts.forEachChild(node, walk);
    }
    walk(rootNode);
    return counts;
  }

  function collectDirectReturnSurfaces(rootNode, arrayLengths = new Map(), knownValues = new Map(), pruneBranches = false) {
    const entries = [];
    if (!rootNode) {
      return entries;
    }
    if (!ts.isBlock(rootNode)) {
      const counts = countDirectRenderSurface(rootNode, arrayLengths, knownValues, pruneBranches);
      if (counts.total > 0) {
        const pos = lineColOf(ts, sourceFile, rootNode);
        entries.push({
          line: pos.line,
          surface_jsx_count: counts.total,
          surface_intrinsic_jsx_count: counts.intrinsic,
          surface_component_tag_count: counts.component,
          component_tag_refs: refsMapToArray(counts.componentRefs),
          expanded_component_tag_refs: refsMapToArray(counts.expandedComponentRefs),
          surface_fragment_count: counts.fragment,
          surface_expanded_jsx_count: counts.expanded,
          surface_expanded_intrinsic_jsx_count: counts.expandedIntrinsic,
        });
      }
      return entries;
    }
    function walk(node) {
      if (node !== rootNode && (ts.isFunctionLike(node) || ts.isClassLike(node))) {
        return;
      }
      if (ts.isReturnStatement(node) && node.expression) {
        const counts = countDirectRenderSurface(node.expression, arrayLengths, knownValues, pruneBranches);
        if (counts.total > 0) {
          const pos = lineColOf(ts, sourceFile, node);
          entries.push({
            line: pos.line,
            surface_jsx_count: counts.total,
            surface_intrinsic_jsx_count: counts.intrinsic,
            surface_component_tag_count: counts.component,
            component_tag_refs: refsMapToArray(counts.componentRefs),
            expanded_component_tag_refs: refsMapToArray(counts.expandedComponentRefs),
            surface_fragment_count: counts.fragment,
            surface_expanded_jsx_count: counts.expanded,
            surface_expanded_intrinsic_jsx_count: counts.expandedIntrinsic,
          });
        }
      }
      ts.forEachChild(node, walk);
    }
    walk(rootNode);
    return entries;
  }

  const moduleStaticArrayLengths = collectStaticArrayLengths(sourceFile);
  const moduleKnownValues = collectKnownValues(sourceFile, moduleStaticArrayLengths);

  for (const statement of sourceFile.statements) {
    if (ts.canHaveModifiers(statement)) {
      const modifiers = ts.getModifiers(statement) || [];
      if (modifiers.some((item) => item.kind === ts.SyntaxKind.ExportKeyword)) {
        if (statement.name && ts.isIdentifier(statement.name)) {
          exportedNames.add(statement.name.text);
        }
      }
      if (modifiers.some((item) => item.kind === ts.SyntaxKind.DefaultKeyword)) {
        exports.push({
          kind: 'default',
          name: statement.name && ts.isIdentifier(statement.name) ? statement.name.text : 'default',
          line: lineColOf(ts, sourceFile, statement).line,
        });
      }
    }
    if (ts.isExportDeclaration(statement) && statement.moduleSpecifier && ts.isStringLiteralLike(statement.moduleSpecifier)) {
      const specifier = statement.moduleSpecifier.text;
      imports.push({ kind: 'export_from', specifier, line: lineColOf(ts, sourceFile, statement).line });
      staticImports.push(specifier);
      exports.push({
        kind: statement.exportClause ? 'named' : 'all',
        name: '*',
        line: lineColOf(ts, sourceFile, statement).line,
      });
    }
    if (ts.isImportDeclaration(statement) && statement.moduleSpecifier && ts.isStringLiteralLike(statement.moduleSpecifier)) {
      const specifier = statement.moduleSpecifier.text;
      imports.push({
        kind: statement.importClause?.isTypeOnly ? 'type' : 'static',
        specifier,
        line: lineColOf(ts, sourceFile, statement).line,
      });
      staticImports.push(specifier);
      const feature = importFeature(specifier);
      if (feature) countFeature(feature);
      const importClause = statement.importClause;
      const isRelative = specifier.startsWith('.');
      if (importClause?.name && ts.isIdentifier(importClause.name)) {
        const localName = importClause.name.text;
        const importedValue = isRelative
          ? loadRelativeExportValue(fullPath, specifier, 'default')
          : (isUpperName(localName) ? refValue(localName) : UNKNOWN_VALUE);
        if (importedValue !== UNKNOWN_VALUE) {
          importedKnownValues.set(localName, importedValue);
        } else if (isUpperName(localName)) {
          importedKnownValues.set(localName, refValue(localName));
        }
      }
      const namedBindings = importClause?.namedBindings;
      if (namedBindings && ts.isNamedImports(namedBindings)) {
        for (const element of namedBindings.elements) {
          const localName = String(element.name?.text || '');
          const remoteName = element.propertyName && ts.isIdentifier(element.propertyName) ? element.propertyName.text : localName;
          if (!localName) continue;
          const importedValue = isRelative
            ? loadRelativeExportValue(fullPath, specifier, remoteName)
            : (isUpperName(remoteName || localName) ? refValue(remoteName || localName) : UNKNOWN_VALUE);
          if (importedValue !== UNKNOWN_VALUE) {
            importedKnownValues.set(localName, importedValue);
          } else if (isUpperName(remoteName || localName)) {
            importedKnownValues.set(localName, refValue(remoteName || localName));
          }
        }
      }
    }
    if (ts.isFunctionDeclaration(statement) && statement.name) {
      addComponent(statement.name.text, 'function', statement.name, {
        async: Boolean(statement.modifiers?.some((item) => item.kind === ts.SyntaxKind.AsyncKeyword)),
        returnsJsx: hasJsxReturn(ts, statement),
      });
      const pos = lineColOf(ts, sourceFile, statement.name);
      const key = `function:${statement.name.text}:${pos.line}`;
      const entry = componentEntries.get(key);
      if (entry) {
        const localArrayLengths = collectStaticArrayLengths(statement.body);
        const mergedLengths = mergeArrayLengths(moduleStaticArrayLengths, localArrayLengths);
        const knownValues = collectKnownValues(statement.body, mergedLengths, moduleKnownValues);
        const counts = countDirectRenderSurface(statement.body, mergedLengths, knownValues, false);
        const liveCounts = countDirectRenderSurface(statement.body, mergedLengths, knownValues, true);
        const rawReturns = collectDirectReturnSurfaces(statement.body, mergedLengths, knownValues, false);
        const liveReturns = collectDirectReturnSurfaces(statement.body, mergedLengths, knownValues, true);
        entry.surface_jsx_count = counts.total;
        entry.surface_intrinsic_jsx_count = counts.intrinsic;
        entry.surface_live_jsx_count = liveCounts.total;
        entry.surface_live_intrinsic_jsx_count = liveCounts.intrinsic;
        entry.surface_component_tag_count = counts.component;
        entry.component_tag_refs = refsMapToArray(counts.componentRefs);
        entry.live_component_tag_refs = refsMapToArray(liveCounts.componentRefs);
        entry.expanded_component_tag_refs = refsMapToArray(counts.expandedComponentRefs);
        entry.live_expanded_component_tag_refs = refsMapToArray(liveCounts.expandedComponentRefs);
        entry.surface_fragment_count = counts.fragment;
        entry.surface_expanded_jsx_count = counts.expanded;
        entry.surface_expanded_intrinsic_jsx_count = counts.expandedIntrinsic;
        entry.surface_live_expanded_jsx_count = liveCounts.expanded;
        entry.surface_live_expanded_intrinsic_jsx_count = liveCounts.expandedIntrinsic;
        entry.return_surfaces = rawReturns.map((surface, index) => {
          const liveSurface = liveReturns.find((item) => item.line === surface.line) || liveReturns[index] || null;
          return {
            ...surface,
            surface_live_jsx_count: Number(liveSurface?.surface_jsx_count ?? surface.surface_jsx_count ?? 0),
            surface_live_intrinsic_jsx_count: Number(liveSurface?.surface_intrinsic_jsx_count ?? surface.surface_intrinsic_jsx_count ?? 0),
            surface_live_expanded_jsx_count: Number(liveSurface?.surface_expanded_jsx_count ?? surface.surface_expanded_jsx_count ?? 0),
            surface_live_expanded_intrinsic_jsx_count: Number(
              liveSurface?.surface_expanded_intrinsic_jsx_count
              ?? surface.surface_expanded_intrinsic_jsx_count
              ?? 0
            ),
            live_component_tag_refs: Array.isArray(liveSurface?.component_tag_refs) ? liveSurface.component_tag_refs : [],
            live_expanded_component_tag_refs: Array.isArray(liveSurface?.expanded_component_tag_refs) ? liveSurface.expanded_component_tag_refs : [],
          };
        });
      }
    }
    if (ts.isClassDeclaration(statement) && statement.name) {
      let isClassComponent = false;
      if (statement.heritageClauses) {
        for (const clause of statement.heritageClauses) {
          for (const type of clause.types) {
            const baseName = callName(ts, type.expression);
            if (baseName === 'Component') {
              isClassComponent = true;
              countFeature('class_component');
            }
          }
        }
      }
      let isErrorBoundary = false;
      for (const member of statement.members) {
        if (
          ts.isMethodDeclaration(member)
          && member.name
          && ts.isIdentifier(member.name)
          && member.name.text === 'componentDidCatch'
        ) {
          isErrorBoundary = true;
        }
        if (
          ts.isMethodDeclaration(member)
          && member.modifiers?.some((item) => item.kind === ts.SyntaxKind.StaticKeyword)
          && member.name
          && ts.isIdentifier(member.name)
          && member.name.text === 'getDerivedStateFromError'
        ) {
          isErrorBoundary = true;
        }
      }
      if (isErrorBoundary) countFeature('error_boundary');
      addComponent(statement.name.text, isClassComponent ? 'class' : 'class', statement.name, {
        async: false,
        returnsJsx: hasJsxReturn(ts, statement),
        errorBoundary: isErrorBoundary,
      });
      const pos = lineColOf(ts, sourceFile, statement.name);
      const key = `class:${statement.name.text}:${pos.line}`;
      const entry = componentEntries.get(key);
      if (entry) {
        const renderMethod = statement.members.find((member) =>
          ts.isMethodDeclaration(member)
          && member.name
          && ts.isIdentifier(member.name)
          && member.name.text === 'render'
        );
        const localArrayLengths = collectStaticArrayLengths(renderMethod?.body || null);
        const mergedLengths = mergeArrayLengths(moduleStaticArrayLengths, localArrayLengths);
        const knownValues = collectKnownValues(renderMethod?.body || null, mergedLengths, moduleKnownValues);
        const counts = countDirectRenderSurface(renderMethod?.body || null, mergedLengths, knownValues, false);
        const liveCounts = countDirectRenderSurface(renderMethod?.body || null, mergedLengths, knownValues, true);
        const rawReturns = collectDirectReturnSurfaces(renderMethod?.body || null, mergedLengths, knownValues, false);
        const liveReturns = collectDirectReturnSurfaces(renderMethod?.body || null, mergedLengths, knownValues, true);
        entry.surface_jsx_count = counts.total;
        entry.surface_intrinsic_jsx_count = counts.intrinsic;
        entry.surface_live_jsx_count = liveCounts.total;
        entry.surface_live_intrinsic_jsx_count = liveCounts.intrinsic;
        entry.surface_component_tag_count = counts.component;
        entry.component_tag_refs = refsMapToArray(counts.componentRefs);
        entry.live_component_tag_refs = refsMapToArray(liveCounts.componentRefs);
        entry.expanded_component_tag_refs = refsMapToArray(counts.expandedComponentRefs);
        entry.live_expanded_component_tag_refs = refsMapToArray(liveCounts.expandedComponentRefs);
        entry.surface_fragment_count = counts.fragment;
        entry.surface_expanded_jsx_count = counts.expanded;
        entry.surface_expanded_intrinsic_jsx_count = counts.expandedIntrinsic;
        entry.surface_live_expanded_jsx_count = liveCounts.expanded;
        entry.surface_live_expanded_intrinsic_jsx_count = liveCounts.expandedIntrinsic;
        entry.return_surfaces = rawReturns.map((surface, index) => {
          const liveSurface = liveReturns.find((item) => item.line === surface.line) || liveReturns[index] || null;
          return {
            ...surface,
            surface_live_jsx_count: Number(liveSurface?.surface_jsx_count ?? surface.surface_jsx_count ?? 0),
            surface_live_intrinsic_jsx_count: Number(liveSurface?.surface_intrinsic_jsx_count ?? surface.surface_intrinsic_jsx_count ?? 0),
            surface_live_expanded_jsx_count: Number(liveSurface?.surface_expanded_jsx_count ?? surface.surface_expanded_jsx_count ?? 0),
            surface_live_expanded_intrinsic_jsx_count: Number(
              liveSurface?.surface_expanded_intrinsic_jsx_count
              ?? surface.surface_expanded_intrinsic_jsx_count
              ?? 0
            ),
            live_component_tag_refs: Array.isArray(liveSurface?.component_tag_refs) ? liveSurface.component_tag_refs : [],
            live_expanded_component_tag_refs: Array.isArray(liveSurface?.expanded_component_tag_refs) ? liveSurface.expanded_component_tag_refs : [],
          };
        });
      }
    }
    if (ts.isVariableStatement(statement)) {
      const exported = Boolean(statement.modifiers?.some((item) => item.kind === ts.SyntaxKind.ExportKeyword));
      for (const decl of statement.declarationList.declarations) {
        if (!decl.name || !ts.isIdentifier(decl.name) || !decl.initializer) continue;
        const name = decl.name.text;
        if (exported) exportedNames.add(name);
        if (ts.isArrowFunction(decl.initializer) || ts.isFunctionExpression(decl.initializer)) {
          addComponent(name, 'function', decl.name, {
            async: Boolean(decl.initializer.modifiers?.some((item) => item.kind === ts.SyntaxKind.AsyncKeyword)),
            returnsJsx: hasJsxReturn(ts, decl.initializer.body),
          });
          const pos = lineColOf(ts, sourceFile, decl.name);
          const key = `function:${name}:${pos.line}`;
          const entry = componentEntries.get(key);
          if (entry) {
            const localArrayLengths = collectStaticArrayLengths(decl.initializer.body);
            const mergedLengths = mergeArrayLengths(moduleStaticArrayLengths, localArrayLengths);
            const knownValues = collectKnownValues(decl.initializer.body, mergedLengths, moduleKnownValues);
            const counts = countDirectRenderSurface(decl.initializer.body, mergedLengths, knownValues, false);
            const liveCounts = countDirectRenderSurface(decl.initializer.body, mergedLengths, knownValues, true);
            const rawReturns = collectDirectReturnSurfaces(decl.initializer.body, mergedLengths, knownValues, false);
            const liveReturns = collectDirectReturnSurfaces(decl.initializer.body, mergedLengths, knownValues, true);
            entry.surface_jsx_count = counts.total;
            entry.surface_intrinsic_jsx_count = counts.intrinsic;
            entry.surface_live_jsx_count = liveCounts.total;
            entry.surface_live_intrinsic_jsx_count = liveCounts.intrinsic;
            entry.surface_component_tag_count = counts.component;
            entry.component_tag_refs = refsMapToArray(counts.componentRefs);
            entry.live_component_tag_refs = refsMapToArray(liveCounts.componentRefs);
            entry.expanded_component_tag_refs = refsMapToArray(counts.expandedComponentRefs);
            entry.live_expanded_component_tag_refs = refsMapToArray(liveCounts.expandedComponentRefs);
            entry.surface_fragment_count = counts.fragment;
            entry.surface_expanded_jsx_count = counts.expanded;
            entry.surface_expanded_intrinsic_jsx_count = counts.expandedIntrinsic;
            entry.surface_live_expanded_jsx_count = liveCounts.expanded;
            entry.surface_live_expanded_intrinsic_jsx_count = liveCounts.expandedIntrinsic;
            entry.return_surfaces = rawReturns.map((surface, index) => {
              const liveSurface = liveReturns.find((item) => item.line === surface.line) || liveReturns[index] || null;
              return {
                ...surface,
                surface_live_jsx_count: Number(liveSurface?.surface_jsx_count ?? surface.surface_jsx_count ?? 0),
                surface_live_intrinsic_jsx_count: Number(liveSurface?.surface_intrinsic_jsx_count ?? surface.surface_intrinsic_jsx_count ?? 0),
                surface_live_expanded_jsx_count: Number(liveSurface?.surface_expanded_jsx_count ?? surface.surface_expanded_jsx_count ?? 0),
                surface_live_expanded_intrinsic_jsx_count: Number(
                  liveSurface?.surface_expanded_intrinsic_jsx_count
                  ?? surface.surface_expanded_intrinsic_jsx_count
                  ?? 0
                ),
                live_component_tag_refs: Array.isArray(liveSurface?.component_tag_refs) ? liveSurface.component_tag_refs : [],
                live_expanded_component_tag_refs: Array.isArray(liveSurface?.expanded_component_tag_refs) ? liveSurface.expanded_component_tag_refs : [],
              };
            });
          }
        }
      }
    }
  }

  function visit(node) {
    maybeAddBlocker(node);

    if (ts.isCallExpression(node)) {
      if (node.expression.kind === ts.SyntaxKind.ImportKeyword && node.arguments.length > 0 && ts.isStringLiteralLike(node.arguments[0])) {
        const specifier = node.arguments[0].text;
        imports.push({ kind: 'dynamic', specifier, line: lineColOf(ts, sourceFile, node).line });
        dynamicImports.push(specifier);
      }
      const name = callName(ts, node.expression);
      const feature = CALL_FEATURES.get(name);
      if (feature) {
        countFeature(feature);
        hookCalls.push({
          name,
          line: lineColOf(ts, sourceFile, node).line,
          column: lineColOf(ts, sourceFile, node).column,
        });
      }
      if (name === 'dispatchEvent') {
        countFeature('custom_event');
      }
      if (
        ts.isPropertyAccessExpression(node.expression)
        && ts.isPropertyAccessExpression(node.expression.expression)
        && ts.isIdentifier(node.expression.expression.expression)
        && node.expression.expression.expression.text === 'navigator'
        && node.expression.expression.name.text === 'mediaDevices'
        && node.expression.name.text === 'getUserMedia'
      ) {
        countFeature('media');
      }
    }

    if (ts.isIdentifier(node)) {
      const feature = IDENTIFIER_FEATURES.get(node.text);
      if (feature) {
        countFeature(feature);
      }
    }

    if (
      ts.isPropertyAccessExpression(node)
      && ts.isIdentifier(node.expression)
      && node.expression.text === 'navigator'
      && node.name.text === 'mediaDevices'
    ) {
      countFeature('media');
    }

    if (ts.isJsxOpeningElement(node) || ts.isJsxSelfClosingElement(node)) {
      const tag = jsxTagName(ts, node.tagName);
      jsxElements.push({
        tag,
        line: lineColOf(ts, sourceFile, node).line,
      });
      if (tag === 'Suspense') {
        countFeature('suspense');
      }
      if (tag === 'StrictMode') {
        countFeature('strict_mode');
      }
      for (const attr of node.attributes.properties) {
        if (!ts.isJsxAttribute(attr) || !attr.name || attr.name.text !== 'className' || !attr.initializer) continue;
        if (ts.isStringLiteral(attr.initializer)) {
          classTokens.push(...attr.initializer.text.split(/\s+/).filter(Boolean));
        } else if (
          ts.isJsxExpression(attr.initializer)
          && attr.initializer.expression
          && ts.isNoSubstitutionTemplateLiteral(attr.initializer.expression)
        ) {
          classTokens.push(...attr.initializer.expression.text.split(/\s+/).filter(Boolean));
        }
      }
    }

    if (ts.isJsxFragment(node)) {
      jsxElements.push({
        tag: 'Fragment',
        line: lineColOf(ts, sourceFile, node).line,
      });
    }

    ts.forEachChild(node, visit);
  }

  visit(sourceFile);

  const features = Object.fromEntries(Object.entries(featureCounts).map(([name, count]) => [name, count > 0]));
  blockers.sort((a, b) => a.file.localeCompare(b.file) || a.line - b.line || a.column - b.column || a.code.localeCompare(b.code));
  components.sort((a, b) => a.line - b.line || a.name.localeCompare(b.name));
  imports.sort((a, b) => a.line - b.line || a.kind.localeCompare(b.kind) || a.specifier.localeCompare(b.specifier));
  hookCalls.sort((a, b) => a.line - b.line || a.column - b.column || a.name.localeCompare(b.name));
  jsxElements.sort((a, b) => a.line - b.line || a.tag.localeCompare(b.tag));
  const uniqueStaticImports = [...new Set(staticImports)].sort();
  const uniqueDynamicImports = [...new Set(dynamicImports)].sort();
  const assetRefs = [...new Set(imports.map((item) => item.specifier).filter((item) => ASSET_EXTS.has(path.extname(item).toLowerCase())))].sort();

  return {
    path: relPath,
    hash: sha256(text),
    ext,
    runtime_root: sourceRootForPath(relPath, runtimeRoots),
    generated: isGeneratedFile(relPath),
    jsx_like: ext === '.tsx' || ext === '.jsx' || jsxElements.length > 0,
    static_imports: uniqueStaticImports,
    dynamic_imports: uniqueDynamicImports,
    asset_refs: assetRefs,
    features,
    feature_counts: featureCounts,
    components,
    blockers,
    imports,
    hook_calls: hookCalls,
    jsx_elements: jsxElements,
    tailwind_class_tokens: [...new Set(classTokens)].sort(),
    tailwind_class_token_count: classTokens.length,
    export_count: exports.length,
  };
}

let tsGlobal = null;

function main() {
  const args = parseArgs(process.argv);
  const repo = path.resolve(args.repo);
  const runtimeRoots = normalizeRoots(repo, args.runtimeRoots);
  const ts = loadTypeScript(repo);
  tsGlobal = ts;
  const modules = walkSources(repo, runtimeRoots).map((fullPath) => scanModule(ts, repo, runtimeRoots, fullPath));
  const out = {
    format: 'tsx_ast_v1',
    repo_root: repo,
    source_roots: runtimeRoots,
    typescript_version: ts.version,
    modules,
  };
  if (args.outPath) {
    const outPath = path.resolve(args.outPath);
    fs.mkdirSync(path.dirname(outPath), { recursive: true });
    fs.writeFileSync(outPath, `${JSON.stringify(out, null, 2)}\n`, 'utf8');
    writeSurfaceFeed(path.dirname(outPath), repo, runtimeRoots, ts.version, modules);
  }
  writeSidecarSummary(args.summaryOut ? path.resolve(args.summaryOut) : '', {
    format: out.format,
    repo_root: repo,
    source_roots: runtimeRoots.join(','),
    typescript_version: ts.version,
    module_count: modules.length,
    out_path: args.outPath ? path.resolve(args.outPath) : '',
  });
  process.stdout.write(`${JSON.stringify(out)}\n`);
}

main();
