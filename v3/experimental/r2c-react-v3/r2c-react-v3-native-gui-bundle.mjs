#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { createRequire } from 'node:module';
import { resolvePrimaryRouteSurface } from './r2c-react-v3-route-matrix-shared.mjs';
import {
  renderNativeGuiRuntimeCheng,
  renderNativeGuiRuntimeMainCheng,
} from './r2c-react-v3-native-gui-runtime-shared.mjs';

const NATIVE_GUI_BUNDLE_HEAP_GUARD_ENV = 'R2C_REACT_V3_NATIVE_GUI_BUNDLE_HEAP_GUARD';
const DEFAULT_NATIVE_GUI_BUNDLE_MAX_OLD_SPACE_MB = 768;
const DEFAULT_LAYOUT_SURFACE_NODE_LIMIT = 256;
const DEFAULT_LAYOUT_SURFACE_COMPONENT_EXPANSION_LIMIT = 64;
const DEFAULT_LAYOUT_SURFACE_MODULE_PARSE_LIMIT = 64;
const DEFAULT_LAYOUT_SURFACE_MAP_EXPANSION_LIMIT = 24;
const DEFAULT_LAYOUT_SURFACE_MAX_SOURCE_CHARS = 2 * 1024 * 1024;
const DEFAULT_LAYOUT_SURFACE_MAX_RSS_MB = 768;
const NATIVE_GUI_RUNTIME_CONTROLLER_TIMEOUT_MS = 120000;
const UNKNOWN_STATIC_VALUE = Symbol('r2c_react_v3_unknown_static_value');
const STATIC_NO_RETURN = Symbol('r2c_react_v3_static_no_return');
const JSX_RESOLUTION_CONTINUE = Symbol('r2c_react_v3_jsx_resolution_continue');
const JSX_RESOLUTION_NONE = Symbol('r2c_react_v3_jsx_resolution_none');

function parseArgs(argv) {
  const out = {
    repo: '',
    outDir: '',
    outPath: '',
    summaryOut: '',
    codegenManifestPath: '',
    execSnapshotPath: '',
    hostContractPath: '',
    assetManifestPath: '',
    tailwindManifestPath: '',
    routeCatalogPath: '',
    tsxAstPath: '',
    routeState: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--codegen-manifest') out.codegenManifestPath = String(argv[++i] || '');
    else if (arg === '--exec-snapshot') out.execSnapshotPath = String(argv[++i] || '');
    else if (arg === '--host-contract') out.hostContractPath = String(argv[++i] || '');
    else if (arg === '--asset-manifest') out.assetManifestPath = String(argv[++i] || '');
    else if (arg === '--tailwind-manifest') out.tailwindManifestPath = String(argv[++i] || '');
    else if (arg === '--route-catalog') out.routeCatalogPath = String(argv[++i] || '');
    else if (arg === '--tsx-ast') out.tsxAstPath = String(argv[++i] || '');
    else if (arg === '--route-state') out.routeState = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-native-gui-bundle.mjs --repo <path> [--out-dir <dir>] [--out <file>] [--summary-out <file>] [--codegen-manifest <file>] [--exec-snapshot <file>] [--host-contract <file>] [--asset-manifest <file>] [--tailwind-manifest <file>] [--route-catalog <file>] [--tsx-ast <file>] [--route-state <id>]');
      process.exit(0);
    }
  }
  if (!out.repo) {
    throw new Error('missing --repo');
  }
  return out;
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

function readJsonIfExists(filePath) {
  if (!filePath || !fs.existsSync(filePath)) return null;
  return readJson(filePath);
}

function uniqueStrings(values) {
  const seen = new Set();
  const out = [];
  for (const raw of values || []) {
    const value = String(raw || '').trim();
    if (!value || seen.has(value)) continue;
    seen.add(value);
    out.push(value);
  }
  return out;
}

function enrichHostContractFromCompileReport(hostContract, outDir) {
  const compileReportPath = path.resolve(path.join(outDir, 'compile_report_v1.json'));
  const compileReport = readJsonIfExists(compileReportPath);
  if (!compileReport) return hostContract;
  const featureTotals = compileReport && typeof compileReport.feature_totals === 'object' && compileReport.feature_totals
    ? compileReport.feature_totals
    : {};
  const featureHits = uniqueStrings([
    ...(Array.isArray(hostContract?.feature_hits) ? hostContract.feature_hits : []),
    ...Object.entries(featureTotals)
      .filter(([, value]) => Number(value || 0) > 0)
      .map(([key]) => key),
  ]);
  const methodGroups = Array.isArray(hostContract?.methods)
    ? hostContract.methods.filter((item) => item && item.required).map((item) => String(item.group || ''))
    : [];
  const requiredGroups = uniqueStrings([
    ...(Array.isArray(hostContract?.required_groups) ? hostContract.required_groups : []),
    ...(Array.isArray(compileReport?.required_host_groups) ? compileReport.required_host_groups : []),
    ...methodGroups,
  ]);
  return {
    ...hostContract,
    feature_hits: featureHits,
    required_groups: requiredGroups,
  };
}

function shQuote(text) {
  return `'${String(text ?? '').replace(/'/g, `'\"'\"'`)}'`;
}

function requireFormat(doc, expected, label) {
  const actual = String(doc?.format || '');
  if (actual !== expected) {
    throw new Error(`unexpected ${label} format: ${actual || '<empty>'}`);
  }
}

function requireFile(filePath, label) {
  if (!fs.existsSync(filePath)) {
    throw new Error(`missing ${label}: ${filePath}`);
  }
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

function readPositiveIntEnv(name, fallback) {
  const raw = String(process.env[name] || '').trim();
  if (!raw) return fallback;
  const value = Number.parseInt(raw, 10);
  if (!Number.isFinite(value) || value <= 0) return fallback;
  return value;
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
    maxBuffer: 64 * 1024 * 1024,
    env: options.env || process.env,
    cwd: options.cwd || process.cwd(),
  });
}

function ensureNodeHeapLimit() {
  if (process.env[NATIVE_GUI_BUNDLE_HEAP_GUARD_ENV] === '1') return;
  const envNodeOptions = String(process.env.NODE_OPTIONS || '').trim();
  const hasHeapLimit = [...(process.execArgv || []), ...envNodeOptions.split(/\s+/).filter(Boolean)]
    .some((arg) => String(arg).startsWith('--max-old-space-size='));
  if (hasHeapLimit) return;
  const maxOldSpaceMb = readPositiveIntEnv('R2C_REACT_V3_NATIVE_GUI_BUNDLE_MAX_OLD_SPACE_MB', DEFAULT_NATIVE_GUI_BUNDLE_MAX_OLD_SPACE_MB);
  const result = spawnForExec(process.execPath, [
    `--max-old-space-size=${maxOldSpaceMb}`,
    process.argv[1],
    ...process.argv.slice(2),
  ], {
    env: {
      ...process.env,
      [NATIVE_GUI_BUNDLE_HEAP_GUARD_ENV]: '1',
    },
  });
  if (typeof result.stdout === 'string' && result.stdout.length > 0) process.stdout.write(result.stdout);
  if (typeof result.stderr === 'string' && result.stderr.length > 0) process.stderr.write(result.stderr);
  if (result.error) throw result.error;
  process.exit(exitCodeOf(result));
}

function resolveDefaultToolingBin(workspaceRoot) {
  const explicit = String(process.env.R2C_REACT_V3_TOOLING_BIN || '').trim();
  if (explicit) return explicit;
  const stage3 = path.join(workspaceRoot, 'artifacts', 'v3_bootstrap', 'cheng.stage3');
  if (fs.existsSync(stage3)) return stage3;
  return path.join(workspaceRoot, 'artifacts', 'v3_backend_driver', 'cheng');
}

function pathEntryExists(filePath) {
  try {
    fs.lstatSync(filePath);
    return true;
  } catch {
    return false;
  }
}

function ensurePackageCompileSupport(workspaceRoot, packageRoot) {
  const v3Link = path.join(packageRoot, 'v3');
  const runtimeRoot = path.join(packageRoot, 'src', 'runtime');
  const nativeLink = path.join(runtimeRoot, 'native');
  const stdLink = path.join(packageRoot, 'src', 'std');
  fs.mkdirSync(runtimeRoot, { recursive: true });
  if (!pathEntryExists(v3Link)) {
    fs.symlinkSync(path.join(workspaceRoot, 'v3'), v3Link);
  }
  if (!pathEntryExists(nativeLink)) {
    fs.symlinkSync(path.join(workspaceRoot, 'src', 'runtime', 'native'), nativeLink);
  }
  if (!pathEntryExists(stdLink)) {
    fs.symlinkSync(path.join(workspaceRoot, 'src', 'std'), stdLink);
  }
}

function chengStr(text) {
  return JSON.stringify(String(text || ''));
}

function resolveModulePath(codegenManifest, importPath) {
  const packageRoot = String(codegenManifest.package_root || '');
  const packageImportPrefix = String(codegenManifest.package_import_prefix || '');
  if (!packageRoot || !packageImportPrefix) {
    throw new Error('codegen manifest missing package_root or package_import_prefix');
  }
  if (!importPath.startsWith(`${packageImportPrefix}/`)) {
    throw new Error(`module is outside generated package: ${importPath}`);
  }
  const rel = importPath.slice(packageImportPrefix.length + 1);
  return path.join(packageRoot, 'src', `${rel}.cheng`);
}

function generatedPackageRootReady(packageRoot) {
  const requiredEntries = [
    path.join(packageRoot, 'src', 'runtime.cheng'),
    path.join(packageRoot, 'src', 'ui_host.cheng'),
    path.join(packageRoot, 'src', 'main.cheng'),
    path.join(packageRoot, 'src', 'project.cheng'),
    path.join(packageRoot, 'src', 'exec_bundle.cheng'),
  ];
  return requiredEntries.every((entryPath) => pathEntryExists(entryPath));
}

function normalizeCodegenManifestPaths(codegenManifest, outDir, options = {}) {
  const packageImportPrefix = String(codegenManifest?.package_import_prefix || '').trim();
  if (!packageImportPrefix) {
    throw new Error('codegen manifest missing package_import_prefix');
  }
  const preferOutDirArtifacts = options.preferOutDirArtifacts !== false;
  const declaredPackageRootRaw = String(codegenManifest?.package_root || '').trim();
  const declaredPackageRoot = declaredPackageRootRaw ? path.resolve(declaredPackageRootRaw) : '';
  const expectedPackageRoot = path.resolve(path.join(outDir, 'cheng_codegen'));
  const packageRoot = preferOutDirArtifacts && generatedPackageRootReady(expectedPackageRoot)
    ? expectedPackageRoot
    : declaredPackageRoot;
  if (!packageRoot) {
    throw new Error('codegen manifest missing package_root');
  }
  const declaredRouteCatalogRaw = String(codegenManifest?.route_catalog_path || '').trim();
  const declaredRouteCatalogPath = declaredRouteCatalogRaw ? path.resolve(declaredRouteCatalogRaw) : '';
  const expectedRouteCatalogPath = path.resolve(path.join(outDir, 'cheng_codegen_route_catalog_v1.json'));
  const routeCatalogPath = preferOutDirArtifacts && pathEntryExists(expectedRouteCatalogPath)
    ? expectedRouteCatalogPath
    : declaredRouteCatalogPath;
  return {
    ...codegenManifest,
    package_root: packageRoot,
    route_catalog_path: routeCatalogPath,
  };
}

function countAssetKinds(entries) {
  const counts = new Map();
  for (const entry of entries) {
    const kind = String(entry?.kind || 'other');
    counts.set(kind, (counts.get(kind) || 0) + 1);
  }
  return Object.fromEntries([...counts.entries()].sort(([a], [b]) => a.localeCompare(b)));
}

function parseJsonStdout(stdoutText, expectedFormat, label) {
  const trimmed = String(stdoutText || '').trim();
  if (!trimmed) throw new Error(`${label}_stdout_empty`);
  const doc = decodeHexEncodedStrings(JSON.parse(trimmed));
  if (String(doc?.format || '') !== expectedFormat) {
    throw new Error(`${label}_format_mismatch:${String(doc?.format || '')}`);
  }
  return doc;
}

function looksLikeHexEncodedString(value) {
  return typeof value === 'string' && value.length > 0 && value.length % 2 === 0 && /^[0-9a-f]+$/i.test(value);
}

function decodeHexText(value) {
  try {
    return Buffer.from(String(value || ''), 'hex').toString('utf8');
  } catch {
    return String(value || '');
  }
}

function decodeHexEncodedStrings(value) {
  if (Array.isArray(value)) {
    return value.map((item) => decodeHexEncodedStrings(item));
  }
  if (value && typeof value === 'object') {
    return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, decodeHexEncodedStrings(item)]));
  }
  if (looksLikeHexEncodedString(value)) {
    return decodeHexText(value);
  }
  return value;
}

function parseKeyValueStdout(stdoutText, label) {
  const trimmed = String(stdoutText || '').trim();
  if (!trimmed) throw new Error(`${label}_stdout_empty`);
  const out = {};
  for (const line of trimmed.split(/\r?\n/)) {
    const text = line.trim();
    if (!text) continue;
    const pivot = text.indexOf('=');
    if (pivot <= 0) continue;
    out[text.slice(0, pivot)] = text.slice(pivot + 1);
  }
  return out;
}

function parseIntField(fields, key, fallback = 0) {
  const raw = String(fields?.[key] || '').trim();
  if (!raw) return fallback;
  const value = Number.parseInt(raw, 10);
  return Number.isFinite(value) ? value : fallback;
}

function parseBoolField(fields, key, fallback = false) {
  const raw = String(fields?.[key] || '').trim();
  if (raw === 'true') return true;
  if (raw === 'false') return false;
  return fallback;
}

function buildNativeGuiTheme() {
  return {
    background_top: '#f2f7fd',
    background_bottom: '#e6eefb',
    panel_background: '#ffffff',
    panel_shadow: '#cddaf0',
    accent_color: '#3b82f6',
    accent_soft: '#dbeafe',
    border_color: '#d5e2f0',
    text_primary: '#102a43',
    text_secondary: '#334e68',
    text_muted: '#6b7f95',
    text_soft: '#486581',
    chip_background: '#eef4fb',
    branch_background: '#fff7d6',
    branch_border: '#f2d675',
    component_background: '#e8f1ff',
    component_border: '#bfd7f6',
    text_background: '#f8fafc',
    input_background: '#ffffff',
    input_border: '#9fb3c8',
    button_background: '#dbeafe',
    button_border: '#60a5fa',
  };
}

function buildMetricStyle(theme) {
  return {
    background_color: '#ffffff',
    border_color: theme.border_color,
    text_color: theme.text_primary,
    font_size: 15,
    font_weight: 'regular',
    corner_radius: 14,
  };
}

function normalizeInlineText(value, maxLen = 80) {
  const normalized = String(value || '').replace(/\s+/g, ' ').trim();
  if (!normalized) return '';
  if (normalized.length <= maxLen) return normalized;
  const cutoff = Math.max(1, maxLen - 3);
  return `${normalized.slice(0, cutoff)}...`;
}

function lineOfTsNode(ts, sourceFile, node) {
  if (!node || !sourceFile) return 0;
  return sourceFile.getLineAndCharacterOfPosition(node.getStart(sourceFile)).line + 1;
}

function loadTypeScriptForRepo(repo) {
  const packageJsonPath = path.join(repo, 'package.json');
  if (!fs.existsSync(packageJsonPath)) {
    throw new Error(`missing repo package.json: ${packageJsonPath}`);
  }
  const requireFromRepo = createRequire(packageJsonPath);
  return requireFromRepo('typescript');
}

function findModuleDoc(tsxAstDoc, modulePath) {
  const modules = Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules : [];
  return modules.find((item) => String(item?.path || '') === String(modulePath || '')) || null;
}

function buildUniqueComponentRegistry(tsxAstDoc) {
  const seen = new Map();
  const duplicates = new Set();
  const modules = Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules : [];
  for (const module of modules) {
    const modulePath = String(module?.path || '');
    const components = Array.isArray(module?.components) ? module.components : [];
    for (const component of components) {
      const componentName = String(component?.name || '').trim();
      if (!componentName) continue;
      if (seen.has(componentName)) {
        duplicates.add(componentName);
        continue;
      }
      seen.set(componentName, {
        modulePath,
        componentName,
      });
    }
  }
  for (const duplicate of duplicates) {
    seen.delete(duplicate);
  }
  return seen;
}

function buildRouteStaticEvalEnv(modulePath, componentName, routeState) {
  const env = new Map();
  const moduleText = String(modulePath || '').trim();
  const componentText = String(componentName || '').trim();
  const routeText = String(routeState || '').trim();
  if (!routeText) return env;
  if (moduleText === 'app/App.tsx' && componentText === 'AppContent') {
    env.set('__r2cTruthRouteState', routeText);
    env.set('__r2cTruthMode', true);
    env.set('__r2cSmokeMode', false);
    if (routeText === 'content_detail') {
      const truthContentFixture = markStaticObjectCompleteness({
        id: 'truth-content',
        type: 'image',
        publishCategory: 'content',
        userId: '12D3KooWDode1D7fBs5zNLvKW97hVyMxpGRnusNC',
        userName: 'UniMaker 节点',
        avatar: '',
        content: '通过 cheng-libp2p 发布的示例内容，用于原生 Android 与 React 真值截图对齐。',
        media: 'data:image/svg+xml;charset=UTF-8,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20width%3D%22960%22%20height%3D%221280%22%20viewBox%3D%220%200%20960%201280%22%3E%3Crect%20width%3D%22960%22%20height%3D%221280%22%20fill%3D%22%238B5CF6%22%2F%3E%3Ctext%20x%3D%2296%22%20y%3D%221040%22%20fill%3D%22%23ffffff%22%20font-size%3D%2288%22%3EUniMaker%20Content%3C%2Ftext%3E%3C%2Fsvg%3E',
        coverMedia: 'data:image/svg+xml;charset=UTF-8,%3Csvg%20xmlns%3D%22http%3A%2F%2Fwww.w3.org%2F2000%2Fsvg%22%20width%3D%22960%22%20height%3D%221280%22%20viewBox%3D%220%200%20960%201280%22%3E%3Crect%20width%3D%22960%22%20height%3D%221280%22%20fill%3D%22%238B5CF6%22%2F%3E%3Ctext%20x%3D%2296%22%20y%3D%221040%22%20fill%3D%22%23ffffff%22%20font-size%3D%2288%22%3EUniMaker%20Content%3C%2Ftext%3E%3C%2Fsvg%3E',
        likes: 128,
        comments: 32,
        timestamp: 0,
      }, true);
      env.set('__r2cTruthContentFixture', truthContentFixture);
    }
  }
  return env;
}

function currentRssBytes() {
  const usage = typeof process.memoryUsage === 'function' ? process.memoryUsage() : null;
  return Number(usage?.rss || 0);
}

function createLayoutSurfaceBudgetState() {
  return {
    maxNodes: readPositiveIntEnv('R2C_REACT_V3_LAYOUT_SURFACE_MAX_NODES', DEFAULT_LAYOUT_SURFACE_NODE_LIMIT),
    maxComponentExpansions: readPositiveIntEnv('R2C_REACT_V3_LAYOUT_SURFACE_MAX_COMPONENT_EXPANSIONS', DEFAULT_LAYOUT_SURFACE_COMPONENT_EXPANSION_LIMIT),
    maxParsedModules: readPositiveIntEnv('R2C_REACT_V3_LAYOUT_SURFACE_MAX_PARSED_MODULES', DEFAULT_LAYOUT_SURFACE_MODULE_PARSE_LIMIT),
    maxSourceChars: readPositiveIntEnv('R2C_REACT_V3_LAYOUT_SURFACE_MAX_SOURCE_CHARS', DEFAULT_LAYOUT_SURFACE_MAX_SOURCE_CHARS),
    maxRssBytes: readPositiveIntEnv('R2C_REACT_V3_LAYOUT_SURFACE_MAX_RSS_MB', DEFAULT_LAYOUT_SURFACE_MAX_RSS_MB) * 1024 * 1024,
    nextId: 0,
    truncated: false,
    componentExpansionCount: 0,
    parsedModuleCount: 0,
  };
}

function assertLayoutSurfaceBudget(state, phase) {
  const rssBytes = currentRssBytes();
  if (rssBytes > state.maxRssBytes) {
    throw new Error(`native_gui_layout_surface_memory_budget_exceeded:${phase}:${rssBytes}`);
  }
}

function pickPreferredComponent(moduleDoc) {
  const components = Array.isArray(moduleDoc?.components) ? moduleDoc.components : [];
  if (components.length === 0) return null;
  const scoreOf = (component) => ([
    Number(component?.surface_live_expanded_jsx_count || 0),
    Number(component?.surface_live_jsx_count || 0),
    Number(component?.surface_expanded_jsx_count || 0),
    Number(component?.surface_jsx_count || 0),
    component?.exported ? 1 : 0,
  ]);
  const compareScore = (left, right) => {
    const a = scoreOf(left);
    const b = scoreOf(right);
    for (let index = 0; index < a.length; index += 1) {
      if (a[index] !== b[index]) return b[index] - a[index];
    }
    return String(left?.name || '').localeCompare(String(right?.name || ''));
  };
  const ordered = [...components].sort(compareScore);
  return ordered[0] || null;
}

function parseSourceModule(ts, repo, modulePath, cache, budgetState) {
  const key = String(modulePath || '').trim();
  if (!key) return null;
  if (cache.has(key)) return cache.get(key);
  assertLayoutSurfaceBudget(budgetState, 'parse_module_before');
  if (budgetState.parsedModuleCount >= budgetState.maxParsedModules) {
    throw new Error(`native_gui_layout_surface_module_budget_exceeded:${budgetState.maxParsedModules}`);
  }
  const fullPath = path.join(repo, key);
  if (!fs.existsSync(fullPath)) {
    cache.set(key, null);
    return null;
  }
  const sourceText = fs.readFileSync(fullPath, 'utf8');
  if (sourceText.length > budgetState.maxSourceChars) {
    throw new Error(`native_gui_layout_surface_source_too_large:${key}:${sourceText.length}`);
  }
  const scriptKind = key.endsWith('.tsx') || key.endsWith('.jsx')
    ? ts.ScriptKind.TSX
    : ts.ScriptKind.TS;
  const sourceFile = ts.createSourceFile(fullPath, sourceText, ts.ScriptTarget.Latest, true, scriptKind);
  const out = {
    modulePath: key,
    fullPath,
    sourceText,
    sourceFile,
  };
  budgetState.parsedModuleCount += 1;
  cache.set(key, out);
  assertLayoutSurfaceBudget(budgetState, 'parse_module_after');
  return out;
}

function expressionContainsJsx(ts, node) {
  if (!node) return false;
  let found = false;
  const walk = (current) => {
    if (!current || found) return;
    if (ts.isJsxElement(current) || ts.isJsxSelfClosingElement(current) || ts.isJsxFragment(current)) {
      found = true;
      return;
    }
    ts.forEachChild(current, walk);
  };
  walk(node);
  return found;
}

function findComponentDeclaration(ts, sourceFile, componentName) {
  for (const statement of sourceFile.statements) {
    if (ts.isFunctionDeclaration(statement) && statement.name?.text === componentName) {
      return { kind: 'function', body: statement.body || null, node: statement };
    }
    if (ts.isClassDeclaration(statement) && statement.name?.text === componentName) {
      const renderMethod = statement.members.find((member) =>
        ts.isMethodDeclaration(member)
        && member.name
        && ts.isIdentifier(member.name)
        && member.name.text === 'render'
      ) || null;
      if (renderMethod) {
        return { kind: 'class', body: renderMethod.body || null, node: renderMethod };
      }
    }
    if (!ts.isVariableStatement(statement)) continue;
    for (const declaration of statement.declarationList.declarations) {
      if (!ts.isIdentifier(declaration.name) || declaration.name.text !== componentName) continue;
      const initializer = declaration.initializer;
      if (!initializer) continue;
      if (ts.isArrowFunction(initializer) || ts.isFunctionExpression(initializer)) {
        return { kind: 'variable', body: initializer.body || null, node: initializer };
      }
    }
  }
  return null;
}

function collectReturnExpressions(ts, rootNode) {
  if (!rootNode) return [];
  if (!ts.isBlock(rootNode)) {
    return expressionContainsJsx(ts, rootNode) ? [rootNode] : [];
  }
  const entries = [];
  const walk = (node) => {
    if (!node) return;
    if (node !== rootNode && (ts.isFunctionLike(node) || ts.isClassLike(node))) return;
    if (ts.isReturnStatement(node) && node.expression && expressionContainsJsx(ts, node.expression)) {
      entries.push(node.expression);
    }
    ts.forEachChild(node, walk);
  };
  walk(rootNode);
  return entries;
}

function pickLastReturnedJsxExpression(ts, rootNode) {
  const returns = collectReturnExpressions(ts, rootNode);
  return returns[returns.length - 1] || null;
}

function findLocalRenderHelperBody(ts, componentBody, helperName) {
  if (!componentBody || !ts.isBlock(componentBody)) return null;
  for (const statement of componentBody.statements || []) {
    if (ts.isFunctionDeclaration(statement) && statement.name?.text === helperName) {
      if ((statement.parameters || []).length === 0) return statement.body || null;
      continue;
    }
    if (!ts.isVariableStatement(statement)) continue;
    for (const declaration of statement.declarationList.declarations || []) {
      if (!ts.isIdentifier(declaration.name) || declaration.name.text !== helperName) continue;
      const initializer = declaration.initializer;
      if (!initializer) continue;
      if ((ts.isArrowFunction(initializer) || ts.isFunctionExpression(initializer))
        && (initializer.parameters || []).length === 0) {
        return initializer.body || null;
      }
    }
  }
  return null;
}

function extractStaticExpressionText(ts, expr) {
  if (!expr) return '';
  if (ts.isStringLiteral(expr) || ts.isNoSubstitutionTemplateLiteral(expr)) {
    return expr.text;
  }
  if (ts.isNumericLiteral(expr)) {
    return expr.text;
  }
  if (expr.kind === ts.SyntaxKind.TrueKeyword) return 'true';
  if (expr.kind === ts.SyntaxKind.FalseKeyword) return 'false';
  return '';
}

function staticValueToInlineText(value) {
  if (isUnknownStaticValue(value) || value === null || value === undefined) return '';
  if (typeof value === 'string' || typeof value === 'number' || typeof value === 'boolean') {
    return String(value);
  }
  return '';
}

function isUnknownStaticValue(value) {
  return value === UNKNOWN_STATIC_VALUE;
}

function isStaticNoReturn(value) {
  return value === STATIC_NO_RETURN;
}

function isStaticPlainObject(value) {
  return Boolean(value) && typeof value === 'object' && !Array.isArray(value);
}

function markStaticObjectCompleteness(target, complete) {
  if (!isStaticPlainObject(target)) return target;
  Object.defineProperty(target, '__r2cStaticObjectComplete', {
    value: complete === true,
    enumerable: false,
    configurable: true,
    writable: true,
  });
  return target;
}

function isStaticObjectComplete(value) {
  return isStaticPlainObject(value) && value.__r2cStaticObjectComplete === true;
}

function normalizeStaticString(value) {
  return typeof value === 'string' ? value.trim() : '';
}

function isStaticP2PMediaUri(value) {
  return normalizeStaticString(value).startsWith('p2pmedia://');
}

function makeStaticFunctionDescriptor(parsed, node, env) {
  return {
    __r2cStaticFunction: true,
    parsed,
    node,
    env: new Map(env),
  };
}

function isStaticFunctionDescriptor(value) {
  return Boolean(value && value.__r2cStaticFunction);
}

function makeStaticImportBindingDescriptor(binding) {
  return {
    __r2cStaticImportBinding: true,
    specifier: String(binding?.specifier || ''),
    importedName: String(binding?.importedName || ''),
    resolvedModulePath: String(binding?.resolvedModulePath || ''),
    external: Boolean(binding?.external),
  };
}

function isStaticImportBindingDescriptor(value) {
  return Boolean(value && value.__r2cStaticImportBinding);
}

function makeStaticStateSetterDescriptor(stateName) {
  return {
    __r2cStaticStateSetter: true,
    stateName: String(stateName || '').trim(),
  };
}

function isStaticStateSetterDescriptor(value) {
  return Boolean(value && value.__r2cStaticStateSetter);
}

function resolveStaticTruthiness(value) {
  if (isUnknownStaticValue(value)) return UNKNOWN_STATIC_VALUE;
  if (value === null || value === undefined) return false;
  if (typeof value === 'boolean') return value;
  if (typeof value === 'number') return value !== 0 && !Number.isNaN(value);
  if (typeof value === 'string') return value.length > 0;
  if (Array.isArray(value)) return true;
  if (typeof value === 'object') return true;
  return UNKNOWN_STATIC_VALUE;
}

function resolveRelativeModulePath(repo, fromModulePath, specifier) {
  const request = String(specifier || '').trim();
  if (!request.startsWith('.')) return '';
  const fromDir = path.posix.dirname(String(fromModulePath || '').replace(/\\/g, '/'));
  const base = path.posix.normalize(path.posix.join(fromDir, request));
  const candidates = [
    base,
    `${base}.ts`,
    `${base}.tsx`,
    `${base}.js`,
    `${base}.jsx`,
    path.posix.join(base, 'index.ts'),
    path.posix.join(base, 'index.tsx'),
    path.posix.join(base, 'index.js'),
    path.posix.join(base, 'index.jsx'),
  ];
  for (const candidate of candidates) {
    const fullPath = path.join(repo, candidate);
    if (fs.existsSync(fullPath) && fs.statSync(fullPath).isFile()) return candidate;
  }
  return '';
}

function getModuleImportBindings(ts, repo, parsed, importCache) {
  const cacheKey = String(parsed?.modulePath || '');
  if (importCache.has(cacheKey)) return importCache.get(cacheKey);
  const out = new Map();
  for (const statement of parsed?.sourceFile?.statements || []) {
    if (!ts.isImportDeclaration(statement) || !statement.importClause || !ts.isStringLiteral(statement.moduleSpecifier)) {
      continue;
    }
    const specifier = statement.moduleSpecifier.text;
    const resolvedModulePath = resolveRelativeModulePath(repo, parsed.modulePath, specifier);
    const external = !resolvedModulePath;
    const clause = statement.importClause;
    if (clause.name?.text) {
      out.set(String(clause.name.text), {
        specifier,
        importedName: 'default',
        resolvedModulePath,
        external,
      });
    }
    if (clause.namedBindings && ts.isNamedImports(clause.namedBindings)) {
      for (const element of clause.namedBindings.elements || []) {
        const localName = String(element.name?.text || '').trim();
        if (!localName) continue;
        out.set(localName, {
          specifier,
          importedName: String(element.propertyName?.text || element.name.text || '').trim() || localName,
          resolvedModulePath,
          external,
        });
      }
    }
  }
  importCache.set(cacheKey, out);
  return out;
}

function findTopLevelNamedDeclaration(ts, sourceFile, name) {
  for (const statement of sourceFile?.statements || []) {
    if (ts.isFunctionDeclaration(statement) && statement.name?.text === name) {
      return { kind: 'function', node: statement };
    }
    if (ts.isVariableStatement(statement)) {
      for (const declaration of statement.declarationList.declarations || []) {
        if (ts.isIdentifier(declaration.name) && declaration.name.text === name) {
          return { kind: 'variable', node: declaration };
        }
      }
    }
    if (ts.isExportDeclaration(statement)
      && statement.moduleSpecifier
      && ts.isStringLiteral(statement.moduleSpecifier)) {
      if (!statement.exportClause) {
        return {
          kind: 'reexport_star',
          specifier: statement.moduleSpecifier.text,
          importedName: name,
        };
      }
      if (ts.isNamedExports(statement.exportClause)) {
        for (const element of statement.exportClause.elements || []) {
          if (String(element.name?.text || '') !== name) continue;
          return {
            kind: 'reexport',
            specifier: statement.moduleSpecifier.text,
            importedName: String(element.propertyName?.text || element.name.text || '').trim() || name,
          };
        }
      }
    }
  }
  return null;
}

function bindPatternStaticValue(ts, pattern, value, env) {
  if (!pattern) return;
  if (ts.isIdentifier(pattern)) {
    env.set(String(pattern.text), value);
    return;
  }
  if (ts.isObjectBindingPattern(pattern)) {
    for (const element of pattern.elements || []) {
      const propertyName = String(
        element.propertyName && ts.isIdentifier(element.propertyName)
          ? element.propertyName.text
          : element.name && ts.isIdentifier(element.name)
            ? element.name.text
            : '',
      ).trim();
      let nextValue = UNKNOWN_STATIC_VALUE;
      if (!isUnknownStaticValue(value) && isStaticPlainObject(value) && propertyName && Object.prototype.hasOwnProperty.call(value, propertyName)) {
        nextValue = value[propertyName];
      }
      bindPatternStaticValue(ts, element.name, nextValue, env);
    }
    return;
  }
  if (ts.isArrayBindingPattern(pattern)) {
    for (let index = 0; index < (pattern.elements || []).length; index += 1) {
      const element = pattern.elements[index];
      if (ts.isOmittedExpression(element)) continue;
      const nextValue = Array.isArray(value) && index < value.length ? value[index] : UNKNOWN_STATIC_VALUE;
      bindPatternStaticValue(ts, element.name, nextValue, env);
    }
  }
}

function resolveNamedValueFromModule(ts, repo, modulePath, name, sourceCache, budgetState, importCache, valueCache, evalStack) {
  const key = `${String(modulePath || '').trim()}#${String(name || '').trim()}`;
  if (!modulePath || !name) return UNKNOWN_STATIC_VALUE;
  if (valueCache.has(key)) return valueCache.get(key);
  if (evalStack.has(key)) return UNKNOWN_STATIC_VALUE;
  evalStack.add(key);
  const parsed = parseSourceModule(ts, repo, modulePath, sourceCache, budgetState);
  let value = UNKNOWN_STATIC_VALUE;
  if (parsed?.sourceFile) {
    const declaration = findTopLevelNamedDeclaration(ts, parsed.sourceFile, name);
    if (declaration?.kind === 'function') {
      value = makeStaticFunctionDescriptor(parsed, declaration.node, new Map());
    } else if (declaration?.kind === 'variable') {
      value = evaluateStaticValue(
        ts,
        repo,
        parsed,
        declaration.node.initializer,
        new Map(),
        sourceCache,
        budgetState,
        importCache,
        valueCache,
        evalStack,
      );
    } else if (declaration?.kind === 'reexport' || declaration?.kind === 'reexport_star') {
      const targetModulePath = resolveRelativeModulePath(repo, parsed.modulePath, declaration.specifier);
      if (targetModulePath) {
        value = resolveNamedValueFromModule(
          ts,
          repo,
          targetModulePath,
          declaration.importedName,
          sourceCache,
          budgetState,
          importCache,
          valueCache,
          evalStack,
        );
      }
    }
  }
  valueCache.set(key, value);
  evalStack.delete(key);
  return value;
}

function resolveIdentifierStaticValue(ts, repo, parsed, name, env, sourceCache, budgetState, importCache, valueCache, evalStack) {
  const text = String(name || '').trim();
  if (!text) return UNKNOWN_STATIC_VALUE;
  if (env.has(text)) return env.get(text);
  if (text === 'undefined') return undefined;
  const imports = getModuleImportBindings(ts, repo, parsed, importCache);
  if (imports.has(text)) {
    const binding = imports.get(text);
    if (binding?.resolvedModulePath) {
      return resolveNamedValueFromModule(
        ts,
        repo,
        binding.resolvedModulePath,
        binding.importedName,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
        evalStack,
      );
    }
    if (binding?.external) {
      return makeStaticImportBindingDescriptor(binding);
    }
  }
  return resolveNamedValueFromModule(
    ts,
    repo,
    parsed.modulePath,
    text,
    sourceCache,
    budgetState,
    importCache,
    valueCache,
    evalStack,
  );
}

function applyStaticFunction(ts, repo, fnValue, args, sourceCache, budgetState, importCache, valueCache, evalStack) {
  if (!isStaticFunctionDescriptor(fnValue)) return UNKNOWN_STATIC_VALUE;
  const localEnv = new Map(fnValue.env);
  for (let index = 0; index < (fnValue.node?.parameters || []).length; index += 1) {
    const param = fnValue.node.parameters[index];
    let argValue = index < args.length ? args[index] : UNKNOWN_STATIC_VALUE;
    if (isUnknownStaticValue(argValue) && param.initializer) {
      argValue = evaluateStaticValue(
        ts,
        repo,
        fnValue.parsed,
        param.initializer,
        localEnv,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
        evalStack,
      );
    }
    bindPatternStaticValue(ts, param.name, argValue, localEnv);
  }
  if (fnValue.node.body && ts.isBlock(fnValue.node.body)) {
    const result = evaluateStaticStatements(
      ts,
      repo,
      fnValue.parsed,
      fnValue.node.body,
      localEnv,
      sourceCache,
      budgetState,
      importCache,
      valueCache,
      evalStack,
    );
    return isStaticNoReturn(result) ? undefined : result;
  }
  return evaluateStaticValue(
    ts,
    repo,
    fnValue.parsed,
    fnValue.node.body || null,
    localEnv,
    sourceCache,
    budgetState,
    importCache,
    valueCache,
    evalStack,
  );
}

function bindUseStateDeclaration(ts, repo, parsed, declaration, env, sourceCache, budgetState, importCache, valueCache) {
  if (!declaration?.initializer || !ts.isCallExpression(declaration.initializer)) return false;
  if (!ts.isIdentifier(declaration.initializer.expression) || declaration.initializer.expression.text !== 'useState') return false;
  if (!ts.isArrayBindingPattern(declaration.name)) return false;
  const elements = declaration.name.elements || [];
  const firstArg = declaration.initializer.arguments[0];
  const initialValue = (ts.isArrowFunction(firstArg) || ts.isFunctionExpression(firstArg))
    ? applyStaticFunction(
      ts,
      repo,
      makeStaticFunctionDescriptor(parsed, firstArg, env),
      [],
      sourceCache,
      budgetState,
      importCache,
      valueCache,
    )
    : evaluateStaticValue(ts, repo, parsed, firstArg, env, sourceCache, budgetState, importCache, valueCache);
  const stateElement = elements[0];
  const setterElement = elements[1];
  let stateName = '';
  if (stateElement && !ts.isOmittedExpression(stateElement)) {
    if (ts.isIdentifier(stateElement.name)) {
      stateName = String(stateElement.name.text || '').trim();
    }
    bindPatternStaticValue(ts, stateElement.name, initialValue, env);
  }
  if (setterElement && !ts.isOmittedExpression(setterElement)) {
    if (ts.isIdentifier(setterElement.name) && stateName) {
      env.set(String(setterElement.name.text || '').trim(), makeStaticStateSetterDescriptor(stateName));
    } else {
      bindPatternStaticValue(ts, setterElement.name, UNKNOWN_STATIC_VALUE, env);
    }
  }
  return true;
}

function applyStaticEffectStateUpdates(ts, repo, parsed, expr, env, sourceCache, budgetState, importCache, valueCache) {
  if (!ts.isCallExpression(expr) || !ts.isIdentifier(expr.expression)) return false;
  const hookName = String(expr.expression.text || '').trim();
  if (hookName !== 'useEffect' && hookName !== 'useLayoutEffect') return false;
  const callback = expr.arguments[0];
  if (!(ts.isArrowFunction(callback) || ts.isFunctionExpression(callback))) return true;
  const applySetterCall = (callExpr) => {
    if (!ts.isCallExpression(callExpr) || !ts.isIdentifier(callExpr.expression) || callExpr.arguments.length <= 0) {
      return false;
    }
    const setter = env.get(String(callExpr.expression.text || '').trim());
    if (!isStaticStateSetterDescriptor(setter) || !setter.stateName) {
      return false;
    }
    const nextValue = evaluateStaticValue(
      ts,
      repo,
      parsed,
      callExpr.arguments[0],
      env,
      sourceCache,
      budgetState,
      importCache,
      valueCache,
    );
    if (isStaticFunctionDescriptor(nextValue)) {
      return false;
    }
    env.set(setter.stateName, nextValue);
    return true;
  };
  if (ts.isBlock(callback.body)) {
    for (const statement of callback.body.statements || []) {
      if (ts.isExpressionStatement(statement)) {
        applySetterCall(statement.expression);
      }
    }
    return true;
  }
  applySetterCall(callback.body);
  return true;
}

function evaluateStaticStatements(ts, repo, parsed, block, env, sourceCache, budgetState, importCache, valueCache, evalStack) {
  const runStatement = (statement, scope) => {
    if (!statement) return STATIC_NO_RETURN;
    if (ts.isBlock(statement)) {
      const blockScope = new Map(scope);
      for (const item of statement.statements || []) {
        const result = runStatement(item, blockScope);
        if (!isStaticNoReturn(result)) return result;
      }
      return STATIC_NO_RETURN;
    }
    if (ts.isFunctionDeclaration(statement) && statement.name?.text) {
      scope.set(String(statement.name.text), makeStaticFunctionDescriptor(parsed, statement, scope));
      return STATIC_NO_RETURN;
    }
    if (ts.isExpressionStatement(statement)) {
      if (applyStaticEffectStateUpdates(ts, repo, parsed, statement.expression, scope, sourceCache, budgetState, importCache, valueCache)) {
        return STATIC_NO_RETURN;
      }
      evaluateStaticValue(
        ts,
        repo,
        parsed,
        statement.expression,
        scope,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
        evalStack,
      );
      return STATIC_NO_RETURN;
    }
    if (ts.isVariableStatement(statement)) {
      for (const declaration of statement.declarationList.declarations || []) {
        if (bindUseStateDeclaration(ts, repo, parsed, declaration, scope, sourceCache, budgetState, importCache, valueCache)) {
          continue;
        }
        const value = evaluateStaticValue(
          ts,
          repo,
          parsed,
          declaration.initializer,
          scope,
          sourceCache,
          budgetState,
          importCache,
          valueCache,
          evalStack,
        );
        bindPatternStaticValue(ts, declaration.name, value, scope);
      }
      return STATIC_NO_RETURN;
    }
    if (ts.isIfStatement(statement)) {
      const condValue = evaluateStaticValue(
        ts,
        repo,
        parsed,
        statement.expression,
        scope,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
        evalStack,
      );
      const truthiness = resolveStaticTruthiness(condValue);
      if (truthiness === true) {
        return runStatement(statement.thenStatement, scope);
      }
      if (truthiness === false) {
        return statement.elseStatement ? runStatement(statement.elseStatement, scope) : STATIC_NO_RETURN;
      }
      return STATIC_NO_RETURN;
    }
    if (ts.isForOfStatement(statement)) {
      const iterableValue = evaluateStaticValue(
        ts,
        repo,
        parsed,
        statement.expression,
        scope,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
        evalStack,
      );
      if (Array.isArray(iterableValue) || typeof iterableValue === 'string') {
        const iterableItems = Array.isArray(iterableValue) ? iterableValue : [...iterableValue];
        for (const itemValue of iterableItems) {
          const iterationScope = new Map(scope);
          const initializer = statement.initializer;
          if (ts.isVariableDeclarationList(initializer)) {
            const declaration = initializer.declarations?.[0];
            if (declaration) {
              bindPatternStaticValue(ts, declaration.name, itemValue, iterationScope);
            }
          } else if (ts.isIdentifier(initializer)) {
            iterationScope.set(String(initializer.text || '').trim(), itemValue);
          }
          const result = runStatement(statement.statement, iterationScope);
          if (!isStaticNoReturn(result)) return result;
        }
      }
      return STATIC_NO_RETURN;
    }
    if (ts.isReturnStatement(statement)) {
      if (!statement.expression) return undefined;
      return evaluateStaticValue(
        ts,
        repo,
        parsed,
        statement.expression,
        scope,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
        evalStack,
      );
    }
    return STATIC_NO_RETURN;
  };

  const scope = new Map(env);
  for (const statement of block?.statements || []) {
    const result = runStatement(statement, scope);
    if (!isStaticNoReturn(result)) return result;
  }
  return STATIC_NO_RETURN;
}

function evaluateStaticValue(ts, repo, parsed, expr, env, sourceCache, budgetState, importCache, valueCache, evalStack = new Set()) {
  if (!expr) return UNKNOWN_STATIC_VALUE;
  if (ts.isAsExpression(expr) || ts.isTypeAssertionExpression(expr) || ts.isSatisfiesExpression?.(expr)) {
    return evaluateStaticValue(ts, repo, parsed, expr.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
  }
  if (ts.isParenthesizedExpression(expr)) {
    return evaluateStaticValue(ts, repo, parsed, expr.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
  }
  if (ts.isStringLiteral(expr) || ts.isNoSubstitutionTemplateLiteral(expr)) return expr.text;
  if (ts.isTemplateExpression(expr)) {
    let out = expr.head.text;
    for (const span of expr.templateSpans || []) {
      const value = evaluateStaticValue(ts, repo, parsed, span.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      if (isUnknownStaticValue(value)) return UNKNOWN_STATIC_VALUE;
      out += String(value);
      out += span.literal.text;
    }
    return out;
  }
  if (ts.isNumericLiteral(expr)) return Number(expr.text);
  if (expr.kind === ts.SyntaxKind.TrueKeyword) return true;
  if (expr.kind === ts.SyntaxKind.FalseKeyword) return false;
  if (expr.kind === ts.SyntaxKind.NullKeyword) return null;
  if (ts.isIdentifier(expr)) {
    return resolveIdentifierStaticValue(ts, repo, parsed, expr.text, env, sourceCache, budgetState, importCache, valueCache, evalStack);
  }
  if (ts.isArrayLiteralExpression(expr)) {
    const out = [];
    for (const element of expr.elements || []) {
      if (ts.isSpreadElement(element)) {
        const spreadValue = evaluateStaticValue(ts, repo, parsed, element.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (Array.isArray(spreadValue)) out.push(...spreadValue);
        continue;
      }
      out.push(evaluateStaticValue(ts, repo, parsed, element, env, sourceCache, budgetState, importCache, valueCache, evalStack));
    }
    return out;
  }
  if (ts.isObjectLiteralExpression(expr)) {
    const out = {};
    let complete = true;
    for (const property of expr.properties || []) {
      if (ts.isPropertyAssignment(property)) {
        const name = ts.isIdentifier(property.name) || ts.isStringLiteral(property.name)
          ? String(property.name.text)
          : '';
        if (!name) continue;
        out[name] = evaluateStaticValue(ts, repo, parsed, property.initializer, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      } else if (ts.isShorthandPropertyAssignment(property)) {
        out[String(property.name.text)] = resolveIdentifierStaticValue(ts, repo, parsed, property.name.text, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      } else if (ts.isSpreadAssignment(property)) {
        const spreadValue = evaluateStaticValue(ts, repo, parsed, property.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (isStaticPlainObject(spreadValue)) {
          Object.assign(out, spreadValue);
          if (!isStaticObjectComplete(spreadValue)) complete = false;
        } else {
          complete = false;
        }
      }
    }
    return markStaticObjectCompleteness(out, complete);
  }
  if (ts.isPropertyAccessExpression(expr) || (typeof ts.isPropertyAccessChain === 'function' && ts.isPropertyAccessChain(expr))) {
    const target = evaluateStaticValue(ts, repo, parsed, expr.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    if ((target === null || target === undefined)
      && typeof ts.isPropertyAccessChain === 'function'
      && ts.isPropertyAccessChain(expr)) {
      return undefined;
    }
    const propertyName = String(expr.name?.text || '').trim();
    if (Array.isArray(target) && propertyName === 'length') return target.length;
    if (typeof target === 'string' && propertyName === 'length') return target.length;
    if (isStaticPlainObject(target) && Object.prototype.hasOwnProperty.call(target, propertyName)) return target[propertyName];
    if (isStaticObjectComplete(target)) return undefined;
    return UNKNOWN_STATIC_VALUE;
  }
  if (ts.isElementAccessExpression(expr) || (typeof ts.isElementAccessChain === 'function' && ts.isElementAccessChain(expr))) {
    const target = evaluateStaticValue(ts, repo, parsed, expr.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    if ((target === null || target === undefined)
      && typeof ts.isElementAccessChain === 'function'
      && ts.isElementAccessChain(expr)) {
      return undefined;
    }
    const indexValue = evaluateStaticValue(ts, repo, parsed, expr.argumentExpression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    if (Array.isArray(target) && Number.isInteger(indexValue) && indexValue >= 0 && indexValue < target.length) return target[indexValue];
    if (isStaticPlainObject(target) && !isUnknownStaticValue(indexValue) && Object.prototype.hasOwnProperty.call(target, String(indexValue))) return target[String(indexValue)];
    if (isStaticObjectComplete(target) && !isUnknownStaticValue(indexValue)) return undefined;
    return UNKNOWN_STATIC_VALUE;
  }
  if (ts.isPrefixUnaryExpression(expr)) {
    if (expr.operator === ts.SyntaxKind.TypeOfKeyword) {
      if (ts.isIdentifier(expr.operand)) {
        const operandValue = resolveIdentifierStaticValue(ts, repo, parsed, expr.operand.text, env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (isUnknownStaticValue(operandValue)) return 'undefined';
        if (operandValue === null) return 'object';
        return typeof operandValue;
      }
      const operandValue = evaluateStaticValue(ts, repo, parsed, expr.operand, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      if (isUnknownStaticValue(operandValue)) return UNKNOWN_STATIC_VALUE;
      if (operandValue === null) return 'object';
      return typeof operandValue;
    }
    const operand = evaluateStaticValue(ts, repo, parsed, expr.operand, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    if (expr.operator === ts.SyntaxKind.ExclamationToken) {
      const truthiness = resolveStaticTruthiness(operand);
      return truthiness === true ? false : truthiness === false ? true : UNKNOWN_STATIC_VALUE;
    }
    if (expr.operator === ts.SyntaxKind.MinusToken) return typeof operand === 'number' ? -operand : UNKNOWN_STATIC_VALUE;
  }
  if (ts.isBinaryExpression(expr)) {
    const left = evaluateStaticValue(ts, repo, parsed, expr.left, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    if (expr.operatorToken.kind === ts.SyntaxKind.AmpersandAmpersandToken) {
      const leftTruthiness = resolveStaticTruthiness(left);
      if (leftTruthiness === false) return left;
      if (leftTruthiness === true) return evaluateStaticValue(ts, repo, parsed, expr.right, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      const rightValue = evaluateStaticValue(ts, repo, parsed, expr.right, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      return resolveStaticTruthiness(rightValue) === false ? rightValue : UNKNOWN_STATIC_VALUE;
    }
    if (expr.operatorToken.kind === ts.SyntaxKind.BarBarToken) {
      const leftTruthiness = resolveStaticTruthiness(left);
      if (leftTruthiness === true) return left;
      if (leftTruthiness === false) return evaluateStaticValue(ts, repo, parsed, expr.right, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      const rightValue = evaluateStaticValue(ts, repo, parsed, expr.right, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      return resolveStaticTruthiness(rightValue) === true ? rightValue : UNKNOWN_STATIC_VALUE;
    }
    const right = evaluateStaticValue(ts, repo, parsed, expr.right, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    if (expr.operatorToken.kind === ts.SyntaxKind.QuestionQuestionToken) {
      return left !== null && left !== undefined && !isUnknownStaticValue(left) ? left : right;
    }
    if (expr.operatorToken.kind === ts.SyntaxKind.EqualsEqualsEqualsToken) return left === right;
    if (expr.operatorToken.kind === ts.SyntaxKind.ExclamationEqualsEqualsToken) return left !== right;
    if (expr.operatorToken.kind === ts.SyntaxKind.GreaterThanToken && typeof left === 'number' && typeof right === 'number') return left > right;
    if (expr.operatorToken.kind === ts.SyntaxKind.GreaterThanEqualsToken && typeof left === 'number' && typeof right === 'number') return left >= right;
    if (expr.operatorToken.kind === ts.SyntaxKind.LessThanToken && typeof left === 'number' && typeof right === 'number') return left < right;
    if (expr.operatorToken.kind === ts.SyntaxKind.LessThanEqualsToken && typeof left === 'number' && typeof right === 'number') return left <= right;
    if (expr.operatorToken.kind === ts.SyntaxKind.MinusToken && typeof left === 'number' && typeof right === 'number') return left - right;
    if (expr.operatorToken.kind === ts.SyntaxKind.PlusToken) {
      if (typeof left === 'number' && typeof right === 'number') return left + right;
      if (!isUnknownStaticValue(left) && !isUnknownStaticValue(right)) return String(left) + String(right);
    }
    return UNKNOWN_STATIC_VALUE;
  }
  if (ts.isConditionalExpression(expr)) {
    const condition = evaluateStaticValue(ts, repo, parsed, expr.condition, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    const truthiness = resolveStaticTruthiness(condition);
    if (truthiness === true) return evaluateStaticValue(ts, repo, parsed, expr.whenTrue, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    if (truthiness === false) return evaluateStaticValue(ts, repo, parsed, expr.whenFalse, env, sourceCache, budgetState, importCache, valueCache, evalStack);
    return UNKNOWN_STATIC_VALUE;
  }
  if (ts.isArrowFunction(expr) || ts.isFunctionExpression(expr) || ts.isFunctionDeclaration(expr)) {
    return makeStaticFunctionDescriptor(parsed, expr, env);
  }
  if (ts.isCallExpression(expr)) {
    if (ts.isIdentifier(expr.expression)) {
      const fnName = String(expr.expression.text || '').trim();
      if (fnName === 'readTruthRouteQuery') {
        const truthRoute = String(env.get('__r2cTruthRouteState') || '').trim();
        if (truthRoute) {
          return markStaticObjectCompleteness({
            enabled: true,
            route: truthRoute,
          }, true);
        }
      }
      if (fnName === 'readPwaSmokeQuery') {
        if (env.get('__r2cSmokeMode') === false) {
          return markStaticObjectCompleteness({
            enabled: false,
            kind: 'none',
            role: 'receiver',
            reporter: '',
            namespace: '',
            messageText: 'pwa-smoke-message',
            targetPeerId: '',
            flow: 'content',
            mediaKind: 'image',
            title: '',
          }, true);
        }
      }
      if (fnName === 'readOverlayAppRoute') {
        if (env.get('__r2cTruthMode') === true) {
          return null;
        }
      }
      if (fnName === 'buildTruthContent') {
        const truthContentFixture = env.get('__r2cTruthContentFixture');
        if (isStaticPlainObject(truthContentFixture)) {
          return truthContentFixture;
        }
      }
      if (fnName === 'resolvePreviewMediaSource') {
        const tinyPreview = normalizeStaticString(evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack));
        if (tinyPreview) return tinyPreview;
        const coverMedia = normalizeStaticString(evaluateStaticValue(ts, repo, parsed, expr.arguments[1], env, sourceCache, budgetState, importCache, valueCache, evalStack));
        if (coverMedia && !isStaticP2PMediaUri(coverMedia)) return coverMedia;
        return '';
      }
      if (fnName === 'collectImageDetailMediaRefs') {
        const options = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (!isStaticPlainObject(options)) return UNKNOWN_STATIC_VALUE;
        if (normalizeStaticString(options.type) !== 'image') return [];
        const out = [];
        const pushUnique = (value) => {
          const text = normalizeStaticString(value);
          if (!text || out.includes(text)) return;
          out.push(text);
        };
        for (const item of Array.isArray(options.mediaItems) ? options.mediaItems : []) {
          pushUnique(item);
        }
        pushUnique(options.media);
        if (out.length === 0) {
          pushUnique(options.coverMedia);
        }
        return out;
      }
      if (fnName === 'resolveImageDetailSource') {
        const media = normalizeStaticString(evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack));
        const resolvedSource = normalizeStaticString(evaluateStaticValue(ts, repo, parsed, expr.arguments[1], env, sourceCache, budgetState, importCache, valueCache, evalStack));
        const tinyPreview = evaluateStaticValue(ts, repo, parsed, expr.arguments[2], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        const coverMedia = evaluateStaticValue(ts, repo, parsed, expr.arguments[3], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (resolvedSource) return resolvedSource;
        if (!media) {
          const preview = normalizeStaticString(tinyPreview);
          if (preview) return preview;
          const cover = normalizeStaticString(coverMedia);
          return cover && !isStaticP2PMediaUri(cover) ? cover : '';
        }
        if (!isStaticP2PMediaUri(media)) return media;
        const preview = normalizeStaticString(tinyPreview);
        if (preview) return preview;
        const cover = normalizeStaticString(coverMedia);
        return cover && !isStaticP2PMediaUri(cover) ? cover : '';
      }
      if (fnName === 'Boolean') {
        const value = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        const truthiness = resolveStaticTruthiness(value);
        return truthiness === UNKNOWN_STATIC_VALUE ? UNKNOWN_STATIC_VALUE : truthiness;
      }
      if (fnName === 'parseInt' || fnName === 'Number') {
        const rawValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (typeof rawValue === 'number') return rawValue;
        if (typeof rawValue === 'string') {
          const parsedNumber = fnName === 'Number'
            ? Number(rawValue)
            : Number.parseInt(rawValue, (() => {
              const radixValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[1], env, sourceCache, budgetState, importCache, valueCache, evalStack);
              return typeof radixValue === 'number' ? radixValue : undefined;
            })());
          return Number.isNaN(parsedNumber) ? UNKNOWN_STATIC_VALUE : parsedNumber;
        }
      }
      if (fnName === 'encodeURIComponent') {
        const rawValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (typeof rawValue === 'string') return encodeURIComponent(rawValue);
      }
      if (fnName === 'createContext' || fnName === 'useContext') {
        return evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
      }
      if (fnName === 'useState') {
        const firstArg = expr.arguments[0];
        const initialValue = ts.isArrowFunction(firstArg) || ts.isFunctionExpression(firstArg)
          ? applyStaticFunction(ts, repo, makeStaticFunctionDescriptor(parsed, firstArg, env), [], sourceCache, budgetState, importCache, valueCache, evalStack)
          : evaluateStaticValue(ts, repo, parsed, firstArg, env, sourceCache, budgetState, importCache, valueCache, evalStack);
        return [initialValue, UNKNOWN_STATIC_VALUE];
      }
      if (fnName === 'useDeferredValue') {
        return evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
      }
      if (fnName === 'useMemo') {
        const callbackValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        return applyStaticFunction(ts, repo, callbackValue, [], sourceCache, budgetState, importCache, valueCache, evalStack);
      }
      if (fnName === 'useCallback') {
        return evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
      }
      const fnValue = resolveIdentifierStaticValue(ts, repo, parsed, fnName, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      if (isStaticFunctionDescriptor(fnValue)) {
        const args = expr.arguments.map((arg) => evaluateStaticValue(ts, repo, parsed, arg, env, sourceCache, budgetState, importCache, valueCache, evalStack));
        return applyStaticFunction(ts, repo, fnValue, args, sourceCache, budgetState, importCache, valueCache, evalStack);
      }
    }
    if (ts.isPropertyAccessExpression(expr.expression)) {
      if (ts.isIdentifier(expr.expression.expression)) {
        const builtinTarget = String(expr.expression.expression.text || '').trim();
        const builtinMethod = String(expr.expression.name?.text || '').trim();
        if (builtinTarget === 'Object') {
          const argValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
          if (builtinMethod === 'values' && isStaticPlainObject(argValue)) return Object.values(argValue);
          if (builtinMethod === 'keys' && isStaticPlainObject(argValue)) return Object.keys(argValue);
          if (builtinMethod === 'entries' && isStaticPlainObject(argValue)) return Object.entries(argValue);
          if (builtinMethod === 'fromEntries') {
            const entryValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
            if (Array.isArray(entryValue)) {
              const out = {};
              let complete = true;
              for (const row of entryValue) {
                if (!Array.isArray(row) || row.length < 2 || isUnknownStaticValue(row[0])) {
                  complete = false;
                  continue;
                }
                out[String(row[0])] = row[1];
              }
              return markStaticObjectCompleteness(out, complete);
            }
          }
        }
        if (builtinTarget === 'Array') {
          if (builtinMethod === 'isArray') {
            const argValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
            return Array.isArray(argValue);
          }
        }
        if (builtinTarget === 'JSON') {
          if (builtinMethod === 'stringify') {
            const argValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
            if (!isUnknownStaticValue(argValue)) {
              try {
                return JSON.stringify(argValue);
              } catch {
                return UNKNOWN_STATIC_VALUE;
              }
            }
          }
          if (builtinMethod === 'parse') {
            const argValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
            if (typeof argValue === 'string') {
              try {
                const parsedValue = JSON.parse(argValue);
                return isStaticPlainObject(parsedValue)
                  ? markStaticObjectCompleteness(parsedValue, true)
                  : parsedValue;
              } catch {
                return UNKNOWN_STATIC_VALUE;
              }
            }
          }
        }
        if (builtinTarget === 'Math') {
          const argValues = expr.arguments.map((arg) => evaluateStaticValue(ts, repo, parsed, arg, env, sourceCache, budgetState, importCache, valueCache, evalStack));
          if (argValues.every((value) => typeof value === 'number')) {
            if (builtinMethod === 'floor') return Math.floor(argValues[0]);
            if (builtinMethod === 'round') return Math.round(argValues[0]);
            if (builtinMethod === 'trunc') return Math.trunc(argValues[0]);
            if (builtinMethod === 'max') return Math.max(...argValues);
            if (builtinMethod === 'min') return Math.min(...argValues);
          }
        }
        if (builtinTarget === 'Date' && builtinMethod === 'now' && expr.arguments.length === 0) {
          return Date.now();
        }
      }
      const targetValue = evaluateStaticValue(ts, repo, parsed, expr.expression.expression, env, sourceCache, budgetState, importCache, valueCache, evalStack);
      const methodName = String(expr.expression.name?.text || '').trim();
      if (Array.isArray(targetValue) && (methodName === 'map' || methodName === 'filter' || methodName === 'find' || methodName === 'reduce' || methodName === 'some')) {
        const callbackValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (!isStaticFunctionDescriptor(callbackValue)) return UNKNOWN_STATIC_VALUE;
        if (methodName === 'map') {
          return targetValue.map((item, index) => applyStaticFunction(ts, repo, callbackValue, [item, index, targetValue], sourceCache, budgetState, importCache, valueCache, evalStack));
        }
        if (methodName === 'filter') {
          const out = [];
          for (let index = 0; index < targetValue.length; index += 1) {
            const item = targetValue[index];
            const keep = applyStaticFunction(ts, repo, callbackValue, [item, index, targetValue], sourceCache, budgetState, importCache, valueCache, evalStack);
            if (keep === false) continue;
            out.push(item);
          }
          return out;
        }
        if (methodName === 'reduce') {
          let acc;
          let startIndex = 0;
          if (expr.arguments.length > 1) {
            acc = evaluateStaticValue(ts, repo, parsed, expr.arguments[1], env, sourceCache, budgetState, importCache, valueCache, evalStack);
          } else if (targetValue.length > 0) {
            acc = targetValue[0];
            startIndex = 1;
          } else {
            return UNKNOWN_STATIC_VALUE;
          }
          for (let index = startIndex; index < targetValue.length; index += 1) {
            acc = applyStaticFunction(ts, repo, callbackValue, [acc, targetValue[index], index, targetValue], sourceCache, budgetState, importCache, valueCache, evalStack);
          }
          return acc;
        }
        if (methodName === 'some') {
          for (let index = 0; index < targetValue.length; index += 1) {
            const item = targetValue[index];
            const match = applyStaticFunction(ts, repo, callbackValue, [item, index, targetValue], sourceCache, budgetState, importCache, valueCache, evalStack);
            if (resolveStaticTruthiness(match) === true) return true;
          }
          return false;
        }
        for (let index = 0; index < targetValue.length; index += 1) {
          const item = targetValue[index];
          const match = applyStaticFunction(ts, repo, callbackValue, [item, index, targetValue], sourceCache, budgetState, importCache, valueCache, evalStack);
          if (match === true) return item;
        }
        return undefined;
      }
      if (Array.isArray(targetValue) && methodName === 'includes') {
        const searchValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        return targetValue.includes(searchValue);
      }
      if (Array.isArray(targetValue) && methodName === 'push') {
        const pushedValues = expr.arguments.map((arg) => evaluateStaticValue(ts, repo, parsed, arg, env, sourceCache, budgetState, importCache, valueCache, evalStack));
        targetValue.push(...pushedValues);
        return targetValue.length;
      }
      if (Array.isArray(targetValue) && methodName === 'join') {
        const separatorValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        const separator = separatorValue === undefined || isUnknownStaticValue(separatorValue) ? ',' : String(separatorValue);
        const parts = [];
        for (const item of targetValue) {
          if (isUnknownStaticValue(item) || item === null || item === undefined) {
            return UNKNOWN_STATIC_VALUE;
          }
          if (typeof item !== 'string' && typeof item !== 'number' && typeof item !== 'boolean') {
            return UNKNOWN_STATIC_VALUE;
          }
          parts.push(String(item));
        }
        return parts.join(separator);
      }
      if (Array.isArray(targetValue) && methodName === 'sort') return [...targetValue];
      if (Array.isArray(targetValue) && methodName === 'slice') {
        const start = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        const end = evaluateStaticValue(ts, repo, parsed, expr.arguments[1], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if ((start === undefined || typeof start === 'number') && (end === undefined || typeof end === 'number')) {
          return targetValue.slice(start, end);
        }
      }
      if (typeof targetValue === 'string' && methodName === 'trim') return targetValue.trim();
      if (typeof targetValue === 'string' && methodName === 'toLowerCase') return targetValue.toLowerCase();
      if (typeof targetValue === 'string' && methodName === 'padStart') {
        const maxLength = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        const fillString = evaluateStaticValue(ts, repo, parsed, expr.arguments[1], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (typeof maxLength === 'number') {
          return targetValue.padStart(maxLength, typeof fillString === 'string' ? fillString : ' ');
        }
      }
      if (typeof targetValue === 'string' && methodName === 'startsWith') {
        const searchValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (typeof searchValue === 'string') return targetValue.startsWith(searchValue);
      }
      if (typeof targetValue === 'string' && methodName === 'endsWith') {
        const searchValue = evaluateStaticValue(ts, repo, parsed, expr.arguments[0], env, sourceCache, budgetState, importCache, valueCache, evalStack);
        if (typeof searchValue === 'string') return targetValue.endsWith(searchValue);
      }
    }
  }
  return UNKNOWN_STATIC_VALUE;
}

function populateStaticEnvFromBlock(ts, repo, parsed, block, env, sourceCache, budgetState, importCache, valueCache) {
  for (const statement of block?.statements || []) {
    if (ts.isFunctionDeclaration(statement) && statement.name?.text) {
      env.set(String(statement.name.text), makeStaticFunctionDescriptor(parsed, statement, env));
      continue;
    }
    if (ts.isExpressionStatement(statement)
      && applyStaticEffectStateUpdates(ts, repo, parsed, statement.expression, env, sourceCache, budgetState, importCache, valueCache)) {
      continue;
    }
    if (!ts.isVariableStatement(statement)) continue;
    for (const declaration of statement.declarationList.declarations || []) {
      if (bindUseStateDeclaration(ts, repo, parsed, declaration, env, sourceCache, budgetState, importCache, valueCache)) {
        continue;
      }
      const value = evaluateStaticValue(ts, repo, parsed, declaration.initializer, env, sourceCache, budgetState, importCache, valueCache);
      bindPatternStaticValue(ts, declaration.name, value, env);
    }
  }
}

function resolveVisibleReturnedJsxFromStatement(ts, repo, parsed, statement, env, sourceCache, budgetState, importCache, valueCache) {
  if (!statement) return JSX_RESOLUTION_CONTINUE;
  if (ts.isBlock(statement)) {
    const scope = new Map(env);
    for (const item of statement.statements || []) {
      const result = resolveVisibleReturnedJsxFromStatement(ts, repo, parsed, item, scope, sourceCache, budgetState, importCache, valueCache);
      if (result !== JSX_RESOLUTION_CONTINUE) return result;
    }
    const fallback = pickLastReturnedJsxExpression(ts, statement);
    return fallback || JSX_RESOLUTION_CONTINUE;
  }
  if (ts.isFunctionDeclaration(statement) && statement.name?.text) {
    env.set(String(statement.name.text), makeStaticFunctionDescriptor(parsed, statement, env));
    return JSX_RESOLUTION_CONTINUE;
  }
  if (ts.isVariableStatement(statement)) {
    for (const declaration of statement.declarationList.declarations || []) {
      const value = evaluateStaticValue(ts, repo, parsed, declaration.initializer, env, sourceCache, budgetState, importCache, valueCache);
      bindPatternStaticValue(ts, declaration.name, value, env);
    }
    return JSX_RESOLUTION_CONTINUE;
  }
  if (ts.isIfStatement(statement)) {
    const conditionValue = evaluateStaticValue(ts, repo, parsed, statement.expression, env, sourceCache, budgetState, importCache, valueCache);
    const truthiness = resolveStaticTruthiness(conditionValue);
    if (truthiness === true) {
      return resolveVisibleReturnedJsxFromStatement(ts, repo, parsed, statement.thenStatement, new Map(env), sourceCache, budgetState, importCache, valueCache);
    }
    if (truthiness === false) {
      return resolveVisibleReturnedJsxFromStatement(ts, repo, parsed, statement.elseStatement, new Map(env), sourceCache, budgetState, importCache, valueCache);
    }
    return JSX_RESOLUTION_CONTINUE;
  }
  if (ts.isReturnStatement(statement)) {
    if (!statement.expression || statement.expression.kind === ts.SyntaxKind.NullKeyword) return JSX_RESOLUTION_NONE;
    return expressionContainsJsx(ts, statement.expression) ? statement.expression : JSX_RESOLUTION_NONE;
  }
  return JSX_RESOLUTION_CONTINUE;
}

function resolveVisibleReturnedJsxExpression(ts, repo, parsed, body, env, sourceCache, budgetState, importCache, valueCache) {
  if (!body) return null;
  if (!ts.isBlock(body)) return expressionContainsJsx(ts, body) ? body : null;
  const result = resolveVisibleReturnedJsxFromStatement(ts, repo, parsed, body, new Map(env), sourceCache, budgetState, importCache, valueCache);
  if (result === JSX_RESOLUTION_CONTINUE || result === JSX_RESOLUTION_NONE) return null;
  return result;
}

function camelToKebabCase(value) {
  return String(value || '')
    .replace(/([a-z0-9])([A-Z])/g, '$1-$2')
    .replace(/([A-Z])([A-Z][a-z])/g, '$1-$2')
    .toLowerCase();
}

function loadLucideIconSource(ts, fullPath, visited = new Set()) {
  if (!fullPath || visited.has(fullPath) || !fs.existsSync(fullPath)) return null;
  visited.add(fullPath);
  const sourceText = fs.readFileSync(fullPath, 'utf8');
  const sourceFile = ts.createSourceFile(fullPath, sourceText, ts.ScriptTarget.Latest, true, ts.ScriptKind.JS);
  for (const statement of sourceFile.statements || []) {
    if (!ts.isExportDeclaration(statement) || !statement.moduleSpecifier || !ts.isStringLiteral(statement.moduleSpecifier)) {
      continue;
    }
    const exportClause = statement.exportClause;
    const reexportsDefault = !exportClause
      || (ts.isNamedExports(exportClause)
        && exportClause.elements.some((element) => String(element.name?.text || '') === 'default'));
    if (!reexportsDefault) continue;
    const specifier = String(statement.moduleSpecifier.text || '').trim();
    if (!specifier.startsWith('.')) continue;
    const nextFullPath = path.resolve(path.dirname(fullPath), specifier);
    const candidate = fs.existsSync(nextFullPath)
      ? nextFullPath
      : fs.existsSync(`${nextFullPath}.js`)
        ? `${nextFullPath}.js`
        : '';
    if (!candidate) continue;
    const resolved = loadLucideIconSource(ts, candidate, visited);
    if (resolved) return resolved;
  }
  for (const statement of sourceFile.statements || []) {
    if (!ts.isVariableStatement(statement)) continue;
    for (const declaration of statement.declarationList.declarations || []) {
      const initializer = declaration.initializer;
      if (!initializer || !ts.isCallExpression(initializer) || initializer.arguments.length < 2) continue;
      if (!ts.isIdentifier(initializer.expression) || initializer.expression.text !== 'createLucideIcon') continue;
      const iconNameExpr = initializer.arguments[0];
      const shapeArray = initializer.arguments[1];
      if (!ts.isStringLiteral(iconNameExpr) || !ts.isArrayLiteralExpression(shapeArray)) continue;
      const shapes = shapeArray.elements
        .map((element) => {
          if (!ts.isArrayLiteralExpression(element) || element.elements.length < 1) return null;
          const tagExpr = element.elements[0];
          const tag = ts.isStringLiteral(tagExpr) ? tagExpr.text : '';
          return tag ? { tag } : null;
        })
        .filter(Boolean);
      return {
        iconName: String(iconNameExpr.text || '').trim(),
        shapes,
      };
    }
  }
  return null;
}

function loadLucideIconSpec(ts, repo, iconName, lucideIconCache) {
  const key = String(iconName || '').trim();
  if (!key) return null;
  if (lucideIconCache.has(key)) return lucideIconCache.get(key);
  const relPath = path.join('node_modules', 'lucide-react', 'dist', 'esm', 'icons', `${camelToKebabCase(key)}.js`);
  const fullPath = path.join(repo, relPath);
  if (!fs.existsSync(fullPath)) {
    lucideIconCache.set(key, null);
    return null;
  }
  const iconSource = loadLucideIconSource(ts, fullPath);
  const resolvedIconName = String(iconSource?.iconName || key).trim() || key;
  const shapes = Array.isArray(iconSource?.shapes) ? iconSource.shapes : [];
  const out = {
    iconName: resolvedIconName,
    className: `lucide lucide-${camelToKebabCase(resolvedIconName)}`,
    shapes,
  };
  lucideIconCache.set(key, out);
  return out;
}

function loadLucideIconSpecFromStaticValue(ts, repo, value, lucideIconCache) {
  if (!isStaticImportBindingDescriptor(value)) return null;
  if (value.specifier !== 'lucide-react') return null;
  return loadLucideIconSpec(ts, repo, value.importedName, lucideIconCache);
}

function extractJsxAttributes(ts, repo, parsed, attributes, env, sourceCache, budgetState, importCache, valueCache) {
  const out = {};
  const props = Array.isArray(attributes?.properties) ? attributes.properties : [];
  for (const property of props) {
    if (!ts.isJsxAttribute(property)) continue;
    const key = String(property.name?.text || '').trim();
    if (!key) continue;
    if (!property.initializer) {
      out[key] = 'true';
      continue;
    }
    if (ts.isStringLiteral(property.initializer)) {
      out[key] = property.initializer.text;
      continue;
    }
    if (ts.isJsxExpression(property.initializer) && property.initializer.expression) {
      const staticValue = evaluateStaticValue(
        ts,
        repo,
        parsed,
        property.initializer.expression,
        env,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
      );
      const resolvedText = staticValueToInlineText(staticValue);
      if (resolvedText) {
        out[key] = resolvedText;
        continue;
      }
      const value = extractStaticExpressionText(ts, property.initializer.expression);
      if (value) out[key] = value;
    }
  }
  return out;
}

function extractJsxAttributeExpressions(ts, attributes) {
  const out = {};
  const props = Array.isArray(attributes?.properties) ? attributes.properties : [];
  for (const property of props) {
    if (!ts.isJsxAttribute(property)) continue;
    const key = String(property.name?.text || '').trim();
    if (!key) continue;
    if (!property.initializer) {
      out[key] = true;
      continue;
    }
    if (ts.isStringLiteral(property.initializer)) {
      out[key] = property.initializer.text;
      continue;
    }
    if (ts.isJsxExpression(property.initializer)) {
      out[key] = property.initializer.expression || null;
    }
  }
  return out;
}

function collectJsxAttributeNames(attributes) {
  const out = [];
  const props = Array.isArray(attributes?.properties) ? attributes.properties : [];
  for (const property of props) {
    if (!property?.name?.text) continue;
    out.push(String(property.name.text).trim());
  }
  return out.filter(Boolean);
}

function splitClassTokens(className) {
  return String(className || '').split(/\s+/).map((item) => item.trim()).filter(Boolean);
}

function hasClassPrefix(classTokens, prefix) {
  return classTokens.some((token) => token === prefix || token.startsWith(`${prefix}-`) || token.startsWith(`${prefix}:`));
}

function classifyLayoutRole(tagName, nodeKind, classTokens, attrNames, attrs) {
  const tag = String(tagName || '').trim().toLowerCase();
  if (nodeKind === 'tree_button' || tag === 'button') return 'button';
  if (nodeKind === 'tree_input' || tag === 'input' || tag === 'textarea' || tag === 'select') return 'input';
  if (nodeKind === 'tree_link' || tag === 'a') return 'link';
  if (nodeKind === 'tree_media' || tag === 'img' || tag === 'video' || tag === 'canvas') return 'media';
  if (classTokens.includes('grid')) return 'grid';
  if (classTokens.includes('flex') || classTokens.includes('inline-flex')) {
    if (classTokens.includes('flex-col')) return 'flex_col';
    if (classTokens.includes('flex-row')) return 'flex_row';
    return 'flex';
  }
  if (classTokens.includes('fixed')) return 'fixed_layer';
  if (classTokens.includes('absolute')) return 'absolute_layer';
  if (classTokens.includes('sticky')) return 'sticky_layer';
  if (classTokens.includes('overflow-auto') || classTokens.includes('overflow-y-auto') || classTokens.includes('overflow-x-auto')) return 'scroll_area';
  if (classTokens.includes('rounded') || hasClassPrefix(classTokens, 'rounded')) return 'surface';
  if (String(attrs.role || '').trim()) return `role:${String(attrs.role).trim()}`;
  if (attrNames.some((name) => /^on[A-Z]/.test(name))) return 'interactive';
  return 'element';
}

function collectStyleTraits(classTokens, attrNames, attrs) {
  const traits = [];
  const add = (value) => {
    const text = String(value || '').trim();
    if (!text || traits.includes(text)) return;
    traits.push(text);
  };
  if (classTokens.includes('fixed')) add('fixed');
  if (classTokens.includes('absolute')) add('absolute');
  if (classTokens.includes('sticky')) add('sticky');
  if (classTokens.includes('grid')) add('grid');
  if (classTokens.includes('flex') || classTokens.includes('inline-flex')) add(classTokens.includes('flex-col') ? 'flex-col' : (classTokens.includes('flex-row') ? 'flex-row' : 'flex'));
  if (hasClassPrefix(classTokens, 'gap')) add('gap');
  if (hasClassPrefix(classTokens, 'space-x') || hasClassPrefix(classTokens, 'space-y')) add('space');
  if (hasClassPrefix(classTokens, 'p') || hasClassPrefix(classTokens, 'px') || hasClassPrefix(classTokens, 'py')) add('padding');
  if (hasClassPrefix(classTokens, 'm') || hasClassPrefix(classTokens, 'mx') || hasClassPrefix(classTokens, 'my')) add('margin');
  if (hasClassPrefix(classTokens, 'w') || hasClassPrefix(classTokens, 'h') || classTokens.includes('size-full')) add('size');
  if (classTokens.some((token) => token.startsWith('bg-'))) add('bg');
  if (classTokens.some((token) => token === 'border' || token.startsWith('border-'))) add('border');
  if (hasClassPrefix(classTokens, 'rounded')) add('rounded');
  if (classTokens.some((token) => token.startsWith('shadow'))) add('shadow');
  if (classTokens.some((token) => token.startsWith('text-'))) add('text');
  if (classTokens.some((token) => token.startsWith('font-'))) add('font');
  if (classTokens.some((token) => token.startsWith('overflow-'))) add('overflow');
  if (classTokens.some((token) => token.startsWith('backdrop-'))) add('backdrop');
  if (classTokens.some((token) => token.startsWith('animate-') || token.startsWith('transition'))) add('motion');
  if (attrNames.some((name) => /^on[A-Z]/.test(name))) add('event');
  if (String(attrs.role || '').trim()) add(`role:${String(attrs.role).trim()}`);
  if (String(attrs.type || '').trim()) add(`type:${String(attrs.type).trim()}`);
  if (String(attrs.placeholder || '').trim()) add('placeholder');
  return traits;
}

function summarizeClassTokens(classTokens, maxCount = 3) {
  if (!Array.isArray(classTokens) || classTokens.length === 0) return '';
  return classTokens.slice(0, maxCount).join(' ');
}

function buildNodeDetailText(node) {
  const parts = [];
  const layoutRole = String(node.layout_role || '').trim();
  if (layoutRole) parts.push(layoutRole);
  for (const trait of Array.isArray(node.style_traits) ? node.style_traits.slice(0, 4) : []) {
    parts.push(String(trait || '').trim());
  }
  const classPreview = String(node.class_preview || '').trim();
  if (classPreview) parts.push(classPreview);
  return normalizeInlineText(parts.join('  '), 96);
}

function collectDirectChildText(ts, repo, parsed, children, env, sourceCache, budgetState, importCache, valueCache) {
  const values = [];
  for (const child of children || []) {
    if (ts.isJsxText(child)) {
      const text = normalizeInlineText(child.getText(), 56);
      if (text) values.push(text);
      continue;
    }
    if (ts.isJsxExpression(child) && child.expression) {
      const staticValue = evaluateStaticValue(
        ts,
        repo,
        parsed,
        child.expression,
        env,
        sourceCache,
        budgetState,
        importCache,
        valueCache,
      );
      const text = normalizeInlineText(
        staticValueToInlineText(staticValue) || extractStaticExpressionText(ts, child.expression),
        56,
      );
      if (text) values.push(text);
    }
  }
  return normalizeInlineText(values.join(' '), 56);
}

function classifyTreeNodeKind(tagName) {
  const tag = String(tagName || '').trim().toLowerCase();
  if (tag === 'button') return 'tree_button';
  if (tag === 'input' || tag === 'textarea' || tag === 'select') return 'tree_input';
  if (tag === 'img' || tag === 'video' || tag === 'canvas') return 'tree_media';
  if (tag === 'a') return 'tree_link';
  return 'tree_element';
}

function elementLabel(tagName, attrs, textSnippet) {
  const parts = [`<${tagName}>`];
  const directText = normalizeInlineText(textSnippet, 40);
  if (directText) {
    parts.push(directText);
  } else if (attrs.placeholder) {
    parts.push(attrs.placeholder);
  } else if (attrs['aria-label']) {
    parts.push(attrs['aria-label']);
  } else if (attrs.className) {
    const classes = String(attrs.className).split(/\s+/).filter(Boolean).slice(0, 2);
    if (classes.length > 0) parts.push(`.${classes.join('.')}`);
  }
  return normalizeInlineText(parts.join(' '), 76);
}

function createOutlineBuilder(ts, repo, sourceCache, uniqueComponents) {
  const state = createLayoutSurfaceBudgetState();
  const importCache = new Map();
  const valueCache = new Map();
  const lucideIconCache = new Map();

  const allocateNode = (node) => {
    assertLayoutSurfaceBudget(state, 'allocate_node');
    if (state.nextId >= state.maxNodes) {
      state.truncated = true;
      return null;
    }
    state.nextId += 1;
    return {
      id: `node_${state.nextId}`,
      children: [],
      ...node,
    };
  };

  const buildPropsEnv = (parsed, attrExpressions, env) => {
    const out = new Map();
    for (const [key, rawValue] of Object.entries(attrExpressions || {})) {
      if (rawValue && typeof rawValue === 'object' && typeof rawValue.kind === 'number') {
        out.set(key, evaluateStaticValue(ts, repo, parsed, rawValue, env, sourceCache, state, importCache, valueCache));
      } else {
        out.set(key, rawValue);
      }
    }
    return out;
  };

  const buildComponentStaticEnv = (parsed, declaration, propsEnv = new Map()) => {
    const env = new Map(propsEnv);
    const propsObject = Object.fromEntries(propsEnv.entries());
    for (const param of declaration?.node?.parameters || []) {
      bindPatternStaticValue(ts, param.name, propsEnv.get('props') ?? propsObject, env);
    }
    if (declaration?.body && ts.isBlock(declaration.body)) {
      populateStaticEnvFromBlock(ts, repo, parsed, declaration.body, env, sourceCache, state, importCache, valueCache);
    }
    return env;
  };

  const resolveComponentRenderExpr = (parsed, declaration, propsEnv = new Map()) => {
    if (!declaration) return { env: new Map(propsEnv), expr: null };
    const env = buildComponentStaticEnv(parsed, declaration, propsEnv);
    return {
      env,
      expr: resolveVisibleReturnedJsxExpression(ts, repo, parsed, declaration.body, env, sourceCache, state, importCache, valueCache),
    };
  };

  const buildComponentChildren = (modulePath, componentName, stack, propsEnv = new Map()) => {
    const parsed = parseSourceModule(ts, repo, modulePath, sourceCache, state);
    if (!parsed?.sourceFile) return [];
    const declaration = findComponentDeclaration(ts, parsed.sourceFile, componentName);
    if (!declaration) return [];
    const { env, expr } = resolveComponentRenderExpr(parsed, declaration, propsEnv);
    if (!expr) return [];
    return buildNodes(expr, parsed, componentName, stack, env);
  };

  const buildBranchChildren = (label, children) => {
    const branchNode = allocateNode({
      kind: 'tree_branch',
      label,
      line: 0,
    });
    if (!branchNode) return [];
    branchNode.children.push(...children);
    return [branchNode];
  };

  const buildNodes = (expr, parsed, ownerComponentName, stack = new Set(), env = new Map()) => {
    if (!expr || state.truncated) return [];
    const sourceFile = parsed.sourceFile;

    if (ts.isParenthesizedExpression(expr)) {
      return buildNodes(expr.expression, parsed, ownerComponentName, stack, env);
    }
    if (ts.isJsxFragment(expr)) {
      const fragmentNode = allocateNode({
        kind: 'tree_fragment',
        label: '<>',
        line: lineOfTsNode(ts, sourceFile, expr),
      });
      if (!fragmentNode) return [];
      for (const child of expr.children || []) {
        const nextExpr = ts.isJsxExpression(child) ? child.expression : child;
        fragmentNode.children.push(...buildNodes(nextExpr, parsed, ownerComponentName, stack, env));
      }
      return [fragmentNode];
    }
    if (ts.isJsxElement(expr) || ts.isJsxSelfClosingElement(expr)) {
      const opening = ts.isJsxElement(expr) ? expr.openingElement : expr;
      const tagName = String(opening.tagName.getText(sourceFile) || '').trim();
      const attrs = extractJsxAttributes(ts, repo, parsed, opening.attributes, env, sourceCache, state, importCache, valueCache);
      const attrExpressions = extractJsxAttributeExpressions(ts, opening.attributes);
      const attrNames = collectJsxAttributeNames(opening.attributes);
      const classTokens = splitClassTokens(attrs.className);
      const line = lineOfTsNode(ts, sourceFile, opening);
      const children = ts.isJsxElement(expr) ? expr.children : [];
      const directChildText = collectDirectChildText(ts, repo, parsed, children, env, sourceCache, state, importCache, valueCache);
      if (/^[a-z]/.test(tagName)) {
        const nodeKind = classifyTreeNodeKind(tagName);
        const node = allocateNode({
          kind: nodeKind,
          tag: tagName,
          label: elementLabel(
            tagName,
            attrs,
            directChildText,
          ),
          line,
          module_path: parsed.modulePath,
          component_name: ownerComponentName,
          attr_names: attrNames,
          class_tokens: classTokens,
          class_preview: summarizeClassTokens(classTokens),
          layout_role: classifyLayoutRole(tagName, nodeKind, classTokens, attrNames, attrs),
          style_traits: collectStyleTraits(classTokens, attrNames, attrs),
          detail_text: '',
        });
        if (!node) return [];
        node.detail_text = buildNodeDetailText(node);
        for (const child of children) {
          const nextExpr = ts.isJsxExpression(child) ? child.expression : child;
          node.children.push(...buildNodes(nextExpr, parsed, ownerComponentName, stack, env));
        }
        return [node];
      }
      const ref = uniqueComponents.get(tagName) || null;
      const importBinding = getModuleImportBindings(ts, repo, parsed, importCache).get(tagName) || null;
      const boundValue = env.has(tagName)
        ? env.get(tagName)
        : resolveIdentifierStaticValue(ts, repo, parsed, tagName, env, sourceCache, state, importCache, valueCache, new Set());
      const iconSpec = !ref && importBinding?.external && importBinding.specifier === 'lucide-react'
        ? loadLucideIconSpec(ts, repo, importBinding.importedName, lucideIconCache)
        : (loadLucideIconSpecFromStaticValue(ts, repo, boundValue, lucideIconCache) || null);
      const componentNode = allocateNode({
        kind: ref ? 'tree_component' : (iconSpec ? 'tree_element' : 'tree_component_ref'),
        tag: tagName,
        label: iconSpec
          ? elementLabel('svg', { className: normalizeInlineText(`${iconSpec.className} ${attrs.className || ''}`, 96) }, '')
          : normalizeInlineText(`<${tagName}>`, 76),
        line,
        module_path: parsed.modulePath,
        component_name: ownerComponentName,
        attr_names: attrNames,
        class_tokens: iconSpec ? splitClassTokens(`${iconSpec.className} ${attrs.className || ''}`) : classTokens,
        class_preview: iconSpec
          ? normalizeInlineText(`${iconSpec.className} ${attrs.className || ''}`, 96)
          : summarizeClassTokens(classTokens),
        layout_role: iconSpec
          ? classifyLayoutRole('svg', 'tree_element', splitClassTokens(`${iconSpec.className} ${attrs.className || ''}`), attrNames, attrs)
          : (ref ? 'component' : 'component_ref'),
        style_traits: iconSpec
          ? collectStyleTraits(splitClassTokens(`${iconSpec.className} ${attrs.className || ''}`), attrNames, attrs)
          : collectStyleTraits(classTokens, attrNames, attrs),
        detail_text: '',
      });
      if (!componentNode) return [];
      componentNode.detail_text = buildNodeDetailText(componentNode);
      if (iconSpec) {
        componentNode.tag = 'svg';
        for (const shape of iconSpec.shapes || []) {
          const iconChild = allocateNode({
            kind: 'tree_element',
            tag: shape.tag,
            label: normalizeInlineText(`<${shape.tag}>`, 76),
            line,
            module_path: parsed.modulePath,
            component_name: ownerComponentName,
            attr_names: [],
            class_tokens: [],
            class_preview: '',
            layout_role: 'element',
            style_traits: [],
            detail_text: '',
          });
          if (!iconChild) break;
          iconChild.detail_text = buildNodeDetailText(iconChild);
          componentNode.children.push(iconChild);
        }
        return [componentNode];
      }
      if (ref) {
        if (state.componentExpansionCount >= state.maxComponentExpansions) {
          throw new Error(`native_gui_layout_surface_component_budget_exceeded:${state.maxComponentExpansions}`);
        }
        state.componentExpansionCount += 1;
        assertLayoutSurfaceBudget(state, 'expand_component');
        const cycleKey = `${ref.modulePath}#${ref.componentName}`;
        if (!stack.has(cycleKey)) {
          stack.add(cycleKey);
          componentNode.children.push(...buildComponentChildren(
            ref.modulePath,
            ref.componentName,
            stack,
            buildPropsEnv(parsed, attrExpressions, env),
          ));
          stack.delete(cycleKey);
        }
      }
      return [componentNode];
    }
    if (ts.isJsxText(expr)) {
      const text = normalizeInlineText(expr.getText(), 68);
      if (!text) return [];
      const node = allocateNode({
        kind: 'tree_text',
        label: `# ${text}`,
        line: lineOfTsNode(ts, sourceFile, expr),
        module_path: parsed.modulePath,
        component_name: ownerComponentName,
        attr_names: [],
        class_tokens: [],
        class_preview: '',
        layout_role: 'text',
        style_traits: [],
        detail_text: '',
      });
      if (node) node.detail_text = buildNodeDetailText(node);
      return node ? [node] : [];
    }
    if (ts.isJsxExpression(expr)) {
      return buildNodes(expr.expression, parsed, ownerComponentName, stack, env);
    }
    if (ts.isConditionalExpression(expr)) {
      const conditionValue = evaluateStaticValue(ts, repo, parsed, expr.condition, env, sourceCache, state, importCache, valueCache);
      const truthiness = resolveStaticTruthiness(conditionValue);
      if (truthiness === true) {
        return buildNodes(expr.whenTrue, parsed, ownerComponentName, stack, env);
      }
      if (truthiness === false) {
        return buildNodes(expr.whenFalse, parsed, ownerComponentName, stack, env);
      }
      const thenNodes = buildNodes(expr.whenTrue, parsed, ownerComponentName, stack, env);
      const elseNodes = buildNodes(expr.whenFalse, parsed, ownerComponentName, stack, env);
      if (thenNodes.length > 0 && elseNodes.length === 0) return thenNodes;
      if (elseNodes.length > 0 && thenNodes.length === 0) return elseNodes;
      if (thenNodes.length === 0 && elseNodes.length === 0) return [];
      return [
        ...buildBranchChildren('if ?', thenNodes),
        ...buildBranchChildren('else', elseNodes),
      ];
    }
    if (ts.isBinaryExpression(expr)) {
      if (expr.operatorToken.kind === ts.SyntaxKind.AmpersandAmpersandToken) {
        const conditionValue = evaluateStaticValue(ts, repo, parsed, expr.left, env, sourceCache, state, importCache, valueCache);
        const truthiness = resolveStaticTruthiness(conditionValue);
        if (truthiness === false) return [];
        if (truthiness === true) return buildNodes(expr.right, parsed, ownerComponentName, stack, env);
        return buildBranchChildren('&&', buildNodes(expr.right, parsed, ownerComponentName, stack, env));
      }
      if (expr.operatorToken.kind === ts.SyntaxKind.BarBarToken) {
        const conditionValue = evaluateStaticValue(ts, repo, parsed, expr.left, env, sourceCache, state, importCache, valueCache);
        const truthiness = resolveStaticTruthiness(conditionValue);
        if (truthiness === true) return [];
        if (truthiness === false) return buildNodes(expr.right, parsed, ownerComponentName, stack, env);
        return buildBranchChildren('||', buildNodes(expr.right, parsed, ownerComponentName, stack, env));
      }
    }
    if (ts.isArrayLiteralExpression(expr)) {
      const out = [];
      for (const element of expr.elements) {
        out.push(...buildNodes(element, parsed, ownerComponentName, stack, env));
      }
      return out;
    }
    if (ts.isCallExpression(expr)
      && ts.isPropertyAccessExpression(expr.expression)
      && expr.expression.name.text === 'map'
      && expr.arguments.length > 0
      && (ts.isArrowFunction(expr.arguments[0]) || ts.isFunctionExpression(expr.arguments[0]))) {
      const callback = expr.arguments[0];
      const collectionValue = evaluateStaticValue(
        ts,
        repo,
        parsed,
        expr.expression.expression,
        env,
        sourceCache,
        state,
        importCache,
        valueCache,
      );
      const items = Array.isArray(collectionValue) && collectionValue.length > 0
        ? collectionValue.slice(0, DEFAULT_LAYOUT_SURFACE_MAP_EXPANSION_LIMIT)
        : [UNKNOWN_STATIC_VALUE];
      const repeatNode = allocateNode({
        kind: 'tree_repeat',
        label: '{map()}',
        line: lineOfTsNode(ts, sourceFile, expr),
        module_path: parsed.modulePath,
        component_name: ownerComponentName,
        attr_names: [],
        class_tokens: [],
        class_preview: '',
        layout_role: 'repeat',
        style_traits: ['collection'],
        detail_text: '',
      });
      if (!repeatNode) return [];
      repeatNode.detail_text = buildNodeDetailText(repeatNode);
      for (let index = 0; index < items.length; index += 1) {
        const itemValue = items[index];
        const callbackEnv = new Map(env);
        for (let paramIndex = 0; paramIndex < (callback.parameters || []).length; paramIndex += 1) {
          const param = callback.parameters[paramIndex];
          const argValue = paramIndex === 0
            ? itemValue
            : paramIndex === 1
              ? index
              : collectionValue;
          bindPatternStaticValue(ts, param.name, argValue, callbackEnv);
        }
        const callbackBody = ts.isBlock(callback.body)
          ? resolveVisibleReturnedJsxExpression(ts, repo, parsed, callback.body, callbackEnv, sourceCache, state, importCache, valueCache)
          : callback.body;
        repeatNode.children.push(...buildNodes(callbackBody, parsed, ownerComponentName, stack, callbackEnv));
      }
      return [repeatNode];
    }
    if (ts.isCallExpression(expr)
      && ts.isIdentifier(expr.expression)
      && expr.arguments.length === 0) {
      const helperName = String(expr.expression.text || '').trim();
      if (helperName) {
        const declaration = findComponentDeclaration(ts, parsed.sourceFile, ownerComponentName);
        const helperBody = findLocalRenderHelperBody(ts, declaration?.body || null, helperName);
        if (helperBody) {
          const helperKey = `${parsed.modulePath}#${ownerComponentName}#helper:${helperName}`;
          if (stack.has(helperKey)) return [];
          stack.add(helperKey);
          const helperExpr = resolveVisibleReturnedJsxExpression(ts, repo, parsed, helperBody, env, sourceCache, state, importCache, valueCache);
          const helperNodes = helperExpr
            ? buildNodes(helperExpr, parsed, ownerComponentName, stack, env)
            : [];
          stack.delete(helperKey);
          return helperNodes;
        }
      }
    }
    return [];
  };

  return {
    buildComponentTree(modulePath, componentName, initialEnv = new Map()) {
      state.nextId = 0;
      state.truncated = false;
      state.componentExpansionCount = 0;
      state.parsedModuleCount = 0;
      const parsed = parseSourceModule(ts, repo, modulePath, sourceCache, state);
      if (!parsed?.sourceFile) return { nodes: [], truncated: false, reason: 'parsed_module_missing' };
      const declaration = findComponentDeclaration(ts, parsed.sourceFile, componentName);
      if (!declaration) return { nodes: [], truncated: false, reason: 'component_declaration_missing' };
      const { env, expr } = resolveComponentRenderExpr(parsed, declaration, new Map(initialEnv));
      if (!expr) return { nodes: [], truncated: false, reason: 'visible_returned_jsx_missing' };
      const stack = new Set([`${modulePath}#${componentName}`]);
      const nodes = buildNodes(expr, parsed, componentName, stack, env);
      return {
        nodes,
        truncated: state.truncated,
        component_expansion_count: state.componentExpansionCount,
        parsed_module_count: state.parsedModuleCount,
        reason: nodes.length > 0 ? 'ok' : 'outline_empty',
      };
    },
  };
}

function isPureImplementationWrapperNode(node) {
  if (!node || String(node.kind || '') !== 'tree_element') return false;
  if (String(node.tag || '').trim() !== 'div') return false;
  const componentName = String(node.component_name || '').trim();
  if (componentName !== 'VirtualizedMasonry' && componentName !== 'HeightReporter') return false;
  if (String(node.class_preview || '').trim()) return false;
  if (String(node.label || '').trim() !== '<div>') return false;
  const attrNames = Array.isArray(node.attr_names) ? node.attr_names.map((item) => String(item || '').trim()).filter(Boolean) : [];
  if (attrNames.length <= 0) return true;
  return attrNames.every((name) => name === 'style' || name === 'key' || name === 'ref');
}

function buildSpeculativeRepeatNodeKey(node) {
  return [
    String(node?.kind || ''),
    String(node?.tag || ''),
    String(node?.module_path || ''),
    String(node?.component_name || ''),
    String(node?.line || 0),
    String(node?.class_preview || ''),
    String(node?.label || ''),
    String(node?.text_preview || ''),
  ].join('|');
}

function flattenLayoutTree(nodes, depth = 0, out = [], context = null) {
  const state = context || {
    repeatStack: [],
    speculativeBranchDepth: 0,
  };
  for (const node of nodes || []) {
    const kind = String(node?.kind || '');
    if (!node) continue;
    if (isPureImplementationWrapperNode(node)) {
      flattenLayoutTree(node.children, depth, out, state);
      continue;
    }
    const entersRepeat = kind === 'tree_repeat';
    const entersSpeculativeBranch = kind === 'tree_branch' && state.repeatStack.length > 0;
    if (entersRepeat) {
      state.repeatStack.push(new Set());
    }
    if (entersSpeculativeBranch) {
      state.speculativeBranchDepth += 1;
    }
    let skipNode = false;
    if (kind === 'tree_text') {
      skipNode = true;
    } else if (state.repeatStack.length > 0 && state.speculativeBranchDepth > 0) {
      const dedupeSet = state.repeatStack[state.repeatStack.length - 1];
      const dedupeKey = buildSpeculativeRepeatNodeKey(node);
      if (dedupeSet.has(dedupeKey)) {
        skipNode = true;
      } else {
        dedupeSet.add(dedupeKey);
      }
    }
    if (!skipNode) {
      out.push({ ...node, depth });
    }
    flattenLayoutTree(node.children, skipNode ? depth : depth + 1, out, state);
    if (entersSpeculativeBranch) {
      state.speculativeBranchDepth -= 1;
    }
    if (entersRepeat) {
      state.repeatStack.pop();
    }
  }
  return out;
}

function needsSyntheticMountRoot(flatNodes) {
  if (!Array.isArray(flatNodes) || flatNodes.length === 0) return false;
  return !flatNodes.some((node) =>
    String(node?.kind || '') === 'tree_element'
    && String(node?.tag || '') === 'div'
    && String(node?.class_preview || '') === ''
    && Number(node?.depth || 0) === 0);
}

function treeNodeStyle(theme, node) {
  switch (String(node?.kind || '')) {
    case 'tree_component':
      return {
        background_color: theme.component_background,
        border_color: theme.component_border,
        text_color: theme.text_primary,
        font_size: 14,
        font_weight: 'semibold',
      };
    case 'tree_component_ref':
      return {
        background_color: theme.chip_background,
        border_color: theme.border_color,
        text_color: theme.text_secondary,
        font_size: 13,
        font_weight: 'medium',
      };
    case 'tree_text':
      return {
        background_color: theme.text_background,
        border_color: theme.border_color,
        text_color: theme.text_muted,
        font_size: 12,
        font_weight: 'regular',
      };
    case 'tree_button':
      return {
        background_color: theme.button_background,
        border_color: theme.button_border,
        text_color: theme.text_primary,
        font_size: 13,
        font_weight: 'semibold',
      };
    case 'tree_input':
      return {
        background_color: theme.input_background,
        border_color: theme.input_border,
        text_color: theme.text_secondary,
        font_size: 13,
        font_weight: 'regular',
      };
    case 'tree_branch':
    case 'tree_repeat':
      return {
        background_color: theme.branch_background,
        border_color: theme.branch_border,
        text_color: theme.text_secondary,
        font_size: 12,
        font_weight: 'medium',
      };
    default:
      return {
        background_color: '#ffffff',
        border_color: theme.border_color,
        text_color: theme.text_secondary,
        font_size: 13,
        font_weight: 'regular',
      };
  }
}

function incrementHistogram(map, key) {
  const text = String(key || '').trim() || 'unknown';
  map.set(text, (map.get(text) || 0) + 1);
}

function buildSortedHistogram(map) {
  return Object.fromEntries([...map.entries()].sort(([left], [right]) => left.localeCompare(right)));
}

function classifyStyleLayoutVisualRole(node) {
  const kind = String(node?.kind || '');
  const layoutRole = String(node?.layout_role || '');
  const traits = new Set(Array.isArray(node?.style_traits) ? node.style_traits.map((item) => String(item || '')) : []);
  if (layoutRole === 'fixed_layer' || layoutRole === 'absolute_layer' || traits.has('fixed') || traits.has('absolute')) return 'overlay';
  if (kind === 'tree_button' || kind === 'tree_input' || kind === 'tree_link') return 'control';
  if (kind === 'tree_component' || kind === 'tree_component_ref') return 'component';
  if (layoutRole === 'scroll_area' || traits.has('overflow')) return 'scroll_area';
  if (layoutRole === 'surface' || traits.has('bg') || traits.has('border') || traits.has('rounded') || traits.has('shadow')) return 'surface';
  if (layoutRole === 'grid' || layoutRole === 'flex' || layoutRole === 'flex_col' || layoutRole === 'flex_row' || layoutRole === 'repeat' || traits.has('grid') || traits.has('flex')) return 'container';
  if (kind === 'tree_text') return 'text';
  return 'inline';
}

function classifyStyleLayoutDensity(node, visualRole) {
  const traits = new Set(Array.isArray(node?.style_traits) ? node.style_traits.map((item) => String(item || '')) : []);
  if (visualRole === 'overlay' || visualRole === 'surface') return 'roomy';
  if (visualRole === 'text') return 'compact';
  if (traits.has('padding') || traits.has('gap') || traits.has('space')) return 'comfortable';
  if (visualRole === 'control' || visualRole === 'component') return 'comfortable';
  return 'regular';
}

function classifyStyleLayoutProminence(node, visualRole) {
  if (visualRole === 'overlay' || visualRole === 'surface' || visualRole === 'component') return 'high';
  if (visualRole === 'control' || visualRole === 'container' || visualRole === 'scroll_area') return 'medium';
  if (Number(node?.depth || 0) <= 1) return 'medium';
  return 'low';
}

function chooseStyleLayoutAccentTone(visualRole) {
  switch (String(visualRole || '')) {
    case 'overlay':
      return 'warning';
    case 'surface':
      return 'info';
    case 'control':
      return 'accent';
    case 'component':
      return 'component';
    case 'scroll_area':
      return 'muted';
    case 'container':
      return 'soft';
    case 'text':
      return 'neutral';
    default:
      return 'neutral';
  }
}

function buildStyleLayoutBadge(node, styleNode) {
  const visualRole = String(styleNode?.visual_role || '').trim();
  const layoutRole = String(node?.layout_role || '').trim();
  if (visualRole === 'overlay') return 'overlay';
  if (visualRole === 'surface') return 'surface';
  if (visualRole === 'control') return layoutRole || 'control';
  if (visualRole === 'component') return 'component';
  if (visualRole === 'scroll_area') return 'scroll';
  if (layoutRole) return layoutRole;
  return visualRole || 'inline';
}

function computeStyleLayoutRowHeight(node, styleNode) {
  const detailText = String(node?.detail_text || '').trim();
  const visualRole = String(styleNode?.visual_role || '');
  const density = String(styleNode?.density || '');
  let height = detailText ? 34 : 22;
  if (visualRole === 'overlay' || visualRole === 'surface') height += 6;
  else if (visualRole === 'control' || visualRole === 'component') height += 4;
  if (density === 'roomy') height += 2;
  if (density === 'compact') height -= 4;
  return Math.max(20, Math.min(46, height));
}

function buildStyleLayoutSurfaceDoc(layoutSurface) {
  if (!layoutSurface || String(layoutSurface.format || '') !== 'layout_surface_v1' || !layoutSurface.ready) {
    return null;
  }
  const visualRoleHistogram = new Map();
  const layoutRoleHistogram = new Map();
  const densityHistogram = new Map();
  const nodes = (Array.isArray(layoutSurface.nodes) ? layoutSurface.nodes : []).map((node, index) => {
    const visualRole = classifyStyleLayoutVisualRole(node);
    const density = classifyStyleLayoutDensity(node, visualRole);
    const prominence = classifyStyleLayoutProminence(node, visualRole);
    const accentTone = chooseStyleLayoutAccentTone(visualRole);
    const badgeText = buildStyleLayoutBadge(node, {
      visual_role: visualRole,
    });
    const rowHeight = computeStyleLayoutRowHeight(node, {
      visual_role: visualRole,
      density,
    });
    incrementHistogram(visualRoleHistogram, visualRole);
    incrementHistogram(layoutRoleHistogram, node.layout_role || 'inline');
    incrementHistogram(densityHistogram, density);
    return {
      node_id: String(node.id || `node_${index + 1}`),
      visual_role: visualRole,
      density,
      prominence,
      accent_tone: accentTone,
      badge_text: badgeText,
      row_height: rowHeight,
      detail_text: String(node.detail_text || ''),
      depth: Number(node.depth || 0),
      column_span_hint: visualRole === 'overlay' || visualRole === 'surface' ? 2 : 1,
      layer: visualRole === 'overlay' ? 'overlay' : 'content',
    };
  });
  const overlayNodeCount = nodes.filter((node) => node.visual_role === 'overlay').length;
  const surfaceNodeCount = nodes.filter((node) => node.visual_role === 'surface').length;
  const controlNodeCount = nodes.filter((node) => node.visual_role === 'control').length;
  return {
    format: 'style_layout_surface_v1',
    ready: true,
    route_state: String(layoutSurface.route_state || ''),
    source_module_path: String(layoutSurface.source_module_path || ''),
    source_component_name: String(layoutSurface.source_component_name || ''),
    node_count: nodes.length,
    overlay_node_count: overlayNodeCount,
    surface_node_count: surfaceNodeCount,
    control_node_count: controlNodeCount,
    visual_role_histogram: buildSortedHistogram(visualRoleHistogram),
    layout_role_histogram: buildSortedHistogram(layoutRoleHistogram),
    density_histogram: buildSortedHistogram(densityHistogram),
    nodes,
  };
}

function styleLayoutToneColors(theme, tone) {
  switch (String(tone || 'neutral')) {
    case 'warning':
      return {
        background_color: theme.branch_background,
        border_color: theme.branch_border,
        detail_color: theme.text_soft,
      };
    case 'info':
      return {
        background_color: theme.accent_soft,
        border_color: theme.component_border,
        detail_color: theme.text_soft,
      };
    case 'accent':
      return {
        background_color: theme.button_background,
        border_color: theme.button_border,
        detail_color: theme.text_soft,
      };
    case 'component':
      return {
        background_color: theme.component_background,
        border_color: theme.component_border,
        detail_color: theme.text_soft,
      };
    case 'soft':
      return {
        background_color: theme.chip_background,
        border_color: theme.border_color,
        detail_color: theme.text_soft,
      };
    case 'muted':
      return {
        background_color: theme.text_background,
        border_color: theme.border_color,
        detail_color: theme.text_muted,
      };
    default:
      return {
        background_color: '#ffffff',
        border_color: theme.border_color,
        detail_color: theme.text_muted,
      };
  }
}

function styleLayoutCardStyle(theme, node, styleNode) {
  const base = treeNodeStyle(theme, node);
  if (!styleNode) return { ...base, detail_color: theme.text_muted };
  const palette = styleLayoutToneColors(theme, styleNode.accent_tone);
  return {
    ...base,
    background_color: palette.background_color,
    border_color: palette.border_color,
    detail_color: palette.detail_color,
  };
}

function buildLayoutBadgeDetail(styleNode, node) {
  const badge = String(styleNode?.badge_text || '').trim();
  const detail = String(node?.detail_text || '').trim();
  if (badge && detail) return normalizeInlineText(`${badge}  ${detail}`, 96);
  if (badge) return badge;
  return detail;
}

function readLayoutPolicyInt(fields, key, fallback) {
  const value = parseIntField(fields, key, fallback);
  if (!Number.isFinite(value) || value <= 0) return fallback;
  return value;
}

function isLayoutNodeInteractive(node, styleNode) {
  const visualRole = String(styleNode?.visual_role || '').trim();
  const layoutRole = String(node?.layout_role || '').trim();
  if (visualRole === 'control') return true;
  if (['tree_button', 'tree_input', 'tree_link'].includes(String(node?.kind || ''))) return true;
  if (['button', 'input', 'link', 'interactive'].includes(layoutRole)) return true;
  if (layoutRole.startsWith('role:button')) return true;
  return Array.isArray(node?.attr_names) && node.attr_names.some((name) => /^on[A-Z]/.test(String(name || '')));
}

function buildNativeLayoutPlanDoc(fields, layoutSurface, styleLayoutSurface) {
  if (!layoutSurface || String(layoutSurface.format || '') !== 'layout_surface_v1' || !layoutSurface.ready) {
    return null;
  }
  if (!styleLayoutSurface || String(styleLayoutSurface.format || '') !== 'style_layout_surface_v1' || !styleLayoutSurface.ready) {
    return null;
  }
  const title = String(fields.window_title || 'cheng_gui_preview');
  const entryModule = String(fields.entry_module || 'entry_module');
  const routeState = String(fields.route_state || 'home_default');
  const windowWidth = parseIntField(fields, 'window_width', 390);
  const windowHeight = parseIntField(fields, 'window_height', 844);
  const layoutPolicySource = String(fields.layout_policy_source || 'node_default_v1').trim() || 'node_default_v1';
  const contentInset = readLayoutPolicyInt(fields, 'layout_content_inset', 16);
  const headerHeight = readLayoutPolicyInt(fields, 'layout_header_height', 34);
  const metaChipHeight = readLayoutPolicyInt(fields, 'layout_meta_chip_height', 22);
  const metaTextHeight = readLayoutPolicyInt(fields, 'layout_meta_text_height', 20);
  const topPadding = readLayoutPolicyInt(fields, 'layout_top_padding', 20);
  const topGap = readLayoutPolicyInt(fields, 'layout_top_gap', 12);
  const rowGap = readLayoutPolicyInt(fields, 'layout_row_gap', 6);
  const overlayGap = readLayoutPolicyInt(fields, 'layout_overlay_gap', 8);
  const bottomPadding = readLayoutPolicyInt(fields, 'layout_bottom_padding', 24);
  const overlayMinWidth = readLayoutPolicyInt(fields, 'layout_overlay_min_width', 152);
  const overlayMaxWidth = readLayoutPolicyInt(fields, 'layout_overlay_max_width', 220);
  const overlayWidthRatio = Math.max(0.1, Math.min(0.9, readLayoutPolicyInt(fields, 'layout_overlay_width_ratio_pct', 42) / 100));
  const twoColumnMinWidth = readLayoutPolicyInt(fields, 'layout_two_column_min_width', 260);
  const columnGap = readLayoutPolicyInt(fields, 'layout_column_gap', 10);
  const indentStep = readLayoutPolicyInt(fields, 'layout_indent_step', 10);
  const indentCap = readLayoutPolicyInt(fields, 'layout_indent_cap', 10);
  const theme = buildNativeGuiTheme();
  const styleNodeMap = new Map((Array.isArray(styleLayoutSurface.nodes) ? styleLayoutSurface.nodes : []).map((item) => [String(item.node_id || ''), item]));
  const contentStartY = topPadding + headerHeight + topGap + metaChipHeight + rowGap + metaTextHeight + topGap;
  const maxVisibleBottom = windowHeight - bottomPadding;
  const metaWidth = Math.max(0, windowWidth - (contentInset * 2));
  const overlayReservedWidth = Number(styleLayoutSurface.overlay_node_count || 0) > 0
    ? Math.max(overlayMinWidth, Math.min(overlayMaxWidth, Math.floor(windowWidth * overlayWidthRatio)))
    : 0;
  const contentGap = overlayReservedWidth > 0 ? 12 : 0;
  const contentAreaWidth = Math.max(140, windowWidth - (contentInset * 2) - overlayReservedWidth - contentGap);
  const canUseTwoColumns = contentAreaWidth >= twoColumnMinWidth;
  const effectiveColumnGap = canUseTwoColumns ? columnGap : 0;
  const columnWidth = canUseTwoColumns
    ? Math.max(120, Math.floor((contentAreaWidth - effectiveColumnGap) / 2))
    : contentAreaWidth;
  const overlayX = overlayReservedWidth > 0
    ? (windowWidth - contentInset - overlayReservedWidth)
    : (windowWidth - contentInset - Math.max(140, Math.min(contentAreaWidth, windowWidth - (contentInset * 2))));
  const plannedItems = [];
  const pushFixedItem = (item) => {
    plannedItems.push({
      z_index: Number(item.z_index || 0),
      visible_in_viewport: true,
      plan_role: String(item.plan_role || 'fixed'),
      layer: String(item.layer || 'chrome'),
      synthetic: true,
      ...item,
    });
  };

  pushFixedItem({
    id: 'root',
    kind: 'root',
    x: 0,
    y: 0,
    width: windowWidth,
    height: windowHeight,
    text: '',
    background_color: theme.panel_background,
    stretch_x: true,
    stretch_y: true,
    z_index: 0,
  });
  pushFixedItem({
    id: 'header',
    kind: 'label',
    x: contentInset,
    y: topPadding,
    width: metaWidth,
    height: headerHeight,
    text: `${title}  ${routeState}`,
    background_color: theme.accent_soft,
    border_color: '#bfd7f6',
    text_color: theme.text_primary,
    font_size: 24,
    font_weight: 'semibold',
    corner_radius: 18,
    stretch_x: true,
    z_index: 10,
  });
  pushFixedItem({
    id: 'meta_component',
    kind: 'tree_component_ref',
    x: contentInset,
    y: topPadding + headerHeight + topGap,
    width: metaWidth,
    height: metaChipHeight,
    text: `${layoutSurface.source_component_name}  @  ${layoutSurface.source_module_path}`,
    background_color: theme.chip_background,
    border_color: theme.border_color,
    text_color: theme.text_secondary,
    font_size: 12,
    font_weight: 'medium',
    corner_radius: 12,
    stretch_x: true,
    z_index: 10,
  });
  pushFixedItem({
    id: 'meta_surface',
    kind: 'tree_text',
    x: contentInset,
    y: topPadding + headerHeight + topGap + metaChipHeight + rowGap,
    width: metaWidth,
    height: metaTextHeight,
    text: `layout_surface_v1  nodes=${layoutSurface.node_count}  styled=${Number(layoutSurface.styled_node_count || 0)}  interactive=${Number(layoutSurface.interactive_node_count || 0)}  overlay=${Number(styleLayoutSurface.overlay_node_count || 0)}`,
    background_color: theme.text_background,
    border_color: theme.border_color,
    text_color: theme.text_muted,
    font_size: 12,
    font_weight: 'regular',
    corner_radius: 12,
    stretch_x: true,
    z_index: 10,
  });

  const contentNodes = [];
  const overlayNodes = [];
  for (const node of Array.isArray(layoutSurface.nodes) ? layoutSurface.nodes : []) {
    const styleNode = styleNodeMap.get(String(node.id || '')) || null;
    const planned = {
      node,
      styleNode,
      depth: Math.max(0, Number(node.depth || 0)),
      rowHeight: Number(styleNode?.row_height || (String(node.detail_text || '').trim() ? 34 : 22)),
      detailText: buildLayoutBadgeDetail(styleNode, node),
    };
    if (String(styleNode?.layer || '') === 'overlay') overlayNodes.push(planned);
    else contentNodes.push(planned);
  }

  let flowCursorY = contentStartY;
  let leftColumnY = contentStartY;
  let rightColumnY = contentStartY;
  for (const planned of contentNodes) {
    const node = planned.node;
    const styleNode = planned.styleNode;
    const interactive = isLayoutNodeInteractive(node, styleNode);
    const indent = Math.min(indentCap, planned.depth) * indentStep;
    const fullSpan = !canUseTwoColumns
      || Number(styleNode?.column_span_hint || 1) > 1
      || ['surface', 'component', 'scroll_area'].includes(String(styleNode?.visual_role || ''))
      || planned.depth <= 1;
    let x = contentInset + indent;
    let y = contentStartY;
    let width = contentAreaWidth;
    if (fullSpan) {
      y = Math.max(flowCursorY, leftColumnY, rightColumnY);
      width = Math.max(120, contentAreaWidth - indent);
      const nextY = y + planned.rowHeight + rowGap;
      flowCursorY = nextY;
      leftColumnY = nextY;
      rightColumnY = nextY;
    } else {
      const useLeft = leftColumnY <= rightColumnY;
      const columnBaseX = contentInset + (useLeft ? 0 : (columnWidth + effectiveColumnGap));
      y = useLeft ? leftColumnY : rightColumnY;
      x = columnBaseX + Math.min(indent, 12);
      width = Math.max(110, columnWidth - Math.min(indent, 12));
      if (useLeft) leftColumnY = y + planned.rowHeight + rowGap;
      else rightColumnY = y + planned.rowHeight + rowGap;
      flowCursorY = Math.min(leftColumnY, rightColumnY);
    }
    plannedItems.push({
      id: String(node.id || `layout_${plannedItems.length}`),
      source_node_id: String(node.id || ''),
      kind: String(node.kind || 'tree_element'),
      x,
      y,
      width,
      height: planned.rowHeight,
      text: String(node.label || ''),
      detail_text: planned.detailText,
      corner_radius: 10,
      stretch_x: fullSpan && planned.depth <= 2,
      z_index: 20,
      plan_role: fullSpan ? 'content_full_span' : 'content_column',
      layer: 'content',
      column: fullSpan ? 0 : (x >= (contentInset + columnWidth) ? 1 : 0),
      column_span: fullSpan ? Math.min(2, canUseTwoColumns ? 2 : 1) : 1,
      interactive,
      synthetic: false,
      visible_in_viewport: true,
      visual_role: String(styleNode?.visual_role || ''),
      density: String(styleNode?.density || ''),
      prominence: String(styleNode?.prominence || ''),
      accent_tone: String(styleNode?.accent_tone || ''),
      source_line: Number(node.line || 0),
      source_module_path: String(node.module_path || ''),
      source_component_name: String(node.component_name || ''),
      ...styleLayoutCardStyle(theme, node, styleNode),
    });
  }

  let overlayCursorY = contentStartY;
  for (const planned of overlayNodes) {
    const node = planned.node;
    const styleNode = planned.styleNode;
    const interactive = isLayoutNodeInteractive(node, styleNode);
    const width = overlayReservedWidth > 0 ? overlayReservedWidth : Math.max(140, Math.min(contentAreaWidth, windowWidth - (contentInset * 2)));
    plannedItems.push({
      id: String(node.id || `overlay_${plannedItems.length}`),
      source_node_id: String(node.id || ''),
      kind: String(node.kind || 'tree_element'),
      x: overlayX,
      y: overlayCursorY,
      width,
      height: Math.min(56, planned.rowHeight + 8),
      text: String(node.label || ''),
      detail_text: planned.detailText,
      corner_radius: 14,
      stretch_x: false,
      z_index: 40,
      plan_role: 'overlay_float',
      layer: 'overlay',
      column: canUseTwoColumns ? 2 : 1,
      column_span: 1,
      interactive,
      synthetic: false,
      visible_in_viewport: true,
      visual_role: String(styleNode?.visual_role || ''),
      density: String(styleNode?.density || ''),
      prominence: String(styleNode?.prominence || ''),
      accent_tone: String(styleNode?.accent_tone || ''),
      source_line: Number(node.line || 0),
      source_module_path: String(node.module_path || ''),
      source_component_name: String(node.component_name || ''),
      ...styleLayoutCardStyle(theme, node, styleNode),
    });
    overlayCursorY += Math.min(56, planned.rowHeight + 8) + overlayGap;
  }

  const contentHeight = Math.max(
    windowHeight,
    Math.max(
      contentStartY,
      flowCursorY,
      leftColumnY,
      rightColumnY,
      overlayCursorY,
      ...plannedItems.map((item) => Number(item.y || 0) + Number(item.height || 0)),
    ) + bottomPadding,
  );
  const viewportItems = plannedItems.filter((item) => {
    const itemId = String(item.id || '');
    if (itemId === 'root' || itemId === 'header' || itemId === 'meta_component' || itemId === 'meta_surface') return true;
    return (Number(item.y || 0) + Number(item.height || 0)) <= maxVisibleBottom;
  }).map((item) => ({
    ...item,
    visible_in_viewport: true,
  }));
  const clippedItemCount = plannedItems.length - viewportItems.length;
  if (clippedItemCount > 0) {
    const lastViewportBottom = viewportItems.reduce((max, item) => Math.max(max, Number(item.y || 0) + Number(item.height || 0)), contentStartY);
    const moreY = Math.min(maxVisibleBottom - 22, lastViewportBottom + rowGap);
    if ((moreY + 22) <= maxVisibleBottom) {
      viewportItems.push({
        id: 'layout_more',
        kind: 'tree_text',
        x: contentInset,
        y: moreY,
        width: Math.max(120, windowWidth - (contentInset * 2)),
        height: 22,
        text: `... ${clippedItemCount} more items`,
        detail_text: '',
        corner_radius: 10,
        stretch_x: true,
        z_index: 12,
        plan_role: 'viewport_more',
        layer: 'content',
        column: 0,
        column_span: canUseTwoColumns ? 2 : 1,
        synthetic: true,
        visible_in_viewport: true,
        ...treeNodeStyle(theme, { kind: 'tree_text' }),
      });
    }
  }

  const interactiveItemCount = plannedItems.filter((item) => Boolean(item.interactive)).length;
  const sourceBackedItemCount = plannedItems.filter((item) => String(item.source_node_id || '').trim().length > 0).length;
  return {
    format: 'native_layout_plan_v1',
    ready: true,
    route_state: routeState,
    entry_module: entryModule,
    layout_policy_source: layoutPolicySource,
    source_layout_surface_path: '',
    source_style_layout_surface_path: '',
    window_width: windowWidth,
    window_height: windowHeight,
    content_start_y: contentStartY,
    content_height: contentHeight,
    scroll_height: Math.max(0, contentHeight - windowHeight),
    item_count: plannedItems.length,
    viewport_item_count: viewportItems.length,
    clipped_item_count: clippedItemCount,
    overlay_item_count: plannedItems.filter((item) => String(item.layer || '') === 'overlay').length,
    interactive_item_count: interactiveItemCount,
    source_backed_item_count: sourceBackedItemCount,
    column_count: canUseTwoColumns ? (overlayReservedWidth > 0 ? 3 : 2) : (overlayReservedWidth > 0 ? 2 : 1),
    layout_policy: {
      content_inset: contentInset,
      header_height: headerHeight,
      meta_chip_height: metaChipHeight,
      meta_text_height: metaTextHeight,
      top_padding: topPadding,
      top_gap: topGap,
      row_gap: rowGap,
      overlay_gap: overlayGap,
      bottom_padding: bottomPadding,
      overlay_min_width: overlayMinWidth,
      overlay_max_width: overlayMaxWidth,
      overlay_width_ratio_pct: Math.round(overlayWidthRatio * 100),
      two_column_min_width: twoColumnMinWidth,
      column_gap: columnGap,
      indent_step: indentStep,
      indent_cap: indentCap,
    },
    items: plannedItems,
    viewport_items: viewportItems,
  };
}

function buildLayoutSurfaceDoc(repo, codegenManifest, execSnapshot, routeCatalog, tsxAstDoc, routeStateOverride = '') {
  const entryRouteState = String(routeCatalog?.entryRouteState || execSnapshot?.route_state || '').trim();
  const routeState = String(routeStateOverride || execSnapshot?.route_state || entryRouteState || 'home_default').trim() || 'home_default';
  const primary = resolvePrimaryRouteSurface(routeState, entryRouteState);
  const entryModulePath = String(codegenManifest?.entry_module || '').trim();
  const resolved = primary && primary.strategy !== 'entry_module'
    ? primary
    : {
      strategy: 'entry_module',
      modulePath: entryModulePath,
      componentName: '',
      reason: primary?.reason || 'entry_module_surface',
    };
  const moduleDoc = findModuleDoc(tsxAstDoc, resolved.modulePath);
  const chosenComponent = resolved.componentName
    ? (Array.isArray(moduleDoc?.components) ? moduleDoc.components.find((item) => String(item?.name || '') === resolved.componentName) : null)
    : pickPreferredComponent(moduleDoc);
  const componentName = String(chosenComponent?.name || resolved.componentName || '').trim();
  if (!resolved.modulePath || !componentName) {
    throw new Error(`native_gui_layout_surface_component_missing:${routeState}:${resolved.modulePath || '<module>'}:${componentName || '<component>'}`);
  }

  const ts = loadTypeScriptForRepo(repo);
  const sourceCache = new Map();
  const uniqueComponents = buildUniqueComponentRegistry(tsxAstDoc);
  const routeStaticEnv = buildRouteStaticEvalEnv(resolved.modulePath, componentName, routeState);
  const outline = createOutlineBuilder(ts, repo, sourceCache, uniqueComponents)
    .buildComponentTree(resolved.modulePath, componentName, routeStaticEnv);
  const flatNodes = flattenLayoutTree(outline.nodes);
  if (needsSyntheticMountRoot(flatNodes)) {
    flatNodes.unshift({
      id: 'node_mount_root',
      kind: 'tree_element',
      label: '<div>',
      detail_text: 'element',
      tag: 'div',
      line: 0,
      depth: 0,
      module_path: '',
      component_name: '',
      layout_role: 'element',
      style_traits: [],
      class_preview: '',
      class_tokens: [],
      attr_names: [],
    });
  }
  if (flatNodes.length <= 0) {
    throw new Error(`native_gui_layout_surface_empty:${routeState}:${resolved.modulePath}:${componentName}:${outline.reason || 'unknown'}:${Array.isArray(outline.nodes) ? outline.nodes.length : 0}`);
  }
  const styledNodeCount = flatNodes.filter((node) => Array.isArray(node.style_traits) && node.style_traits.length > 0).length;
  const interactiveNodeCount = flatNodes.filter((node) =>
    (Array.isArray(node.attr_names) && node.attr_names.some((name) => /^on[A-Z]/.test(String(name || ''))))
    || ['tree_button', 'tree_input', 'tree_link'].includes(String(node.kind || ''))
  ).length;

  return {
    format: 'layout_surface_v1',
    ready: true,
    route_state: routeState,
    entry_route_state: entryRouteState,
    source_strategy: resolved.strategy,
    source_reason: resolved.reason,
    source_module_path: resolved.modulePath,
    source_component_name: componentName,
    node_count: flatNodes.length,
    truncated: Boolean(outline.truncated),
    component_expansion_count: Number(outline.component_expansion_count || 0),
    parsed_module_count: Number(outline.parsed_module_count || 0),
    styled_node_count: styledNodeCount,
    interactive_node_count: interactiveNodeCount,
    nodes: flatNodes.map((node) => ({
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
    })),
  };
}

function buildSessionDocFromLayoutSurface(
  fields,
  layoutSurface,
  styleLayoutSurface = null,
  nativeLayoutPlan = null,
  nativeRenderPlan = null,
  nativeRuntimeState = null,
  nativeRuntime = null,
) {
  if (!layoutSurface || String(layoutSurface.format || '') !== 'layout_surface_v1' || !layoutSurface.ready) {
    return null;
  }
  if (!nativeLayoutPlan || String(nativeLayoutPlan.format || '') !== 'native_layout_plan_v1' || !nativeLayoutPlan.ready) {
    return null;
  }
  const title = String(fields.window_title || 'cheng_gui_preview');
  const entryModule = String(fields.entry_module || 'entry_module');
  const routeState = String(fields.route_state || 'home_default');
  const windowWidth = Number(nativeLayoutPlan.window_width || parseIntField(fields, 'window_width', 390));
  const windowHeight = Number(nativeLayoutPlan.window_height || parseIntField(fields, 'window_height', 844));
  const theme = buildNativeGuiTheme();

  return {
    format: 'native_gui_session_v1',
    preview_mode: 'cheng_ui_layout_surface_v1',
    theme,
    window: {
      title,
      width: windowWidth,
      height: windowHeight,
      resizable: true,
      entry_module: entryModule,
      route_state: routeState,
      render_ready: parseBoolField(fields, 'render_ready', false),
      content_height: Number(nativeLayoutPlan.content_height || windowHeight),
      scroll_height: Number(nativeLayoutPlan.scroll_height || 0),
    },
    layout_surface: layoutSurface,
    style_layout_surface: styleLayoutSurface,
    native_layout_plan: nativeLayoutPlan,
    native_render_plan: nativeRenderPlan,
    native_gui_runtime_state: nativeRuntimeState,
    native_gui_runtime: nativeRuntime,
    layout_items: Array.isArray(nativeLayoutPlan.items) ? nativeLayoutPlan.items : [],
  };
}

function buildSessionDocFromPreview(fields, layoutSurface = null, styleLayoutSurface = null) {
  if (String(fields?.format || '') !== 'native_gui_session_preview_v1') {
    throw new Error(`native_gui_session_preview_format_mismatch:${String(fields?.format || '')}`);
  }
  const nativeLayoutPlan = buildNativeLayoutPlanDoc(fields, layoutSurface, styleLayoutSurface);
  if (nativeLayoutPlan) {
    nativeLayoutPlan.source_layout_surface_path = String(layoutSurface?.path || '');
    nativeLayoutPlan.source_style_layout_surface_path = String(styleLayoutSurface?.path || '');
  }
  const layoutSession = buildSessionDocFromLayoutSurface(fields, layoutSurface, styleLayoutSurface, nativeLayoutPlan);
  if (layoutSession) {
    return layoutSession;
  }
  const title = String(fields.window_title || 'cheng_gui_preview');
  const entryModule = String(fields.entry_module || 'entry_module');
  const routeState = String(fields.route_state || 'home_default');
  const semanticNodesCount = parseIntField(fields, 'semantic_nodes_count', 0);
  const moduleCount = parseIntField(fields, 'module_count', 0);
  const componentCount = parseIntField(fields, 'component_count', 0);
  const effectCount = parseIntField(fields, 'effect_count', 0);
  const stateSlotCount = parseIntField(fields, 'state_slot_count', 0);
  const windowWidth = parseIntField(fields, 'window_width', 390);
  const windowHeight = parseIntField(fields, 'window_height', 844);
  const theme = buildNativeGuiTheme();
  const metricStyle = {
    ...buildMetricStyle(theme),
  };
  return {
    format: 'native_gui_session_v1',
    preview_mode: String(fields.preview_mode || 'cheng_ui_host_preview_v1'),
    theme,
    window: {
      title,
      width: windowWidth,
      height: windowHeight,
      resizable: true,
      entry_module: entryModule,
      route_state: routeState,
      render_ready: parseBoolField(fields, 'render_ready', false),
    },
    layout_items: [
      {
        id: 'root',
        kind: 'root',
        x: 0,
        y: 0,
        width: windowWidth,
        height: windowHeight,
        text: '',
        background_color: theme.panel_background,
        stretch_x: true,
        stretch_y: true,
      },
      {
        id: 'header',
        kind: 'label',
        x: 16,
        y: 20,
        width: Math.max(0, windowWidth - 32),
        height: 34,
        text: title,
        background_color: theme.accent_soft,
        border_color: '#bfd7f6',
        text_color: theme.text_primary,
        font_size: 26,
        font_weight: 'semibold',
        corner_radius: 18,
        stretch_x: true,
      },
      {
        id: 'entry',
        kind: 'label',
        x: 16,
        y: 72,
        width: Math.max(0, windowWidth - 32),
        height: 22,
        text: entryModule,
        ...metricStyle,
        stretch_x: true,
      },
      {
        id: 'route',
        kind: 'label',
        x: 16,
        y: 100,
        width: Math.max(0, windowWidth - 32),
        height: 22,
        text: routeState,
        ...metricStyle,
        stretch_x: true,
      },
      {
        id: 'nodes',
        kind: 'label',
        x: 16,
        y: 128,
        width: Math.max(0, windowWidth - 32),
        height: 22,
        text: String(semanticNodesCount),
        ...metricStyle,
        stretch_x: true,
      },
      {
        id: 'modules',
        kind: 'label',
        x: 16,
        y: 156,
        width: Math.max(0, windowWidth - 32),
        height: 22,
        text: String(moduleCount),
        ...metricStyle,
        stretch_x: true,
      },
      {
        id: 'components',
        kind: 'label',
        x: 16,
        y: 184,
        width: Math.max(0, windowWidth - 32),
        height: 22,
        text: String(componentCount),
        ...metricStyle,
        stretch_x: true,
      },
      {
        id: 'effects',
        kind: 'label',
        x: 16,
        y: 212,
        width: Math.max(0, windowWidth - 32),
        height: 22,
        text: String(effectCount),
        ...metricStyle,
        stretch_x: true,
      },
      {
        id: 'state_slots',
        kind: 'label',
        x: 16,
        y: 240,
        width: Math.max(0, windowWidth - 32),
        height: 22,
        text: String(stateSlotCount),
        ...metricStyle,
        stretch_x: true,
      },
    ],
  };
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

function runNativeGuiRuntimeDirect(runtimeExePath, runtimeContractPath, controllerLogPath) {
  const result = spawnForExec(runtimeExePath, [
    '--runtime-contract',
    runtimeContractPath,
  ], {
    timeout: NATIVE_GUI_RUNTIME_CONTROLLER_TIMEOUT_MS,
    cwd: path.dirname(runtimeExePath),
  });
  const stdoutText = String(result.stdout || '');
  const stderrText = String(result.stderr || '');
  const logText = [
    stdoutText,
    stderrText,
    result.error ? String(result.error.stack || result.error.message || result.error) : '',
  ].filter(Boolean).join('\n');
  writeText(controllerLogPath, logText);
  const exitCode = exitCodeOf(result);
  if (exitCode !== 0) {
    throw new Error(`native_gui_runtime_direct_failed:${exitCode}`);
  }
  const trimmed = stdoutText.trim();
  if (!trimmed) {
    throw new Error('native_gui_runtime_direct_empty');
  }
  let doc = null;
  try {
    doc = JSON.parse(trimmed);
  } catch (error) {
    const message = String(error?.message || error || 'unknown');
    throw new Error(`native_gui_runtime_direct_json_parse_failed:${message}`);
  }
  if (String(doc?.format || '') !== 'native_gui_runtime_v1') {
    throw new Error(`native_gui_runtime_direct_format_mismatch:${String(doc?.format || '')}`);
  }
  return {
    exitCode,
    doc,
  };
}

function renderLauncherModule({
  uiHostModule,
  entryRouteState,
  packageId,
  execSnapshot,
}) {
  const snapshotJson = JSON.stringify(execSnapshot);
  return `import ${uiHostModule} as ui_host

fn r2cNativeGuiBundleId(): str =
    return ${chengStr(packageId)}

fn r2cNativeGuiEntryRouteState(): str =
    return ${chengStr(entryRouteState)}

fn r2cNativeGuiEntrySnapshotJson(): str =
    return ${chengStr(snapshotJson)}

fn r2cNativeGuiHostBootstrap(): int32 =
    return ui_host.r2cUiHostCreateWindowSession()

fn main(): int32 =
    return r2cNativeGuiHostBootstrap()
`;
}

function buildNativeGuiRuntimeContractDoc({
  nativeLayoutPlan,
  previewFields,
  theme,
  routeSurface,
  hostAbi,
  runtimeExePath,
  runtimeCompiledExePath,
}) {
  return {
    format: 'native_gui_runtime_contract_v1',
    native_layout_plan: nativeLayoutPlan,
    preview_fields: previewFields,
    theme,
    route_surface: routeSurface,
    host_abi: hostAbi,
    runtime_exe_path: runtimeCompiledExePath,
    runtime_launcher_exe_path: runtimeExePath,
    runtime_compiled_exe_path: runtimeCompiledExePath,
  };
}

function buildRouteSurfaceDoc(layoutSurface, routeState, entryRouteState) {
  return {
    route_state: String(routeState || ''),
    entry_route_state: String(entryRouteState || ''),
    source_strategy: String(layoutSurface?.source_strategy || ''),
    source_reason: String(layoutSurface?.source_reason || ''),
    source_module_path: String(layoutSurface?.source_module_path || ''),
    source_component_name: String(layoutSurface?.source_component_name || ''),
  };
}

function buildNativeGuiHostAbiDoc(hostContract) {
  const requiredMethodsRaw = Array.isArray(hostContract?.methods)
    ? hostContract.methods.filter((item) => item && item.required)
    : [];
  const requiredMethodKeys = new Set(requiredMethodsRaw.map((item) => `${String(item.group || '')}.${String(item.name || '')}`));
  const featureHitsSet = new Set(
    Array.isArray(hostContract?.feature_hits)
      ? hostContract.feature_hits.map((item) => String(item || '')).filter(Boolean)
      : [],
  );
  if (requiredMethodKeys.has('StorageHost.getItem')) featureHitsSet.add('local_storage');
  if (requiredMethodKeys.has('NetHost.fetch')) featureHitsSet.add('fetch');
  if (requiredMethodKeys.has('PlatformHost.emitCustomEvent')) featureHitsSet.add('custom_event');
  if (featureHitsSet.has('mutation_observer')) featureHitsSet.add('resize_observer');
  const featureHits = [...featureHitsSet].sort();
  const methods = Array.isArray(hostContract?.methods)
    ? hostContract.methods.filter((item) => item && item.required)
    : [];
  const firstBatchFeatures = ['local_storage', 'fetch', 'custom_event', 'resize_observer'];
  const firstBatchDetected = firstBatchFeatures.filter((item) => featureHits.includes(item));
  const firstBatchMissing = firstBatchFeatures.filter((item) => !featureHits.includes(item));
  const firstBatchMethodKeys = new Set([
    'StorageHost.getItem',
    'StorageHost.setItem',
    'StorageHost.removeItem',
    'StorageHost.clear',
    'NetHost.fetch',
    'PlatformHost.emitCustomEvent',
  ]);
  return {
    format: 'native_gui_host_abi_v1',
    feature_hits: featureHits,
    required_groups: Array.isArray(hostContract?.required_groups)
      ? hostContract.required_groups.map((item) => String(item || ''))
      : [],
    required_methods: methods.map((item) => ({
      group: String(item.group || ''),
      name: String(item.name || ''),
    })),
    first_batch: {
      ready: firstBatchMissing.length === 0,
      expected_features: firstBatchFeatures,
      detected_features: firstBatchDetected,
      missing_features: firstBatchMissing,
      methods: methods
        .filter((item) => firstBatchMethodKeys.has(`${String(item.group || '')}.${String(item.name || '')}`))
        .map((item) => ({
          group: String(item.group || ''),
          name: String(item.name || ''),
        })),
    },
  };
}

function findRouteCatalogEntry(routeCatalog, routeState) {
  const target = String(routeState || '').trim();
  if (!target || !Array.isArray(routeCatalog?.entries)) return null;
  return routeCatalog.entries.find((entry) => String(entry?.routeId || '').trim() === target) || null;
}

function buildNativeGuiExecSnapshotDoc(codegenManifest, routeCatalog, routeStateOverride = '') {
  const baseSnapshot = (codegenManifest && typeof codegenManifest.base_snapshot === 'object' && codegenManifest.base_snapshot)
    ? codegenManifest.base_snapshot
    : {};
  const entryRouteState = String(
    routeCatalog?.entryRouteState
    || baseSnapshot.route_state
    || codegenManifest?.entry_route_state
    || 'home_default',
  ).trim() || 'home_default';
  const routeState = String(routeStateOverride || baseSnapshot.route_state || entryRouteState || 'home_default').trim() || 'home_default';
  const entry = findRouteCatalogEntry(routeCatalog, routeState);
  const candidates = Array.isArray(entry?.candidates) ? entry.candidates : [];
  const candidate = candidates[0] || null;
  const semanticNodesCount = Math.max(
    0,
    Number(
      candidate?.semanticNodesCount
      ?? ((routeState === entryRouteState || routeState === String(baseSnapshot.route_state || '').trim())
        ? baseSnapshot.semantic_nodes_count
        : 0)
      ?? 0,
    ),
  );
  return {
    ...baseSnapshot,
    format: 'cheng_codegen_exec_snapshot_v1',
    route_state: routeState,
    mount_phase: String(baseSnapshot.mount_phase || 'prepared'),
    commit_phase: String(baseSnapshot.commit_phase || 'committed'),
    render_ready: Boolean(baseSnapshot.render_ready),
    semantic_nodes_loaded: semanticNodesCount > 0,
    semantic_nodes_count: semanticNodesCount,
    component_count: Math.max(0, Number(baseSnapshot.component_count || 0)),
    effect_count: Math.max(0, Number(baseSnapshot.effect_count || 0)),
    state_slot_count: Math.max(0, Number(baseSnapshot.state_slot_count || 0)),
    hook_slot_count: Math.max(0, Number(baseSnapshot.hook_slot_count || 0)),
    module_count: Math.max(0, Number(baseSnapshot.module_count || codegenManifest?.modules?.length || 0)),
    tree_node_count: semanticNodesCount,
    root_node_count: semanticNodesCount > 0 ? 1 : 0,
    element_node_count: semanticNodesCount,
  };
}

function routeCatalogSupportsRoute(routeCatalog, routeState) {
  const target = String(routeState || '').trim();
  if (!target) return false;
  if (String(routeCatalog?.entryRouteState || '').trim() === target) return true;
  if (Array.isArray(routeCatalog?.routes) && routeCatalog.routes.some((item) => String(item || '').trim() === target)) {
    return true;
  }
  if (Array.isArray(routeCatalog?.entries)) {
    return routeCatalog.entries.some((entry) => String(entry?.routeId || '').trim() === target);
  }
  return false;
}

function renderRuntimeLauncherSource({
  compiledRuntimeExePath,
}) {
  return `#!/usr/bin/env sh
set -eu
exec ${shQuote(compiledRuntimeExePath)} "$@"
`;
}

function main() {
  ensureNodeHeapLimit();
  const args = parseArgs(process.argv);
  const workspaceRoot = resolveWorkspaceRoot();
  const repo = path.resolve(args.repo);
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_v3_cheng'));
  const codegenManifestPath = path.resolve(args.codegenManifestPath || path.join(outDir, 'cheng_codegen_v1.json'));
  const codegenInputDir = path.dirname(codegenManifestPath);
  const execSnapshotPath = path.resolve(args.execSnapshotPath || path.join(outDir, 'cheng_codegen_exec_snapshot_v1.json'));
  const hostContractPath = path.resolve(args.hostContractPath || path.join(codegenInputDir, 'unimaker_host_v1.json'));
  const assetManifestPath = path.resolve(args.assetManifestPath || path.join(codegenInputDir, 'asset_manifest_v1.json'));
  const tailwindManifestPath = path.resolve(args.tailwindManifestPath || path.join(codegenInputDir, 'tailwind_style_manifest_v1.json'));
  const tsxAstPath = path.resolve(args.tsxAstPath || path.join(codegenInputDir, 'tsx_ast_v1.json'));
  const bundlePath = path.resolve(args.outPath || path.join(outDir, 'native_gui_bundle_v1.json'));
  const summaryPath = path.resolve(args.summaryOut || path.join(outDir, 'native_gui_bundle.summary.env'));
  const reportPath = path.resolve(path.join(outDir, 'native_gui_bundle_report_v1.json'));
  const layoutSurfacePath = path.resolve(path.join(outDir, 'layout_surface_v1.json'));
  const styleLayoutSurfacePath = path.resolve(path.join(outDir, 'style_layout_surface_v1.json'));
  const nativeLayoutPlanPath = path.resolve(path.join(outDir, 'native_layout_plan_v1.json'));
  const nativeRenderPlanPath = path.resolve(path.join(outDir, 'native_render_plan_v1.json'));
  const nativeRuntimeDocPath = path.resolve(path.join(outDir, 'native_gui_runtime_v1.json'));
  const nativeRuntimeStatePath = path.resolve(path.join(outDir, 'native_gui_runtime_state_v1.json'));

  requireFile(codegenManifestPath, 'codegen manifest');
  const rawCodegenManifest = readJson(codegenManifestPath);
  requireFormat(rawCodegenManifest, 'cheng_codegen_v1', 'codegen manifest');
  const codegenManifest = normalizeCodegenManifestPaths(rawCodegenManifest, outDir, {
    preferOutDirArtifacts: !args.codegenManifestPath,
  });
  const routeCatalogPath = path.resolve(
    args.routeCatalogPath
    || String(codegenManifest.route_catalog_path || '')
    || path.join(codegenInputDir, 'cheng_codegen_route_catalog_v1.json'),
  );
  requireFile(hostContractPath, 'host contract');
  requireFile(assetManifestPath, 'asset manifest');
  requireFile(tailwindManifestPath, 'tailwind manifest');
  requireFile(routeCatalogPath, 'route catalog');
  requireFile(tsxAstPath, 'tsx ast');

  let hostContract = readJson(hostContractPath);
  const assetManifest = readJson(assetManifestPath);
  const tailwindManifest = readJson(tailwindManifestPath);
  const routeCatalog = readJson(routeCatalogPath);
  const tsxAstDoc = readJson(tsxAstPath);

  requireFormat(hostContract, 'unimaker_host_v1', 'host contract');
  requireFormat(assetManifest, 'asset_manifest_v1', 'asset manifest');
  requireFormat(tailwindManifest, 'tailwind_style_manifest_v1', 'tailwind manifest');
  requireFormat(routeCatalog, 'cheng_codegen_route_catalog_v1', 'route catalog');
  requireFormat(tsxAstDoc, 'tsx_ast_v1', 'tsx ast');
  hostContract = enrichHostContractFromCompileReport(hostContract, outDir);
  writeJson(hostContractPath, hostContract);

  const packageRoot = String(codegenManifest.package_root || '');
  const packageId = String(codegenManifest.package_id || '');
  const packageImportPrefix = String(codegenManifest.package_import_prefix || '');
  const uiHostModule = String(codegenManifest.ui_host_module || '');
  const tool = resolveDefaultToolingBin(workspaceRoot);
  const launcherModule = `${packageImportPrefix}/native_gui_bundle_main`;
  const launcherModulePath = path.join(packageRoot, 'src', 'native_gui_bundle_main.cheng');
  const launcherExePath = path.join(outDir, 'native_gui_bundle_main');
  const launcherCompileReportPath = path.join(outDir, 'native_gui_bundle_main.report.txt');
  const launcherCompileLogPath = path.join(outDir, 'native_gui_bundle_main.log');
  const launcherRunLogPath = path.join(outDir, 'native_gui_bundle_main.run.log');
  const runtimeModule = `${packageImportPrefix}/native_gui_runtime`;
  const runtimeMainModule = `${packageImportPrefix}/native_gui_runtime_main`;
  const runtimeModulePath = path.join(packageRoot, 'src', 'native_gui_runtime.cheng');
  const runtimeMainModulePath = path.join(packageRoot, 'src', 'native_gui_runtime_main.cheng');
  const runtimeExePath = path.join(outDir, 'native_gui_runtime_main');
  const runtimeCompiledExePath = path.join(outDir, 'native_gui_runtime_compiled_main');
  const runtimeContractPath = path.join(outDir, 'native_gui_runtime_contract_v1.json');
  const runtimeCompileReportPath = path.join(outDir, 'native_gui_runtime_main.report.txt');
  const runtimeCompileLogPath = path.join(outDir, 'native_gui_runtime_main.log');
  const runtimeRunLogPath = path.join(outDir, 'native_gui_runtime_main.run.log');
  const runtimeControllerLogPath = path.join(outDir, 'native_gui_runtime_main.controller.log');
  const sessionPath = path.join(outDir, 'native_gui_session_v1.json');

  const requiredModules = [
    codegenManifest.runtime_module,
    codegenManifest.ui_host_module,
    codegenManifest.app_runner_module,
    codegenManifest.project_module,
    codegenManifest.entry_project_module,
    codegenManifest.main_module,
    codegenManifest.route_runtime_module,
    codegenManifest.route_runtime_main_module,
    codegenManifest.exec_bundle_module,
  ].map((item) => String(item || '')).filter(Boolean);

  const moduleEntries = requiredModules.map((importPath) => {
    const filePath = resolveModulePath(codegenManifest, importPath);
    return {
      import_path: importPath,
      file_path: filePath,
      exists: fs.existsSync(filePath),
    };
  });
  const missingModule = moduleEntries.find((entry) => !entry.exists);
  if (missingModule) {
    throw new Error(`missing generated package module: ${missingModule.file_path}`);
  }

  const requiredGroups = Array.isArray(hostContract.required_groups)
    ? hostContract.required_groups.map((item) => String(item))
    : [];
  const requiredMethods = Array.isArray(hostContract.methods)
    ? hostContract.methods.filter((item) => item && item.required)
    : [];
  const entryRouteState = String(routeCatalog.entryRouteState || codegenManifest.entry_route_state || codegenManifest.base_snapshot?.route_state || '').trim();
  const targetRouteState = String(args.routeState || entryRouteState || 'home_default').trim() || 'home_default';
  if (!routeCatalogSupportsRoute(routeCatalog, targetRouteState)) {
    throw new Error(`native_gui_route_unsupported:${targetRouteState}`);
  }
  const execSnapshot = buildNativeGuiExecSnapshotDoc(codegenManifest, routeCatalog, targetRouteState);
  writeJson(execSnapshotPath, execSnapshot);
  const launcherSource = renderLauncherModule({
    uiHostModule,
    entryRouteState,
    packageId,
    execSnapshot,
  });
  writeText(launcherModulePath, launcherSource);
  ensurePackageCompileSupport(workspaceRoot, packageRoot);
  const previewRun = compileAndRunEntry(
    tool,
    packageRoot,
    launcherModulePath,
    launcherExePath,
    launcherCompileReportPath,
    launcherCompileLogPath,
    launcherRunLogPath,
  );
  if (previewRun.compileReturnCode !== 0) {
    throw new Error(`native_gui_launcher_compile_failed:${previewRun.compileReturnCode}`);
  }
  if (previewRun.runReturnCode !== 0) {
    throw new Error(`native_gui_launcher_run_failed:${previewRun.runReturnCode}`);
  }
  const previewFields = {
    ...parseKeyValueStdout(previewRun.stdout, 'native_gui_session_preview'),
    route_state: targetRouteState,
  };
  const layoutSurface = buildLayoutSurfaceDoc(repo, codegenManifest, execSnapshot, routeCatalog, tsxAstDoc, targetRouteState);
  if (!layoutSurface) {
    throw new Error('native_gui_layout_surface_missing');
  }
  writeJson(layoutSurfacePath, layoutSurface);
  const styleLayoutSurface = buildStyleLayoutSurfaceDoc(layoutSurface);
  if (!styleLayoutSurface) {
    throw new Error('native_gui_style_layout_surface_missing');
  }
  styleLayoutSurface.source_layout_surface_path = layoutSurfacePath;
  writeJson(styleLayoutSurfacePath, styleLayoutSurface);
  const nativeLayoutPlan = buildNativeLayoutPlanDoc(previewFields, layoutSurface, styleLayoutSurface);
  if (!nativeLayoutPlan) {
    throw new Error('native_gui_native_layout_plan_missing');
  }
  nativeLayoutPlan.source_layout_surface_path = layoutSurfacePath;
  nativeLayoutPlan.source_style_layout_surface_path = styleLayoutSurfacePath;
  writeJson(nativeLayoutPlanPath, nativeLayoutPlan);
  layoutSurface.path = layoutSurfacePath;
  styleLayoutSurface.path = styleLayoutSurfacePath;
  const nativeTheme = buildNativeGuiTheme();
  const routeSurface = buildRouteSurfaceDoc(layoutSurface, targetRouteState, entryRouteState);
  const hostAbi = buildNativeGuiHostAbiDoc(hostContract);
  const runtimeTheme = {
    ...nativeTheme,
    host_abi_first_batch_ready: Boolean(hostAbi.first_batch?.ready),
    host_abi_feature_hits: Array.isArray(hostAbi.feature_hits) ? hostAbi.feature_hits : [],
    host_abi_missing_features: Array.isArray(hostAbi.first_batch?.missing_features) ? hostAbi.first_batch.missing_features : [],
    storage_host_ready: hostAbi.feature_hits.includes('local_storage'),
    fetch_host_ready: hostAbi.feature_hits.includes('fetch'),
    custom_event_host_ready: hostAbi.feature_hits.includes('custom_event'),
    resize_observer_host_ready: hostAbi.feature_hits.includes('resize_observer'),
  };
  const runtimeSource = renderNativeGuiRuntimeCheng({
    execIoModule: String(codegenManifest.exec_io_module || ''),
    nativeLayoutPlan,
    previewFields,
    theme: runtimeTheme,
  });
  const runtimeMainSource = renderNativeGuiRuntimeMainCheng({
    runtimeModule,
  });
  writeText(runtimeModulePath, runtimeSource);
  writeText(runtimeMainModulePath, runtimeMainSource);
  const runtimeContract = buildNativeGuiRuntimeContractDoc({
    nativeLayoutPlan,
    previewFields,
    theme: nativeTheme,
    routeSurface,
    hostAbi,
    runtimeExePath: runtimeCompiledExePath,
    runtimeCompiledExePath,
  });
  writeJson(runtimeContractPath, runtimeContract);
  const runtimeRun = compileAndRunEntry(
    tool,
    packageRoot,
    runtimeMainModulePath,
    runtimeCompiledExePath,
    runtimeCompileReportPath,
    runtimeCompileLogPath,
    runtimeRunLogPath,
  );
  if (runtimeRun.compileReturnCode !== 0) {
    throw new Error(`native_gui_runtime_compile_failed:${runtimeRun.compileReturnCode}`);
  }
  if (runtimeRun.runReturnCode !== 0) {
    throw new Error(`native_gui_runtime_run_failed:${runtimeRun.runReturnCode}`);
  }
  const runtimeControllerRun = runNativeGuiRuntimeDirect(
    runtimeCompiledExePath,
    runtimeContractPath,
    runtimeControllerLogPath,
  );
  const nativeRuntimeDoc = runtimeControllerRun.doc;
  const nativeRenderPlanDoc = nativeRuntimeDoc.render_plan || null;
  const nativeRuntimeStateRaw = (nativeRuntimeDoc.state && typeof nativeRuntimeDoc.state === 'object')
    ? nativeRuntimeDoc.state
    : {};
  const nativeRuntimeState = {
    ...nativeRuntimeStateRaw,
    runtime_manifest_ready: Boolean(nativeRuntimeStateRaw.runtime_manifest_ready ?? false),
    runtime_contract_payload_ready: Boolean(nativeRuntimeStateRaw.runtime_contract_payload_ready ?? true),
    runtime_bundle_payload_ready: Boolean(nativeRuntimeStateRaw.runtime_bundle_payload_ready ?? true),
    runtime_bundle_ready: Boolean(nativeRuntimeStateRaw.runtime_bundle_ready ?? true),
    runtime_contract_ready: Boolean(nativeRuntimeStateRaw.runtime_contract_ready ?? true),
    bundle_route_state: String(nativeRuntimeStateRaw.bundle_route_state || targetRouteState),
    bundle_route_count: Number(nativeRuntimeStateRaw.bundle_route_count ?? routeCatalog.routeCount ?? 0),
    bundle_supported_count: Number(nativeRuntimeStateRaw.bundle_supported_count ?? routeCatalog.supportedCount ?? 0),
    bundle_semantic_nodes_count: Number(nativeRuntimeStateRaw.bundle_semantic_nodes_count ?? execSnapshot.semantic_nodes_count ?? 0),
    bundle_layout_item_count: Number(nativeRuntimeStateRaw.bundle_layout_item_count ?? nativeLayoutPlan.item_count ?? 0),
    bundle_render_command_count: Number(nativeRuntimeStateRaw.bundle_render_command_count ?? nativeRenderPlanDoc?.command_count ?? 0),
    contract_layout_item_count: Number(nativeRuntimeStateRaw.contract_layout_item_count ?? nativeLayoutPlan.item_count ?? 0),
    contract_viewport_item_count: Number(nativeRuntimeStateRaw.contract_viewport_item_count ?? nativeLayoutPlan.viewport_item_count ?? 0),
    contract_interactive_item_count: Number(nativeRuntimeStateRaw.contract_interactive_item_count ?? nativeLayoutPlan.interactive_item_count ?? 0),
  };
  const nativeRenderPlan = nativeRenderPlanDoc;
  if (String(nativeRenderPlan?.format || '') !== 'native_render_plan_v1') {
    throw new Error(`native_gui_render_plan_format_mismatch:${String(nativeRenderPlan?.format || '')}`);
  }
  writeJson(nativeRuntimeDocPath, nativeRuntimeDoc);
  writeJson(nativeRuntimeStatePath, nativeRuntimeState);
  writeJson(nativeRenderPlanPath, nativeRenderPlan);
  const runtimeLauncherSource = renderRuntimeLauncherSource({
    compiledRuntimeExePath: runtimeCompiledExePath,
  });
  writeText(runtimeExePath, runtimeLauncherSource);
  fs.chmodSync(runtimeExePath, 0o755);
  const nativeRuntime = {
    format: 'native_gui_runtime_contract_v1',
    ready: true,
    runtime_module: runtimeModule,
    runtime_main_module: runtimeMainModule,
    runtime_module_path: runtimeModulePath,
    runtime_main_module_path: runtimeMainModulePath,
    runtime_exe_path: runtimeCompiledExePath,
    runtime_launcher_exe_path: runtimeExePath,
    runtime_compiled_exe_path: runtimeCompiledExePath,
    runtime_contract_path: runtimeContractPath,
    runtime_mode: 'cheng_compiled_json_v1',
    runtime_doc_path: nativeRuntimeDocPath,
    runtime_state_path: nativeRuntimeStatePath,
    render_plan_path: nativeRenderPlanPath,
  };
  const hostAbiFirstBatchReady = Boolean(nativeRuntimeState?.host_abi_first_batch_ready ?? hostAbi.first_batch.ready);
  const storageHostReady = Boolean(nativeRuntimeState?.storage_host_ready ?? runtimeTheme.storage_host_ready);
  const fetchHostReady = Boolean(nativeRuntimeState?.fetch_host_ready ?? runtimeTheme.fetch_host_ready);
  const customEventHostReady = Boolean(nativeRuntimeState?.custom_event_host_ready ?? runtimeTheme.custom_event_host_ready);
  const resizeObserverHostReady = Boolean(nativeRuntimeState?.resize_observer_host_ready ?? runtimeTheme.resize_observer_host_ready);
  const sessionDoc = buildSessionDocFromLayoutSurface(
    previewFields,
    layoutSurface,
    styleLayoutSurface,
    nativeLayoutPlan,
    nativeRenderPlan,
    nativeRuntimeState,
    nativeRuntime,
  )
    || buildSessionDocFromPreview(previewFields, layoutSurface, styleLayoutSurface);
  const nativeGuiSessionPreviewReady = Boolean(
    previewRun.compileReturnCode === 0
    && previewRun.runReturnCode === 0
    && sessionDoc,
  );
  const nativeGuiHostReady = Boolean(
    runtimeRun.compileReturnCode === 0
    && runtimeRun.runReturnCode === 0
    && runtimeControllerRun.exitCode === 0
    && nativeRuntime.ready
    && hostAbiFirstBatchReady
    && storageHostReady
    && fetchHostReady
    && customEventHostReady
    && resizeObserverHostReady,
  );
  const nativeGuiRendererReady = Boolean(
    nativeGuiSessionPreviewReady
    && nativeRuntime.ready
    && layoutSurface
    && styleLayoutSurface
    && nativeLayoutPlan
    && nativeRenderPlan
    && String(nativeRenderPlan?.format || '') === 'native_render_plan_v1',
  );
  writeJson(sessionPath, sessionDoc);

  const bundle = {
    format: 'native_gui_bundle_v1',
    bundle_id: `${packageId}#native_gui_bundle`,
    workspace_root: String(codegenManifest.workspace_root || ''),
    repo_root: repo,
    out_dir: outDir,
    generated_at: new Date().toISOString(),
    bundle_ready: true,
    host_ready: nativeGuiHostReady,
    renderer_ready: nativeGuiRendererReady,
    host_bootstrap_mode: 'session_preview_v1',
    session_preview_ready: nativeGuiSessionPreviewReady,
    session_preview_mode: String(sessionDoc?.preview_mode || 'cheng_ui_host_preview_v1'),
    package: {
      package_root: packageRoot,
      package_id: packageId,
      package_import_prefix: packageImportPrefix,
      launcher_module: launcherModule,
      launcher_module_path: launcherModulePath,
      main_module: String(codegenManifest.main_module || ''),
      ui_host_module: uiHostModule,
      runtime_module: String(codegenManifest.runtime_module || ''),
      app_runner_module: String(codegenManifest.app_runner_module || ''),
      entry_project_module: String(codegenManifest.entry_project_module || ''),
      required_modules: moduleEntries,
    },
    launch_contract: {
      launch_function: 'main',
      host_bootstrap_function: 'r2cNativeGuiHostBootstrap',
      entry_route_function: 'r2cNativeGuiEntryRouteState',
      entry_snapshot_function: 'r2cNativeGuiEntrySnapshotJson',
    },
    entry_surface: {
      route_state: String(execSnapshot.route_state || ''),
      mount_phase: String(execSnapshot.mount_phase || ''),
      commit_phase: String(execSnapshot.commit_phase || ''),
      render_ready: Boolean(execSnapshot.render_ready),
      semantic_nodes_loaded: Boolean(execSnapshot.semantic_nodes_loaded),
      semantic_nodes_count: Number(execSnapshot.semantic_nodes_count || 0),
      component_count: Number(execSnapshot.component_count || 0),
      effect_count: Number(execSnapshot.effect_count || 0),
      state_slot_count: Number(execSnapshot.state_slot_count || 0),
      hook_slot_count: Number(execSnapshot.hook_slot_count || 0),
      exec_snapshot_path: execSnapshotPath,
    },
    routes: {
      entry_route_state: entryRouteState,
      route_state: targetRouteState,
      route_count: Number(routeCatalog.routeCount || 0),
      supported_count: Number(routeCatalog.supportedCount || 0),
      unsupported_count: Number(routeCatalog.unsupportedCount || 0),
      route_catalog_path: routeCatalogPath,
    },
    layout_surface: {
      path: layoutSurfacePath,
      ready: true,
      route_state: String(layoutSurface.route_state || ''),
      source_strategy: String(layoutSurface.source_strategy || ''),
      source_module_path: String(layoutSurface.source_module_path || ''),
      source_component_name: String(layoutSurface.source_component_name || ''),
      node_count: Number(layoutSurface.node_count || 0),
      truncated: Boolean(layoutSurface.truncated),
      component_expansion_count: Number(layoutSurface.component_expansion_count || 0),
      parsed_module_count: Number(layoutSurface.parsed_module_count || 0),
      styled_node_count: Number(layoutSurface.styled_node_count || 0),
      interactive_node_count: Number(layoutSurface.interactive_node_count || 0),
    },
    style_layout_surface: {
      path: styleLayoutSurfacePath,
      ready: true,
      node_count: Number(styleLayoutSurface.node_count || 0),
      overlay_node_count: Number(styleLayoutSurface.overlay_node_count || 0),
      surface_node_count: Number(styleLayoutSurface.surface_node_count || 0),
      control_node_count: Number(styleLayoutSurface.control_node_count || 0),
      visual_role_histogram: styleLayoutSurface.visual_role_histogram || {},
      density_histogram: styleLayoutSurface.density_histogram || {},
    },
    native_layout_plan: {
      path: nativeLayoutPlanPath,
      ready: true,
      layout_policy_source: String(nativeLayoutPlan.layout_policy_source || ''),
      item_count: Number(nativeLayoutPlan.item_count || 0),
      viewport_item_count: Number(nativeLayoutPlan.viewport_item_count || 0),
      clipped_item_count: Number(nativeLayoutPlan.clipped_item_count || 0),
      overlay_item_count: Number(nativeLayoutPlan.overlay_item_count || 0),
      interactive_item_count: Number(nativeLayoutPlan.interactive_item_count || 0),
      source_backed_item_count: Number(nativeLayoutPlan.source_backed_item_count || 0),
      scroll_height: Number(nativeLayoutPlan.scroll_height || 0),
      column_count: Number(nativeLayoutPlan.column_count || 0),
    },
    native_render_plan: {
      path: nativeRenderPlanPath,
      ready: true,
      command_count: Number(nativeRenderPlan.command_count || 0),
      visible_layout_item_count: Number(nativeRenderPlan.visible_layout_item_count || 0),
      selected_item_id: String(nativeRenderPlan.selected_item_id || ''),
      focused_item_id: String(nativeRenderPlan.focused_item_id || ''),
    },
    native_runtime: {
      path: nativeRuntimeDocPath,
      ready: true,
      runtime_state_path: nativeRuntimeStatePath,
      runtime_module: runtimeModule,
      runtime_main_module: runtimeMainModule,
      runtime_module_path: runtimeModulePath,
      runtime_main_module_path: runtimeMainModulePath,
      runtime_exe_path: runtimeCompiledExePath,
      runtime_launcher_exe_path: runtimeExePath,
      runtime_compiled_exe_path: runtimeCompiledExePath,
      runtime_contract_path: runtimeContractPath,
      runtime_mode: 'cheng_compiled_json_v1',
      runtime_compile_report_path: runtimeCompileReportPath,
      runtime_compile_log_path: runtimeCompileLogPath,
      runtime_run_log_path: runtimeRunLogPath,
      runtime_controller_log_path: runtimeControllerLogPath,
      runtime_controller_returncode: runtimeControllerRun.exitCode,
      runtime_compile_returncode: runtimeRun.compileReturnCode,
      runtime_run_returncode: runtimeRun.runReturnCode,
      host_abi_first_batch_ready: hostAbiFirstBatchReady,
      storage_host_ready: storageHostReady,
      fetch_host_ready: fetchHostReady,
      custom_event_host_ready: customEventHostReady,
      resize_observer_host_ready: resizeObserverHostReady,
      focused_item_id: String(nativeRuntimeState?.focused_item_id || ''),
      typed_text: String(nativeRuntimeState?.typed_text || ''),
    },
    session_preview: {
      format: 'native_gui_session_v1',
      ready: nativeGuiSessionPreviewReady,
      mode: String(sessionDoc?.preview_mode || 'cheng_ui_host_preview_v1'),
      source_format: String(previewFields.format || ''),
      session_path: sessionPath,
      launcher_exe_path: launcherExePath,
      launcher_compile_report_path: launcherCompileReportPath,
      launcher_compile_log_path: launcherCompileLogPath,
      launcher_run_log_path: launcherRunLogPath,
      launcher_compile_returncode: previewRun.compileReturnCode,
      launcher_run_returncode: previewRun.runReturnCode,
      window_title: String(sessionDoc?.window?.title || ''),
      layout_item_count: Array.isArray(sessionDoc?.layout_items) ? sessionDoc.layout_items.length : 0,
    },
    host_contract: {
      path: hostContractPath,
      required_groups: requiredGroups,
      required_group_count: requiredGroups.length,
      required_method_count: requiredMethods.length,
      feature_hits: hostAbi.feature_hits,
      abi: hostAbi,
      required_methods: requiredMethods.map((item) => ({
        group: String(item.group || ''),
        name: String(item.name || ''),
      })),
    },
    assets: {
      asset_manifest_path: assetManifestPath,
      asset_count: Array.isArray(assetManifest.entries) ? assetManifest.entries.length : 0,
      asset_count_by_kind: countAssetKinds(Array.isArray(assetManifest.entries) ? assetManifest.entries : []),
      tailwind_manifest_path: tailwindManifestPath,
      tailwind_token_count: Array.isArray(tailwindManifest.tokens) ? tailwindManifest.tokens.length : 0,
    },
  };

  writeJson(bundlePath, bundle);
  writeJson(reportPath, {
    format: 'controller_report_v1',
    controller: 'node.native_gui_bundle',
    command: 'native-gui-bundle',
    ok: true,
    reason: 'ok',
    repo_root: repo,
    out_dir: outDir,
    primary_artifact_path: bundlePath,
    primary_artifact_format: 'native_gui_bundle_v1',
    module_count: Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules.length : 0,
    route_count: Number(routeCatalog.routeCount || 0),
    semantic_nodes_count: Number(execSnapshot.semantic_nodes_count || 0),
    asset_count: Array.isArray(assetManifest.entries) ? assetManifest.entries.length : 0,
    tailwind_token_count: Array.isArray(tailwindManifest.tokens) ? tailwindManifest.tokens.length : 0,
    required_host_group_count: requiredGroups.length,
    required_host_groups: requiredGroups,
    typescript_version: String(tsxAstDoc?.typescript_version || ''),
    native_gui_launcher_module: launcherModule,
    native_gui_session_path: sessionPath,
    native_gui_session_preview_mode: String(sessionDoc?.preview_mode || 'cheng_ui_host_preview_v1'),
    native_gui_route_state: targetRouteState,
    native_gui_layout_surface_path: layoutSurfacePath,
    native_gui_layout_surface_node_count: Number(layoutSurface.node_count || 0),
    native_gui_layout_surface_source_module: String(layoutSurface.source_module_path || ''),
    native_gui_layout_surface_source_component: String(layoutSurface.source_component_name || ''),
    native_gui_layout_surface_strategy: String(layoutSurface.source_strategy || ''),
    native_gui_layout_surface_component_expansion_count: Number(layoutSurface.component_expansion_count || 0),
    native_gui_layout_surface_parsed_module_count: Number(layoutSurface.parsed_module_count || 0),
    native_gui_layout_surface_styled_node_count: Number(layoutSurface.styled_node_count || 0),
    native_gui_layout_surface_interactive_node_count: Number(layoutSurface.interactive_node_count || 0),
    native_gui_style_layout_surface_path: styleLayoutSurfacePath,
    native_gui_style_layout_surface_node_count: Number(styleLayoutSurface.node_count || 0),
    native_gui_style_layout_overlay_node_count: Number(styleLayoutSurface.overlay_node_count || 0),
    native_gui_style_layout_surface_surface_node_count: Number(styleLayoutSurface.surface_node_count || 0),
    native_gui_style_layout_surface_control_node_count: Number(styleLayoutSurface.control_node_count || 0),
    native_gui_native_layout_plan_path: nativeLayoutPlanPath,
    native_gui_native_layout_plan_policy_source: String(nativeLayoutPlan.layout_policy_source || ''),
    native_gui_native_layout_plan_item_count: Number(nativeLayoutPlan.item_count || 0),
    native_gui_native_layout_plan_viewport_item_count: Number(nativeLayoutPlan.viewport_item_count || 0),
    native_gui_native_layout_plan_clipped_item_count: Number(nativeLayoutPlan.clipped_item_count || 0),
    native_gui_native_layout_plan_interactive_item_count: Number(nativeLayoutPlan.interactive_item_count || 0),
    native_gui_native_layout_plan_source_backed_item_count: Number(nativeLayoutPlan.source_backed_item_count || 0),
    native_gui_native_layout_plan_scroll_height: Number(nativeLayoutPlan.scroll_height || 0),
    native_gui_host_abi_feature_hits: hostAbi.feature_hits,
    native_gui_host_abi_first_batch_features: Array.isArray(hostAbi.first_batch?.detected_features) ? hostAbi.first_batch.detected_features : [],
    native_gui_host_abi_first_batch_missing_features: Array.isArray(hostAbi.first_batch?.missing_features) ? hostAbi.first_batch.missing_features : [],
    native_gui_host_abi_first_batch_ready: hostAbiFirstBatchReady,
    native_gui_host_ready: nativeGuiHostReady,
    native_gui_renderer_ready: nativeGuiRendererReady,
    native_gui_session_preview_ready: nativeGuiSessionPreviewReady,
    native_gui_layout_surface_ready: true,
    native_gui_style_layout_surface_ready: true,
    native_gui_native_layout_plan_ready: true,
  });
  writeSummary(summaryPath, {
    native_gui_report_path: reportPath,
    native_gui_bundle_path: bundlePath,
    native_gui_launcher_module: launcherModule,
    native_gui_launcher_module_path: launcherModulePath,
    native_gui_launcher_exe_path: launcherExePath,
    native_gui_launcher_compile_report_path: launcherCompileReportPath,
    native_gui_launcher_compile_log_path: launcherCompileLogPath,
    native_gui_launcher_run_log_path: launcherRunLogPath,
    native_gui_launcher_compile_returncode: previewRun.compileReturnCode,
    native_gui_launcher_run_returncode: previewRun.runReturnCode,
    native_gui_launcher_compile_ok: previewRun.compileReturnCode === 0,
    native_gui_launcher_run_ok: previewRun.runReturnCode === 0,
    native_gui_bundle_ready: true,
    native_gui_host_ready: nativeGuiHostReady,
    native_gui_renderer_ready: nativeGuiRendererReady,
    native_gui_exec_snapshot_path: execSnapshotPath,
    native_gui_route_catalog_path: routeCatalogPath,
    native_gui_session_path: sessionPath,
    native_gui_session_preview_ready: nativeGuiSessionPreviewReady,
    native_gui_session_preview_mode: String(sessionDoc?.preview_mode || 'cheng_ui_host_preview_v1'),
    native_gui_layout_surface_path: layoutSurfacePath,
    native_gui_layout_surface_ready: true,
    native_gui_layout_surface_node_count: bundle.layout_surface.node_count,
    native_gui_layout_surface_source_module: bundle.layout_surface.source_module_path,
    native_gui_layout_surface_source_component: bundle.layout_surface.source_component_name,
    native_gui_layout_surface_strategy: bundle.layout_surface.source_strategy,
    native_gui_layout_surface_component_expansion_count: bundle.layout_surface.component_expansion_count,
    native_gui_layout_surface_parsed_module_count: bundle.layout_surface.parsed_module_count,
    native_gui_layout_surface_styled_node_count: bundle.layout_surface.styled_node_count,
    native_gui_layout_surface_interactive_node_count: bundle.layout_surface.interactive_node_count,
    native_gui_style_layout_surface_path: styleLayoutSurfacePath,
    native_gui_style_layout_surface_ready: true,
    native_gui_style_layout_surface_node_count: bundle.style_layout_surface.node_count,
    native_gui_style_layout_overlay_node_count: bundle.style_layout_surface.overlay_node_count,
    native_gui_style_layout_surface_surface_node_count: bundle.style_layout_surface.surface_node_count,
    native_gui_style_layout_surface_control_node_count: bundle.style_layout_surface.control_node_count,
    native_gui_native_layout_plan_path: nativeLayoutPlanPath,
    native_gui_native_layout_plan_ready: true,
    native_gui_native_layout_plan_policy_source: bundle.native_layout_plan.layout_policy_source,
    native_gui_native_layout_plan_item_count: bundle.native_layout_plan.item_count,
    native_gui_native_layout_plan_viewport_item_count: bundle.native_layout_plan.viewport_item_count,
    native_gui_native_layout_plan_clipped_item_count: bundle.native_layout_plan.clipped_item_count,
    native_gui_native_layout_plan_interactive_item_count: bundle.native_layout_plan.interactive_item_count,
    native_gui_native_layout_plan_source_backed_item_count: bundle.native_layout_plan.source_backed_item_count,
    native_gui_native_layout_plan_scroll_height: bundle.native_layout_plan.scroll_height,
    native_gui_render_plan_path: nativeRenderPlanPath,
    native_gui_render_plan_ready: true,
    native_gui_render_plan_command_count: bundle.native_render_plan.command_count,
    native_gui_render_plan_visible_layout_item_count: bundle.native_render_plan.visible_layout_item_count,
    native_gui_runtime_path: nativeRuntimeDocPath,
    native_gui_runtime_ready: true,
    native_gui_runtime_state_path: nativeRuntimeStatePath,
    native_gui_runtime_exe_path: runtimeCompiledExePath,
    native_gui_runtime_launcher_exe_path: runtimeExePath,
    native_gui_runtime_compiled_exe_path: runtimeCompiledExePath,
    native_gui_runtime_contract_path: runtimeContractPath,
    native_gui_runtime_mode: 'cheng_compiled_json_v1',
    native_gui_runtime_compile_report_path: runtimeCompileReportPath,
    native_gui_runtime_compile_log_path: runtimeCompileLogPath,
    native_gui_runtime_run_log_path: runtimeRunLogPath,
    native_gui_runtime_controller_log_path: runtimeControllerLogPath,
    native_gui_runtime_controller_returncode: runtimeControllerRun.exitCode,
    native_gui_runtime_compile_returncode: runtimeRun.compileReturnCode,
    native_gui_runtime_run_returncode: runtimeRun.runReturnCode,
    native_gui_runtime_manifest_ready: Boolean(nativeRuntimeState?.runtime_manifest_ready),
    native_gui_runtime_contract_payload_ready: Boolean(nativeRuntimeState?.runtime_contract_payload_ready),
    native_gui_runtime_bundle_payload_ready: Boolean(nativeRuntimeState?.runtime_bundle_payload_ready),
    native_gui_runtime_bundle_ready: Boolean(nativeRuntimeState?.runtime_bundle_ready),
    native_gui_runtime_contract_ready: Boolean(nativeRuntimeState?.runtime_contract_ready),
    native_gui_runtime_bundle_route_state: String(nativeRuntimeState?.bundle_route_state || targetRouteState),
    native_gui_runtime_bundle_route_count: Number(nativeRuntimeState?.bundle_route_count || 0),
    native_gui_runtime_bundle_supported_count: Number(nativeRuntimeState?.bundle_supported_count || 0),
    native_gui_runtime_bundle_semantic_nodes_count: Number(nativeRuntimeState?.bundle_semantic_nodes_count || 0),
    native_gui_runtime_bundle_layout_item_count: Number(nativeRuntimeState?.bundle_layout_item_count || 0),
    native_gui_runtime_bundle_render_command_count: Number(nativeRuntimeState?.bundle_render_command_count || 0),
    native_gui_runtime_contract_layout_item_count: Number(nativeRuntimeState?.contract_layout_item_count || 0),
    native_gui_runtime_contract_viewport_item_count: Number(nativeRuntimeState?.contract_viewport_item_count || 0),
    native_gui_runtime_contract_interactive_item_count: Number(nativeRuntimeState?.contract_interactive_item_count || 0),
    native_gui_storage_host_ready: storageHostReady,
    native_gui_fetch_host_ready: fetchHostReady,
    native_gui_custom_event_host_ready: customEventHostReady,
    native_gui_resize_observer_host_ready: resizeObserverHostReady,
    native_gui_runtime_focused_item_id: bundle.native_runtime.focused_item_id,
    native_gui_runtime_typed_text: bundle.native_runtime.typed_text,
    native_gui_exec_snapshot_route_state: String(execSnapshot.route_state || ''),
    native_gui_route_state: targetRouteState,
    entry_route_state: entryRouteState,
    route_count: bundle.routes.route_count,
    supported_count: bundle.routes.supported_count,
    unsupported_count: bundle.routes.unsupported_count,
    required_host_group_count: requiredGroups.length,
    required_host_groups: requiredGroups.join(','),
    required_host_method_count: requiredMethods.length,
    native_gui_host_abi_feature_hits: String(nativeRuntimeState?.host_abi_feature_hits_csv || hostAbi.feature_hits.join(',')),
    native_gui_host_abi_first_batch_ready: Boolean(nativeRuntimeState?.host_abi_first_batch_ready ?? hostAbi.first_batch.ready),
    native_gui_host_abi_first_batch_features: hostAbi.first_batch.detected_features.join(','),
    native_gui_host_abi_first_batch_missing_features: String(nativeRuntimeState?.host_abi_missing_features_csv || hostAbi.first_batch.missing_features.join(',')),
    asset_count: bundle.assets.asset_count,
    tailwind_token_count: bundle.assets.tailwind_token_count,
    initial_semantic_nodes_count: bundle.entry_surface.semantic_nodes_count,
    package_root: packageRoot,
    package_id: packageId,
  });

  console.log(JSON.stringify({
    ok: true,
    repo_root: repo,
    out_dir: outDir,
    report_path: reportPath,
    native_gui_bundle_path: bundlePath,
    native_gui_launcher_module: launcherModule,
    native_gui_session_path: sessionPath,
    native_gui_layout_surface_path: layoutSurfacePath,
    native_gui_layout_surface_node_count: bundle.layout_surface.node_count,
    native_gui_layout_surface_source_module: bundle.layout_surface.source_module_path,
    native_gui_layout_surface_source_component: bundle.layout_surface.source_component_name,
    native_gui_layout_surface_strategy: bundle.layout_surface.source_strategy,
    native_gui_layout_surface_component_expansion_count: bundle.layout_surface.component_expansion_count,
    native_gui_layout_surface_parsed_module_count: bundle.layout_surface.parsed_module_count,
    native_gui_layout_surface_styled_node_count: bundle.layout_surface.styled_node_count,
    native_gui_layout_surface_interactive_node_count: bundle.layout_surface.interactive_node_count,
    native_gui_style_layout_surface_path: styleLayoutSurfacePath,
    native_gui_style_layout_surface_node_count: bundle.style_layout_surface.node_count,
    native_gui_style_layout_overlay_node_count: bundle.style_layout_surface.overlay_node_count,
    native_gui_style_layout_surface_surface_node_count: bundle.style_layout_surface.surface_node_count,
    native_gui_style_layout_surface_control_node_count: bundle.style_layout_surface.control_node_count,
    native_gui_native_layout_plan_path: nativeLayoutPlanPath,
    native_gui_native_layout_plan_policy_source: bundle.native_layout_plan.layout_policy_source,
    native_gui_native_layout_plan_item_count: bundle.native_layout_plan.item_count,
    native_gui_native_layout_plan_viewport_item_count: bundle.native_layout_plan.viewport_item_count,
    native_gui_native_layout_plan_clipped_item_count: bundle.native_layout_plan.clipped_item_count,
    native_gui_native_layout_plan_interactive_item_count: bundle.native_layout_plan.interactive_item_count,
    native_gui_native_layout_plan_source_backed_item_count: bundle.native_layout_plan.source_backed_item_count,
    native_gui_native_layout_plan_scroll_height: bundle.native_layout_plan.scroll_height,
    native_gui_render_plan_path: nativeRenderPlanPath,
    native_gui_render_plan_command_count: bundle.native_render_plan.command_count,
    native_gui_render_plan_visible_layout_item_count: bundle.native_render_plan.visible_layout_item_count,
    native_gui_runtime_path: nativeRuntimeDocPath,
    native_gui_runtime_state_path: nativeRuntimeStatePath,
    native_gui_runtime_exe_path: runtimeCompiledExePath,
    native_gui_runtime_launcher_exe_path: runtimeExePath,
    native_gui_runtime_compiled_exe_path: runtimeCompiledExePath,
    native_gui_runtime_contract_path: runtimeContractPath,
    native_gui_runtime_mode: 'cheng_compiled_json_v1',
    native_gui_runtime_controller_log_path: runtimeControllerLogPath,
    native_gui_runtime_controller_returncode: runtimeControllerRun.exitCode,
    native_gui_runtime_manifest_ready: Boolean(nativeRuntimeState?.runtime_manifest_ready),
    native_gui_runtime_contract_payload_ready: Boolean(nativeRuntimeState?.runtime_contract_payload_ready),
    native_gui_runtime_bundle_payload_ready: Boolean(nativeRuntimeState?.runtime_bundle_payload_ready),
    native_gui_runtime_bundle_ready: Boolean(nativeRuntimeState?.runtime_bundle_ready),
    native_gui_runtime_contract_ready: Boolean(nativeRuntimeState?.runtime_contract_ready),
    native_gui_runtime_bundle_route_state: String(nativeRuntimeState?.bundle_route_state || targetRouteState),
    native_gui_runtime_bundle_route_count: Number(nativeRuntimeState?.bundle_route_count || 0),
    native_gui_runtime_bundle_supported_count: Number(nativeRuntimeState?.bundle_supported_count || 0),
    native_gui_runtime_bundle_semantic_nodes_count: Number(nativeRuntimeState?.bundle_semantic_nodes_count || 0),
    native_gui_runtime_bundle_layout_item_count: Number(nativeRuntimeState?.bundle_layout_item_count || 0),
    native_gui_runtime_bundle_render_command_count: Number(nativeRuntimeState?.bundle_render_command_count || 0),
    native_gui_runtime_contract_layout_item_count: Number(nativeRuntimeState?.contract_layout_item_count || 0),
    native_gui_runtime_contract_viewport_item_count: Number(nativeRuntimeState?.contract_viewport_item_count || 0),
    native_gui_runtime_contract_interactive_item_count: Number(nativeRuntimeState?.contract_interactive_item_count || 0),
    native_gui_exec_snapshot_path: execSnapshotPath,
    native_gui_route_catalog_path: routeCatalogPath,
    native_gui_storage_host_ready: storageHostReady,
    native_gui_fetch_host_ready: fetchHostReady,
    native_gui_custom_event_host_ready: customEventHostReady,
    native_gui_resize_observer_host_ready: resizeObserverHostReady,
    native_gui_route_state: targetRouteState,
    route_count: bundle.routes.route_count,
    required_host_groups: requiredGroups,
    native_gui_host_abi_feature_hits: String(nativeRuntimeState?.host_abi_feature_hits_csv || hostAbi.feature_hits.join(',')).split(',').filter(Boolean),
    native_gui_host_abi_first_batch_ready: hostAbiFirstBatchReady,
    native_gui_host_abi_first_batch_features: hostAbi.first_batch.detected_features,
    native_gui_host_abi_first_batch_missing_features: String(nativeRuntimeState?.host_abi_missing_features_csv || hostAbi.first_batch.missing_features.join(',')).split(',').filter(Boolean),
    asset_count: bundle.assets.asset_count,
    tailwind_token_count: bundle.assets.tailwind_token_count,
    session_preview_ready: nativeGuiSessionPreviewReady,
    session_preview_mode: String(sessionDoc?.preview_mode || 'cheng_ui_host_preview_v1'),
    host_ready: nativeGuiHostReady,
    renderer_ready: nativeGuiRendererReady,
  }, null, 2));
}

main();
