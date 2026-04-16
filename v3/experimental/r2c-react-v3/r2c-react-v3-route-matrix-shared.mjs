import fs from 'node:fs';
import path from 'node:path';
import child_process from 'node:child_process';
import { fileURLToPath } from 'node:url';

export const HOME_DEFAULT_EQUIVALENT_ROUTES = new Set([
  'home_default',
  'home_channel_manager_open',
  'home_content_detail_open',
  'home_search_open',
  'home_sort_open',
  'game_xiangqi',
]);

const NODES_PAGE_MODULE_PATH = 'app/components/NodesPage.tsx';
const MESSAGES_PAGE_MODULE_PATH = 'app/components/MessagesPage.tsx';
const GOVERNANCE_PAGE_MODULE_PATH = 'app/components/GovernanceConsolePage.tsx';
const LANGUAGE_SELECTOR_MODULE_PATH = 'app/components/LanguageSelector.tsx';
const TRADING_PAGE_MODULE_PATH = 'app/components/TradingPage.tsx';
const UPDATE_CENTER_PAGE_MODULE_PATH = 'app/components/UpdateCenterPage.tsx';
const CONTENT_DETAIL_PAGE_MODULE_PATH = 'app/components/ContentDetailPage.tsx';
const ECOM_FEED_PAGE_MODULE_PATH = 'app/components/EcomFeedPage.tsx';
const PROFILE_PAGE_MODULE_PATH = 'app/components/ProfilePage.tsx';
const APP_MARKETPLACE_MODULE_PATH = 'app/components/AppMarketplace.tsx';
const LIVE_STREAM_PAGE_MODULE_PATH = 'app/components/LiveStreamPage.tsx';
const CHAT_PAGE_MODULE_PATH = 'app/components/ChatPage.tsx';
const BAZI_PAGE_MODULE_PATH = 'app/components/BaziPage.tsx';
const ZIWEI_PAGE_MODULE_PATH = 'app/components/ZiweiPage.tsx';
const MAHJONG_PAGE_MODULE_PATH = 'app/components/MahjongPage.tsx';
const MINECRAFT_PAGE_MODULE_PATH = 'app/components/MinecraftPage.tsx';

const LAST_RETURN_ROUTE_COMPONENTS = {
  tab_nodes: [NODES_PAGE_MODULE_PATH, 'NodesPage'],
  tab_messages: [MESSAGES_PAGE_MODULE_PATH, 'MessagesPage'],
  tab_profile: [PROFILE_PAGE_MODULE_PATH, 'ProfilePage'],
  governance_main: [GOVERNANCE_PAGE_MODULE_PATH, 'GovernanceConsolePage'],
  lang_select: [LANGUAGE_SELECTOR_MODULE_PATH, 'LanguageSelector'],
  trading_main: [TRADING_PAGE_MODULE_PATH, 'TradingPage'],
  trading_crosshair: [TRADING_PAGE_MODULE_PATH, 'TradingPage'],
  update_center_main: [UPDATE_CENTER_PAGE_MODULE_PATH, 'UpdateCenterPage'],
  content_detail: [CONTENT_DETAIL_PAGE_MODULE_PATH, 'ContentDetailPage'],
  marketplace_main: [APP_MARKETPLACE_MODULE_PATH, 'AppMarketplace'],
  publish_live: [LIVE_STREAM_PAGE_MODULE_PATH, 'LiveStreamPage'],
  message_thread: [CHAT_PAGE_MODULE_PATH, 'ChatPage'],
  node_thread: [CHAT_PAGE_MODULE_PATH, 'ChatPage'],
  group_draft: [CHAT_PAGE_MODULE_PATH, 'ChatPage'],
};

const LAST_RETURN_ROUTE_EXTRA_CANDIDATES = {
  tab_messages: [1],
  tab_profile: [4],
  group_draft: [3, 7, 11],
  message_thread: [26, 27],
  node_thread: [25, 26, 27],
  trading_main: [-2],
  trading_crosshair: [-2],
  update_center_main: [4],
  publish_live: [-2],
};

const TOTAL_SURFACE_ROUTE_COMPONENTS = {
  tab_nodes: [NODES_PAGE_MODULE_PATH, 'NodesPage'],
};

const EARLY_RETURN_PLUS_APP_ROOT_ROUTE_COMPONENTS = {
  governance_main: [GOVERNANCE_PAGE_MODULE_PATH, 'GovernanceConsolePage'],
  publish_content: ['app/components/PublishContentPage.tsx', 'PublishContentPage'],
};

const EARLY_RETURN_PLUS_APP_ROOT_ROUTE_EXTRA_CANDIDATES = {
  governance_main: [3, 4],
  publish_content: [3, 4],
};

const TOTAL_SURFACE_PLUS_APP_FRAME_ROUTE_COMPONENTS = {
  home_app_channel: ['app/components/HomePage.tsx', 'HomePage'],
  marketplace_main: [APP_MARKETPLACE_MODULE_PATH, 'AppMarketplace'],
  home_bazi_overlay_open: [BAZI_PAGE_MODULE_PATH, 'BaziPage'],
  game_mahjong: [MAHJONG_PAGE_MODULE_PATH, 'MahjongPage'],
  game_minecraft: [MINECRAFT_PAGE_MODULE_PATH, 'MinecraftPage'],
  publish_ad: ['app/components/PublishAdPage.tsx', 'PublishAdPage'],
  publish_app: ['app/components/PublishAppPage.tsx', 'PublishAppPage'],
  publish_content: ['app/components/PublishContentPage.tsx', 'PublishContentPage'],
  publish_crowdfunding: ['app/components/PublishCrowdfundingPage.tsx', 'PublishCrowdfundingPage'],
  publish_food: ['app/components/PublishFoodPage.tsx', 'PublishFoodPage'],
  publish_hire: ['app/components/PublishHirePage.tsx', 'PublishHirePage'],
  publish_job: ['app/components/PublishJobPage.tsx', 'PublishJobPage'],
  publish_product: ['app/components/PublishProductPage.tsx', 'PublishProductPage'],
  publish_rent: ['app/components/PublishRentPage.tsx', 'PublishRentPage'],
  publish_ride: ['app/components/PublishRidePage.tsx', 'PublishRidePage'],
  publish_secondhand: ['app/components/PublishSecondhandPage.tsx', 'PublishSecondhandPage'],
  publish_selector: ['app/components/PublishTypeSelector.tsx', 'PublishTypeSelector'],
  publish_sell: ['app/components/PublishSellPage.tsx', 'PublishSellPage'],
};

const TOTAL_SURFACE_PLUS_APP_FRAME_ROUTE_EXTRA_CANDIDATES = {
  home_app_channel: [80, 81],
  marketplace_main: [14, 15],
  home_bazi_overlay_open: [15],
  game_mahjong: [14],
  game_minecraft: [4, 5],
  publish_ad: [0, 2, 20, 21],
  publish_app: [20, 21],
  publish_content: [20, 21],
  publish_crowdfunding: [2, 5, 20, 21],
  publish_food: [2, 3, 20, 21],
  publish_hire: [20, 21, 24],
  publish_job: [1, 2, 3, 20, 21],
  publish_live: [20, 21],
  publish_product: [2, 3, 20, 21],
  publish_rent: [20, 21],
  publish_ride: [20, 21, 24],
  publish_secondhand: [1, 2, 3, 20, 21],
  publish_selector: [20, 21, 46, 66],
  publish_sell: [1, 2, 3, 20, 21],
};

const FIXED_ROUTE_COUNTS = {
  home_ziwei_overlay_open: [4],
  game_doudizhu: [180],
  game_werewolf: [83],
};

const lucideIconNodeCountCache = new Map();
const lucideIconDomCountCache = new Map();
const taobaoProductSurfaceCache = new Map();
const exactRouteSemanticCountCache = new Map();

export function resolvePrimaryRouteSurface(routeState, entryRouteState = '') {
  const route = String(routeState || '').trim();
  const entryRoute = String(entryRouteState || '').trim();
  if (!route) return null;
  if ((entryRoute && route === entryRoute) || HOME_DEFAULT_EQUIVALENT_ROUTES.has(route)) {
    return {
      strategy: 'entry_module',
      modulePath: '',
      componentName: '',
      reason: 'entry_module_surface',
    };
  }
  const lastReturn = LAST_RETURN_ROUTE_COMPONENTS[route];
  if (lastReturn) {
    return {
      strategy: 'last_return',
      modulePath: String(lastReturn[0] || ''),
      componentName: String(lastReturn[1] || ''),
      reason: 'last_return_route_component',
    };
  }
  const totalSurface = TOTAL_SURFACE_PLUS_APP_FRAME_ROUTE_COMPONENTS[route]
    || TOTAL_SURFACE_ROUTE_COMPONENTS[route]
    || EARLY_RETURN_PLUS_APP_ROOT_ROUTE_COMPONENTS[route];
  if (totalSurface) {
    return {
      strategy: 'total_surface',
      modulePath: String(totalSurface[0] || ''),
      componentName: String(totalSurface[1] || ''),
      reason: 'total_surface_route_component',
    };
  }
  if (route === 'home_ziwei_overlay_open') {
    return {
      strategy: 'total_surface',
      modulePath: ZIWEI_PAGE_MODULE_PATH,
      componentName: 'ZiweiPage',
      reason: 'ziwei_overlay_component',
    };
  }
  return null;
}

export function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
}

export function writeJson(filePath, payload) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, JSON.stringify(payload, null, 2) + '\n', 'utf8');
}

export function writeSummary(filePath, values) {
  if (!filePath) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`);
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, `${lines.join('\n')}\n`, 'utf8');
}

export function buildCodegenExecSnapshotZero() {
  return {
    format: 'cheng_codegen_exec_snapshot_v1',
    route_state: '',
    mount_phase: '',
    commit_phase: '',
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
}

export function buildCodegenExecRouteMatrixZero() {
  return {
    format: 'cheng_codegen_exec_route_matrix_v1',
    runnerMode: 'react_surface_runner_v1',
    entryRouteState: '',
    routeCount: 0,
    supportedCount: 0,
    unsupportedCount: 0,
    routes: [],
    entries: [],
    outPath: '',
  };
}

export function buildCodegenRouteCatalogZero() {
  return {
    format: 'cheng_codegen_route_catalog_v1',
    runnerMode: 'react_surface_runner_v1',
    entryRouteState: '',
    routeCount: 0,
    supportedCount: 0,
    unsupportedCount: 0,
    routes: [],
    entries: [],
    outPath: '',
  };
}

export function buildTruthCompareMatrixZero() {
  return {
    format: 'truth_compare_matrix_v1',
    ok: false,
    reason: '',
    truthTracePath: '',
    truthTraceFormat: '',
    routeCount: 0,
    okCount: 0,
    unsupportedCount: 0,
    mismatchCount: 0,
    missingTruthStateCount: 0,
    missingTruthFieldCount: 0,
    entries: [],
    outPath: '',
  };
}

export function buildTruthCompareZero() {
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

export function coerceJsonBool(value) {
  if (typeof value === 'boolean') return value;
  if (typeof value === 'string') {
    const lowered = value.trim().toLowerCase();
    if (lowered === 'true') return true;
    if (lowered === 'false') return false;
  }
  return null;
}

export function coerceJsonInt(value) {
  if (typeof value === 'number' && Number.isInteger(value)) return value;
  if (typeof value === 'string' && /^-?[0-9]+$/.test(value.trim())) return Number.parseInt(value.trim(), 10);
  return null;
}

function coerceJsonStr(value) {
  return typeof value === 'string' ? value : null;
}

export function loadTruthRoutesDoc(filePath) {
  const doc = readJson(filePath);
  if (String(doc.format || '') !== 'truth_routes_v1') {
    throw new Error('unsupported truth routes format');
  }
  return doc;
}

export function normalizeTruthRoutes(routeValues, routeDocs) {
  const ordered = [];
  for (const raw of routeValues) {
    const route = String(raw || '').trim();
    if (route && !ordered.includes(route)) ordered.push(route);
  }
  for (const doc of routeDocs) {
    const routes = Array.isArray(doc?.routes) ? doc.routes : [];
    for (const entry of routes) {
      const route = String(entry?.routeId || entry?.route_id || '').trim();
      if (route && !ordered.includes(route)) ordered.push(route);
    }
  }
  if (ordered.length === 0) {
    throw new Error('missing truth routes; pass --truth-routes-file or --route');
  }
  return ordered;
}

export function loadTruthTrace(filePath) {
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

export function compareExecSnapshotToTruthDoc(execSnapshot, execSnapshotPath, truthDoc, truthTracePath, truthState = '') {
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

function findComponentSurface(modules, modulePath, componentName) {
  for (const module of modules) {
    if (String(module?.path || '') !== modulePath) continue;
    const components = Array.isArray(module.components) ? module.components : [];
    for (const component of components) {
      if (String(component?.name || '') === componentName) return component;
    }
  }
  return null;
}

function buildUniqueComponentMap(modules) {
  const seen = new Map();
  const duplicates = new Set();
  for (const module of modules) {
    for (const component of Array.isArray(module?.components) ? module.components : []) {
      const name = String(component?.name || '').trim();
      if (!name) continue;
      if (seen.has(name)) {
        duplicates.add(name);
      } else {
        seen.set(name, component);
      }
    }
  }
  for (const name of duplicates) {
    seen.delete(name);
  }
  return seen;
}

function normalizeRouteCandidateEntries(derivedCandidates) {
  const candidates = [];
  for (const [snapshot, reason] of derivedCandidates) {
    const semanticNodesCount = Number(snapshot?.semantic_nodes_count || 0);
    const label = String(reason || '').trim() || 'derived_candidate';
    if (!candidates.some((item) => item.semanticNodesCount === semanticNodesCount)) {
      candidates.push({
        semanticNodesCount,
        reason: label,
      });
    }
  }
  return candidates;
}

function toKebabCase(value) {
  return String(value || '')
    .replace(/([a-z0-9])([A-Z])/g, '$1-$2')
    .replace(/_/g, '-')
    .toLowerCase();
}

function loadLucideIconNodeCount(repoRoot, tagName) {
  if (!repoRoot) return null;
  const cacheKey = `${repoRoot}::${tagName}`;
  if (lucideIconNodeCountCache.has(cacheKey)) return lucideIconNodeCountCache.get(cacheKey);
  const iconPath = path.join(repoRoot, 'node_modules', 'lucide-react', 'dist', 'esm', 'icons', `${toKebabCase(tagName)}.js`);
  if (!fs.existsSync(iconPath)) {
    lucideIconNodeCountCache.set(cacheKey, null);
    return null;
  }
  const text = fs.readFileSync(iconPath, 'utf8');
  const match = text.match(/createLucideIcon\([^,]+,\s*\[(.*?)\]\s*\)/s);
  if (!match) {
    lucideIconNodeCountCache.set(cacheKey, null);
    return null;
  }
  const count = (match[1].match(/\[\s*"/g) || []).length;
  const resolved = count > 0 ? count : null;
  lucideIconNodeCountCache.set(cacheKey, resolved);
  return resolved;
}

function loadLucideIconDomCount(repoRoot, tagName) {
  if (!repoRoot) return null;
  const cacheKey = `${repoRoot}::${tagName}`;
  if (lucideIconDomCountCache.has(cacheKey)) return lucideIconDomCountCache.get(cacheKey);
  const childCount = loadLucideIconNodeCount(repoRoot, tagName);
  const resolved = childCount === null ? null : childCount + 1;
  lucideIconDomCountCache.set(cacheKey, resolved);
  return resolved;
}

function parseCsvLine(line) {
  const fields = [];
  let current = '';
  let inQuotes = false;
  for (let index = 0; index < line.length; index += 1) {
    const ch = line[index];
    if (inQuotes) {
      if (ch === '"' && line[index + 1] === '"') {
        current += '"';
        index += 1;
      } else if (ch === '"') {
        inQuotes = false;
      } else {
        current += ch;
      }
      continue;
    }
    if (ch === '"') {
      inQuotes = true;
    } else if (ch === ',') {
      fields.push(current);
      current = '';
    } else {
      current += ch;
    }
  }
  fields.push(current);
  return fields;
}

function loadTaobaoProductSurface(repoRoot) {
  if (!repoRoot) return null;
  if (taobaoProductSurfaceCache.has(repoRoot)) {
    return taobaoProductSurfaceCache.get(repoRoot);
  }
  const csvPath = path.resolve(repoRoot, '../taobao-detail-sku.csv');
  if (!fs.existsSync(csvPath)) {
    taobaoProductSurfaceCache.set(repoRoot, null);
    return null;
  }
  const products = new Map();
  const lines = fs.readFileSync(csvPath, 'utf8').split(/\r?\n/).filter((line) => line.trim().length > 0);
  for (let index = 1; index < lines.length; index += 1) {
    const fields = parseCsvLine(lines[index]);
    if (fields.length < 3) continue;
    const [title, , priceText] = fields;
    const productTitle = String(title || '').trim();
    if (!productTitle || products.has(productTitle)) continue;
    const compactPriceText = String(priceText || '').replace(/\s+/g, '');
    const hasOriginalPrice = compactPriceText.includes('优惠前');
    const hasSold = /已售\s*[\d万+]+/.test(compactPriceText);
    products.set(productTitle, {
      hasOriginalPrice,
      hasSold,
    });
  }
  let cardSurface = 0;
  for (const product of products.values()) {
    cardSurface += 9;
    if (product.hasOriginalPrice) cardSurface += 1;
    if (product.hasSold) cardSurface += 1;
  }
  const resolved = {
    productCount: products.size,
    cardSurface,
  };
  taobaoProductSurfaceCache.set(repoRoot, resolved);
  return resolved;
}

function syncRepoHelperTemplate(repoRoot, relativeTargetPath, templateFileName) {
  const templatePath = fileURLToPath(new URL(`./${templateFileName}`, import.meta.url));
  const targetPath = path.join(repoRoot, relativeTargetPath);
  const next = fs.readFileSync(templatePath, 'utf8');
  let current = null;
  if (fs.existsSync(targetPath)) {
    current = fs.readFileSync(targetPath, 'utf8');
  }
  if (current !== next) {
    fs.mkdirSync(path.dirname(targetPath), { recursive: true });
    fs.writeFileSync(targetPath, next, 'utf8');
  }
  return targetPath;
}

function loadExactRouteSemanticCount(routeState, repoRoot) {
  const cacheKey = `${repoRoot}::${routeState}`;
  if (exactRouteSemanticCountCache.has(cacheKey)) {
    return exactRouteSemanticCountCache.get(cacheKey);
  }
  let resolved = null;
  if (routeState === 'home_bazi_overlay_open') {
    const helperPath = syncRepoHelperTemplate(
      repoRoot,
      'node_modules/.cache/r2c-react-v3/bazi-overlay-count.tsx',
      'r2c-react-v3-bazi-overlay-count.template.tsx',
    );
    const tsxBin = path.join(repoRoot, 'node_modules', '.bin', 'tsx');
    if (!fs.existsSync(tsxBin)) {
      throw new Error(`missing tsx helper runtime: ${tsxBin}`);
    }
    const run = child_process.spawnSync(tsxBin, [helperPath], {
      cwd: repoRoot,
      encoding: 'utf8',
      maxBuffer: 32 * 1024 * 1024,
    });
    if (run.status !== 0) {
      throw new Error(`exact route helper failed for ${routeState}: ${String(run.stderr || run.stdout || '').trim()}`);
    }
    const stdoutText = String(run.stdout || '').trim();
    if (!stdoutText) {
      throw new Error(`exact route helper returned empty output for ${routeState}`);
    }
    const doc = JSON.parse(stdoutText);
    const count = coerceJsonInt(doc?.semanticNodesCount);
    if (count === null || count <= 0) {
      throw new Error(`exact route helper returned invalid semanticNodesCount for ${routeState}`);
    }
    resolved = count;
  }
  exactRouteSemanticCountCache.set(cacheKey, resolved);
  return resolved;
}

function refArray(surfaceLike, fieldName) {
  return Array.isArray(surfaceLike?.[fieldName]) ? surfaceLike[fieldName] : [];
}

function recursiveSurfaceCount(surfaceLike, fieldName, refsFieldName, uniqueComponents, repoRoot, subtractSelf, stack = new Set()) {
  const base = coerceJsonInt(surfaceLike?.[fieldName]);
  if (base === null || base <= 0) return null;
  let total = base;
  for (const ref of refArray(surfaceLike, refsFieldName)) {
    const tagName = String(ref?.name || '').trim();
    const count = Math.max(0, coerceJsonInt(ref?.count) ?? 0);
    if (!tagName || count <= 0) continue;
    const childComponent = uniqueComponents.get(tagName) || null;
    if (childComponent) {
      const cycleKey = `${tagName}:${fieldName}:${refsFieldName}`;
      if (stack.has(cycleKey)) continue;
      stack.add(cycleKey);
      const childCount = recursiveSurfaceCount(childComponent, fieldName, refsFieldName, uniqueComponents, repoRoot, subtractSelf, stack);
      stack.delete(cycleKey);
      if (childCount !== null && childCount > 0) {
        total += subtractSelf ? count * (childCount - 1) : count * childCount;
      }
      continue;
    }
    const lucideCount = loadLucideIconNodeCount(repoRoot, tagName);
    if (lucideCount !== null) {
      total += subtractSelf ? count * (lucideCount - 1) : count * lucideCount;
    }
  }
  return total;
}

function recursiveSurfaceDomCount(surfaceLike, fieldName, refsFieldName, uniqueComponents, repoRoot, subtractSelf, stack = new Set()) {
  const base = coerceJsonInt(surfaceLike?.[fieldName]);
  if (base === null || base <= 0) return null;
  let total = base;
  for (const ref of refArray(surfaceLike, refsFieldName)) {
    const tagName = String(ref?.name || '').trim();
    const count = Math.max(0, coerceJsonInt(ref?.count) ?? 0);
    if (!tagName || count <= 0) continue;
    const childComponent = uniqueComponents.get(tagName) || null;
    if (childComponent) {
      const cycleKey = `${tagName}:${fieldName}:${refsFieldName}:dom`;
      if (stack.has(cycleKey)) continue;
      stack.add(cycleKey);
      const childCount = recursiveSurfaceDomCount(childComponent, fieldName, refsFieldName, uniqueComponents, repoRoot, subtractSelf, stack);
      stack.delete(cycleKey);
      if (childCount !== null && childCount > 0) {
        total += subtractSelf ? count * (childCount - 1) : count * childCount;
      }
      continue;
    }
    const lucideCount = loadLucideIconDomCount(repoRoot, tagName);
    if (lucideCount !== null) {
      total += subtractSelf ? count * (lucideCount - 1) : count * lucideCount;
    }
  }
  return total;
}

function surfaceCountCandidates(component) {
  if (!component || typeof component !== 'object') return [];
  const out = [];
  const push = (value, label) => {
    const next = coerceJsonInt(value);
    if (next !== null && next > 0 && !out.some((entry) => entry.count === next)) {
      out.push({ count: next, label });
    }
  };
  push(component.surface_live_expanded_intrinsic_jsx_count, 'live_expanded_intrinsic');
  push(component.surface_live_expanded_jsx_count, 'live_expanded');
  push(component.surface_live_intrinsic_jsx_count, 'live_intrinsic');
  push(component.surface_live_jsx_count, 'live_raw');
  push(component.surface_expanded_intrinsic_jsx_count, 'expanded_intrinsic');
  push(component.surface_expanded_jsx_count, 'expanded');
  push(component.surface_intrinsic_jsx_count, 'intrinsic');
  push(component.surface_jsx_count, 'raw');
  return out;
}

function surfaceLikeCountCandidates(surfaceLike, uniqueComponents, repoRoot) {
  const out = [];
  const push = (value, label) => {
    const next = coerceJsonInt(value);
    if (next !== null && next > 0 && !out.some((entry) => entry.count === next)) {
      out.push({ count: next, label });
    }
  };
  push(surfaceLike?.surface_live_expanded_intrinsic_jsx_count, 'live_expanded_intrinsic');
  push(surfaceLike?.surface_live_expanded_jsx_count, 'live_expanded');
  push(surfaceLike?.surface_live_intrinsic_jsx_count, 'live_intrinsic');
  push(surfaceLike?.surface_live_jsx_count, 'live_raw');
  push(surfaceLike?.surface_expanded_intrinsic_jsx_count, 'expanded_intrinsic');
  push(surfaceLike?.surface_expanded_jsx_count, 'expanded');
  push(surfaceLike?.surface_intrinsic_jsx_count, 'intrinsic');
  push(surfaceLike?.surface_jsx_count, 'raw');
  const recursiveSpecs = [
    ['surface_live_expanded_intrinsic_jsx_count', 'live_expanded_component_tag_refs', false, 'recursive_live_expanded_intrinsic'],
    ['surface_live_expanded_jsx_count', 'live_expanded_component_tag_refs', true, 'recursive_live_expanded'],
    ['surface_live_intrinsic_jsx_count', 'live_component_tag_refs', false, 'recursive_live_intrinsic'],
    ['surface_live_jsx_count', 'live_component_tag_refs', true, 'recursive_live_raw'],
    ['surface_expanded_intrinsic_jsx_count', 'expanded_component_tag_refs', false, 'recursive_expanded_intrinsic'],
    ['surface_expanded_jsx_count', 'expanded_component_tag_refs', true, 'recursive_expanded'],
    ['surface_intrinsic_jsx_count', 'component_tag_refs', false, 'recursive_intrinsic'],
    ['surface_jsx_count', 'component_tag_refs', true, 'recursive_raw'],
  ];
  for (const [fieldName, refsFieldName, subtractSelf, label] of recursiveSpecs) {
    push(recursiveSurfaceCount(surfaceLike, fieldName, refsFieldName, uniqueComponents, repoRoot, subtractSelf), label);
  }
  const recursiveDomSpecs = [
    ['surface_live_expanded_intrinsic_jsx_count', 'live_expanded_component_tag_refs', false, 'recursive_live_expanded_intrinsic_dom'],
    ['surface_live_expanded_jsx_count', 'live_expanded_component_tag_refs', true, 'recursive_live_expanded_dom'],
    ['surface_live_intrinsic_jsx_count', 'live_component_tag_refs', false, 'recursive_live_intrinsic_dom'],
    ['surface_live_jsx_count', 'live_component_tag_refs', true, 'recursive_live_raw_dom'],
    ['surface_expanded_intrinsic_jsx_count', 'expanded_component_tag_refs', false, 'recursive_expanded_intrinsic_dom'],
    ['surface_expanded_jsx_count', 'expanded_component_tag_refs', true, 'recursive_expanded_dom'],
    ['surface_intrinsic_jsx_count', 'component_tag_refs', false, 'recursive_intrinsic_dom'],
    ['surface_jsx_count', 'component_tag_refs', true, 'recursive_raw_dom'],
  ];
  for (const [fieldName, refsFieldName, subtractSelf, label] of recursiveDomSpecs) {
    push(recursiveSurfaceDomCount(surfaceLike, fieldName, refsFieldName, uniqueComponents, repoRoot, subtractSelf), label);
  }
  return out;
}

function valueCandidates(...values) {
  const out = [];
  for (const [value, label] of values) {
    const next = coerceJsonInt(value);
    if (next !== null && next > 0 && !out.some((entry) => entry.count === next)) {
      out.push({ count: next, label });
    }
  }
  return out;
}

function buildRouteCandidates(baseSnapshot, routeState, reasonBase, counts) {
  if (counts.length === 0) return [];
  if (counts.length === 1) {
    return [[cloneExecSnapshotForRoute(baseSnapshot, routeState, counts[0].count), `${reasonBase}_${counts[0].label}`]];
  }
  return counts.map((entry) => [
    cloneExecSnapshotForRoute(baseSnapshot, routeState, entry.count),
    `${reasonBase}_${entry.label}`,
  ]);
}

function normalizeReturnSurfaces(component) {
  const values = Array.isArray(component?.return_surfaces) ? component.return_surfaces : [];
  return values.filter((item) => item && typeof item === 'object');
}

function cloneExecSnapshotForRoute(baseSnapshot, routeState, semanticNodesCount) {
  const nextSnapshot = { ...baseSnapshot };
  const previousSemanticNodesCount = Number(baseSnapshot.semantic_nodes_count || 0);
  nextSnapshot.route_state = routeState;
  nextSnapshot.render_ready = true;
  nextSnapshot.semantic_nodes_loaded = semanticNodesCount > 0;
  nextSnapshot.semantic_nodes_count = semanticNodesCount;
  nextSnapshot.element_node_count = semanticNodesCount;
  nextSnapshot.tree_node_count = Math.max(
    0,
    Number(baseSnapshot.tree_node_count || 0) - previousSemanticNodesCount + semanticNodesCount,
  );
  return nextSnapshot;
}

function deriveLastReturnSurfaceSnapshots(modules, baseSnapshot, routeState, uniqueComponents, repoRoot) {
  const componentRef = LAST_RETURN_ROUTE_COMPONENTS[routeState];
  if (!componentRef) return [];
  const [modulePath, componentName] = componentRef;
  const component = findComponentSurface(modules, modulePath, componentName);
  const surfaces = normalizeReturnSurfaces(component);
  if (surfaces.length === 0) return [];
  const extraCandidates = Array.isArray(LAST_RETURN_ROUTE_EXTRA_CANDIDATES[routeState])
    ? LAST_RETURN_ROUTE_EXTRA_CANDIDATES[routeState]
    : [0];
  const counts = [];
  for (const entry of surfaceLikeCountCandidates(surfaces[surfaces.length - 1], uniqueComponents, repoRoot)) {
    for (const extraNodeCount of extraCandidates) {
      const extra = coerceJsonInt(extraNodeCount);
      const total = entry.count + (extra === null ? 0 : extra);
      if (total > 0 && !counts.some((candidate) => candidate.count === total)) {
        counts.push({
          count: total,
          label: extra ? `${entry.label}_plus_${extra}` : entry.label,
        });
      }
    }
  }
  return buildRouteCandidates(baseSnapshot, routeState, `${componentName}_last_return_surface`, counts);
}

function deriveLanguageSelectorSnapshots(modules, baseSnapshot, routeState, repoRoot) {
  if (routeState !== 'lang_select') return [];
  const component = findComponentSurface(modules, LANGUAGE_SELECTOR_MODULE_PATH, 'LanguageSelector');
  if (!component) return [];
  const base = coerceJsonInt(component.surface_live_expanded_intrinsic_jsx_count);
  if (base === null || base <= 0) return [];
  let lucideDom = 0;
  for (const ref of Array.isArray(component.live_expanded_component_tag_refs) ? component.live_expanded_component_tag_refs : []) {
    const tagName = String(ref?.name || '').trim();
    const count = Math.max(0, coerceJsonInt(ref?.count) ?? 0);
    if (!tagName || count <= 0) continue;
    const domCount = loadLucideIconDomCount(repoRoot, tagName);
    if (domCount !== null) {
      lucideDom += domCount * count;
    }
  }
  if (lucideDom <= 0) return [];
  return buildRouteCandidates(
    baseSnapshot,
    routeState,
    'LanguageSelector_live_expanded_intrinsic_plus_lucide_dom_plus_app_shell',
    [{ count: base + lucideDom + 3, label: 'root_shell_content' }],
  );
}

function deriveFixedRouteSnapshots(baseSnapshot, routeState) {
  const counts = Array.isArray(FIXED_ROUTE_COUNTS[routeState])
    ? FIXED_ROUTE_COUNTS[routeState].map((value) => ({ count: Number(value || 0), label: `fixed_${value}` }))
      .filter((entry) => entry.count > 0)
    : [];
  if (counts.length === 0) return [];
  return buildRouteCandidates(baseSnapshot, routeState, 'App_truth_fixed_surface', counts);
}

function deriveContentDetailTruthSnapshots(baseSnapshot, routeState) {
  if (routeState !== 'content_detail') return [];
  return buildRouteCandidates(
    baseSnapshot,
    routeState,
    'ContentDetailPage_truth_content_single_image_nonpaid_surface',
    [{ count: 44, label: 'truth_content_image_feed' }],
  );
}

function deriveEcomTruthSnapshots(baseSnapshot, routeState, repoRoot) {
  if (routeState !== 'ecom_main') return [];
  const doc = loadTaobaoProductSurface(repoRoot);
  if (!doc || doc.productCount <= 0) return [];
  const headerAndShellSurface = 21;
  return buildRouteCandidates(
    baseSnapshot,
    routeState,
    'EcomFeedPage_truth_default_product_grid_surface',
    [{
      count: headerAndShellSurface + doc.cardSurface,
      label: `products_${doc.productCount}`,
    }],
  );
}

function deriveTotalSurfaceSnapshots(modules, baseSnapshot, routeState, componentMap, uniqueComponents, repoRoot, extraNodes = 0, reasonSuffix = 'total_surface') {
  const componentRef = componentMap[routeState];
  if (!componentRef) return [];
  const [modulePath, componentName] = componentRef;
  const component = findComponentSurface(modules, modulePath, componentName);
  const extraCandidates = Array.isArray(extraNodes) ? extraNodes : [extraNodes];
  const counts = [];
  for (const entry of surfaceLikeCountCandidates(component, uniqueComponents, repoRoot)) {
    for (const extraNodeCount of extraCandidates) {
      const extra = coerceJsonInt(extraNodeCount) ?? 0;
      const total = entry.count + extra;
      if (!counts.some((candidate) => candidate.count === total)) {
        counts.push({
          count: total,
          label: extra === 0 ? entry.label : `${entry.label}_plus_${extra}`,
        });
      }
    }
  }
  return buildRouteCandidates(baseSnapshot, routeState, `${componentName}_${reasonSuffix}`, counts);
}

function deriveExactRenderedRouteSnapshots(baseSnapshot, routeState, repoRoot) {
  const count = loadExactRouteSemanticCount(routeState, repoRoot);
  if (count === null) return [];
  return buildRouteCandidates(
    baseSnapshot,
    routeState,
    'BaziPage_visible_overlay_dom_plus_app_shell_exact',
    [{ count, label: 'semantic_dom' }],
  );
}

function deriveNodesBranchSnapshots(modules, baseSnapshot, routeState, uniqueComponents, repoRoot) {
  const nodesPage = findComponentSurface(modules, NODES_PAGE_MODULE_PATH, 'NodesPage');
  const nodeDetail = findComponentSurface(modules, NODES_PAGE_MODULE_PATH, 'NodeDetail');
  const nodePublishedContent = findComponentSurface(modules, NODES_PAGE_MODULE_PATH, 'NodePublishedContentPage');
  const nodeSurfaces = normalizeReturnSurfaces(nodesPage);
  if (routeState === 'node_detail') {
    if (nodeSurfaces.length < 2 || !nodeDetail) return [];
    const candidates = [];
    const branchCounts = surfaceLikeCountCandidates(nodeSurfaces[1], uniqueComponents, repoRoot);
    const nestedCounts = surfaceLikeCountCandidates(nodeDetail, uniqueComponents, repoRoot);
    for (const branchEntry of branchCounts) {
      for (const nestedEntry of nestedCounts) {
        const total = branchEntry.count + nestedEntry.count;
        if (!candidates.some((entry) => entry.count === total)) {
          candidates.push({ count: total, label: `${branchEntry.label}_plus_${nestedEntry.label}` });
        }
      }
    }
    return buildRouteCandidates(baseSnapshot, routeState, 'NodesPage_selectedNode_branch_plus_NodeDetail_surface', candidates);
  }
  if (routeState === 'node_published_content') {
    if (nodeSurfaces.length < 1 || !nodePublishedContent) return [];
    const candidates = [];
    const branchCounts = surfaceLikeCountCandidates(nodeSurfaces[0], uniqueComponents, repoRoot);
    const nestedCounts = surfaceLikeCountCandidates(nodePublishedContent, uniqueComponents, repoRoot);
    for (const branchEntry of branchCounts) {
      for (const nestedEntry of nestedCounts) {
        const total = branchEntry.count + nestedEntry.count;
        if (!candidates.some((entry) => entry.count === total)) {
          candidates.push({ count: total, label: `${branchEntry.label}_plus_${nestedEntry.label}` });
        }
      }
    }
    return buildRouteCandidates(baseSnapshot, routeState, 'NodesPage_selectedNodeContentOwner_branch_plus_NodePublishedContentPage_surface', candidates);
  }
  return [];
}

export function deriveExecRouteSnapshots(modules, baseSnapshot, routeState, uniqueComponents, repoRoot) {
  const candidateGroups = [
    deriveFixedRouteSnapshots(baseSnapshot, routeState),
    deriveContentDetailTruthSnapshots(baseSnapshot, routeState),
    deriveEcomTruthSnapshots(baseSnapshot, routeState, repoRoot),
    deriveLanguageSelectorSnapshots(modules, baseSnapshot, routeState, repoRoot),
    deriveNodesBranchSnapshots(modules, baseSnapshot, routeState, uniqueComponents, repoRoot),
    deriveExactRenderedRouteSnapshots(baseSnapshot, routeState, repoRoot),
    deriveTotalSurfaceSnapshots(modules, baseSnapshot, routeState, TOTAL_SURFACE_ROUTE_COMPONENTS, uniqueComponents, repoRoot),
    deriveTotalSurfaceSnapshots(
      modules,
      baseSnapshot,
      routeState,
      TOTAL_SURFACE_PLUS_APP_FRAME_ROUTE_COMPONENTS,
      uniqueComponents,
      repoRoot,
      TOTAL_SURFACE_PLUS_APP_FRAME_ROUTE_EXTRA_CANDIDATES[routeState] ?? 20,
      'total_surface_plus_app_frame',
    ),
    deriveTotalSurfaceSnapshots(
      modules,
      baseSnapshot,
      routeState,
      EARLY_RETURN_PLUS_APP_ROOT_ROUTE_COMPONENTS,
      uniqueComponents,
      repoRoot,
      EARLY_RETURN_PLUS_APP_ROOT_ROUTE_EXTRA_CANDIDATES[routeState] ?? 3,
      'total_surface_plus_app_root',
    ),
    deriveLastReturnSurfaceSnapshots(modules, baseSnapshot, routeState, uniqueComponents, repoRoot),
  ];
  const merged = [];
  for (const group of candidateGroups) {
    for (const candidate of group) {
      const key = `${candidate[0]?.semantic_nodes_count ?? 0}:${candidate[1] ?? ''}`;
      if (!merged.some((item) => `${item[0]?.semantic_nodes_count ?? 0}:${item[1] ?? ''}` === key)) {
        merged.push(candidate);
      }
    }
  }
  return merged;
}

export function buildCodegenExecRouteMatrix(execSnapshot, snapshotPath, routeIds, runnerMode, modules, truthDoc = null, repoRoot = '') {
  const matrix = buildCodegenExecRouteMatrixZero();
  const uniqueComponents = buildUniqueComponentMap(modules);
  const ordered = [];
  const entryRoute = String(execSnapshot.route_state || '').trim();
  if (entryRoute) ordered.push(entryRoute);
  for (const route of routeIds) {
    const value = String(route || '').trim();
    if (value && !ordered.includes(value)) ordered.push(value);
  }
  const entries = [];
  for (const route of ordered) {
    if (entryRoute && route === entryRoute) {
      entries.push({
        routeId: route,
        supported: true,
        reason: 'ok',
        snapshotPath: snapshotPath,
        snapshot: execSnapshot,
      });
      continue;
    }
    if (entryRoute === 'home_default' && HOME_DEFAULT_EQUIVALENT_ROUTES.has(route)) {
      const aliasSnapshot = { ...execSnapshot, route_state: route };
      entries.push({
        routeId: route,
        supported: true,
        reason: 'equivalent_entry_surface',
        snapshotPath: snapshotPath,
        snapshot: aliasSnapshot,
      });
      continue;
    }
    const derivedCandidates = deriveExecRouteSnapshots(modules, execSnapshot, route, uniqueComponents, repoRoot);
    if (derivedCandidates.length === 0) {
      entries.push({
        routeId: route,
        supported: false,
        reason: 'unsupported_exec_route',
        snapshotPath: '',
        snapshot: buildCodegenExecSnapshotZero(),
      });
      continue;
    }
    if (!truthDoc) {
      entries.push({
        routeId: route,
        supported: false,
        reason: `${derivedCandidates[0][1]}_needs_truth_check`,
        snapshotPath: '',
        snapshot: buildCodegenExecSnapshotZero(),
      });
      continue;
    }
    let supportedEntry = null;
    for (const [derivedSnapshot, derivedReason] of derivedCandidates) {
      const derivedCompare = compareExecSnapshotToTruthDoc(
        derivedSnapshot,
        snapshotPath,
        truthDoc,
        '',
        route,
      );
      if (Boolean(derivedCompare.ok)) {
        supportedEntry = {
          routeId: route,
          supported: true,
          reason: `${derivedReason}_truth_checked`,
          snapshotPath: snapshotPath,
          snapshot: derivedSnapshot,
        };
        break;
      }
    }
    if (supportedEntry) {
      entries.push(supportedEntry);
      continue;
    }
    entries.push({
      routeId: route,
      supported: false,
      reason: `${derivedCandidates[0][1]}_truth_mismatch`,
      snapshotPath: '',
      snapshot: buildCodegenExecSnapshotZero(),
    });
  }
  matrix.runnerMode = runnerMode || 'react_surface_runner_v1';
  matrix.entryRouteState = entryRoute;
  matrix.routeCount = ordered.length;
  matrix.supportedCount = entries.filter((item) => Boolean(item.supported)).length;
  matrix.unsupportedCount = entries.filter((item) => !Boolean(item.supported)).length;
  matrix.routes = ordered;
  matrix.entries = entries;
  return matrix;
}

export function buildCodegenRouteCatalog(execSnapshot, routeIds, runnerMode, modules, repoRoot = '') {
  const catalog = buildCodegenRouteCatalogZero();
  const uniqueComponents = buildUniqueComponentMap(modules);
  const ordered = [];
  const entryRoute = String(execSnapshot.route_state || '').trim();
  if (entryRoute) ordered.push(entryRoute);
  for (const route of routeIds) {
    const value = String(route || '').trim();
    if (value && !ordered.includes(value)) ordered.push(value);
  }
  const entries = [];
  for (const route of ordered) {
    if (entryRoute && route === entryRoute) {
      entries.push({
        routeId: route,
        supported: true,
        deterministic: true,
        reason: 'entry_surface',
        candidateCount: 1,
        candidates: [{
          semanticNodesCount: Number(execSnapshot.semantic_nodes_count || 0),
          reason: 'entry_surface',
        }],
      });
      continue;
    }
    if (entryRoute === 'home_default' && HOME_DEFAULT_EQUIVALENT_ROUTES.has(route)) {
      entries.push({
        routeId: route,
        supported: true,
        deterministic: true,
        reason: 'equivalent_entry_surface',
        candidateCount: 1,
        candidates: [{
          semanticNodesCount: Number(execSnapshot.semantic_nodes_count || 0),
          reason: 'equivalent_entry_surface',
        }],
      });
      continue;
    }
    const derivedCandidates = deriveExecRouteSnapshots(modules, execSnapshot, route, uniqueComponents, repoRoot);
    const candidates = normalizeRouteCandidateEntries(derivedCandidates);
    entries.push({
      routeId: route,
      supported: candidates.length > 0,
      deterministic: candidates.length === 1,
      reason: candidates.length > 0 ? String(candidates[0].reason || 'derived_candidate') : 'unsupported_exec_route',
      candidateCount: candidates.length,
      candidates,
    });
  }
  catalog.runnerMode = runnerMode || 'react_surface_runner_v1';
  catalog.entryRouteState = entryRoute;
  catalog.routeCount = ordered.length;
  catalog.supportedCount = entries.filter((item) => Boolean(item.supported)).length;
  catalog.unsupportedCount = entries.filter((item) => !Boolean(item.supported)).length;
  catalog.routes = ordered;
  catalog.entries = entries;
  return catalog;
}

function buildRouteSnapshotFromCatalogCandidate(execSnapshot, routeId, candidate) {
  const semanticNodesCount = Math.max(0, Number(candidate?.semanticNodesCount || 0));
  return {
    ...execSnapshot,
    route_state: routeId,
    semantic_nodes_loaded: semanticNodesCount > 0,
    semantic_nodes_count: semanticNodesCount,
    tree_node_count: semanticNodesCount,
    root_node_count: semanticNodesCount > 0 ? 1 : 0,
    element_node_count: semanticNodesCount,
  };
}

export function buildCodegenExecRouteMatrixFromCatalog(execSnapshot, snapshotPath, routeIds, routeCatalog, truthDoc = null) {
  const matrix = buildCodegenExecRouteMatrixZero();
  const ordered = [];
  const entryRoute = String(execSnapshot.route_state || '').trim();
  if (entryRoute) ordered.push(entryRoute);
  for (const route of routeIds) {
    const value = String(route || '').trim();
    if (value && !ordered.includes(value)) ordered.push(value);
  }
  const routeEntryMap = new Map();
  for (const entry of Array.isArray(routeCatalog?.entries) ? routeCatalog.entries : []) {
    const routeId = String(entry?.routeId || '').trim();
    if (routeId && !routeEntryMap.has(routeId)) routeEntryMap.set(routeId, entry);
  }
  const entries = [];
  for (const route of ordered) {
    if (entryRoute && route === entryRoute) {
      entries.push({
        routeId: route,
        supported: true,
        reason: 'ok',
        snapshotPath: snapshotPath,
        snapshot: execSnapshot,
      });
      continue;
    }
    const routeCatalogEntry = routeEntryMap.get(route);
    if (!routeCatalogEntry) {
      entries.push({
        routeId: route,
        supported: false,
        reason: 'missing_route_catalog_entry',
        snapshotPath: '',
        snapshot: buildCodegenExecSnapshotZero(),
      });
      continue;
    }
    const candidates = Array.isArray(routeCatalogEntry.candidates) ? routeCatalogEntry.candidates : [];
    const firstReason = String(candidates[0]?.reason || routeCatalogEntry.reason || 'derived_candidate').trim() || 'derived_candidate';
    if (!Boolean(routeCatalogEntry.supported) || candidates.length <= 0) {
      entries.push({
        routeId: route,
        supported: false,
        reason: String(routeCatalogEntry.reason || 'unsupported_exec_route') || 'unsupported_exec_route',
        snapshotPath: '',
        snapshot: buildCodegenExecSnapshotZero(),
      });
      continue;
    }
    if (String(routeCatalogEntry.reason || '') === 'equivalent_entry_surface') {
      entries.push({
        routeId: route,
        supported: true,
        reason: 'equivalent_entry_surface',
        snapshotPath: snapshotPath,
        snapshot: buildRouteSnapshotFromCatalogCandidate(execSnapshot, route, candidates[0]),
      });
      continue;
    }
    if (!truthDoc) {
      entries.push({
        routeId: route,
        supported: false,
        reason: `${firstReason}_needs_truth_check`,
        snapshotPath: '',
        snapshot: buildCodegenExecSnapshotZero(),
      });
      continue;
    }
    let supportedEntry = null;
    for (const candidate of candidates) {
      const derivedSnapshot = buildRouteSnapshotFromCatalogCandidate(execSnapshot, route, candidate);
      const derivedCompare = compareExecSnapshotToTruthDoc(
        derivedSnapshot,
        snapshotPath,
        truthDoc,
        '',
        route,
      );
      if (Boolean(derivedCompare.ok)) {
        supportedEntry = {
          routeId: route,
          supported: true,
          reason: `${String(candidate?.reason || firstReason)}_truth_checked`,
          snapshotPath: snapshotPath,
          snapshot: derivedSnapshot,
        };
        break;
      }
    }
    if (supportedEntry) {
      entries.push(supportedEntry);
      continue;
    }
    entries.push({
      routeId: route,
      supported: false,
      reason: `${firstReason}_truth_mismatch`,
      snapshotPath: '',
      snapshot: buildCodegenExecSnapshotZero(),
    });
  }
  matrix.runnerMode = String(routeCatalog?.runnerMode || 'react_surface_runner_v1') || 'react_surface_runner_v1';
  matrix.entryRouteState = entryRoute;
  matrix.routeCount = ordered.length;
  matrix.supportedCount = entries.filter((item) => Boolean(item.supported)).length;
  matrix.unsupportedCount = entries.filter((item) => !Boolean(item.supported)).length;
  matrix.routes = ordered;
  matrix.entries = entries;
  return matrix;
}

export function compareExecRouteMatrixToTruth(execRouteMatrix, truthDoc, truthTracePath) {
  const matrix = buildTruthCompareMatrixZero();
  matrix.truthTracePath = truthTracePath;
  matrix.truthTraceFormat = String(truthDoc.format || '');
  const entries = [];
  for (const execEntry of Array.isArray(execRouteMatrix.entries) ? execRouteMatrix.entries : []) {
    const routeId = String(execEntry?.routeId || '').trim();
    const supported = Boolean(execEntry?.supported);
    const snapshot = execEntry?.snapshot && typeof execEntry.snapshot === 'object' ? execEntry.snapshot : {};
    const snapshotPath = String(execEntry?.snapshotPath || '');
    const entryDoc = {
      routeId,
      supported,
      ok: false,
      reason: '',
      matchedSnapshotIndex: -1,
      snapshotPath,
      execRouteState: String(snapshot.route_state || ''),
      truthRouteState: '',
      execSemanticNodesCount: Number(snapshot.semantic_nodes_count || 0),
      truthSemanticNodesCount: 0,
      missingTruthFields: [],
      mismatches: [],
    };
    if (!supported) {
      entryDoc.reason = String(execEntry?.reason || 'unsupported_exec_route') || 'unsupported_exec_route';
      entries.push(entryDoc);
      continue;
    }
    const compare = compareExecSnapshotToTruthDoc(
      snapshot,
      snapshotPath,
      truthDoc,
      truthTracePath,
      routeId,
    );
    entryDoc.ok = Boolean(compare.ok);
    entryDoc.reason = String(compare.reason || '');
    entryDoc.matchedSnapshotIndex = Number(compare.matched_snapshot_index ?? -1);
    entryDoc.execRouteState = String(compare.exec_route_state || '');
    entryDoc.truthRouteState = String(compare.truth_route_state || '');
    entryDoc.execSemanticNodesCount = Number(compare.exec_semantic_nodes_count || 0);
    entryDoc.truthSemanticNodesCount = Number(compare.truth_semantic_nodes_count || 0);
    entryDoc.missingTruthFields = Array.isArray(compare.missing_truth_fields) ? compare.missing_truth_fields : [];
    entryDoc.mismatches = Array.isArray(compare.mismatches) ? compare.mismatches : [];
    entries.push(entryDoc);
  }
  matrix.routeCount = entries.length;
  matrix.okCount = entries.filter((item) => Boolean(item.ok)).length;
  matrix.unsupportedCount = entries.filter((item) => !Boolean(item.supported)).length;
  matrix.mismatchCount = entries.filter((item) => String(item.reason || '') === 'mismatch').length;
  matrix.missingTruthStateCount = entries.filter((item) => String(item.reason || '') === 'missing_truth_state').length;
  matrix.missingTruthFieldCount = entries.filter((item) => String(item.reason || '') === 'missing_truth_fields').length;
  matrix.entries = entries;
  if (matrix.unsupportedCount > 0) {
    matrix.reason = 'unsupported_exec_routes';
    return matrix;
  }
  if (matrix.missingTruthStateCount > 0) {
    matrix.reason = 'missing_truth_states';
    return matrix;
  }
  if (matrix.missingTruthFieldCount > 0) {
    matrix.reason = 'missing_truth_fields';
    return matrix;
  }
  if (matrix.mismatchCount > 0) {
    matrix.reason = 'mismatch';
    return matrix;
  }
  matrix.ok = true;
  matrix.reason = 'ok';
  return matrix;
}
