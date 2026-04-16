#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { renderTruthCompareEngineCheng } from './r2c-react-v3-truth-compare-engine-shared.mjs';

function parseArgs(argv) {
  const out = {
    execSnapshot: '',
    truthTrace: '',
    truthState: '',
    codegenManifest: '',
    outPath: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--exec-snapshot') out.execSnapshot = String(argv[++i] || '');
    else if (arg === '--truth-trace') out.truthTrace = String(argv[++i] || '');
    else if (arg === '--truth-state') out.truthState = String(argv[++i] || '');
    else if (arg === '--codegen-manifest') out.codegenManifest = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-truth-compare.mjs --exec-snapshot <file> --truth-trace <file> [--truth-state <state>] [--codegen-manifest <file>] [--out <file>] [--summary-out <file>]');
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

function resolveDefaultToolingBin(workspaceRoot) {
  const explicit = String(process.env.R2C_REACT_V3_TOOLING_BIN || '').trim();
  if (explicit) return explicit;
  const stage3 = path.join(workspaceRoot, 'artifacts', 'v3_bootstrap', 'cheng.stage3');
  if (fs.existsSync(stage3)) return stage3;
  return path.join(workspaceRoot, 'artifacts', 'v3_backend_driver', 'cheng');
}

function ensurePackageCompileSupport(workspaceRoot, packageRoot) {
  const v3Link = path.join(packageRoot, 'v3');
  const runtimeRoot = path.join(packageRoot, 'src', 'runtime');
  const nativeLink = path.join(runtimeRoot, 'native');
  fs.mkdirSync(runtimeRoot, { recursive: true });
  if (!fs.existsSync(v3Link)) {
    fs.symlinkSync(path.join(workspaceRoot, 'v3'), v3Link);
  }
  if (!fs.existsSync(nativeLink)) {
    fs.symlinkSync(path.join(workspaceRoot, 'src', 'runtime', 'native'), nativeLink);
  }
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
  });
}

function chengStr(text) {
  return `"${String(text || '').replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\t/g, '\\t')}"`;
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

function renderDirectChunkWriterEmitter(functionPrefix, value, {
  appendNewline = false,
  chunkSize = 3072,
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
  ].join('\n');
}

function renderIndexedStringFunction(name, values, fallback = '') {
  const lines = [`fn ${name}(index: int32): str =`];
  for (let index = 0; index < values.length; index += 1) {
    lines.push(`    if index == ${index}:`);
    lines.push(`        return ${chengStr(String(values[index] ?? ''))}`);
  }
  lines.push(`    return ${chengStr(String(fallback || ''))}`);
  lines.push('');
  return lines.join('\n');
}

function renderIndexedIntFunction(name, values, fallback = 0) {
  const lines = [`fn ${name}(index: int32): int32 =`];
  for (let index = 0; index < values.length; index += 1) {
    lines.push(`    if index == ${index}:`);
    lines.push(`        return ${Number(values[index] ?? fallback)}`);
  }
  lines.push(`    return ${Number(fallback)}`);
  lines.push('');
  return lines.join('\n');
}

function renderIndexedEmitFunction(name, tokens, fallbackToken = '""') {
  const lines = [`fn ${name}(index: int32) =`];
  for (let index = 0; index < tokens.length; index += 1) {
    lines.push(`    if index == ${index}:`);
    lines.push(`        exec_io.r2cWriteStdout(${chengStr(String(tokens[index] ?? fallbackToken))})`);
    lines.push('        return');
  }
  lines.push(`    exec_io.r2cWriteStdout(${chengStr(String(fallbackToken || '""'))})`);
  lines.push('    return');
  lines.push('');
  return lines.join('\n');
}

function renderSmallIntJsonEmitter(name, values, fallback = '0') {
  const uniqueValues = Array.from(new Set(values.map((value) => Number(value)))).sort((left, right) => left - right);
  const lines = [`fn ${name}(value: int32) =`];
  for (const value of uniqueValues) {
    lines.push(`    if value == ${value}:`);
    lines.push(`        exec_io.r2cWriteStdout(${chengStr(String(value))})`);
    lines.push('        return');
  }
  lines.push(`    exec_io.r2cWriteStdout(${chengStr(String(fallback))})`);
  lines.push('    return');
  lines.push('');
  return lines.join('\n');
}

function renderTruthCompareCheng(execSnapshot, execSnapshotPath, truthDoc, truthTracePath, truthState, outPath, execIoModule) {
  const snapshots = Array.isArray(truthDoc?.snapshots) ? truthDoc.snapshots : [];
  const normalizedSnapshots = snapshots
    .filter((entry) => entry && typeof entry === 'object')
    .map((entry) => normalizeTruthSnapshot(entry));
  const execRouteState = String(execSnapshot?.route_state || '');
  const execRenderReadyInt = Boolean(execSnapshot?.render_ready) ? 1 : 0;
  const execSemanticNodesLoadedInt = Boolean(execSnapshot?.semantic_nodes_loaded) ? 1 : 0;
  const execSemanticNodesCount = Number(execSnapshot?.semantic_nodes_count || 0);
  const resolvedTruthState = String(truthState || execRouteState);
  const emptyJsonStringToken = JSON.stringify('');
  const truthStateIds = normalizedSnapshots.map((entry) => String(entry.state_id || ''));
  const truthRouteIds = normalizedSnapshots.map((entry) => String(entry.route_id || ''));
  const truthRenderReadyInts = normalizedSnapshots.map((entry) => (entry.render_ready === null || entry.render_ready === undefined ? -1 : (Boolean(entry.render_ready) ? 1 : 0)));
  const truthSemanticNodesCountInts = normalizedSnapshots.map((entry) => (entry.semantic_nodes_count === null || entry.semantic_nodes_count === undefined ? -1 : Number(entry.semantic_nodes_count)));
  const truthSemanticNodesLoadedInts = truthSemanticNodesCountInts.map((value) => (value < 0 ? -1 : (value > 0 ? 1 : 0)));
  const truthRouteStateJsonTokens = truthStateIds.map((value) => JSON.stringify(value));
  const truthRouteIdJsonTokens = truthRouteIds.map((value) => JSON.stringify(value));
  const truthRouteStateCompareTokens = truthStateIds.map((value) => JSON.stringify(JSON.stringify(value)));
  const truthRenderReadyCompareTokens = truthRenderReadyInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesLoadedCompareTokens = truthSemanticNodesLoadedInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesCountCompareTokens = truthSemanticNodesCountInts.map((value) => JSON.stringify(JSON.stringify(value < 0 ? 0 : value)));
  const truthRenderReadyJsonTokens = truthRenderReadyInts.map((value) => (value !== 0 ? 'true' : 'false'));
  const truthSemanticNodesLoadedJsonTokens = truthSemanticNodesLoadedInts.map((value) => (value !== 0 ? 'true' : 'false'));
  const truthSemanticNodesCountJsonTokens = truthSemanticNodesCountInts.map((value) => String(value < 0 ? 0 : value));
  const matchedIndexValues = [-1, ...normalizedSnapshots.map((_, index) => index)];
  return [
    `import ${execIoModule} as exec_io`,
    '',
    renderIndexedStringFunction('truthSnapshotStateId', truthStateIds, ''),
    renderIndexedStringFunction('truthSnapshotRouteId', truthRouteIds, ''),
    renderIndexedIntFunction('truthSnapshotRenderReadyInt', truthRenderReadyInts, -1),
    renderIndexedIntFunction('truthSnapshotSemanticNodesLoadedInt', truthSemanticNodesLoadedInts, -1),
    renderIndexedIntFunction('truthSnapshotSemanticNodesCountInt', truthSemanticNodesCountInts, -1),
    renderIndexedEmitFunction('emitTruthSnapshotRouteStateJson', truthRouteStateJsonTokens, emptyJsonStringToken),
    renderIndexedEmitFunction('emitTruthSnapshotRouteIdJson', truthRouteIdJsonTokens, emptyJsonStringToken),
    renderIndexedEmitFunction('emitTruthSnapshotRouteStateCompareJson', truthRouteStateCompareTokens, JSON.stringify(JSON.stringify(''))),
    renderIndexedEmitFunction('emitTruthSnapshotRenderReadyJson', truthRenderReadyJsonTokens, 'false'),
    renderIndexedEmitFunction('emitTruthSnapshotRenderReadyCompareJson', truthRenderReadyCompareTokens, JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesLoadedJson', truthSemanticNodesLoadedJsonTokens, 'false'),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesLoadedCompareJson', truthSemanticNodesLoadedCompareTokens, JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesCountJson', truthSemanticNodesCountJsonTokens, '0'),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesCountCompareJson', truthSemanticNodesCountCompareTokens, JSON.stringify(JSON.stringify(0))),
    renderSmallIntJsonEmitter('emitMatchedSnapshotIndexJson', matchedIndexValues, '-1'),
    [
      'fn emitBoolIntJson(value: int32) =',
      '    if value != 0:',
      '        exec_io.r2cWriteStdout("true")',
      '        return',
      '    exec_io.r2cWriteStdout("false")',
      '    return',
      '',
    ].join('\n'),
    [
      'fn emitReasonJson(reasonCode: int32) =',
      '    if reasonCode == 1:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('missing_truth_state'))})`,
      '        return',
      '    if reasonCode == 2:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('missing_truth_fields'))})`,
      '        return',
      '    if reasonCode == 3:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('mismatch'))})`,
      '        return',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify('ok'))})`,
      '    return',
      '',
    ].join('\n'),
    [
      'fn truthFindMatchedSnapshotIndex(targetState: str): int32 =',
      ...normalizedSnapshots.flatMap((entry, index) => [
        `    if ${chengStr(String(entry.state_id || ''))} == targetState:`,
        `        return ${index}`,
        `    if ${chengStr(String(entry.state_id || ''))} == "" && ${chengStr(String(entry.route_id || ''))} == targetState:`,
        `        return ${index}`,
      ]),
      '    return -1',
      '',
    ].join('\n'),
    [
      'fn missingTruthFieldCount(index: int32): int32 =',
      '    var count: int32 = 0',
      '    if truthSnapshotStateId(index) == "":',
      '        count = count + 1',
      '    var renderReadyInt: int32 = truthSnapshotRenderReadyInt(index)',
      '    if renderReadyInt < 0:',
      '        count = count + 1',
      '    var semanticNodesLoadedInt: int32 = truthSnapshotSemanticNodesLoadedInt(index)',
      '    if semanticNodesLoadedInt < 0:',
      '        count = count + 1',
      '    var semanticNodesCountInt: int32 = truthSnapshotSemanticNodesCountInt(index)',
      '    if semanticNodesCountInt < 0:',
      '        count = count + 1',
      '    return count',
      '',
    ].join('\n'),
    [
      'fn mismatchCount(index: int32): int32 =',
      '    var count: int32 = 0',
      `    if truthSnapshotStateId(index) != "" && ${chengStr(execRouteState)} != truthSnapshotStateId(index):`,
      '        count = count + 1',
      '    var renderReadyInt: int32 = truthSnapshotRenderReadyInt(index)',
      '    if renderReadyInt >= 0:',
      `        if renderReadyInt != ${execRenderReadyInt}:`,
      '            count = count + 1',
      '    var semanticNodesLoadedInt: int32 = truthSnapshotSemanticNodesLoadedInt(index)',
      '    if semanticNodesLoadedInt >= 0:',
      `        if semanticNodesLoadedInt != ${execSemanticNodesLoadedInt}:`,
      '            count = count + 1',
      '    var semanticNodesCountInt: int32 = truthSnapshotSemanticNodesCountInt(index)',
      '    if semanticNodesCountInt >= 0:',
      `        if semanticNodesCountInt != ${execSemanticNodesCount}:`,
      '            count = count + 1',
      '    return count',
      '',
    ].join('\n'),
    [
      'fn compareReasonCode(matchedIndex: int32): int32 =',
      '    if matchedIndex < 0:',
      '        return 1',
      '    if missingTruthFieldCount(matchedIndex) > 0:',
      '        return 2',
      '    if mismatchCount(matchedIndex) > 0:',
      '        return 3',
      '    return 0',
      '',
    ].join('\n'),
    [
      'fn emitMissingTruthFields(matchedIndex: int32) =',
      '    exec_io.r2cWriteStdout("[")',
      '    var wrote: int32 = 0',
      '    if truthSnapshotStateId(matchedIndex) == "":',
      '        exec_io.r2cWriteStdout("\\"route_state\\"")',
      '        wrote = 1',
      '    if truthSnapshotRenderReadyInt(matchedIndex) < 0:',
      '        if wrote != 0:',
      '            exec_io.r2cWriteStdout(",")',
      '        exec_io.r2cWriteStdout("\\"render_ready\\"")',
      '        wrote = 1',
      '    if truthSnapshotSemanticNodesLoadedInt(matchedIndex) < 0:',
      '        if wrote != 0:',
      '            exec_io.r2cWriteStdout(",")',
      '        exec_io.r2cWriteStdout("\\"semantic_nodes_loaded\\"")',
      '        wrote = 1',
      '    if truthSnapshotSemanticNodesCountInt(matchedIndex) < 0:',
      '        if wrote != 0:',
      '            exec_io.r2cWriteStdout(",")',
      '        exec_io.r2cWriteStdout("\\"semantic_nodes_count\\"")',
      '    exec_io.r2cWriteStdout("]")',
      '    return',
      '',
    ].join('\n'),
    [
      'fn emitMismatches(matchedIndex: int32) =',
      '    exec_io.r2cWriteStdout("[")',
      '    var wrote: int32 = 0',
      `    if truthSnapshotStateId(matchedIndex) != "" && ${chengStr(execRouteState)} != truthSnapshotStateId(matchedIndex):`,
      '        exec_io.r2cWriteStdout("{\\"field\\":\\"route_state\\",\\"exec_value\\":")',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(execRouteState)))})`,
      '        exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '        emitTruthSnapshotRouteStateCompareJson(matchedIndex)',
      '        exec_io.r2cWriteStdout("}")',
      '        wrote = 1',
      '    var renderReadyInt: int32 = truthSnapshotRenderReadyInt(matchedIndex)',
      '    if renderReadyInt >= 0:',
      `        if renderReadyInt != ${execRenderReadyInt}:`,
      '            if wrote != 0:',
      '                exec_io.r2cWriteStdout(",")',
      '            exec_io.r2cWriteStdout("{\\"field\\":\\"render_ready\\",\\"exec_value\\":")',
      `            exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(Boolean(execRenderReadyInt))))})`,
      '            exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '            emitTruthSnapshotRenderReadyCompareJson(matchedIndex)',
      '            exec_io.r2cWriteStdout("}")',
      '            wrote = 1',
      '    var semanticNodesLoadedInt: int32 = truthSnapshotSemanticNodesLoadedInt(matchedIndex)',
      '    if semanticNodesLoadedInt >= 0:',
      `        if semanticNodesLoadedInt != ${execSemanticNodesLoadedInt}:`,
      '            if wrote != 0:',
      '                exec_io.r2cWriteStdout(",")',
      '            exec_io.r2cWriteStdout("{\\"field\\":\\"semantic_nodes_loaded\\",\\"exec_value\\":")',
      `            exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(Boolean(execSemanticNodesLoadedInt))))})`,
      '            exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '            emitTruthSnapshotSemanticNodesLoadedCompareJson(matchedIndex)',
      '            exec_io.r2cWriteStdout("}")',
      '            wrote = 1',
      '    var semanticNodesCountInt: int32 = truthSnapshotSemanticNodesCountInt(matchedIndex)',
      '    if semanticNodesCountInt >= 0:',
      `        if semanticNodesCountInt != ${execSemanticNodesCount}:`,
      '            if wrote != 0:',
      '                exec_io.r2cWriteStdout(",")',
      '            exec_io.r2cWriteStdout("{\\"field\\":\\"semantic_nodes_count\\",\\"exec_value\\":")',
      `            exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(execSemanticNodesCount)))})`,
      '            exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '            emitTruthSnapshotSemanticNodesCountCompareJson(matchedIndex)',
      '            exec_io.r2cWriteStdout("}")',
      '    exec_io.r2cWriteStdout("]")',
      '    return',
      '',
    ].join('\n'),
    [
      'fn emitJson() =',
      `    var matchedIndex: int32 = truthFindMatchedSnapshotIndex(${chengStr(resolvedTruthState)})`,
      '    var reasonCode: int32 = compareReasonCode(matchedIndex)',
      '    exec_io.r2cWriteStdout("{\\"format\\":\\"truth_compare_v1\\"")',
      '    exec_io.r2cWriteStdout(",\\"ok\\":")',
      '    if reasonCode == 0:',
      '        exec_io.r2cWriteStdout("true")',
      '    else:',
      '        exec_io.r2cWriteStdout("false")',
      '    exec_io.r2cWriteStdout(",\\"reason\\":")',
      '    emitReasonJson(reasonCode)',
      `    exec_io.r2cWriteStdout(${chengStr(",\"truth_trace_path\":" + JSON.stringify(truthTracePath))})`,
      `    exec_io.r2cWriteStdout(${chengStr(",\"truth_trace_format\":" + JSON.stringify(String(truthDoc?.format || '')))})`,
      `    exec_io.r2cWriteStdout(${chengStr(",\"truth_state\":" + JSON.stringify(resolvedTruthState))})`,
      '    exec_io.r2cWriteStdout(",\\"matched_snapshot_index\\":")',
      '    emitMatchedSnapshotIndexJson(matchedIndex)',
      `    exec_io.r2cWriteStdout(${chengStr(",\"exec_snapshot_path\":" + JSON.stringify(execSnapshotPath))})`,
      `    exec_io.r2cWriteStdout(${chengStr(",\"out_path\":" + JSON.stringify(outPath))})`,
      `    exec_io.r2cWriteStdout(${chengStr(",\"exec_route_state\":" + JSON.stringify(execRouteState))})`,
      `    exec_io.r2cWriteStdout(",\\"exec_render_ready\\":${execRenderReadyInt !== 0 ? 'true' : 'false'}")`,
      `    exec_io.r2cWriteStdout(",\\"exec_semantic_nodes_loaded\\":${execSemanticNodesLoadedInt !== 0 ? 'true' : 'false'}")`,
      `    exec_io.r2cWriteStdout(",\\"exec_semantic_nodes_count\\":${execSemanticNodesCount}")`,
      '    exec_io.r2cWriteStdout(",\\"truth_route_state\\":")',
      '    if matchedIndex >= 0:',
      '        emitTruthSnapshotRouteStateJson(matchedIndex)',
      '    else:',
      `        exec_io.r2cWriteStdout(${chengStr(emptyJsonStringToken)})`,
      '    exec_io.r2cWriteStdout(",\\"truth_route_id\\":")',
      '    if matchedIndex >= 0:',
      '        emitTruthSnapshotRouteIdJson(matchedIndex)',
      '    else:',
      `        exec_io.r2cWriteStdout(${chengStr(emptyJsonStringToken)})`,
      '    exec_io.r2cWriteStdout(",\\"truth_render_ready\\":")',
      '    if matchedIndex >= 0:',
      '        var renderReadyInt: int32 = truthSnapshotRenderReadyInt(matchedIndex)',
      '        if renderReadyInt >= 0:',
      '            emitBoolIntJson(renderReadyInt)',
      '        else:',
      '            exec_io.r2cWriteStdout("false")',
      '    else:',
      '        exec_io.r2cWriteStdout("false")',
      '    exec_io.r2cWriteStdout(",\\"truth_semantic_nodes_loaded\\":")',
      '    if matchedIndex >= 0:',
      '        var semanticNodesLoadedInt: int32 = truthSnapshotSemanticNodesLoadedInt(matchedIndex)',
      '        if semanticNodesLoadedInt >= 0:',
      '            emitBoolIntJson(semanticNodesLoadedInt)',
      '        else:',
      '            exec_io.r2cWriteStdout("false")',
      '    else:',
      '        exec_io.r2cWriteStdout("false")',
      '    exec_io.r2cWriteStdout(",\\"truth_semantic_nodes_count\\":")',
      '    if matchedIndex >= 0:',
      '        var semanticNodesCountInt: int32 = truthSnapshotSemanticNodesCountInt(matchedIndex)',
      '        if semanticNodesCountInt >= 0:',
      '            emitTruthSnapshotSemanticNodesCountJson(matchedIndex)',
      '        else:',
      '            exec_io.r2cWriteStdout("0")',
      '    else:',
      '        exec_io.r2cWriteStdout("0")',
      '    exec_io.r2cWriteStdout(",\\"missing_truth_fields\\":")',
      '    if matchedIndex >= 0:',
      '        emitMissingTruthFields(matchedIndex)',
      '    else:',
      '        exec_io.r2cWriteStdout("[]")',
      '    exec_io.r2cWriteStdout(",\\"mismatches\\":")',
      '    if matchedIndex >= 0:',
      '        emitMismatches(matchedIndex)',
      '    else:',
      '        exec_io.r2cWriteStdout("[]")',
      '    exec_io.r2cWriteStdout("}")',
      '    return',
      '',
    ].join('\n'),
    [
      'fn emit() =',
      '    emitJson()',
      '    exec_io.r2cWriteStdout("\\n")',
      '    return',
      '',
    ].join('\n'),
    '',
  ].join('\n');
}

function renderTruthCompareMainCheng(compareModule) {
  return [
    `import ${compareModule} as truth_compare`,
    '',
    'fn main(): int32 =',
    '    truth_compare.emit()',
    '    return 0',
    '',
  ].join('\n');
}

function renderTruthCompareDataCheng(execSnapshot, execSnapshotPath, truthDoc, truthTracePath, truthState, outPath, execIoModule) {
  const snapshots = Array.isArray(truthDoc?.snapshots) ? truthDoc.snapshots : [];
  const normalizedSnapshots = snapshots
    .filter((entry) => entry && typeof entry === 'object')
    .map((entry) => normalizeTruthSnapshot(entry));
  const execRouteState = String(execSnapshot?.route_state || '');
  const execRenderReadyInt = Boolean(execSnapshot?.render_ready) ? 1 : 0;
  const execSemanticNodesLoadedInt = Boolean(execSnapshot?.semantic_nodes_loaded) ? 1 : 0;
  const execSemanticNodesCount = Number(execSnapshot?.semantic_nodes_count || 0);
  const resolvedTruthState = String(truthState || execRouteState);
  const emptyJsonStringToken = JSON.stringify('');
  const truthStateIds = normalizedSnapshots.map((entry) => String(entry.state_id || ''));
  const truthRouteIds = normalizedSnapshots.map((entry) => String(entry.route_id || ''));
  const truthRenderReadyInts = normalizedSnapshots.map((entry) => (entry.render_ready === null || entry.render_ready === undefined ? -1 : (Boolean(entry.render_ready) ? 1 : 0)));
  const truthSemanticNodesCountInts = normalizedSnapshots.map((entry) => (entry.semantic_nodes_count === null || entry.semantic_nodes_count === undefined ? -1 : Number(entry.semantic_nodes_count)));
  const truthSemanticNodesLoadedInts = truthSemanticNodesCountInts.map((value) => (value < 0 ? -1 : (value > 0 ? 1 : 0)));
  const truthRouteStateJsonTokens = truthStateIds.map((value) => JSON.stringify(value));
  const truthRouteIdJsonTokens = truthRouteIds.map((value) => JSON.stringify(value));
  const truthRouteStateCompareTokens = truthStateIds.map((value) => JSON.stringify(JSON.stringify(value)));
  const truthRenderReadyCompareTokens = truthRenderReadyInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesLoadedCompareTokens = truthSemanticNodesLoadedInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesCountCompareTokens = truthSemanticNodesCountInts.map((value) => JSON.stringify(JSON.stringify(value < 0 ? 0 : value)));
  const truthSemanticNodesCountJsonTokens = truthSemanticNodesCountInts.map((value) => String(value < 0 ? 0 : value));
  return [
    `import ${execIoModule} as exec_io`,
    '',
    `fn truthSnapshotCount(): int32 =`,
    `    return ${normalizedSnapshots.length}`,
    '',
    `fn truthState(): str =`,
    `    return ${chengStr(resolvedTruthState)}`,
    '',
    `fn execRouteState(): str =`,
    `    return ${chengStr(execRouteState)}`,
    '',
    `fn execRenderReadyInt(): int32 =`,
    `    return ${execRenderReadyInt}`,
    '',
    `fn execSemanticNodesLoadedInt(): int32 =`,
    `    return ${execSemanticNodesLoadedInt}`,
    '',
    `fn execSemanticNodesCount(): int32 =`,
    `    return ${execSemanticNodesCount}`,
    '',
    renderIndexedStringFunction('truthSnapshotStateId', truthStateIds, ''),
    renderIndexedStringFunction('truthSnapshotRouteId', truthRouteIds, ''),
    renderIndexedIntFunction('truthSnapshotRenderReadyInt', truthRenderReadyInts, -1),
    renderIndexedIntFunction('truthSnapshotSemanticNodesLoadedInt', truthSemanticNodesLoadedInts, -1),
    renderIndexedIntFunction('truthSnapshotSemanticNodesCountInt', truthSemanticNodesCountInts, -1),
    [
      'fn emitTruthTracePathJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(truthTracePath))})`,
      '    return',
      '',
      'fn emitTruthTraceFormatJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(String(truthDoc?.format || '')))})`,
      '    return',
      '',
      'fn emitTruthStateJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(resolvedTruthState))})`,
      '    return',
      '',
      'fn emitExecSnapshotPathJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(execSnapshotPath))})`,
      '    return',
      '',
      'fn emitOutPathJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(outPath))})`,
      '    return',
      '',
      'fn emitExecRouteStateJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(execRouteState))})`,
      '    return',
      '',
      'fn emitExecRouteStateCompareJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(execRouteState)))})`,
      '    return',
      '',
      'fn emitExecRenderReadyCompareJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(Boolean(execRenderReadyInt))))})`,
      '    return',
      '',
      'fn emitExecSemanticNodesLoadedCompareJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(Boolean(execSemanticNodesLoadedInt))))})`,
      '    return',
      '',
      'fn emitExecSemanticNodesCountCompareJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(execSemanticNodesCount)))})`,
      '    return',
      '',
    ].join('\n'),
    renderIndexedEmitFunction('emitTruthSnapshotRouteStateJson', truthRouteStateJsonTokens, emptyJsonStringToken),
    renderIndexedEmitFunction('emitTruthSnapshotRouteIdJson', truthRouteIdJsonTokens, emptyJsonStringToken),
    renderIndexedEmitFunction('emitTruthSnapshotRouteStateCompareJson', truthRouteStateCompareTokens, JSON.stringify(JSON.stringify(''))),
    renderIndexedEmitFunction('emitTruthSnapshotRenderReadyCompareJson', truthRenderReadyCompareTokens, JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesLoadedCompareJson', truthSemanticNodesLoadedCompareTokens, JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesCountJson', truthSemanticNodesCountJsonTokens, '0'),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesCountCompareJson', truthSemanticNodesCountCompareTokens, JSON.stringify(JSON.stringify(0))),
    '',
  ].join('\n');
}

function parseJsonStdout(stdoutText, expectedFormat, label) {
  const trimmed = String(stdoutText || '').trim();
  if (!trimmed) throw new Error(`${label}_stdout_empty`);
  const doc = JSON.parse(trimmed);
  if (String(doc.format || '') !== expectedFormat) {
    throw new Error(`${label}_format_mismatch:${String(doc.format || '')}`);
  }
  return doc;
}

function compileAndRunEntry(tool, packageRoot, mainPath, exePath, compileReportPath, compileLogPath, runLogPath) {
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
  const run = spawnForExec(exePath, [], { timeout: 30000 });
  writeText(runLogPath, [run.stdout || '', run.stderr || '', run.error ? String(run.error.stack || run.error.message || run.error) : ''].filter(Boolean).join('\n'));
  return {
    compileReturnCode,
    runReturnCode: exitCodeOf(run),
    stdout: String(run.stdout || ''),
  };
}

function prepareGeneratedPackage(workspaceRoot, codegenManifestPath, outPath) {
  const packageRoot = path.join(path.dirname(outPath), 'cheng_truth_compare_exec');
  const packageId = 'pkg://cheng/cheng_truth_compare_exec';
  const modulePrefix = 'cheng_truth_compare_exec';
  const packageImportPrefix = 'cheng/cheng_truth_compare_exec';
  if (codegenManifestPath) {
    const codegenManifest = readJson(codegenManifestPath);
    if (String(codegenManifest.format || '') !== 'cheng_codegen_v1') {
      throw new Error('unsupported codegen manifest format');
    }
    const existingPackageRoot = path.resolve(String(codegenManifest.package_root || ''));
    if (!existingPackageRoot || !fs.existsSync(existingPackageRoot)) {
      throw new Error(`missing codegen package root: ${existingPackageRoot}`);
    }
    ensurePackageCompileSupport(workspaceRoot, existingPackageRoot);
    const importPrefix = String(codegenManifest.package_import_prefix || packageImportPrefix);
    const execIoModule = String(codegenManifest.exec_io_module || `${importPrefix}/exec_io`);
    const compareModule = String(codegenManifest.truth_compare_module || `${importPrefix}/truth_compare`);
    const compareMainModule = String(codegenManifest.truth_compare_main_module || `${importPrefix}/truth_compare_main`);
    const compareDataModule = String(codegenManifest.truth_compare_data_module || `${importPrefix}/truth_compare_data`);
    const srcRoot = path.join(existingPackageRoot, 'src');
    fs.mkdirSync(srcRoot, { recursive: true });
    const execIoPath = path.join(srcRoot, 'exec_io.cheng');
    const comparePath = path.join(srcRoot, 'truth_compare.cheng');
    const compareMainPath = path.join(srcRoot, 'truth_compare_main.cheng');
    if (!fs.existsSync(execIoPath)) {
      throw new Error(`missing generated exec_io module: ${execIoPath}`);
    }
    if (!fs.existsSync(comparePath)) {
      throw new Error(`missing generated truth_compare module: ${comparePath}`);
    }
    if (!fs.existsSync(compareMainPath)) {
      throw new Error(`missing generated truth_compare_main module: ${compareMainPath}`);
    }
    return {
      packageRoot: existingPackageRoot,
      compareModule,
      compareMainModule,
      compareDataModule,
      execIoModule,
      comparePath,
      compareMainPath,
      compareDataPath: path.join(srcRoot, 'truth_compare_data.cheng'),
      standalonePackage: false,
    };
  }
  fs.rmSync(packageRoot, { recursive: true, force: true });
  const srcRoot = path.join(packageRoot, 'src');
  fs.mkdirSync(srcRoot, { recursive: true });
  ensurePackageCompileSupport(workspaceRoot, packageRoot);
  writeText(path.join(packageRoot, 'cheng-package.toml'), `package_id = "${packageId}"\nmodule_prefix = "${modulePrefix}"\n`);
  writeText(path.join(srcRoot, 'exec_io.cheng'), renderExecIoCheng());
  return {
    packageRoot,
    compareModule: `${packageImportPrefix}/truth_compare`,
    compareMainModule: `${packageImportPrefix}/truth_compare_main`,
    compareDataModule: `${packageImportPrefix}/truth_compare_data`,
    execIoModule: `${packageImportPrefix}/exec_io`,
    comparePath: path.join(srcRoot, 'truth_compare.cheng'),
    compareMainPath: path.join(srcRoot, 'truth_compare_main.cheng'),
    compareDataPath: path.join(srcRoot, 'truth_compare_data.cheng'),
    standalonePackage: true,
  };
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
  const workspaceRoot = resolveWorkspaceRoot();
  const execSnapshotPath = path.resolve(args.execSnapshot);
  const truthTracePath = path.resolve(args.truthTrace);
  const codegenManifestPath = args.codegenManifest ? path.resolve(args.codegenManifest) : '';
  const outPath = path.resolve(args.outPath || path.join(path.dirname(execSnapshotPath), 'truth_compare_v1.json'));
  const execSnapshot = readJson(execSnapshotPath);
  if (String(execSnapshot.format || '') !== 'cheng_codegen_exec_snapshot_v1') {
    throw new Error('unsupported exec snapshot format');
  }
  const truthDoc = loadTruthTrace(truthTracePath);
  const truthState = args.truthState || String(execSnapshot.route_state || '');
  const tool = resolveDefaultToolingBin(workspaceRoot);
  const packageDoc = prepareGeneratedPackage(workspaceRoot, codegenManifestPath, outPath);
  const compareDoc = compareExecSnapshotToTruthDoc(
    execSnapshot,
    execSnapshotPath,
    truthDoc,
    truthTracePath,
    truthState,
  );
  compareDoc.out_path = outPath;
  writeText(packageDoc.compareDataPath, renderTruthCompareDataCheng(
    execSnapshot,
    execSnapshotPath,
    truthDoc,
    truthTracePath,
    truthState,
    outPath,
    packageDoc.execIoModule,
  ));
  if (packageDoc.standalonePackage) {
    writeText(packageDoc.comparePath, renderTruthCompareEngineCheng(packageDoc.execIoModule, packageDoc.compareDataModule));
    writeText(packageDoc.compareMainPath, renderTruthCompareMainCheng(packageDoc.compareModule));
  }
  const exePath = path.join(path.dirname(outPath), 'truth_compare_exec');
  const compileReportPath = path.join(path.dirname(outPath), 'truth_compare_exec.report.txt');
  const compileLogPath = path.join(path.dirname(outPath), 'truth_compare_exec.log');
  const runLogPath = path.join(path.dirname(outPath), 'truth_compare_exec.run.log');
  const execRun = compileAndRunEntry(
    tool,
    packageDoc.packageRoot,
    packageDoc.compareMainPath,
    exePath,
    compileReportPath,
    compileLogPath,
    runLogPath,
  );
  if (execRun.compileReturnCode !== 0) {
    writeSummary(args.summaryOut, {
      truth_compare_ok: false,
      truth_compare_reason: 'truth_compare_compile_failed',
      truth_compare_path: outPath,
      truth_compare_main_path: packageDoc.compareMainPath,
      truth_compare_data_path: packageDoc.compareDataPath,
      truth_compare_exe_path: exePath,
      truth_compare_compile_report_path: compileReportPath,
      truth_compare_compile_log_path: compileLogPath,
      truth_compare_run_log_path: runLogPath,
      truth_compare_compile_returncode: execRun.compileReturnCode,
      truth_compare_run_returncode: -1,
      truth_state: String(truthState || ''),
      matched_snapshot_index: -1,
      mismatch_count: 0,
      missing_truth_fields_count: 0,
      exec_semantic_nodes_count: Number(execSnapshot.semantic_nodes_count || 0),
      truth_semantic_nodes_count: 0,
    });
    process.exit(1);
  }
  if (execRun.runReturnCode !== 0) {
    writeSummary(args.summaryOut, {
      truth_compare_ok: false,
      truth_compare_reason: 'truth_compare_run_failed',
      truth_compare_path: outPath,
      truth_compare_main_path: packageDoc.compareMainPath,
      truth_compare_data_path: packageDoc.compareDataPath,
      truth_compare_exe_path: exePath,
      truth_compare_compile_report_path: compileReportPath,
      truth_compare_compile_log_path: compileLogPath,
      truth_compare_run_log_path: runLogPath,
      truth_compare_compile_returncode: execRun.compileReturnCode,
      truth_compare_run_returncode: execRun.runReturnCode,
      truth_state: String(truthState || ''),
      matched_snapshot_index: -1,
      mismatch_count: 0,
      missing_truth_fields_count: 0,
      exec_semantic_nodes_count: Number(execSnapshot.semantic_nodes_count || 0),
      truth_semantic_nodes_count: 0,
    });
    process.exit(1);
  }
  const emittedCompare = parseJsonStdout(execRun.stdout, 'truth_compare_v1', 'truth_compare');
  emittedCompare.out_path = outPath;
  writeJson(outPath, emittedCompare);
  writeSummary(args.summaryOut, {
    truth_compare_ok: Boolean(emittedCompare.ok),
    truth_compare_reason: String(emittedCompare.reason || ''),
    truth_compare_path: outPath,
    truth_compare_main_path: packageDoc.compareMainPath,
    truth_compare_data_path: packageDoc.compareDataPath,
    truth_compare_exe_path: exePath,
    truth_compare_compile_report_path: compileReportPath,
    truth_compare_compile_log_path: compileLogPath,
    truth_compare_run_log_path: runLogPath,
    truth_compare_compile_returncode: execRun.compileReturnCode,
    truth_compare_run_returncode: execRun.runReturnCode,
    truth_state: String(emittedCompare.truth_state || ''),
    matched_snapshot_index: Number(emittedCompare.matched_snapshot_index ?? -1),
    mismatch_count: Array.isArray(emittedCompare.mismatches) ? emittedCompare.mismatches.length : 0,
    missing_truth_fields_count: Array.isArray(emittedCompare.missing_truth_fields) ? emittedCompare.missing_truth_fields.length : 0,
    exec_semantic_nodes_count: Number(emittedCompare.exec_semantic_nodes_count || 0),
    truth_semantic_nodes_count: Number(emittedCompare.truth_semantic_nodes_count || 0),
  });
  console.log(JSON.stringify({
    ok: Boolean(emittedCompare.ok),
    reason: String(emittedCompare.reason || ''),
    out_path: outPath,
    truth_compare_main_path: packageDoc.compareMainPath,
    truth_compare_data_path: packageDoc.compareDataPath,
    truth_compare_exe_path: exePath,
    truth_state: String(emittedCompare.truth_state || ''),
    matched_snapshot_index: Number(emittedCompare.matched_snapshot_index ?? -1),
    mismatch_count: Array.isArray(emittedCompare.mismatches) ? emittedCompare.mismatches.length : 0,
    missing_truth_fields_count: Array.isArray(emittedCompare.missing_truth_fields) ? emittedCompare.missing_truth_fields.length : 0,
    exec_semantic_nodes_count: Number(emittedCompare.exec_semantic_nodes_count || 0),
    truth_semantic_nodes_count: Number(emittedCompare.truth_semantic_nodes_count || 0),
  }, null, 2));
  process.exit(Boolean(emittedCompare.ok) ? 0 : 2);
}

main();
