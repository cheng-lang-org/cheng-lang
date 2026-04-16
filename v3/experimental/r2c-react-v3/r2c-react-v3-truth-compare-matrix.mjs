#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { renderTruthCompareMatrixEngineCheng } from './r2c-react-v3-truth-compare-engine-shared.mjs';
import {
  compareExecRouteMatrixToTruth,
  loadTruthTrace,
  readJson,
  writeJson,
  writeSummary,
} from './r2c-react-v3-route-matrix-shared.mjs';

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

function writeText(filePath, text) {
  fs.mkdirSync(path.dirname(filePath), { recursive: true });
  fs.writeFileSync(filePath, text, 'utf8');
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

function renderTruthCompareMatrixCheng(execRouteMatrix, truthDoc, truthTracePath, outPath, execIoModule) {
  const entries = Array.isArray(execRouteMatrix?.entries) ? execRouteMatrix.entries : [];
  const routeCount = entries.length;
  const normalizedTruth = Array.isArray(truthDoc?.snapshots)
    ? truthDoc.snapshots.filter((entry) => entry && typeof entry === 'object').map((entry) => {
      const routeDoc = typeof entry.route === 'object' && entry.route !== null ? entry.route : {};
      const stateId = typeof entry.stateId === 'string' ? entry.stateId : (typeof entry.state_id === 'string' ? entry.state_id : '');
      const routeId = typeof routeDoc.routeId === 'string' ? routeDoc.routeId : (typeof routeDoc.route_id === 'string' ? routeDoc.route_id : '');
      const renderReady = typeof entry.renderReady === 'boolean'
        ? entry.renderReady
        : (typeof entry.render_ready === 'boolean' ? entry.render_ready : null);
      const semanticNodesCount = typeof entry.semanticNodesCount === 'number'
        ? entry.semanticNodesCount
        : (typeof entry.semantic_nodes_count === 'number' ? entry.semantic_nodes_count : null);
      return {
        state_id: String(stateId || ''),
        route_id: String(routeId || ''),
        render_ready: renderReady,
        semantic_nodes_count: semanticNodesCount,
      };
    })
    : [];
  const truthStateIds = normalizedTruth.map((entry) => String(entry.state_id || ''));
  const truthRouteIds = normalizedTruth.map((entry) => String(entry.route_id || ''));
  const truthRenderReadyInts = normalizedTruth.map((entry) => (entry.render_ready === null || entry.render_ready === undefined ? -1 : (entry.render_ready ? 1 : 0)));
  const truthSemanticNodesCountInts = normalizedTruth.map((entry) => (entry.semantic_nodes_count === null || entry.semantic_nodes_count === undefined ? -1 : Number(entry.semantic_nodes_count)));
  const truthSemanticNodesLoadedInts = truthSemanticNodesCountInts.map((value) => (value < 0 ? -1 : (value > 0 ? 1 : 0)));
  const truthRouteStateJsonTokens = truthStateIds.map((value) => JSON.stringify(value));
  const truthRouteStateCompareTokens = truthStateIds.map((value) => JSON.stringify(JSON.stringify(value)));
  const truthRenderReadyCompareTokens = truthRenderReadyInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesLoadedCompareTokens = truthSemanticNodesLoadedInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesCountCompareTokens = truthSemanticNodesCountInts.map((value) => JSON.stringify(JSON.stringify(value < 0 ? 0 : value)));
  const truthSemanticNodesCountJsonTokens = truthSemanticNodesCountInts.map((value) => String(value < 0 ? 0 : value));

  const routeIds = entries.map((entry) => String(entry?.routeId || ''));
  const routeSupportedInts = entries.map((entry) => (entry?.supported ? 1 : 0));
  const routeUnsupportedReasonJsonTokens = entries.map((entry) => JSON.stringify(String(entry?.reason || 'unsupported_exec_route') || 'unsupported_exec_route'));
  const routeSnapshotPathJsonTokens = entries.map((entry) => JSON.stringify(String(entry?.snapshotPath || '')));
  const routeIdJsonTokens = routeIds.map((value) => JSON.stringify(value));
  const routeSnapshots = entries.map((entry) => (entry?.snapshot && typeof entry.snapshot === 'object' ? entry.snapshot : {}));
  const routeExecRouteStates = routeSnapshots.map((snapshot) => String(snapshot.route_state || ''));
  const routeExecRouteStateJsonTokens = routeExecRouteStates.map((value) => JSON.stringify(value));
  const routeExecRouteStateCompareTokens = routeExecRouteStates.map((value) => JSON.stringify(JSON.stringify(value)));
  const routeExecRenderReadyInts = routeSnapshots.map((snapshot) => (snapshot.render_ready ? 1 : 0));
  const routeExecSemanticNodesLoadedInts = routeSnapshots.map((snapshot) => (snapshot.semantic_nodes_loaded ? 1 : 0));
  const routeExecSemanticNodesCounts = routeSnapshots.map((snapshot) => Number(snapshot.semantic_nodes_count || 0));
  const routeExecSemanticNodesCountJsonTokens = routeExecSemanticNodesCounts.map((value) => String(Number(value || 0)));
  const routeExecSemanticNodesCountCompareTokens = routeExecSemanticNodesCounts.map((value) => JSON.stringify(JSON.stringify(Number(value || 0))));
  const countValues = Array.from({ length: routeCount + 1 }, (_, index) => index);
  const matchedIndexValues = [-1, ...normalizedTruth.map((_, index) => index)];

  return [
    `import ${execIoModule} as exec_io`,
    '',
    renderIndexedStringFunction('truthSnapshotStateId', truthStateIds, ''),
    renderIndexedStringFunction('truthSnapshotRouteId', truthRouteIds, ''),
    renderIndexedIntFunction('truthSnapshotRenderReadyInt', truthRenderReadyInts, -1),
    renderIndexedIntFunction('truthSnapshotSemanticNodesLoadedInt', truthSemanticNodesLoadedInts, -1),
    renderIndexedIntFunction('truthSnapshotSemanticNodesCountInt', truthSemanticNodesCountInts, -1),
    renderIndexedEmitFunction('emitTruthSnapshotRouteStateJson', truthRouteStateJsonTokens, JSON.stringify('')),
    renderIndexedEmitFunction('emitTruthSnapshotRouteStateCompareJson', truthRouteStateCompareTokens, JSON.stringify(JSON.stringify(''))),
    renderIndexedEmitFunction('emitTruthSnapshotRenderReadyCompareJson', truthRenderReadyCompareTokens, JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesLoadedCompareJson', truthSemanticNodesLoadedCompareTokens, JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesCountJson', truthSemanticNodesCountJsonTokens, '0'),
    renderIndexedEmitFunction('emitTruthSnapshotSemanticNodesCountCompareJson', truthSemanticNodesCountCompareTokens, JSON.stringify(JSON.stringify(0))),
    renderIndexedStringFunction('routeId', routeIds, ''),
    renderIndexedIntFunction('routeSupportedInt', routeSupportedInts, 0),
    renderIndexedEmitFunction('emitRouteUnsupportedReasonJson', routeUnsupportedReasonJsonTokens, JSON.stringify('unsupported_exec_route')),
    renderIndexedEmitFunction('emitRouteSnapshotPathJson', routeSnapshotPathJsonTokens, JSON.stringify('')),
    renderIndexedEmitFunction('emitRouteIdJson', routeIdJsonTokens, JSON.stringify('')),
    renderIndexedStringFunction('routeExecRouteState', routeExecRouteStates, ''),
    renderIndexedEmitFunction('emitRouteExecRouteStateJson', routeExecRouteStateJsonTokens, JSON.stringify('')),
    renderIndexedEmitFunction('emitRouteExecRouteStateCompareJson', routeExecRouteStateCompareTokens, JSON.stringify(JSON.stringify(''))),
    renderIndexedIntFunction('routeExecRenderReadyInt', routeExecRenderReadyInts, 0),
    renderIndexedIntFunction('routeExecSemanticNodesLoadedInt', routeExecSemanticNodesLoadedInts, 0),
    renderIndexedIntFunction('routeExecSemanticNodesCountInt', routeExecSemanticNodesCounts, 0),
    renderIndexedEmitFunction('emitRouteExecSemanticNodesCountJson', routeExecSemanticNodesCountJsonTokens, '0'),
    renderIndexedEmitFunction('emitRouteExecSemanticNodesCountCompareJson', routeExecSemanticNodesCountCompareTokens, JSON.stringify(JSON.stringify(0))),
    renderSmallIntJsonEmitter('emitMatchedSnapshotIndexJson', matchedIndexValues, '-1'),
    renderSmallIntJsonEmitter('emitCountJson', countValues, '0'),
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
      'fn emitEntryReasonJson(index: int32, reasonCode: int32) =',
      '    if reasonCode == 1:',
      '        emitRouteUnsupportedReasonJson(index)',
      '        return',
      '    if reasonCode == 2:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('missing_truth_state'))})`,
      '        return',
      '    if reasonCode == 3:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('missing_truth_fields'))})`,
      '        return',
      '    if reasonCode == 4:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('mismatch'))})`,
      '        return',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify('ok'))})`,
      '    return',
      '',
    ].join('\n'),
    [
      'fn emitMatrixReasonJson(reasonCode: int32) =',
      '    if reasonCode == 1:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('unsupported_exec_routes'))})`,
      '        return',
      '    if reasonCode == 2:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('missing_truth_states'))})`,
      '        return',
      '    if reasonCode == 3:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('missing_truth_fields'))})`,
      '        return',
      '    if reasonCode == 4:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify('mismatch'))})`,
      '        return',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify('ok'))})`,
      '    return',
      '',
    ].join('\n'),
    [
      'fn truthFindMatchedSnapshotIndex(targetState: str): int32 =',
      ...normalizedTruth.flatMap((entry, index) => [
        `    if ${chengStr(String(entry.state_id || ''))} == targetState:`,
        `        return ${index}`,
        `    if ${chengStr(String(entry.state_id || ''))} == "" && ${chengStr(String(entry.route_id || ''))} == targetState:`,
        `        return ${index}`,
      ]),
      '    return -1',
      '',
    ].join('\n'),
    [
      'fn routeMatchedSnapshotIndex(index: int32): int32 =',
      '    if routeSupportedInt(index) == 0:',
      '        return -1',
      ...routeIds.flatMap((routeId, index) => [
        `    if index == ${index}:`,
        `        return truthFindMatchedSnapshotIndex(${chengStr(routeId)})`,
      ]),
      '    return -1',
      '',
    ].join('\n'),
    [
      'fn routeMissingTruthFieldCount(index: int32, matchedIndex: int32): int32 =',
      '    var count: int32 = 0',
      '    if truthSnapshotStateId(matchedIndex) == "":',
      '        count = count + 1',
      '    var renderReadyInt: int32 = truthSnapshotRenderReadyInt(matchedIndex)',
      '    if renderReadyInt < 0:',
      '        count = count + 1',
      '    var semanticNodesLoadedInt: int32 = truthSnapshotSemanticNodesLoadedInt(matchedIndex)',
      '    if semanticNodesLoadedInt < 0:',
      '        count = count + 1',
      '    var semanticNodesCountInt: int32 = truthSnapshotSemanticNodesCountInt(matchedIndex)',
      '    if semanticNodesCountInt < 0:',
      '        count = count + 1',
      '    return count',
      '',
    ].join('\n'),
    [
      'fn routeMismatchCount(index: int32, matchedIndex: int32): int32 =',
      '    var count: int32 = 0',
      '    if truthSnapshotStateId(matchedIndex) != "" && routeExecRouteState(index) != truthSnapshotStateId(matchedIndex):',
      '        count = count + 1',
      '    var renderReadyInt: int32 = truthSnapshotRenderReadyInt(matchedIndex)',
      '    if renderReadyInt >= 0:',
      '        if routeExecRenderReadyInt(index) != renderReadyInt:',
      '            count = count + 1',
      '    var semanticNodesLoadedInt: int32 = truthSnapshotSemanticNodesLoadedInt(matchedIndex)',
      '    if semanticNodesLoadedInt >= 0:',
      '        if routeExecSemanticNodesLoadedInt(index) != semanticNodesLoadedInt:',
      '            count = count + 1',
      '    var semanticNodesCountInt: int32 = truthSnapshotSemanticNodesCountInt(matchedIndex)',
      '    if semanticNodesCountInt >= 0:',
      '        if routeExecSemanticNodesCountInt(index) != semanticNodesCountInt:',
      '            count = count + 1',
      '    return count',
      '',
    ].join('\n'),
    [
      'fn routeReasonCode(index: int32): int32 =',
      '    if routeSupportedInt(index) == 0:',
      '        return 1',
      '    var matchedIndex: int32 = routeMatchedSnapshotIndex(index)',
      '    if matchedIndex < 0:',
      '        return 2',
      '    if routeMissingTruthFieldCount(index, matchedIndex) > 0:',
      '        return 3',
      '    if routeMismatchCount(index, matchedIndex) > 0:',
      '        return 4',
      '    return 0',
      '',
    ].join('\n'),
    [
      'fn emitMissingTruthFields(index: int32, matchedIndex: int32) =',
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
      'fn emitMismatches(index: int32, matchedIndex: int32) =',
      '    exec_io.r2cWriteStdout("[")',
      '    var wrote: int32 = 0',
      '    if truthSnapshotStateId(matchedIndex) != "" && routeExecRouteState(index) != truthSnapshotStateId(matchedIndex):',
      '        exec_io.r2cWriteStdout("{\\"field\\":\\"route_state\\",\\"exec_value\\":")',
      '        emitRouteExecRouteStateCompareJson(index)',
      '        exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '        emitTruthSnapshotRouteStateCompareJson(matchedIndex)',
      '        exec_io.r2cWriteStdout("}")',
      '        wrote = 1',
      '    var renderReadyInt: int32 = truthSnapshotRenderReadyInt(matchedIndex)',
      '    if renderReadyInt >= 0:',
      '        if routeExecRenderReadyInt(index) != renderReadyInt:',
      '            if wrote != 0:',
      '                exec_io.r2cWriteStdout(",")',
      '            exec_io.r2cWriteStdout("{\\"field\\":\\"render_ready\\",\\"exec_value\\":")',
      '            if routeExecRenderReadyInt(index) != 0:',
      `                exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(true)))})`,
      '            else:',
      `                exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(false)))})`,
      '            exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '            emitTruthSnapshotRenderReadyCompareJson(matchedIndex)',
      '            exec_io.r2cWriteStdout("}")',
      '            wrote = 1',
      '    var semanticNodesLoadedInt: int32 = truthSnapshotSemanticNodesLoadedInt(matchedIndex)',
      '    if semanticNodesLoadedInt >= 0:',
      '        if routeExecSemanticNodesLoadedInt(index) != semanticNodesLoadedInt:',
      '            if wrote != 0:',
      '                exec_io.r2cWriteStdout(",")',
      '            exec_io.r2cWriteStdout("{\\"field\\":\\"semantic_nodes_loaded\\",\\"exec_value\\":")',
      '            if routeExecSemanticNodesLoadedInt(index) != 0:',
      `                exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(true)))})`,
      '            else:',
      `                exec_io.r2cWriteStdout(${chengStr(JSON.stringify(JSON.stringify(false)))})`,
      '            exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '            emitTruthSnapshotSemanticNodesLoadedCompareJson(matchedIndex)',
      '            exec_io.r2cWriteStdout("}")',
      '            wrote = 1',
      '    var semanticNodesCountInt: int32 = truthSnapshotSemanticNodesCountInt(matchedIndex)',
      '    if semanticNodesCountInt >= 0:',
      '        if routeExecSemanticNodesCountInt(index) != semanticNodesCountInt:',
      '            if wrote != 0:',
      '                exec_io.r2cWriteStdout(",")',
      '            exec_io.r2cWriteStdout("{\\"field\\":\\"semantic_nodes_count\\",\\"exec_value\\":")',
      '            emitRouteExecSemanticNodesCountCompareJson(index)',
      '            exec_io.r2cWriteStdout(",\\"truth_value\\":")',
      '            emitTruthSnapshotSemanticNodesCountCompareJson(matchedIndex)',
      '            exec_io.r2cWriteStdout("}")',
      '    exec_io.r2cWriteStdout("]")',
      '    return',
      '',
    ].join('\n'),
    [
      'fn emitEntry(index: int32) =',
      '    var reasonCode: int32 = routeReasonCode(index)',
      '    var matchedIndex: int32 = routeMatchedSnapshotIndex(index)',
      '    exec_io.r2cWriteStdout("{\\"routeId\\":")',
      '    emitRouteIdJson(index)',
      '    exec_io.r2cWriteStdout(",\\"supported\\":")',
      '    emitBoolIntJson(routeSupportedInt(index))',
      '    exec_io.r2cWriteStdout(",\\"ok\\":")',
      '    if reasonCode == 0:',
      '        exec_io.r2cWriteStdout("true")',
      '    else:',
      '        exec_io.r2cWriteStdout("false")',
      '    exec_io.r2cWriteStdout(",\\"reason\\":")',
      '    emitEntryReasonJson(index, reasonCode)',
      '    exec_io.r2cWriteStdout(",\\"matchedSnapshotIndex\\":")',
      '    emitMatchedSnapshotIndexJson(matchedIndex)',
      '    exec_io.r2cWriteStdout(",\\"snapshotPath\\":")',
      '    emitRouteSnapshotPathJson(index)',
      '    exec_io.r2cWriteStdout(",\\"execRouteState\\":")',
      '    emitRouteExecRouteStateJson(index)',
      '    exec_io.r2cWriteStdout(",\\"truthRouteState\\":")',
      '    if matchedIndex >= 0:',
      '        emitTruthSnapshotRouteStateJson(matchedIndex)',
      '    else:',
      `        exec_io.r2cWriteStdout(${chengStr(JSON.stringify(''))})`,
      '    exec_io.r2cWriteStdout(",\\"execSemanticNodesCount\\":")',
      '    emitRouteExecSemanticNodesCountJson(index)',
      '    exec_io.r2cWriteStdout(",\\"truthSemanticNodesCount\\":")',
      '    if matchedIndex >= 0:',
      '        if truthSnapshotSemanticNodesCountInt(matchedIndex) >= 0:',
      '            emitTruthSnapshotSemanticNodesCountJson(matchedIndex)',
      '        else:',
      '            exec_io.r2cWriteStdout("0")',
      '    else:',
      '        exec_io.r2cWriteStdout("0")',
      '    exec_io.r2cWriteStdout(",\\"missingTruthFields\\":")',
      '    if routeSupportedInt(index) != 0 && matchedIndex >= 0:',
      '        emitMissingTruthFields(index, matchedIndex)',
      '    else:',
      '        exec_io.r2cWriteStdout("[]")',
      '    exec_io.r2cWriteStdout(",\\"mismatches\\":")',
      '    if routeSupportedInt(index) != 0 && matchedIndex >= 0:',
      '        emitMismatches(index, matchedIndex)',
      '    else:',
      '        exec_io.r2cWriteStdout("[]")',
      '    exec_io.r2cWriteStdout("}")',
      '    return',
      '',
    ].join('\n'),
    [
      'fn emitEntries() =',
      '    exec_io.r2cWriteStdout("[")',
      ...entries.flatMap((_, index) => [
        ...(index > 0 ? ['    exec_io.r2cWriteStdout(",")'] : []),
        `    emitEntry(${index})`,
      ]),
      '    exec_io.r2cWriteStdout("]")',
      '    return',
      '',
    ].join('\n'),
    [
      'fn countReasonCode(reasonCode: int32): int32 =',
      '    var count: int32 = 0',
      ...entries.flatMap((_, index) => [
        `    if routeReasonCode(${index}) == reasonCode:`,
        '        count = count + 1',
      ]),
      '    return count',
      '',
    ].join('\n'),
    [
      'fn emitJson() =',
      '    var unsupportedCount: int32 = countReasonCode(1)',
      '    var missingTruthStateCount: int32 = countReasonCode(2)',
      '    var missingTruthFieldCount: int32 = countReasonCode(3)',
      '    var mismatchCount: int32 = countReasonCode(4)',
      '    var okCount: int32 = countReasonCode(0)',
      '    var matrixReasonCode: int32 = 0',
      '    if unsupportedCount > 0:',
      '        matrixReasonCode = 1',
      '    else:',
      '        if missingTruthStateCount > 0:',
      '            matrixReasonCode = 2',
      '        else:',
      '            if missingTruthFieldCount > 0:',
      '                matrixReasonCode = 3',
      '            else:',
      '                if mismatchCount > 0:',
      '                    matrixReasonCode = 4',
      '    exec_io.r2cWriteStdout("{\\"format\\":\\"truth_compare_matrix_v1\\"")',
      '    exec_io.r2cWriteStdout(",\\"ok\\":")',
      '    if matrixReasonCode == 0:',
      '        exec_io.r2cWriteStdout("true")',
      '    else:',
      '        exec_io.r2cWriteStdout("false")',
      '    exec_io.r2cWriteStdout(",\\"reason\\":")',
      '    emitMatrixReasonJson(matrixReasonCode)',
      `    exec_io.r2cWriteStdout(${chengStr(",\"truthTracePath\":" + JSON.stringify(truthTracePath))})`,
      `    exec_io.r2cWriteStdout(${chengStr(",\"truthTraceFormat\":" + JSON.stringify(String(truthDoc?.format || '')))})`,
      `    exec_io.r2cWriteStdout(",\\"routeCount\\":${routeCount}")`,
      '    exec_io.r2cWriteStdout(",\\"okCount\\":")',
      '    emitCountJson(okCount)',
      '    exec_io.r2cWriteStdout(",\\"unsupportedCount\\":")',
      '    emitCountJson(unsupportedCount)',
      '    exec_io.r2cWriteStdout(",\\"mismatchCount\\":")',
      '    emitCountJson(mismatchCount)',
      '    exec_io.r2cWriteStdout(",\\"missingTruthStateCount\\":")',
      '    emitCountJson(missingTruthStateCount)',
      '    exec_io.r2cWriteStdout(",\\"missingTruthFieldCount\\":")',
      '    emitCountJson(missingTruthFieldCount)',
      '    exec_io.r2cWriteStdout(",\\"entries\\":")',
      '    emitEntries()',
      `    exec_io.r2cWriteStdout(${chengStr(",\"outPath\":" + JSON.stringify(outPath))})`,
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

function renderTruthCompareMatrixMainCheng(compareMatrixModule) {
  return [
    `import ${compareMatrixModule} as truth_compare_matrix`,
    '',
    'fn main(): int32 =',
    '    truth_compare_matrix.emit()',
    '    return 0',
    '',
  ].join('\n');
}

function renderTruthCompareMatrixDataCheng(execRouteMatrix, truthDoc, truthTracePath, outPath, execIoModule) {
  const entries = Array.isArray(execRouteMatrix?.entries) ? execRouteMatrix.entries : [];
  const normalizedTruth = Array.isArray(truthDoc?.snapshots)
    ? truthDoc.snapshots.filter((entry) => entry && typeof entry === 'object').map((entry) => {
      const routeDoc = typeof entry.route === 'object' && entry.route !== null ? entry.route : {};
      const stateId = typeof entry.stateId === 'string' ? entry.stateId : (typeof entry.state_id === 'string' ? entry.state_id : '');
      const routeId = typeof routeDoc.routeId === 'string' ? routeDoc.routeId : (typeof routeDoc.route_id === 'string' ? routeDoc.route_id : '');
      const renderReady = typeof entry.renderReady === 'boolean'
        ? entry.renderReady
        : (typeof entry.render_ready === 'boolean' ? entry.render_ready : null);
      const semanticNodesCount = typeof entry.semanticNodesCount === 'number'
        ? entry.semanticNodesCount
        : (typeof entry.semantic_nodes_count === 'number' ? entry.semantic_nodes_count : null);
      return {
        state_id: String(stateId || ''),
        route_id: String(routeId || ''),
        render_ready: renderReady,
        semantic_nodes_count: semanticNodesCount,
      };
    })
    : [];
  const truthStateIds = normalizedTruth.map((entry) => String(entry.state_id || ''));
  const truthRouteIds = normalizedTruth.map((entry) => String(entry.route_id || ''));
  const truthRenderReadyInts = normalizedTruth.map((entry) => (entry.render_ready === null || entry.render_ready === undefined ? -1 : (entry.render_ready ? 1 : 0)));
  const truthSemanticNodesCountInts = normalizedTruth.map((entry) => (entry.semantic_nodes_count === null || entry.semantic_nodes_count === undefined ? -1 : Number(entry.semantic_nodes_count)));
  const truthSemanticNodesLoadedInts = truthSemanticNodesCountInts.map((value) => (value < 0 ? -1 : (value > 0 ? 1 : 0)));
  const truthRouteStateJsonTokens = truthStateIds.map((value) => JSON.stringify(value));
  const truthRouteStateCompareTokens = truthStateIds.map((value) => JSON.stringify(JSON.stringify(value)));
  const truthRenderReadyCompareTokens = truthRenderReadyInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesLoadedCompareTokens = truthSemanticNodesLoadedInts.map((value) => JSON.stringify(JSON.stringify(value !== 0)));
  const truthSemanticNodesCountCompareTokens = truthSemanticNodesCountInts.map((value) => JSON.stringify(JSON.stringify(value < 0 ? 0 : value)));
  const truthSemanticNodesCountJsonTokens = truthSemanticNodesCountInts.map((value) => String(value < 0 ? 0 : value));
  const routeIds = entries.map((entry) => String(entry?.routeId || ''));
  const routeSupportedInts = entries.map((entry) => (entry?.supported ? 1 : 0));
  const routeUnsupportedReasonJsonTokens = entries.map((entry) => JSON.stringify(String(entry?.reason || 'unsupported_exec_route') || 'unsupported_exec_route'));
  const routeSnapshotPathJsonTokens = entries.map((entry) => JSON.stringify(String(entry?.snapshotPath || '')));
  const routeIdJsonTokens = routeIds.map((value) => JSON.stringify(value));
  const routeSnapshots = entries.map((entry) => (entry?.snapshot && typeof entry.snapshot === 'object' ? entry.snapshot : {}));
  const routeExecRouteStates = routeSnapshots.map((snapshot) => String(snapshot.route_state || ''));
  const routeExecRouteStateJsonTokens = routeExecRouteStates.map((value) => JSON.stringify(value));
  const routeExecRouteStateCompareTokens = routeExecRouteStates.map((value) => JSON.stringify(JSON.stringify(value)));
  const routeExecRenderReadyInts = routeSnapshots.map((snapshot) => (snapshot.render_ready ? 1 : 0));
  const routeExecSemanticNodesLoadedInts = routeSnapshots.map((snapshot) => (snapshot.semantic_nodes_loaded ? 1 : 0));
  const routeExecSemanticNodesCounts = routeSnapshots.map((snapshot) => Number(snapshot.semantic_nodes_count || 0));
  const routeExecSemanticNodesCountCompareTokens = routeExecSemanticNodesCounts.map((value) => JSON.stringify(JSON.stringify(Number(value || 0))));
  return [
    `import ${execIoModule} as exec_io`,
    '',
    `fn routeCount(): int32 =`,
    `    return ${entries.length}`,
    '',
    `fn truthSnapshotCount(): int32 =`,
    `    return ${normalizedTruth.length}`,
    '',
    renderIndexedStringFunction('routeId', routeIds, ''),
    renderIndexedIntFunction('routeSupportedInt', routeSupportedInts, 0),
    renderIndexedStringFunction('routeExecRouteState', routeExecRouteStates, ''),
    renderIndexedIntFunction('routeExecRenderReadyInt', routeExecRenderReadyInts, 0),
    renderIndexedIntFunction('routeExecSemanticNodesLoadedInt', routeExecSemanticNodesLoadedInts, 0),
    renderIndexedIntFunction('routeExecSemanticNodesCountInt', routeExecSemanticNodesCounts, 0),
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
      'fn emitOutPathJson() =',
      `    exec_io.r2cWriteStdout(${chengStr(JSON.stringify(outPath))})`,
      '    return',
      '',
    ].join('\n'),
    renderIndexedEmitFunction('emitRouteUnsupportedReasonJson', routeUnsupportedReasonJsonTokens, JSON.stringify('unsupported_exec_route')),
    renderIndexedEmitFunction('emitRouteSnapshotPathJson', routeSnapshotPathJsonTokens, JSON.stringify('')),
    renderIndexedEmitFunction('emitRouteIdJson', routeIdJsonTokens, JSON.stringify('')),
    renderIndexedEmitFunction('emitRouteExecRouteStateJson', routeExecRouteStateJsonTokens, JSON.stringify('')),
    renderIndexedEmitFunction('emitRouteExecRouteStateCompareJson', routeExecRouteStateCompareTokens, JSON.stringify(JSON.stringify(''))),
    renderIndexedEmitFunction('emitRouteExecRenderReadyCompareJson', routeExecRenderReadyInts.map((value) => JSON.stringify(JSON.stringify(value !== 0))), JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitRouteExecSemanticNodesLoadedCompareJson', routeExecSemanticNodesLoadedInts.map((value) => JSON.stringify(JSON.stringify(value !== 0))), JSON.stringify(JSON.stringify(false))),
    renderIndexedEmitFunction('emitRouteExecSemanticNodesCountCompareJson', routeExecSemanticNodesCountCompareTokens, JSON.stringify(JSON.stringify(0))),
    renderIndexedEmitFunction('emitTruthSnapshotRouteStateJson', truthRouteStateJsonTokens, JSON.stringify('')),
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
  const packageRoot = path.join(path.dirname(outPath), 'cheng_truth_compare_matrix_exec');
  const packageId = 'pkg://cheng/cheng_truth_compare_matrix_exec';
  const modulePrefix = 'cheng_truth_compare_matrix_exec';
  const packageImportPrefix = 'cheng/cheng_truth_compare_matrix_exec';
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
    const compareMatrixModule = String(codegenManifest.truth_compare_matrix_module || `${importPrefix}/truth_compare_matrix`);
    const compareMatrixMainModule = String(codegenManifest.truth_compare_matrix_main_module || `${importPrefix}/truth_compare_matrix_main`);
    const compareMatrixDataModule = String(codegenManifest.truth_compare_matrix_data_module || `${importPrefix}/truth_compare_matrix_data`);
    const srcRoot = path.join(existingPackageRoot, 'src');
    fs.mkdirSync(srcRoot, { recursive: true });
    const execIoPath = path.join(srcRoot, 'exec_io.cheng');
    const compareMatrixPath = path.join(srcRoot, 'truth_compare_matrix.cheng');
    const compareMatrixMainPath = path.join(srcRoot, 'truth_compare_matrix_main.cheng');
    if (!fs.existsSync(execIoPath)) {
      throw new Error(`missing generated exec_io module: ${execIoPath}`);
    }
    if (!fs.existsSync(compareMatrixPath)) {
      throw new Error(`missing generated truth_compare_matrix module: ${compareMatrixPath}`);
    }
    if (!fs.existsSync(compareMatrixMainPath)) {
      throw new Error(`missing generated truth_compare_matrix_main module: ${compareMatrixMainPath}`);
    }
    return {
      packageRoot: existingPackageRoot,
      compareMatrixModule,
      compareMatrixMainModule,
      compareMatrixDataModule,
      execIoModule,
      compareMatrixPath,
      compareMatrixMainPath,
      compareMatrixDataPath: path.join(srcRoot, 'truth_compare_matrix_data.cheng'),
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
    compareMatrixModule: `${packageImportPrefix}/truth_compare_matrix`,
    compareMatrixMainModule: `${packageImportPrefix}/truth_compare_matrix_main`,
    compareMatrixDataModule: `${packageImportPrefix}/truth_compare_matrix_data`,
    execIoModule: `${packageImportPrefix}/exec_io`,
    compareMatrixPath: path.join(srcRoot, 'truth_compare_matrix.cheng'),
    compareMatrixMainPath: path.join(srcRoot, 'truth_compare_matrix_main.cheng'),
    compareMatrixDataPath: path.join(srcRoot, 'truth_compare_matrix_data.cheng'),
    standalonePackage: true,
  };
}

function parseArgs(argv) {
  const out = {
    execRouteMatrix: '',
    truthTrace: '',
    codegenManifest: '',
    outPath: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--exec-route-matrix') out.execRouteMatrix = String(argv[++i] || '');
    else if (arg === '--truth-trace') out.truthTrace = String(argv[++i] || '');
    else if (arg === '--codegen-manifest') out.codegenManifest = String(argv[++i] || '');
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-truth-compare-matrix.mjs --exec-route-matrix <file> --truth-trace <file> [--codegen-manifest <file>] [--out <file>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.execRouteMatrix) throw new Error('missing --exec-route-matrix');
  if (!out.truthTrace) throw new Error('missing --truth-trace');
  return out;
}

function main() {
  const args = parseArgs(process.argv);
  const workspaceRoot = resolveWorkspaceRoot();
  const execRouteMatrixPath = path.resolve(args.execRouteMatrix);
  const truthTracePath = path.resolve(args.truthTrace);
  const codegenManifestPath = args.codegenManifest ? path.resolve(args.codegenManifest) : '';
  const outPath = path.resolve(args.outPath || path.join(path.dirname(execRouteMatrixPath), 'truth_compare_matrix_v1.json'));
  const execRouteMatrix = readJson(execRouteMatrixPath);
  if (String(execRouteMatrix.format || '') !== 'cheng_codegen_exec_route_matrix_v1') {
    throw new Error('unsupported exec route matrix format');
  }
  const truthDoc = loadTruthTrace(truthTracePath);
  const compare = compareExecRouteMatrixToTruth(execRouteMatrix, truthDoc, truthTracePath);
  compare.outPath = outPath;
  const tool = resolveDefaultToolingBin(workspaceRoot);
  const packageDoc = prepareGeneratedPackage(workspaceRoot, codegenManifestPath, outPath);
  writeText(packageDoc.compareMatrixDataPath, renderTruthCompareMatrixDataCheng(
    execRouteMatrix,
    truthDoc,
    truthTracePath,
    outPath,
    packageDoc.execIoModule,
  ));
  if (packageDoc.standalonePackage) {
    writeText(packageDoc.compareMatrixPath, renderTruthCompareMatrixEngineCheng(packageDoc.execIoModule, packageDoc.compareMatrixDataModule));
    writeText(packageDoc.compareMatrixMainPath, renderTruthCompareMatrixMainCheng(packageDoc.compareMatrixModule));
  }
  const exePath = path.join(path.dirname(outPath), 'truth_compare_matrix_exec');
  const compileReportPath = path.join(path.dirname(outPath), 'truth_compare_matrix_exec.report.txt');
  const compileLogPath = path.join(path.dirname(outPath), 'truth_compare_matrix_exec.log');
  const runLogPath = path.join(path.dirname(outPath), 'truth_compare_matrix_exec.run.log');
  const execRun = compileAndRunEntry(
    tool,
    packageDoc.packageRoot,
    packageDoc.compareMatrixMainPath,
    exePath,
    compileReportPath,
    compileLogPath,
    runLogPath,
  );
  if (execRun.compileReturnCode !== 0) {
    writeSummary(args.summaryOut, {
      truth_compare_matrix_ok: false,
      truth_compare_matrix_reason: 'truth_compare_matrix_compile_failed',
      truth_compare_matrix_path: outPath,
      truth_compare_matrix_main_path: packageDoc.compareMatrixMainPath,
      truth_compare_matrix_data_path: packageDoc.compareMatrixDataPath,
      truth_compare_matrix_exe_path: exePath,
      truth_compare_matrix_compile_report_path: compileReportPath,
      truth_compare_matrix_compile_log_path: compileLogPath,
      truth_compare_matrix_run_log_path: runLogPath,
      truth_compare_matrix_compile_returncode: execRun.compileReturnCode,
      truth_compare_matrix_run_returncode: -1,
      route_count: Number(compare.routeCount || 0),
      ok_count: Number(compare.okCount || 0),
      unsupported_count: Number(compare.unsupportedCount || 0),
      mismatch_count: Number(compare.mismatchCount || 0),
      missing_truth_state_count: Number(compare.missingTruthStateCount || 0),
      missing_truth_field_count: Number(compare.missingTruthFieldCount || 0),
    });
    process.exit(1);
  }
  if (execRun.runReturnCode !== 0) {
    writeSummary(args.summaryOut, {
      truth_compare_matrix_ok: false,
      truth_compare_matrix_reason: 'truth_compare_matrix_run_failed',
      truth_compare_matrix_path: outPath,
      truth_compare_matrix_main_path: packageDoc.compareMatrixMainPath,
      truth_compare_matrix_data_path: packageDoc.compareMatrixDataPath,
      truth_compare_matrix_exe_path: exePath,
      truth_compare_matrix_compile_report_path: compileReportPath,
      truth_compare_matrix_compile_log_path: compileLogPath,
      truth_compare_matrix_run_log_path: runLogPath,
      truth_compare_matrix_compile_returncode: execRun.compileReturnCode,
      truth_compare_matrix_run_returncode: execRun.runReturnCode,
      route_count: Number(compare.routeCount || 0),
      ok_count: Number(compare.okCount || 0),
      unsupported_count: Number(compare.unsupportedCount || 0),
      mismatch_count: Number(compare.mismatchCount || 0),
      missing_truth_state_count: Number(compare.missingTruthStateCount || 0),
      missing_truth_field_count: Number(compare.missingTruthFieldCount || 0),
    });
    process.exit(1);
  }
  const emittedCompare = parseJsonStdout(execRun.stdout, 'truth_compare_matrix_v1', 'truth_compare_matrix');
  emittedCompare.outPath = outPath;
  writeJson(outPath, emittedCompare);
  writeSummary(args.summaryOut, {
    truth_compare_matrix_ok: Boolean(emittedCompare.ok),
    truth_compare_matrix_reason: String(emittedCompare.reason || ''),
    truth_compare_matrix_path: outPath,
    truth_compare_matrix_main_path: packageDoc.compareMatrixMainPath,
    truth_compare_matrix_data_path: packageDoc.compareMatrixDataPath,
    truth_compare_matrix_exe_path: exePath,
    truth_compare_matrix_compile_report_path: compileReportPath,
    truth_compare_matrix_compile_log_path: compileLogPath,
    truth_compare_matrix_run_log_path: runLogPath,
    truth_compare_matrix_compile_returncode: execRun.compileReturnCode,
    truth_compare_matrix_run_returncode: execRun.runReturnCode,
    route_count: Number(emittedCompare.routeCount || 0),
    ok_count: Number(emittedCompare.okCount || 0),
    unsupported_count: Number(emittedCompare.unsupportedCount || 0),
    mismatch_count: Number(emittedCompare.mismatchCount || 0),
    missing_truth_state_count: Number(emittedCompare.missingTruthStateCount || 0),
    missing_truth_field_count: Number(emittedCompare.missingTruthFieldCount || 0),
  });
  console.log(JSON.stringify({
    ok: Boolean(emittedCompare.ok),
    reason: String(emittedCompare.reason || ''),
    out_path: outPath,
    truth_compare_matrix_main_path: packageDoc.compareMatrixMainPath,
    truth_compare_matrix_data_path: packageDoc.compareMatrixDataPath,
    truth_compare_matrix_exe_path: exePath,
    route_count: Number(emittedCompare.routeCount || 0),
    ok_count: Number(emittedCompare.okCount || 0),
    unsupported_count: Number(emittedCompare.unsupportedCount || 0),
    mismatch_count: Number(emittedCompare.mismatchCount || 0),
    missing_truth_state_count: Number(emittedCompare.missingTruthStateCount || 0),
  }, null, 2));
  process.exit(Boolean(emittedCompare.ok) ? 0 : 2);
}

main();
