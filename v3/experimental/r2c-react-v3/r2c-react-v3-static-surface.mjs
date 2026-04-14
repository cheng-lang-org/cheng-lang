#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const SKIP_DIRS = new Set(['node_modules', 'dist', 'build', 'artifacts', '.git', '.build', '.tmp-deploy']);
const SOURCE_EXTS = new Set(['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs']);
const ASSET_KIND_BY_EXT = new Map([
  ['.png', 'image'],
  ['.jpg', 'image'],
  ['.jpeg', 'image'],
  ['.gif', 'image'],
  ['.webp', 'image'],
  ['.svg', 'image'],
  ['.ico', 'image'],
  ['.woff', 'font'],
  ['.woff2', 'font'],
  ['.ttf', 'font'],
  ['.otf', 'font'],
  ['.wasm', 'wasm'],
  ['.mp3', 'audio'],
  ['.wav', 'audio'],
  ['.ogg', 'audio'],
  ['.mp4', 'video'],
  ['.webm', 'video'],
  ['.html', 'html'],
  ['.css', 'css'],
  ['.json', 'json'],
]);
const CLASS_NAME_RE = /className\s*=\s*(?:\{\s*)?(?<quote>["'`])(?<value>(?:\\.|(?!\k<quote>).)*?)(?:\k<quote>)(?:\s*\})?/gms;

const HOST_METHODS = {
  WindowHost: ['createWindow', 'requestFrame', 'dispatchInput', 'invalidateLayout'],
  StorageHost: ['getItem', 'setItem', 'removeItem', 'clear'],
  NetHost: ['fetch', 'historyPush', 'historyReplace', 'workerSpawn', 'wasmInstantiate'],
  RtcHost: ['peerConnectionOpen', 'peerConnectionClose', 'readStats', 'openDataChannel'],
  MediaHost: ['getUserMedia', 'attachAudio', 'setTrackEnabled'],
  PlatformHost: ['readEnv', 'serviceWorkerUpdate', 'capacitorInvoke', 'emitCustomEvent'],
  SceneHost: ['createScene', 'createMesh', 'setCamera', 'stepPhysics'],
};

const HOST_GROUP_HITS = {
  StorageHost: new Set(['local_storage']),
  NetHost: new Set(['fetch', 'broadcast_channel', 'worker', 'webassembly']),
  RtcHost: new Set(['rtc']),
  MediaHost: new Set(['media']),
  PlatformHost: new Set(['capacitor', 'custom_event', 'resize_observer', 'mutation_observer']),
  SceneHost: new Set(['react_three']),
  WindowHost: new Set(['strict_mode', 'suspense']),
};

function parseArgs(argv) {
  const out = {
    repo: '',
    tsxAstPath: '',
    outDir: '',
    summaryOut: '',
    assetRoots: [],
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--tsx-ast') out.tsxAstPath = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--asset-root') out.assetRoots.push(String(argv[++i] || ''));
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-static-surface.mjs --repo <path> [--tsx-ast <file>] [--out-dir <dir>] [--summary-out <file>] [--asset-root <dir>]');
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

function pathUnderRoots(relPath, roots) {
  if (roots.includes('.')) return true;
  return roots.some((root) => relPath === root || relPath.startsWith(`${root}/`));
}

function normalizeRoots(repo, requested, fallback) {
  const picks = requested.length > 0 ? requested : fallback;
  const out = [];
  for (const item of picks) {
    const root = normalizeRootValue(item);
    if (root !== '.' && !fs.existsSync(path.join(repo, root))) {
      if (requested.length > 0) {
        throw new Error(`missing asset root: ${root}`);
      }
      continue;
    }
    if (!out.includes(root)) {
      out.push(root);
    }
  }
  return out.length > 0 ? out : ['.'];
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
}

function classifyAsset(relPath) {
  return ASSET_KIND_BY_EXT.get(path.extname(relPath).toLowerCase()) || 'other';
}

function sha256File(filePath) {
  try {
    return crypto.createHash('sha256').update(fs.readFileSync(filePath)).digest('hex');
  } catch {
    return '';
  }
}

function assetFiles(repo, assetRoots) {
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
      if (!pathUnderRoots(relPath, assetRoots)) continue;
      const ext = path.extname(fullPath).toLowerCase();
      if (!ASSET_KIND_BY_EXT.has(ext) || SOURCE_EXTS.has(ext)) continue;
      out.push(fullPath);
    }
  }
  walk(repo);
  out.sort();
  return out;
}

function extractClassTokens(text) {
  const out = [];
  let match = null;
  while ((match = CLASS_NAME_RE.exec(text)) !== null) {
    const raw = String(match.groups?.value || '').trim();
    if (!raw || raw.includes('${')) continue;
    for (const token of raw.split(/\s+/).filter(Boolean)) {
      out.push(token);
    }
  }
  return out;
}

function buildHostContract(modules) {
  const featureHits = new Set();
  for (const module of modules) {
    for (const [name, enabled] of Object.entries(module.features || {})) {
      if (enabled) featureHits.add(name);
    }
  }
  const methods = [];
  const requiredGroups = [];
  for (const [group, requiredFeatures] of Object.entries(HOST_GROUP_HITS)) {
    const required = [...requiredFeatures].some((feature) => featureHits.has(feature));
    if (required) requiredGroups.push(group);
    for (const name of HOST_METHODS[group]) {
      methods.push({
        group,
        name,
        signature: 'opaque',
        required,
      });
    }
  }
  requiredGroups.sort();
  return {
    format: 'unimaker_host_v1',
    name: 'unimaker_host_v1',
    methods,
    required_groups: requiredGroups,
  };
}

function buildTailwindManifest(repo, modules) {
  const tokenSources = new Map();
  const tokenCounts = new Map();
  for (const module of modules) {
    const fullPath = path.join(repo, module.path);
    let text = '';
    try {
      text = fs.readFileSync(fullPath, 'utf8');
    } catch {
      text = '';
    }
    for (const token of extractClassTokens(text)) {
      if (!tokenSources.has(token)) tokenSources.set(token, new Set());
      tokenSources.get(token).add(module.path);
      tokenCounts.set(token, (tokenCounts.get(token) || 0) + 1);
    }
  }
  const tokens = [...tokenSources.keys()].sort().map((token) => ({
    token,
    sources: [...tokenSources.get(token)].sort(),
    count: tokenCounts.get(token) || 0,
  }));
  return {
    format: 'tailwind_style_manifest_v1',
    repo_root: repo,
    tokens,
  };
}

function buildAssetManifest(repo, modules, assetRoots) {
  const entries = [];
  const seen = new Set();
  const knownAssetPaths = new Set();
  for (const module of modules) {
    const dynamicImports = new Set(module.dynamic_imports || []);
    for (const asset of module.asset_refs || []) {
      const key = `${module.path}\n${asset}`;
      if (seen.has(key)) continue;
      seen.add(key);
      knownAssetPaths.add(asset);
      entries.push({
        path: asset,
        kind: classifyAsset(asset),
        source_module: module.path,
        import_mode: dynamicImports.has(asset) ? 'dynamic' : 'static',
        content_hash: '',
      });
    }
  }
  for (const assetPath of assetFiles(repo, assetRoots)) {
    const relPath = path.relative(repo, assetPath).split(path.sep).join('/');
    if (knownAssetPaths.has(relPath)) continue;
    entries.push({
      path: relPath,
      kind: classifyAsset(relPath),
      source_module: '',
      import_mode: 'discovered',
      content_hash: sha256File(assetPath),
    });
  }
  entries.sort((a, b) =>
    a.path.localeCompare(b.path)
    || a.source_module.localeCompare(b.source_module)
    || a.import_mode.localeCompare(b.import_mode));
  return {
    format: 'asset_manifest_v1',
    repo_root: repo,
    asset_roots: assetRoots,
    entries,
  };
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
  const fallbackAssetRoots = [...new Set([...runtimeRoots, 'app', 'public', 'styles'])];
  const assetRoots = normalizeRoots(repo, args.assetRoots, fallbackAssetRoots);
  const moduleInventory = {
    format: 'module_inventory_v1',
    repo_root: repo,
    source_roots: runtimeRoots,
    modules,
  };
  const hostContract = buildHostContract(modules);
  const assetManifest = buildAssetManifest(repo, modules, assetRoots);
  const tailwindManifest = buildTailwindManifest(repo, modules);
  const moduleInventoryPath = path.join(outDir, 'module_inventory_v1.json');
  const hostContractPath = path.join(outDir, 'unimaker_host_v1.json');
  const assetManifestPath = path.join(outDir, 'asset_manifest_v1.json');
  const tailwindManifestPath = path.join(outDir, 'tailwind_style_manifest_v1.json');
  writeJson(moduleInventoryPath, moduleInventory);
  writeJson(hostContractPath, hostContract);
  writeJson(assetManifestPath, assetManifest);
  writeJson(tailwindManifestPath, tailwindManifest);
  writeSummary(args.summaryOut, {
    module_count: modules.length,
    asset_count: assetManifest.entries.length,
    tailwind_token_count: tailwindManifest.tokens.length,
    required_host_group_count: hostContract.required_groups.length,
    required_host_groups: hostContract.required_groups.join(','),
    typescript_version: String(doc.typescript_version || ''),
    module_inventory_path: moduleInventoryPath,
    host_contract_path: hostContractPath,
    asset_manifest_path: assetManifestPath,
    tailwind_manifest_path: tailwindManifestPath,
  });
  console.log(JSON.stringify({
    ok: true,
    repo_root: repo,
    out_dir: outDir,
    tsx_ast_path: tsxAstPath,
    module_count: modules.length,
    asset_count: assetManifest.entries.length,
    tailwind_token_count: tailwindManifest.tokens.length,
    required_host_groups: hostContract.required_groups,
  }, null, 2));
}

main();
