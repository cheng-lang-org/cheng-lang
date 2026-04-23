#!/usr/bin/env node
import crypto from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { spawnSync } from 'node:child_process';

function parseArgs(argv) {
  const out = {
    repo: '',
    route: '',
    outDir: '',
    baseUrl: 'http://127.0.0.1:4173/',
    toolingBin: '',
    autoCloseMs: '1200',
    resize: '',
    waitMs: '30000',
    browserWidth: '',
    browserHeight: '',
    summaryOut: '',
    outPath: '',
    nativeSummary: '',
    truthSummary: '',
    nativeScreenshot: '',
    truthScreenshot: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--route') out.route = String(argv[++i] || '');
    else if (arg === '--route-state') out.route = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--base-url') out.baseUrl = String(argv[++i] || out.baseUrl);
    else if (arg === '--tooling-bin') out.toolingBin = String(argv[++i] || '');
    else if (arg === '--auto-close-ms') out.autoCloseMs = String(argv[++i] || out.autoCloseMs);
    else if (arg === '--resize') out.resize = String(argv[++i] || '');
    else if (arg === '--wait-ms') out.waitMs = String(argv[++i] || out.waitMs);
    else if (arg === '--browser-width') out.browserWidth = String(argv[++i] || '');
    else if (arg === '--browser-height') out.browserHeight = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--native-summary') out.nativeSummary = String(argv[++i] || '');
    else if (arg === '--truth-summary') out.truthSummary = String(argv[++i] || '');
    else if (arg === '--native-screenshot') out.nativeScreenshot = String(argv[++i] || '');
    else if (arg === '--truth-screenshot') out.truthScreenshot = String(argv[++i] || '');
    else if (arg === '--help' || arg === '-h') {
      console.log(
        'Usage: r2c-react-render-compare.mjs --repo <path> --route <state> ' +
        '[--out-dir <dir>] [--base-url <url>] [--tooling-bin <cheng.stage3>] ' +
        '[--auto-close-ms <n>] [--resize WxH] [--wait-ms <n>] ' +
        '[--browser-width <n>] [--browser-height <n>] [--summary-out <file>] [--out <file>] ' +
        '[--native-summary <file> --truth-summary <file> --native-screenshot <file> --truth-screenshot <file>]'
      );
      process.exit(0);
    }
  }
  if (!out.repo) throw new Error('missing --repo');
  if (!out.route) throw new Error('missing --route');
  return out;
}

function writeText(filePath, text) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, text, 'utf8');
}

function writeJson(filePath, value) {
  writeText(filePath, `${JSON.stringify(value, null, 2)}\n`);
}

function readJson(filePath) {
  return JSON.parse(fs.readFileSync(filePath, 'utf8'));
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

function exitCodeOf(result) {
  if (typeof result.status === 'number') return result.status;
  if (typeof result.signal === 'string' && result.signal.length > 0) return -1;
  if (result.error) return -1;
  return -1;
}

function run(bin, argv, options = {}) {
  return spawnSync(bin, argv, {
    encoding: 'utf8',
    timeout: options.timeout ?? 900000,
    maxBuffer: 64 * 1024 * 1024,
    env: options.env || process.env,
    cwd: options.cwd || process.cwd(),
  });
}

function resolveWorkspaceRoot() {
  const scriptPath = path.resolve(process.argv[1]);
  const scriptName = path.basename(scriptPath);
  let current = path.dirname(scriptPath);
  while (true) {
    const mirroredScriptPath = path.join(current, 'v3', 'experimental', 'r2c-react', scriptName);
    if (fs.existsSync(path.join(current, 'v3', 'src')) &&
        fs.existsSync(path.join(current, 'src', 'runtime')) &&
        fs.existsSync(mirroredScriptPath)) {
      return current;
    }
    const parent = path.dirname(current);
    if (parent === current) break;
    current = parent;
  }
  throw new Error(`failed to resolve workspace root from ${scriptPath}`);
}

function resolveStage3(workspaceRoot, explicit) {
  const configured = String(explicit || process.env.R2C_REACT_V3_TOOLING_BIN || '').trim();
  if (configured) return path.resolve(configured);
  const stage3 = path.join(workspaceRoot, 'artifacts', 'v3_bootstrap', 'cheng.stage3');
  if (fs.existsSync(stage3)) return stage3;
  throw new Error(`missing cheng.stage3: ${stage3}`);
}

function parsePositiveInt(text, fallback) {
  const raw = String(text || '').trim();
  if (!raw) return fallback;
  const value = Number.parseInt(raw, 10);
  return Number.isFinite(value) && value > 0 ? value : fallback;
}

function uniqueStrings(values) {
  const out = [];
  const seen = new Set();
  for (const raw of values || []) {
    const value = String(raw || '').trim();
    if (!value || seen.has(value)) continue;
    seen.add(value);
    out.push(value);
  }
  return out;
}

function uniqueObjects(values, keyFn) {
  const out = [];
  const seen = new Set();
  for (const value of values || []) {
    const key = String(keyFn(value) || '').trim();
    if (!key || seen.has(key)) continue;
    seen.add(key);
    out.push(value);
  }
  return out;
}

function assetKindFromPath(filePath) {
  const ext = path.extname(String(filePath || '')).toLowerCase();
  if (['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg', '.ico'].includes(ext)) return 'image';
  if (['.woff', '.woff2', '.ttf', '.otf'].includes(ext)) return 'font';
  if (['.wasm'].includes(ext)) return 'wasm';
  if (['.mp3', '.wav', '.ogg'].includes(ext)) return 'audio';
  if (['.mp4', '.webm'].includes(ext)) return 'video';
  if (['.css'].includes(ext)) return 'style';
  if (['.json'].includes(ext)) return 'json';
  return 'other';
}

function collectFeatureHits(tsxAstDoc) {
  const hits = new Set();
  for (const moduleDoc of Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules : []) {
    const features = moduleDoc && typeof moduleDoc.features === 'object' && moduleDoc.features
      ? moduleDoc.features
      : {};
    for (const [name, enabled] of Object.entries(features)) {
      if (enabled) hits.add(String(name));
    }
  }
  return [...hits].sort();
}

function buildSyntheticHostContractDoc(tsxAstDoc, repo, route) {
  const featureHits = collectFeatureHits(tsxAstDoc);
  const methods = [];
  if (featureHits.includes('local_storage')) {
    methods.push(
      { group: 'StorageHost', name: 'getItem', required: true },
      { group: 'StorageHost', name: 'setItem', required: true },
      { group: 'StorageHost', name: 'removeItem', required: true },
      { group: 'StorageHost', name: 'clear', required: true },
    );
  }
  if (featureHits.includes('fetch')) {
    methods.push({ group: 'NetHost', name: 'fetch', required: true });
  }
  if (featureHits.includes('custom_event')) {
    methods.push({ group: 'PlatformHost', name: 'emitCustomEvent', required: true });
  }
  if (featureHits.includes('resize_observer')) {
    methods.push({ group: 'WindowHost', name: 'observeResize', required: true });
  }
  if (featureHits.includes('mutation_observer')) {
    methods.push({ group: 'WindowHost', name: 'observeMutation', required: true });
  }
  if (featureHits.includes('rtc')) {
    methods.push({ group: 'RtcHost', name: 'createPeerConnection', required: true });
  }
  if (featureHits.includes('media')) {
    methods.push({ group: 'MediaHost', name: 'getUserMedia', required: true });
  }
  if (featureHits.includes('broadcast_channel')) {
    methods.push({ group: 'PlatformHost', name: 'openBroadcastChannel', required: true });
  }
  if (featureHits.includes('worker')) {
    methods.push({ group: 'PlatformHost', name: 'spawnWorker', required: true });
  }
  if (featureHits.includes('webassembly')) {
    methods.push({ group: 'PlatformHost', name: 'instantiateWasm', required: true });
  }
  const uniqueMethods = uniqueObjects(methods, (item) => `${item.group}.${item.name}`);
  const requiredGroups = uniqueStrings(uniqueMethods.map((item) => item.group));
  return {
    format: 'unimaker_host_v1',
    repo_root: repo,
    route_state: route,
    required_groups: requiredGroups,
    methods: uniqueMethods,
    feature_hits: featureHits,
  };
}

function buildSyntheticAssetManifestDoc(tsxAstDoc, repo) {
  const entries = [];
  for (const moduleDoc of Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules : []) {
    const modulePath = String(moduleDoc?.path || '').trim();
    for (const assetRef of Array.isArray(moduleDoc?.asset_refs) ? moduleDoc.asset_refs : []) {
      const assetPath = String(assetRef || '').trim();
      if (!assetPath) continue;
      entries.push({
        path: assetPath,
        kind: assetKindFromPath(assetPath),
        source_module_path: modulePath,
      });
    }
  }
  return {
    format: 'asset_manifest_v1',
    repo_root: repo,
    entries: uniqueObjects(entries, (item) => `${item.source_module_path}:${item.path}`),
  };
}

function buildSyntheticTailwindManifestDoc(tsxAstDoc, repo) {
  const tokens = uniqueStrings(
    (Array.isArray(tsxAstDoc?.modules) ? tsxAstDoc.modules : [])
      .flatMap((moduleDoc) => Array.isArray(moduleDoc?.tailwind_class_tokens) ? moduleDoc.tailwind_class_tokens : [])
  ).map((token) => ({
    token,
  }));
  return {
    format: 'tailwind_style_manifest_v1',
    repo_root: repo,
    tokens,
  };
}

function readChunk(buffer, offset) {
  const length = buffer.readUInt32BE(offset);
  const type = buffer.slice(offset + 4, offset + 8).toString('ascii');
  const dataStart = offset + 8;
  const dataEnd = dataStart + length;
  const crcEnd = dataEnd + 4;
  return {
    length,
    type,
    data: buffer.slice(dataStart, dataEnd),
    nextOffset: crcEnd,
  };
}

function paethPredictor(left, up, upLeft) {
  const p = left + up - upLeft;
  const pa = Math.abs(p - left);
  const pb = Math.abs(p - up);
  const pc = Math.abs(p - upLeft);
  if (pa <= pb && pa <= pc) return left;
  if (pb <= pc) return up;
  return upLeft;
}

function decodePng(filePath) {
  const buffer = fs.readFileSync(filePath);
  const signature = '89504e470d0a1a0a';
  if (buffer.slice(0, 8).toString('hex') !== signature) {
    throw new Error(`png_signature_mismatch:${filePath}`);
  }
  let offset = 8;
  let width = 0;
  let height = 0;
  let bitDepth = 0;
  let colorType = 0;
  const idatChunks = [];
  while (offset < buffer.length) {
    const chunk = readChunk(buffer, offset);
    offset = chunk.nextOffset;
    if (chunk.type === 'IHDR') {
      width = chunk.data.readUInt32BE(0);
      height = chunk.data.readUInt32BE(4);
      bitDepth = chunk.data.readUInt8(8);
      colorType = chunk.data.readUInt8(9);
      const compression = chunk.data.readUInt8(10);
      const filter = chunk.data.readUInt8(11);
      const interlace = chunk.data.readUInt8(12);
      if (bitDepth !== 8) throw new Error(`png_bit_depth_unsupported:${bitDepth}`);
      if (compression !== 0 || filter !== 0 || interlace !== 0) {
        throw new Error(`png_format_unsupported:${compression}:${filter}:${interlace}`);
      }
    } else if (chunk.type === 'IDAT') {
      idatChunks.push(chunk.data);
    } else if (chunk.type === 'IEND') {
      break;
    }
  }
  let bytesPerPixel = 0;
  if (colorType === 0) bytesPerPixel = 1;
  else if (colorType === 2) bytesPerPixel = 3;
  else if (colorType === 4) bytesPerPixel = 2;
  else if (colorType === 6) bytesPerPixel = 4;
  else throw new Error(`png_color_type_unsupported:${colorType}`);
  const rowBytes = width * bytesPerPixel;
  const inflated = zlib.inflateSync(Buffer.concat(idatChunks));
  const pixels = new Uint8Array(width * height * 4);
  let inOffset = 0;
  const previous = new Uint8Array(rowBytes);
  const current = new Uint8Array(rowBytes);
  for (let y = 0; y < height; y += 1) {
    const filterType = inflated[inOffset];
    inOffset += 1;
    for (let x = 0; x < rowBytes; x += 1) {
      const raw = inflated[inOffset];
      inOffset += 1;
      const left = x >= bytesPerPixel ? current[x - bytesPerPixel] : 0;
      const up = previous[x];
      const upLeft = x >= bytesPerPixel ? previous[x - bytesPerPixel] : 0;
      let value = raw;
      if (filterType === 1) value = (raw + left) & 255;
      else if (filterType === 2) value = (raw + up) & 255;
      else if (filterType === 3) value = (raw + Math.floor((left + up) / 2)) & 255;
      else if (filterType === 4) value = (raw + paethPredictor(left, up, upLeft)) & 255;
      else if (filterType !== 0) throw new Error(`png_filter_unsupported:${filterType}`);
      current[x] = value;
    }
    for (let x = 0; x < width; x += 1) {
      const dst = (y * width + x) * 4;
      if (colorType === 0) {
        const gray = current[x];
        pixels[dst] = gray;
        pixels[dst + 1] = gray;
        pixels[dst + 2] = gray;
        pixels[dst + 3] = 255;
      } else if (colorType === 2) {
        const src = x * 3;
        pixels[dst] = current[src];
        pixels[dst + 1] = current[src + 1];
        pixels[dst + 2] = current[src + 2];
        pixels[dst + 3] = 255;
      } else if (colorType === 4) {
        const src = x * 2;
        const gray = current[src];
        pixels[dst] = gray;
        pixels[dst + 1] = gray;
        pixels[dst + 2] = gray;
        pixels[dst + 3] = current[src + 1];
      } else {
        const src = x * 4;
        pixels[dst] = current[src];
        pixels[dst + 1] = current[src + 1];
        pixels[dst + 2] = current[src + 2];
        pixels[dst + 3] = current[src + 3];
      }
    }
    previous.set(current);
  }
  return {
    path: filePath,
    width,
    height,
    pixels,
    file_size: buffer.length,
    file_sha256: crypto.createHash('sha256').update(buffer).digest('hex'),
    pixel_sha256: crypto.createHash('sha256').update(Buffer.from(pixels)).digest('hex'),
  };
}

function compareDecodedPng(left, right) {
  if (left.width !== right.width || left.height !== right.height) {
    return {
      ok: false,
      reason: 'dimension_mismatch',
      width: left.width,
      height: left.height,
      other_width: right.width,
      other_height: right.height,
      pixel_count: 0,
      diff_pixel_count: 0,
      diff_ratio: 1,
      max_channel_delta: 255,
      total_abs_delta: 0,
      first_diff: null,
    };
  }
  const pixelCount = left.width * left.height;
  let diffPixelCount = 0;
  let maxChannelDelta = 0;
  let totalAbsDelta = 0;
  let firstDiff = null;
  for (let index = 0; index < left.pixels.length; index += 4) {
    let different = false;
    for (let channel = 0; channel < 4; channel += 1) {
      const delta = Math.abs(left.pixels[index + channel] - right.pixels[index + channel]);
      totalAbsDelta += delta;
      if (delta > 0) different = true;
      if (delta > maxChannelDelta) maxChannelDelta = delta;
    }
    if (!different) continue;
    diffPixelCount += 1;
    if (!firstDiff) {
      const pixelIndex = index / 4;
      firstDiff = {
        x: pixelIndex % left.width,
        y: Math.floor(pixelIndex / left.width),
        left_rgba: Array.from(left.pixels.slice(index, index + 4)),
        right_rgba: Array.from(right.pixels.slice(index, index + 4)),
      };
    }
  }
  return {
    ok: diffPixelCount === 0,
    reason: diffPixelCount === 0 ? 'ok' : 'pixel_mismatch',
    width: left.width,
    height: left.height,
    other_width: right.width,
    other_height: right.height,
    pixel_count: pixelCount,
    diff_pixel_count: diffPixelCount,
    diff_ratio: pixelCount > 0 ? diffPixelCount / pixelCount : 0,
    max_channel_delta: maxChannelDelta,
    total_abs_delta: totalAbsDelta,
    first_diff: firstDiff,
  };
}

function requireFile(filePath, label) {
  if (!fs.existsSync(filePath)) {
    throw new Error(`missing ${label}: ${filePath}`);
  }
}

function parseIntField(value, fallback = 0) {
  const raw = String(value ?? '').trim();
  if (!raw) return fallback;
  const parsed = Number.parseInt(raw, 10);
  return Number.isFinite(parsed) ? parsed : fallback;
}

function boolFieldText(value) {
  return String(value ?? '').trim().toLowerCase() === 'true';
}

function directCompareEnabled(args) {
  return Boolean(
    String(args.nativeSummary || '').trim() ||
    String(args.truthSummary || '').trim() ||
    String(args.nativeScreenshot || '').trim() ||
    String(args.truthScreenshot || '').trim()
  );
}

function assertDirectCompareArgs(args) {
  const missing = [];
  if (!String(args.nativeSummary || '').trim()) missing.push('--native-summary');
  if (!String(args.truthSummary || '').trim()) missing.push('--truth-summary');
  if (!String(args.nativeScreenshot || '').trim()) missing.push('--native-screenshot');
  if (!String(args.truthScreenshot || '').trim()) missing.push('--truth-screenshot');
  if (missing.length > 0) {
    throw new Error(`direct_compare_missing:${missing.join(',')}`);
  }
}

function buildRenderCompareReport({
  directMode,
  route,
  repo,
  workspaceRoot,
  stage3,
  outDir,
  tsFrontendLogPath,
  codegenSurfaceLogPath,
  nativeBundleLogPath,
  nativeRunLogPath,
  truthRunLogPath,
  nativeSummaryPath,
  truthSummaryPath,
  nativeResolvedScreenshotPath,
  truthScreenshotPath,
  nativeWidth,
  nativeHeight,
  nativePng,
  truthPng,
  compare,
  nativeSummary,
  truthSummary,
}) {
  const nativeRuntimeBundleSemanticNodesCount = parseIntField(nativeSummary.native_gui_runtime_bundle_semantic_nodes_count, 0);
  const truthSemanticNodesCount = parseIntField(truthSummary.semantic_nodes_count, 0);
  return {
    format: 'render_compare_v1',
    ok: Boolean(compare.ok),
    reason: String(compare.reason || ''),
    direct_mode: Boolean(directMode),
    repo_root: repo,
    workspace_root: workspaceRoot,
    route_state: route,
    stage3_path: String(stage3 || ''),
    out_dir: outDir,
    tsx_frontend_log_path: tsFrontendLogPath,
    codegen_surface_log_path: codegenSurfaceLogPath,
    native_gui_bundle_log_path: nativeBundleLogPath,
    native_gui_run_log_path: nativeRunLogPath,
    truth_capture_log_path: truthRunLogPath,
    native_gui_summary_ready: true,
    native_gui_summary_path: nativeSummaryPath,
    truth_summary_path: truthSummaryPath,
    native_gui_route_state: String(nativeSummary.native_gui_route_state || ''),
    native_gui_screenshot_path: String(nativeSummary.native_gui_screenshot_path || nativeResolvedScreenshotPath),
    native_gui_runtime_manifest_ready: boolFieldText(nativeSummary.native_gui_runtime_manifest_ready),
    native_gui_runtime_contract_payload_ready: boolFieldText(nativeSummary.native_gui_runtime_contract_payload_ready),
    native_gui_runtime_bundle_payload_ready: boolFieldText(nativeSummary.native_gui_runtime_bundle_payload_ready),
    native_gui_runtime_bundle_ready: boolFieldText(nativeSummary.native_gui_runtime_bundle_ready),
    native_gui_runtime_contract_ready: boolFieldText(nativeSummary.native_gui_runtime_contract_ready),
    native_gui_runtime_bundle_route_state: String(nativeSummary.native_gui_runtime_bundle_route_state || ''),
    native_gui_runtime_bundle_route_count: parseIntField(nativeSummary.native_gui_runtime_bundle_route_count, 0),
    native_gui_runtime_bundle_supported_count: parseIntField(nativeSummary.native_gui_runtime_bundle_supported_count, 0),
    native_gui_runtime_bundle_semantic_nodes_count: nativeRuntimeBundleSemanticNodesCount,
    native_gui_runtime_bundle_layout_item_count: parseIntField(nativeSummary.native_gui_runtime_bundle_layout_item_count, 0),
    native_gui_runtime_bundle_render_command_count: parseIntField(nativeSummary.native_gui_runtime_bundle_render_command_count, 0),
    native_gui_runtime_contract_layout_item_count: parseIntField(nativeSummary.native_gui_runtime_contract_layout_item_count, 0),
    native_gui_runtime_contract_viewport_item_count: parseIntField(nativeSummary.native_gui_runtime_contract_viewport_item_count, 0),
    native_gui_runtime_contract_interactive_item_count: parseIntField(nativeSummary.native_gui_runtime_contract_interactive_item_count, 0),
    native_gui_native_layout_plan_item_count: parseIntField(nativeSummary.native_gui_native_layout_plan_item_count, 0),
    native_gui_render_plan_command_count: parseIntField(nativeSummary.native_gui_render_plan_command_count, 0),
    native_gui_storage_host_ready: boolFieldText(nativeSummary.native_gui_storage_host_ready),
    native_gui_fetch_host_ready: boolFieldText(nativeSummary.native_gui_fetch_host_ready),
    native_gui_custom_event_host_ready: boolFieldText(nativeSummary.native_gui_custom_event_host_ready),
    native_gui_resize_observer_host_ready: boolFieldText(nativeSummary.native_gui_resize_observer_host_ready),
    truth_trace_path: String(truthSummary.truth_trace_path || ''),
    truth_trace_format: String(truthSummary.truth_trace_format || ''),
    truth_runtime_state_path: String(truthSummary.runtime_state_path || ''),
    truth_dom_path: String(truthSummary.dom_path || ''),
    truth_semantic_nodes_path: String(truthSummary.semantic_nodes_path || ''),
    truth_runtime_meta_path: String(truthSummary.runtime_meta_path || ''),
    truth_screenshot_path: String(truthSummary.screenshot_path || truthScreenshotPath),
    truth_semantic_nodes_count: truthSemanticNodesCount,
    truth_document_nodes_count: parseIntField(truthSummary.document_nodes_count, 0),
    truth_dom_hash: String(truthSummary.dom_hash || ''),
    truth_screenshot_sha256: String(truthSummary.screenshot_sha256 || ''),
    truth_pathname: String(truthSummary.pathname || ''),
    truth_search: String(truthSummary.search || ''),
    truth_hash: String(truthSummary.hash || ''),
    route_state_match: String(nativeSummary.native_gui_route_state || '') === route,
    semantic_nodes_count_delta: nativeRuntimeBundleSemanticNodesCount - truthSemanticNodesCount,
    native_window_width: nativeWidth,
    native_window_height: nativeHeight,
    truth_window_width: truthPng.width,
    truth_window_height: truthPng.height,
    native_screenshot_path: nativeResolvedScreenshotPath,
    truth_screenshot_path: truthScreenshotPath,
    native_file_sha256: nativePng.file_sha256,
    truth_file_sha256: truthPng.file_sha256,
    native_pixel_sha256: nativePng.pixel_sha256,
    truth_pixel_sha256: truthPng.pixel_sha256,
    pixel_count: compare.pixel_count,
    diff_pixel_count: compare.diff_pixel_count,
    diff_ratio: compare.diff_ratio,
    max_channel_delta: compare.max_channel_delta,
    total_abs_delta: compare.total_abs_delta,
    first_diff: compare.first_diff,
    native_summary: {
      native_gui_route_state: String(nativeSummary.native_gui_route_state || ''),
      native_gui_screenshot_path: String(nativeSummary.native_gui_screenshot_path || ''),
      native_gui_layout_item_count: String(nativeSummary.native_gui_layout_item_count || ''),
      native_gui_render_plan_command_count: String(nativeSummary.native_gui_render_plan_command_count || ''),
      native_gui_native_layout_plan_item_count: String(nativeSummary.native_gui_native_layout_plan_item_count || ''),
      native_gui_runtime_manifest_ready: String(nativeSummary.native_gui_runtime_manifest_ready || ''),
      native_gui_runtime_contract_payload_ready: String(nativeSummary.native_gui_runtime_contract_payload_ready || ''),
      native_gui_runtime_bundle_payload_ready: String(nativeSummary.native_gui_runtime_bundle_payload_ready || ''),
      native_gui_runtime_bundle_ready: String(nativeSummary.native_gui_runtime_bundle_ready || ''),
      native_gui_runtime_contract_ready: String(nativeSummary.native_gui_runtime_contract_ready || ''),
      native_gui_runtime_bundle_route_state: String(nativeSummary.native_gui_runtime_bundle_route_state || ''),
      native_gui_runtime_bundle_route_count: String(nativeSummary.native_gui_runtime_bundle_route_count || ''),
      native_gui_runtime_bundle_supported_count: String(nativeSummary.native_gui_runtime_bundle_supported_count || ''),
      native_gui_runtime_bundle_semantic_nodes_count: String(nativeSummary.native_gui_runtime_bundle_semantic_nodes_count || ''),
      native_gui_runtime_bundle_layout_item_count: String(nativeSummary.native_gui_runtime_bundle_layout_item_count || ''),
      native_gui_runtime_bundle_render_command_count: String(nativeSummary.native_gui_runtime_bundle_render_command_count || ''),
      native_gui_runtime_contract_layout_item_count: String(nativeSummary.native_gui_runtime_contract_layout_item_count || ''),
      native_gui_runtime_contract_viewport_item_count: String(nativeSummary.native_gui_runtime_contract_viewport_item_count || ''),
      native_gui_runtime_contract_interactive_item_count: String(nativeSummary.native_gui_runtime_contract_interactive_item_count || ''),
    },
    truth_summary: {
      truth_trace_path: String(truthSummary.truth_trace_path || ''),
      truth_trace_format: String(truthSummary.truth_trace_format || ''),
      runtime_state_path: String(truthSummary.runtime_state_path || ''),
      semantic_nodes_path: String(truthSummary.semantic_nodes_path || ''),
      semantic_nodes_count: String(truthSummary.semantic_nodes_count || ''),
      document_nodes_count: String(truthSummary.document_nodes_count || ''),
      dom_hash: String(truthSummary.dom_hash || ''),
      screenshot_sha256: String(truthSummary.screenshot_sha256 || ''),
    },
  };
}

function main() {
  const args = parseArgs(process.argv);
  const repo = path.resolve(args.repo);
  const route = String(args.route || '').trim();
  const workspaceRoot = resolveWorkspaceRoot();
  const directMode = directCompareEnabled(args);
  if (directMode) {
    assertDirectCompareArgs(args);
  }
  const stage3 = directMode ? '' : resolveStage3(workspaceRoot, args.toolingBin);
  const scriptDir = path.dirname(path.resolve(process.argv[1]));
  const tsFrontendHelper = path.join(scriptDir, 'r2c-react-ts-frontend.mjs');
  const codegenSurfaceHelper = path.join(scriptDir, 'r2c-react-codegen-surface.mjs');
  const nativeGuiBundleHelper = path.join(scriptDir, 'r2c-react-native-gui-bundle.mjs');
  const nativeGuiRunHelper = path.join(scriptDir, 'r2c-react-native-gui-run.mjs');
  const truthRuntimeHelper = path.join(scriptDir, 'r2c-react-truth-runtime.mjs');
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_render_compare', route));
  const nativeOutDir = path.join(outDir, 'native_gui');
  const truthOutDir = path.join(outDir, 'truth');
  const tsxAstPath = path.join(nativeOutDir, 'tsx_ast_v1.json');
  const hostContractPath = path.join(nativeOutDir, 'unimaker_host_v1.json');
  const assetManifestPath = path.join(nativeOutDir, 'asset_manifest_v1.json');
  const tailwindManifestPath = path.join(nativeOutDir, 'tailwind_style_manifest_v1.json');
  const tsFrontendLogPath = path.join(outDir, 'tsx_frontend.log');
  const codegenSurfaceLogPath = path.join(outDir, 'codegen_surface.log');
  const nativeRunLogPath = path.join(outDir, 'native_gui_run.controller.log');
  const truthRunLogPath = path.join(outDir, 'truth_capture.controller.log');
  const reportPath = path.resolve(args.outPath || path.join(outDir, 'render_compare_report_v1.json'));
  const summaryPath = path.resolve(args.summaryOut || path.join(outDir, 'render_compare.summary.env'));
  const nativeScreenshotPath = path.join(nativeOutDir, `${route}.native_gui.png`);

  fs.mkdirSync(outDir, { recursive: true });

  let nativeSummaryPath = '';
  let truthSummaryPath = '';
  let nativeSummary = {};
  let truthSummary = {};
  let nativeResolvedScreenshotPath = '';
  let truthScreenshotPath = '';
  let nativeWidth = 0;
  let nativeHeight = 0;

  if (directMode) {
    nativeSummaryPath = path.resolve(args.nativeSummary);
    truthSummaryPath = path.resolve(args.truthSummary);
    nativeResolvedScreenshotPath = path.resolve(args.nativeScreenshot);
    truthScreenshotPath = path.resolve(args.truthScreenshot);
    requireFile(nativeSummaryPath, 'native gui summary');
    requireFile(truthSummaryPath, 'truth summary');
    requireFile(nativeResolvedScreenshotPath, 'native gui screenshot');
    requireFile(truthScreenshotPath, 'truth screenshot');
    nativeSummary = parseSummaryEnv(nativeSummaryPath);
    truthSummary = parseSummaryEnv(truthSummaryPath);
    nativeWidth = parsePositiveInt(args.browserWidth, parsePositiveInt(nativeSummary.native_gui_window_width, 393));
    nativeHeight = parsePositiveInt(args.browserHeight, parsePositiveInt(nativeSummary.native_gui_window_height, 852));
  } else {
    const tsFrontendArgs = [
      process.execPath,
      tsFrontendHelper,
      '--repo', repo,
      '--out', tsxAstPath,
      '--summary-out', path.join(nativeOutDir, 'tsx_frontend.summary.env'),
    ];
    const tsFrontendRun = run(tsFrontendArgs[0], tsFrontendArgs.slice(1), {
      cwd: workspaceRoot,
    });
    writeText(tsFrontendLogPath, [tsFrontendRun.stdout || '', tsFrontendRun.stderr || '', tsFrontendRun.error ? String(tsFrontendRun.error.stack || tsFrontendRun.error.message || tsFrontendRun.error) : ''].filter(Boolean).join('\n'));
    const tsFrontendExitCode = exitCodeOf(tsFrontendRun);
    if (tsFrontendExitCode !== 0) {
      throw new Error(`tsx_frontend_failed:${tsFrontendExitCode}`);
    }
    const tsxAstDoc = readJson(tsxAstPath);
    writeJson(hostContractPath, buildSyntheticHostContractDoc(tsxAstDoc, repo, route));
    writeJson(assetManifestPath, buildSyntheticAssetManifestDoc(tsxAstDoc, repo));
    writeJson(tailwindManifestPath, buildSyntheticTailwindManifestDoc(tsxAstDoc, repo));

    const codegenArgs = [
      process.execPath,
      codegenSurfaceHelper,
      '--repo', repo,
      '--out-dir', nativeOutDir,
    ];
    const codegenRun = run(codegenArgs[0], codegenArgs.slice(1), {
      cwd: workspaceRoot,
      env: {
        ...process.env,
        R2C_REACT_V3_TOOLING_BIN: stage3,
      },
    });
    writeText(codegenSurfaceLogPath, [codegenRun.stdout || '', codegenRun.stderr || '', codegenRun.error ? String(codegenRun.error.stack || codegenRun.error.message || codegenRun.error) : ''].filter(Boolean).join('\n'));
    const codegenExitCode = exitCodeOf(codegenRun);
    if (codegenExitCode !== 0) {
      throw new Error(`codegen_surface_failed:${codegenExitCode}`);
    }

    const nativeBundleLogPath = path.join(outDir, 'native_gui_bundle.log');
    const nativeArgs = [
      process.execPath,
      nativeGuiBundleHelper,
      '--repo', repo,
      '--route-state', route,
      '--out-dir', nativeOutDir,
      '--host-contract', hostContractPath,
      '--asset-manifest', assetManifestPath,
      '--tailwind-manifest', tailwindManifestPath,
    ];
    const nativeBundleRun = run(nativeArgs[0], nativeArgs.slice(1), {
      cwd: workspaceRoot,
      env: {
        ...process.env,
        R2C_REACT_V3_TOOLING_BIN: stage3,
      },
    });
    writeText(nativeBundleLogPath, [nativeBundleRun.stdout || '', nativeBundleRun.stderr || '', nativeBundleRun.error ? String(nativeBundleRun.error.stack || nativeBundleRun.error.message || nativeBundleRun.error) : ''].filter(Boolean).join('\n'));
    const nativeBundleExitCode = exitCodeOf(nativeBundleRun);
    if (nativeBundleExitCode !== 0) {
      throw new Error(`native_gui_bundle_failed:${nativeBundleExitCode}`);
    }
    const nativeRunArgs = [
      process.execPath,
      nativeGuiRunHelper,
      '--repo', repo,
      '--route-state', route,
      '--out-dir', nativeOutDir,
      '--screenshot-out', nativeScreenshotPath,
      '--auto-close-ms', String(args.autoCloseMs || '1200'),
    ];
    if (String(args.resize || '').trim()) {
      nativeRunArgs.push('--resize', String(args.resize).trim());
    }
    const nativeRun = run(nativeRunArgs[0], nativeRunArgs.slice(1), { cwd: workspaceRoot });
    writeText(nativeRunLogPath, [nativeRun.stdout || '', nativeRun.stderr || '', nativeRun.error ? String(nativeRun.error.stack || nativeRun.error.message || nativeRun.error) : ''].filter(Boolean).join('\n'));
    const nativeExitCode = exitCodeOf(nativeRun);
    if (nativeExitCode !== 0) {
      throw new Error(`native_gui_run_failed:${nativeExitCode}`);
    }
    nativeSummaryPath = path.join(nativeOutDir, 'native_gui_run.summary.env');
    requireFile(nativeSummaryPath, 'native gui summary');
    nativeSummary = parseSummaryEnv(nativeSummaryPath);
    nativeWidth = parsePositiveInt(args.browserWidth, parsePositiveInt(nativeSummary.native_gui_window_width, 393));
    nativeHeight = parsePositiveInt(args.browserHeight, parsePositiveInt(nativeSummary.native_gui_window_height, 852));
    nativeResolvedScreenshotPath = path.resolve(String(nativeSummary.native_gui_screenshot_path || nativeScreenshotPath));
    requireFile(nativeResolvedScreenshotPath, 'native gui screenshot');

    truthSummaryPath = path.join(truthOutDir, `${route}.summary.env`);
    const truthArgs = [
      process.execPath,
      truthRuntimeHelper,
      '--route', route,
      '--out-dir', truthOutDir,
      '--summary-out', truthSummaryPath,
      '--base-url', String(args.baseUrl || 'http://127.0.0.1:4173/'),
      '--width', String(nativeWidth),
      '--height', String(nativeHeight),
      '--wait-ms', String(args.waitMs || '30000'),
    ];
    const truthRun = run(truthArgs[0], truthArgs.slice(1), { cwd: workspaceRoot });
    writeText(truthRunLogPath, [truthRun.stdout || '', truthRun.stderr || '', truthRun.error ? String(truthRun.error.stack || truthRun.error.message || truthRun.error) : ''].filter(Boolean).join('\n'));
    const truthExitCode = exitCodeOf(truthRun);
    if (truthExitCode !== 0) {
      throw new Error(`truth_capture_failed:${truthExitCode}`);
    }
    requireFile(truthSummaryPath, 'truth summary');
    truthSummary = parseSummaryEnv(truthSummaryPath);
    truthScreenshotPath = path.resolve(String(truthSummary.screenshot_path || path.join(truthOutDir, `${route}.screenshot.png`)));
    requireFile(truthScreenshotPath, 'truth screenshot');
  }

  const nativePng = decodePng(nativeResolvedScreenshotPath);
  const truthPng = decodePng(truthScreenshotPath);
  const compare = compareDecodedPng(nativePng, truthPng);
  const report = buildRenderCompareReport({
    directMode,
    route,
    repo,
    workspaceRoot,
    stage3,
    outDir,
    tsFrontendLogPath,
    codegenSurfaceLogPath,
    nativeBundleLogPath: directMode ? '' : path.join(outDir, 'native_gui_bundle.log'),
    nativeRunLogPath,
    truthRunLogPath,
    nativeSummaryPath,
    truthSummaryPath,
    nativeResolvedScreenshotPath,
    truthScreenshotPath,
    nativeWidth,
    nativeHeight,
    nativePng,
    truthPng,
    compare,
    nativeSummary,
    truthSummary,
  });
  writeJson(reportPath, report);
  writeSummary(summaryPath, {
    ok: report.ok,
    reason: report.reason,
    direct_mode: report.direct_mode,
    route_state: route,
    route_state_match: report.route_state_match,
    native_gui_summary_path: nativeSummaryPath,
    truth_summary_path: truthSummaryPath,
    native_screenshot_path: nativeResolvedScreenshotPath,
    truth_screenshot_path: truthScreenshotPath,
    native_gui_runtime_bundle_ready: report.native_gui_runtime_bundle_ready,
    native_gui_runtime_contract_ready: report.native_gui_runtime_contract_ready,
    native_gui_runtime_bundle_route_state: report.native_gui_runtime_bundle_route_state,
    native_gui_runtime_bundle_semantic_nodes_count: report.native_gui_runtime_bundle_semantic_nodes_count,
    native_gui_runtime_bundle_layout_item_count: report.native_gui_runtime_bundle_layout_item_count,
    native_gui_runtime_bundle_render_command_count: report.native_gui_runtime_bundle_render_command_count,
    native_gui_runtime_contract_interactive_item_count: report.native_gui_runtime_contract_interactive_item_count,
    native_gui_native_layout_plan_item_count: report.native_gui_native_layout_plan_item_count,
    native_gui_render_plan_command_count: report.native_gui_render_plan_command_count,
    truth_trace_path: report.truth_trace_path,
    truth_trace_format: report.truth_trace_format,
    truth_semantic_nodes_count: report.truth_semantic_nodes_count,
    truth_document_nodes_count: report.truth_document_nodes_count,
    truth_dom_hash: report.truth_dom_hash,
    truth_screenshot_sha256: report.truth_screenshot_sha256,
    semantic_nodes_count_delta: report.semantic_nodes_count_delta,
    native_window_width: nativeWidth,
    native_window_height: nativeHeight,
    pixel_count: report.pixel_count,
    diff_pixel_count: report.diff_pixel_count,
    diff_ratio: report.diff_ratio,
    max_channel_delta: report.max_channel_delta,
    total_abs_delta: report.total_abs_delta,
    report_path: reportPath,
  });
  console.log(JSON.stringify(report, null, 2));
  if (!report.ok) process.exit(3);
}

main();
