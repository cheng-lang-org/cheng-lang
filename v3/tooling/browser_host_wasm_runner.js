const crypto = require('crypto');
const fs = require('fs');

const WASM_STR_LEN_OFFSET = 4;
const WASM_STR_STORE_ID_OFFSET = 8;
const WASM_STR_FLAGS_OFFSET = 12;
const WASM_STR_SIZE = 16;
const WASM_SEQ_LEN_OFFSET = 0;
const WASM_SEQ_CAP_OFFSET = 4;
const WASM_SEQ_BUFFER_OFFSET = 8;
const WASM_SEQ_HEADER_SIZE = 12;
const HOST_BRIDGE_HEAP_BASE = 8 * 1024 * 1024;

const encoder = new TextEncoder();
const decoder = new TextDecoder();

const wasmPath = process.argv[2];
if (!wasmPath) {
  throw new Error('missing wasm path');
}
const bytes = fs.readFileSync(wasmPath);
const memory = new WebAssembly.Memory({ initial: 32, maximum: 512 });

let nextHeapPtr = HOST_BRIDGE_HEAP_BASE;
let nextBufferHandle = 1;
let nextInputHandle = 1;
let lastMoqFountainErrorText = '';
const bufferHandleToBytes = new Map();
const inputHandleToBytes = new Map();

function normalizeI32(value) {
  return value | 0;
}

function alignUp(value, align) {
  const size = Math.max(1, normalizeI32(align));
  return Math.ceil(Math.max(0, normalizeI32(value)) / size) * size;
}

function ensureCapacity(endExclusive) {
  const required = Math.max(0, normalizeI32(endExclusive));
  const pageSize = 64 * 1024;
  while (memory.buffer.byteLength < required) {
    const missing = required - memory.buffer.byteLength;
    const pages = Math.max(1, Math.ceil(missing / pageSize));
    memory.grow(pages);
  }
}

function allocRegion(size, align = 8) {
  const length = Math.max(0, normalizeI32(size));
  const ptr = alignUp(nextHeapPtr, align);
  ensureCapacity(ptr + length + 64);
  nextHeapPtr = ptr + length;
  return ptr;
}

function copyWasmMemoryBytes(ptr, size) {
  const base = normalizeI32(ptr);
  const length = Math.max(0, normalizeI32(size));
  if (base < 0 || base + length > memory.buffer.byteLength) {
    return new Uint8Array(0);
  }
  return new Uint8Array(memory.buffer).slice(base, base + length);
}

function copyBytesToWasmMemory(ptr, bytesToCopy, limit = bytesToCopy.byteLength) {
  const base = normalizeI32(ptr);
  const length = Math.max(0, normalizeI32(limit));
  if (base < 0 || base + length > memory.buffer.byteLength) {
    return false;
  }
  new Uint8Array(memory.buffer).set(bytesToCopy.subarray(0, length), base);
  return true;
}

function fillWasmMemory(ptr, value, size) {
  const base = normalizeI32(ptr);
  const length = Math.max(0, normalizeI32(size));
  if (base < 0 || base + length > memory.buffer.byteLength) {
    return false;
  }
  new Uint8Array(memory.buffer).fill(normalizeI32(value) & 0xff, base, base + length);
  return true;
}

function readCStringBytes(ptr) {
  const base = normalizeI32(ptr);
  if (base <= 0 || base >= memory.buffer.byteLength) {
    return new Uint8Array(0);
  }
  const view = new Uint8Array(memory.buffer);
  let end = base;
  while (end < view.byteLength && view[end] !== 0) {
    end += 1;
  }
  return view.slice(base, end);
}

function allocCStringBytes(bytesValue) {
  const length = Math.max(0, normalizeI32(bytesValue.byteLength));
  const ptr = allocRegion(length + 1, 1);
  const view = new Uint8Array(memory.buffer);
  if (length > 0) {
    view.set(bytesValue.subarray(0, length), ptr);
  }
  view[ptr + length] = 0;
  return ptr;
}

function allocBridgeString(text) {
  const encoded = encoder.encode(String(text));
  const dataPtr = allocRegion(encoded.byteLength, 1);
  if (encoded.byteLength > 0) {
    new Uint8Array(memory.buffer).set(encoded, dataPtr);
  }
  const objectPtr = allocRegion(WASM_STR_SIZE, 4);
  const view = new DataView(memory.buffer);
  view.setUint32(objectPtr, dataPtr, true);
  view.setInt32(objectPtr + WASM_STR_LEN_OFFSET, encoded.byteLength, true);
  view.setInt32(objectPtr + WASM_STR_STORE_ID_OFFSET, 0, true);
  view.setInt32(objectPtr + WASM_STR_FLAGS_OFFSET, 0, true);
  return objectPtr;
}

function isProbablyValidBridgeStringObject(objectPtr) {
  const base = normalizeI32(objectPtr);
  if (base <= 0 || base + WASM_STR_SIZE > memory.buffer.byteLength) {
    return false;
  }
  const view = new DataView(memory.buffer);
  const dataPtr = view.getUint32(base, true);
  const length = view.getInt32(base + WASM_STR_LEN_OFFSET, true);
  const flags = view.getInt32(base + WASM_STR_FLAGS_OFFSET, true);
  if (length < 0 || length > 268435456) {
    return false;
  }
  if (flags !== 0 && flags !== 1) {
    return false;
  }
  if (dataPtr === 0) {
    return length === 0;
  }
  return dataPtr > 0 && dataPtr < memory.buffer.byteLength;
}

function readBridgeStringBytes(handle) {
  const objectPtr = normalizeI32(handle);
  if (objectPtr <= 0) {
    return new Uint8Array(0);
  }
  if (isProbablyValidBridgeStringObject(objectPtr)) {
    const view = new DataView(memory.buffer);
    const dataPtr = view.getUint32(objectPtr, true);
    const length = view.getInt32(objectPtr + WASM_STR_LEN_OFFSET, true);
    return copyWasmMemoryBytes(dataPtr, length);
  }
  if (objectPtr + WASM_STR_SIZE <= memory.buffer.byteLength) {
    const view = new DataView(memory.buffer);
    const compatPtr = view.getUint32(objectPtr, true);
    if (compatPtr > 0 && compatPtr < memory.buffer.byteLength) {
      return readCStringBytes(compatPtr);
    }
  }
  return readCStringBytes(objectPtr);
}

function readBridgeStringSeqTexts(seqPtr) {
  const header = readSeqHeader(seqPtr);
  const len = Math.max(0, normalizeI32(header.len));
  const bufferPtr = normalizeI32(header.bufferPtr);
  const texts = [];
  if (len <= 0 || bufferPtr <= 0) {
    return texts;
  }
  for (let i = 0; i < len; i += 1) {
    const itemPtr = bufferPtr + i * WASM_STR_SIZE;
    texts.push(decoder.decode(readBridgeStringBytes(itemPtr)));
  }
  return texts;
}

function allocJoinedBridgeString(seqPtr, sep) {
  return allocBridgeString(readBridgeStringSeqTexts(seqPtr).join(sep));
}

function readSeqHeader(seqPtr) {
  const base = normalizeI32(seqPtr);
  if (base <= 0 || base + WASM_SEQ_HEADER_SIZE > memory.buffer.byteLength) {
    return { len: 0, cap: 0, bufferPtr: 0 };
  }
  const view = new DataView(memory.buffer);
  return {
    len: view.getInt32(base + WASM_SEQ_LEN_OFFSET, true),
    cap: view.getInt32(base + WASM_SEQ_CAP_OFFSET, true),
    bufferPtr: view.getUint32(base + WASM_SEQ_BUFFER_OFFSET, true),
  };
}

function writeSeqHeader(seqPtr, header) {
  const base = normalizeI32(seqPtr);
  if (base <= 0 || base + WASM_SEQ_HEADER_SIZE > memory.buffer.byteLength) {
    return false;
  }
  const view = new DataView(memory.buffer);
  view.setInt32(base + WASM_SEQ_LEN_OFFSET, normalizeI32(header.len), true);
  view.setInt32(base + WASM_SEQ_CAP_OFFSET, normalizeI32(header.cap), true);
  view.setUint32(base + WASM_SEQ_BUFFER_OFFSET, normalizeI32(header.bufferPtr), true);
  return true;
}

function growSeqSlot(seqPtr, idx, elemSize) {
  const seqBase = normalizeI32(seqPtr);
  const index = normalizeI32(idx);
  const elemBytes = normalizeI32(elemSize);
  if (seqBase <= 0 || index < 0 || elemBytes <= 0) {
    return 0;
  }
  const header = readSeqHeader(seqBase);
  let len = Math.max(0, normalizeI32(header.len));
  let cap = Math.max(0, normalizeI32(header.cap));
  let bufferPtr = normalizeI32(header.bufferPtr);
  const needLen = index + 1;
  if (needLen > cap || bufferPtr <= 0) {
    let newCap = cap < 4 ? 4 : cap;
    while (newCap < needLen) {
      const doubled = newCap * 2;
      if (doubled <= 0) {
        newCap = needLen;
        break;
      }
      newCap = doubled;
    }
    const oldBytes = Math.max(0, cap * elemBytes);
    const newBytes = Math.max(0, newCap * elemBytes);
    const newBufferPtr = allocRegion(newBytes, Math.min(Math.max(elemBytes, 1), 8));
    if (oldBytes > 0 && bufferPtr > 0) {
      copyBytesToWasmMemory(newBufferPtr, copyWasmMemoryBytes(bufferPtr, oldBytes), oldBytes);
    }
    if (newBytes > oldBytes) {
      fillWasmMemory(newBufferPtr + oldBytes, 0, newBytes - oldBytes);
    }
    bufferPtr = newBufferPtr;
    cap = newCap;
  }
  if (needLen > len) {
    len = needLen;
  }
  if (!writeSeqHeader(seqBase, { len, cap, bufferPtr })) {
    return 0;
  }
  return bufferPtr + index * elemBytes;
}

function registerBufferBytes(bytesValue) {
  const handle = nextBufferHandle++;
  bufferHandleToBytes.set(handle, new Uint8Array(bytesValue));
  return normalizeI32(handle);
}

function registerBufferText(text) {
  return registerBufferBytes(encoder.encode(String(text)));
}

function bufferHandleLen(handle) {
  return normalizeI32(bufferHandleToBytes.get(normalizeI32(handle))?.byteLength ?? 0);
}

function bufferHandleByte(handle, idx) {
  const bytesValue = bufferHandleToBytes.get(normalizeI32(handle));
  const index = normalizeI32(idx);
  if (!bytesValue || index < 0 || index >= bytesValue.byteLength) {
    return -1;
  }
  return normalizeI32(bytesValue[index] ?? 0);
}

function bufferHandleRelease(handle) {
  bufferHandleToBytes.delete(normalizeI32(handle));
  return 1;
}

function bufferHandleText(handle) {
  const bytesValue = bufferHandleToBytes.get(normalizeI32(handle)) ?? new Uint8Array(0);
  return decoder.decode(bytesValue);
}

function registerInputBytes(bytesValue) {
  if (!(bytesValue instanceof Uint8Array) || bytesValue.byteLength <= 0) {
    return 0;
  }
  const handle = nextInputHandle++;
  inputHandleToBytes.set(handle, new Uint8Array(bytesValue));
  return normalizeI32(handle);
}

function registerInputText(text) {
  const normalized = String(text).trim();
  if (!normalized) {
    return 0;
  }
  return registerInputBytes(encoder.encode(normalized));
}

function expectEqual(actual, expected, label) {
  if ((actual | 0) !== (expected | 0)) {
    throw new Error(`${label} mismatch: expected=${expected} actual=${actual}`);
  }
}

function expectText(actual, expected, label) {
  if (actual !== expected) {
    throw new Error(`${label} mismatch: expected=${JSON.stringify(expected)} actual=${JSON.stringify(actual)}`);
  }
}

WebAssembly.instantiate(bytes, {
  env: {
    memory,
    alloc: (size) => allocRegion(Math.max(0, normalizeI32(size)), 8),
    cheng_rawbytes_get_at: (base, idx) => {
      const bytesValue = copyWasmMemoryBytes(normalizeI32(base) + normalizeI32(idx), 1);
      return bytesValue.byteLength > 0 ? normalizeI32(bytesValue[0] ?? 0) : 0;
    },
    cheng_rawbytes_set_at: (base, idx, value) => {
      copyBytesToWasmMemory(normalizeI32(base) + normalizeI32(idx), new Uint8Array([normalizeI32(value) & 0xff]), 1);
    },
    cheng_str_param_to_cstring_compat: (textHandle) => allocCStringBytes(readBridgeStringBytes(textHandle)),
    cheng_cstrlen: (ptr) => normalizeI32(readCStringBytes(ptr).byteLength),
    ptr_add: (ptr, off) => normalizeI32(ptr) + normalizeI32(off),
    driver_c_new_string: (size) => {
      const ptr = allocRegion(Math.max(0, normalizeI32(size)) + 1, 1);
      fillWasmMemory(ptr, 0, Math.max(0, normalizeI32(size)) + 1);
      return ptr;
    },
    driver_c_new_string_copy_n: (raw, n) => allocCStringBytes(copyWasmMemoryBytes(raw, n)),
    driver_c_i32_to_str: (value) => allocCStringBytes(encoder.encode(String(normalizeI32(value)))),
    copyMem: (dest, src, size) => {
      const bytesValue = copyWasmMemoryBytes(src, size);
      copyBytesToWasmMemory(dest, bytesValue, bytesValue.byteLength);
    },
    setMem: (dest, value, size) => {
      fillWasmMemory(dest, value, size);
    },
    cheng_seq_set_grow: (seqPtr, idx, elemSize) => growSeqSlot(seqPtr, idx, elemSize),
    driver_c_str_concat_bridge: (leftHandle, rightHandle) =>
      allocBridgeString(
        decoder.decode(readBridgeStringBytes(leftHandle)) +
        decoder.decode(readBridgeStringBytes(rightHandle)),
      ),
    cheng_v3_strformat_fmt_bridge: (partsHandle) => allocJoinedBridgeString(partsHandle, ''),
    cheng_v3_strformat_lines_bridge: (partsHandle) => allocJoinedBridgeString(partsHandle, '\n'),
    cheng_bounds_check: (len, idx) => {
      if ((idx | 0) < 0 || (idx | 0) >= (len | 0)) {
        throw new Error(`cheng_bounds_check len=${len} idx=${idx}`);
      }
    },
    cheng_v3_sha256_hex_bridge: (textHandle) =>
      allocBridgeString(
        crypto.createHash('sha256').update(Buffer.from(readBridgeStringBytes(textHandle))).digest('hex'),
      ),
    driver_c_str_eq_bridge: (leftHandle, rightHandle) => {
      const left = readBridgeStringBytes(leftHandle);
      const right = readBridgeStringBytes(rightHandle);
      if (left.byteLength !== right.byteLength) {
        return 0;
      }
      for (let i = 0; i < left.byteLength; i += 1) {
        if ((left[i] ?? 0) !== (right[i] ?? 0)) {
          return 0;
        }
      }
      return 1;
    },
    cheng_seq_string_buffer_register_compat: () => {},
    cheng_v3_browser_now_ms_bridge: () => 24680,
    cheng_v3_browser_echo_i32_bridge: (value) => value | 0,
    cheng_v3_browser_peer_handle_bridge: () => 41,
    cheng_v3_browser_did_handle_bridge: () => 84,
    cheng_v3_browser_dm_active_count_bridge: () => 7,
    cheng_v3_browser_dm_connect_start_bridge: (peerHandle) => (peerHandle | 0) + 1000,
    cheng_v3_browser_dm_wait_start_bridge: (peerHandle, timeoutMs) => ((peerHandle | 0) ^ (timeoutMs | 0)) | 0,
    cheng_v3_browser_dm_send_start_bridge: (peerHandle, conversationHandle, messageHandle, timestampMs) =>
      ((peerHandle | 0) + (conversationHandle | 0) + (messageHandle | 0) + (timestampMs | 0)) | 0,
    cheng_v3_buffer_handle_from_text_bridge: (textHandle) => registerBufferBytes(readBridgeStringBytes(textHandle)),
    cheng_v3_browser_moq_fountain_last_error_store_bridge: (textHandle) => {
      lastMoqFountainErrorText = decoder.decode(readBridgeStringBytes(textHandle)).trim();
      return 1;
    },
    cheng_v3_browser_distributed_video_build_bridge: (a, b, c, d, e, f) => ((a + b + c + d + e + f) | 0),
    cheng_v3_browser_distributed_video_rebuild_bridge: (a, b, c, d, e) => ((a ^ b ^ c ^ d ^ e) | 0),
    cheng_v3_browser_distributed_video_last_error_handle_bridge: () => 501,
    cheng_v3_browser_input_len_bridge: (handle) => normalizeI32(inputHandleToBytes.get(normalizeI32(handle))?.byteLength ?? -1),
    cheng_v3_browser_input_copy_bridge: (handle, data, n) => {
      const bytesValue = inputHandleToBytes.get(normalizeI32(handle));
      if (!(bytesValue instanceof Uint8Array)) {
        return 0;
      }
      return copyBytesToWasmMemory(data, bytesValue, normalizeI32(n)) ? 1 : 0;
    },
    cheng_v3_buffer_handle_from_raw_bridge: (data, n) => registerBufferBytes(copyWasmMemoryBytes(data, n)),
    cheng_v3_browser_moq_fountain_last_error_handle_bridge: () => registerBufferText(lastMoqFountainErrorText),
    cheng_v3_browser_buffer_handle_len_bridge: (handle) => bufferHandleLen(handle),
    cheng_v3_browser_buffer_handle_byte_bridge: (handle, idx) => bufferHandleByte(handle, idx),
    cheng_v3_browser_buffer_handle_release_bridge: (handle) => bufferHandleRelease(handle),
  },
}).then(({ instance }) => {
  const exp = instance.exports;
  expectEqual(exp.main(), 0, 'main');
  expectEqual(exp.browserNowMs(), 24680, 'browserNowMs');
  expectEqual(exp.browserEchoI32(31337), 31337, 'browserEchoI32');
  expectEqual(exp.browserPeerHandle(), 41, 'browserPeerHandle');
  expectEqual(exp.browserDidHandle(), 84, 'browserDidHandle');
  expectEqual(exp.browserDmActiveCount(), 7, 'browserDmActiveCount');
  expectEqual(exp.browserDmConnectStart(9), 1009, 'browserDmConnectStart');
  expectEqual(exp.browserDmWaitStart(11, 22), (11 ^ 22) | 0, 'browserDmWaitStart');
  expectEqual(exp.browserDmSendStart(1, 2, 3, 4), 10, 'browserDmSendStart');
  expectEqual(exp.browserDistributedVideoBuild(1, 2, 3, 4, 5, 6), 21, 'browserDistributedVideoBuild');
  expectEqual(
    exp.browserDistributedVideoRebuild(10, 11, 12, 13, 14),
    (10 ^ 11 ^ 12 ^ 13 ^ 14) | 0,
    'browserDistributedVideoRebuild',
  );
  expectEqual(exp.browserDistributedVideoLastErrorHandle(), 501, 'browserDistributedVideoLastErrorHandle');
  expectEqual(exp.browserStrformatFmtLenProbe(), 9, 'browserStrformatFmtLenProbe');
  expectEqual(exp.browserStrformatLinesLenProbe(), 10, 'browserStrformatLinesLenProbe');
  expectEqual(exp.browserMoqLocalSeqBoxCountProbe(), 2, 'browserMoqLocalSeqBoxCountProbe');
  expectEqual(exp.browserMoqResultSeqBoxCountProbe(), 2, 'browserMoqResultSeqBoxCountProbe');
  expectEqual(exp.browserMoqInlinePrefixSeqBoxCountProbe(), 2, 'browserMoqInlinePrefixSeqBoxCountProbe');
  expectEqual(exp.browserMoqLocalPrefixSeqBoxCountProbe(), 2, 'browserMoqLocalPrefixSeqBoxCountProbe');
  expectEqual(exp.browserMoqResultPrefixSeqBoxCountProbe(), 2, 'browserMoqResultPrefixSeqBoxCountProbe');
  expectEqual(exp.browserMoqInlineTrailingSeqBoxCountProbe(), 2, 'browserMoqInlineTrailingSeqBoxCountProbe');
  expectEqual(exp.browserMoqLocalTrailingSeqBoxCountProbe(), 2, 'browserMoqLocalTrailingSeqBoxCountProbe');
  expectEqual(exp.browserMoqResultTrailingSeqBoxCountProbe(), 2, 'browserMoqResultTrailingSeqBoxCountProbe');
  expectEqual(exp.browserMoqLocalManualActualRemoteAckCountProbe(), 2, 'browserMoqLocalManualActualRemoteAckCountProbe');
  expectEqual(exp.browserMoqResultManualActualRemoteAckCountProbe(), 2, 'browserMoqResultManualActualRemoteAckCountProbe');
  expectEqual(exp.browserMoqLocalManualActualObjectCountProbe(), 1, 'browserMoqLocalManualActualObjectCountProbe');
  expectEqual(exp.browserMoqLocalManualActualDropletCountProbe(), 1, 'browserMoqLocalManualActualDropletCountProbe');
  expectEqual(exp.browserMoqSplitPeerCsvCountProbe(), 2, 'browserMoqSplitPeerCsvCountProbe');
  expectEqual(exp.browserMoqSplitPeerCsvSecondLenProbe(), 6, 'browserMoqSplitPeerCsvSecondLenProbe');
  expectEqual(exp.browserMoqAddBoundLiteralCountProbe(), 1, 'browserMoqAddBoundLiteralCountProbe');
  expectEqual(exp.browserMoqAddBoundTrimCountProbe(), 1, 'browserMoqAddBoundTrimCountProbe');
  expectEqual(exp.browserMoqAddVarTrimCountProbe(), 1, 'browserMoqAddVarTrimCountProbe');
  expectEqual(exp.browserMoqLocalMapTrimHardcodedCountProbe(), 2, 'browserMoqLocalMapTrimHardcodedCountProbe');
  expectEqual(exp.browserMoqPeerLiteralEqProbe(), 0, 'browserMoqPeerLiteralEqProbe');
  expectEqual(exp.browserMoqPeerTrimEqProbe(), 0, 'browserMoqPeerTrimEqProbe');
  expectEqual(exp.browserMoqAddedTrimDiffEqProbe(), 0, 'browserMoqAddedTrimDiffEqProbe');
  expectEqual(exp.browserMoqAddedTrimTwoCountProbe(), 2, 'browserMoqAddedTrimTwoCountProbe');
  expectEqual(exp.browserMoqLoopOutLenProbe(), 1, 'browserMoqLoopOutLenProbe');
  expectEqual(exp.browserMoqNestedDedupNoBreakCountProbe(), 2, 'browserMoqNestedDedupNoBreakCountProbe');
  expectEqual(exp.browserMoqNestedDedupLiteralCountProbe(), 2, 'browserMoqNestedDedupLiteralCountProbe');
  expectEqual(exp.browserMoqOuterInnerLiteralDedupCountProbe(), 2, 'browserMoqOuterInnerLiteralDedupCountProbe');
  expectEqual(exp.browserMoqLocalNormalizeHardcodedCountProbe(), 2, 'browserMoqLocalNormalizeHardcodedCountProbe');
  expectEqual(exp.browserMoqLocalNormalizeSplitCountProbe(), 2, 'browserMoqLocalNormalizeSplitCountProbe');
  expectEqual(exp.browserMoqNormalizePeerCsvCountProbe(), 2, 'browserMoqNormalizePeerCsvCountProbe');
  expectEqual(exp.browserMoqNormalizePeerCsvSecondLenProbe(), 6, 'browserMoqNormalizePeerCsvSecondLenProbe');
  expectEqual(exp.browserMoqLocalNormalizedPrefixSeqBoxCountProbe(), 2, 'browserMoqLocalNormalizedPrefixSeqBoxCountProbe');
  expectEqual(exp.browserMoqLocalNormalizedActualRemoteAckCountProbe(), 2, 'browserMoqLocalNormalizedActualRemoteAckCountProbe');
  expectEqual(exp.browserMoqResultLoopActualRemoteAckCountProbe(), 2, 'browserMoqResultLoopActualRemoteAckCountProbe');
  expectEqual(exp.browserMoqResultLoopActualObjectLenProbe(), 1, 'browserMoqResultLoopActualObjectLenProbe');
  if ((exp.browserMoqResultLoopActualDropletLenProbe() | 0) <= 0) {
    throw new Error('browserMoqResultLoopActualDropletLenProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildObjectCountProbe(), 1, 'browserMoqBuildObjectCountProbe');
  expectEqual(exp.browserMoqObjectSourceBlockCountProbe(), 1, 'browserMoqObjectSourceBlockCountProbe');
  if ((exp.browserMoqBuildHeaderCoreWireLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildHeaderCoreWireLenProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildRemoteAckCountProbe(), 2, 'browserMoqBuildRemoteAckCountProbe');
  expectEqual(exp.browserMoqBuildRemoteAckFirstLenProbe(), 6, 'browserMoqBuildRemoteAckFirstLenProbe');
  expectEqual(exp.browserMoqBuildLongPeerRemoteAckCountProbe(), 2, 'browserMoqBuildLongPeerRemoteAckCountProbe');
  expectEqual(exp.browserMoqBuildLongPeerRemoteAckFirstLenProbe(), 52, 'browserMoqBuildLongPeerRemoteAckFirstLenProbe');
  if ((exp.browserMoqBuildLongPeerWireLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongPeerWireLenProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildCopiedLongPeerRemoteAckCountProbe(), 2, 'browserMoqBuildCopiedLongPeerRemoteAckCountProbe');
  expectEqual(exp.browserMoqBuildCopiedLongPeerRemoteAckFirstLenProbe(), 52, 'browserMoqBuildCopiedLongPeerRemoteAckFirstLenProbe');
  expectEqual(exp.browserMoqBuildAllocCopiedLongPeerRemoteAckCountProbe(), 2, 'browserMoqBuildAllocCopiedLongPeerRemoteAckCountProbe');
  expectEqual(exp.browserMoqChooseUniqueBlockIndexesLen3Probe(), 2, 'browserMoqChooseUniqueBlockIndexesLen3Probe');
  if ((exp.browserMoqChooseUniqueBlockIndexesLastProbe() | 0) < 0) {
    throw new Error('browserMoqChooseUniqueBlockIndexesLastProbe mismatch');
  }
  expectEqual(exp.browserMoqChooseUniqueBlockIndexesLongSeedCountProbe(), 3, 'browserMoqChooseUniqueBlockIndexesLongSeedCountProbe');
  if ((exp.browserMoqChooseUniqueBlockIndexesLongSeedLastProbe() | 0) < 0) {
    throw new Error('browserMoqChooseUniqueBlockIndexesLongSeedLastProbe mismatch');
  }
  if ((exp.browserMoqBuildLongBlobObjectOnlyDropletCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobObjectOnlyDropletCountProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildLongBlobObjectsArrayCountProbe(), 1, 'browserMoqBuildLongBlobObjectsArrayCountProbe');
  if ((exp.browserMoqBuildLongBlobAppendDropletsCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobAppendDropletsCountProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildLongBlobManualResultObjectsOnlyCountProbe(), 1, 'browserMoqBuildLongBlobManualResultObjectsOnlyCountProbe');
  expectEqual(exp.browserMoqBuildLongBlobManualResultFullCountProbe(), 1, 'browserMoqBuildLongBlobManualResultFullCountProbe');
  expectEqual(exp.browserMoqBuildLongBlobComputedBundleCidLenProbe(), 64, 'browserMoqBuildLongBlobComputedBundleCidLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobLiteralLongBundleObjectNoRepairDropletCountProbe(), 3, 'browserMoqBuildLongBlobLiteralLongBundleObjectNoRepairDropletCountProbe');
  if ((exp.browserMoqBuildLongBlobLiteralLongBundleObjectDropletCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobLiteralLongBundleObjectDropletCountProbe mismatch');
  }
  if ((exp.browserMoqBuildLongBlobCopiedLongBundleObjectDropletCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobCopiedLongBundleObjectDropletCountProbe mismatch');
  }
  if ((exp.browserMoqBuildLongBlobComputedBundleObjectDropletCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobComputedBundleObjectDropletCountProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildLongBlobLoopObjectCountProbe(), 1, 'browserMoqBuildLongBlobLoopObjectCountProbe');
  if ((exp.browserMoqBuildLongBlobLoopDropletCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobLoopDropletCountProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildLongBlobLoopDirectObjectLenProbe(), 1, 'browserMoqBuildLongBlobLoopDirectObjectLenProbe');
  if ((exp.browserMoqBuildLongBlobLoopDirectDropletLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobLoopDirectDropletLenProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildLongBlobDerivedResultObjectCountProbe(), 1, 'browserMoqBuildLongBlobDerivedResultObjectCountProbe');
  if ((exp.browserMoqBuildLongBlobDerivedResultDropletCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobDerivedResultDropletCountProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildLongBlobPeersAfterObjectCountProbe(), 2, 'browserMoqBuildLongBlobPeersAfterObjectCountProbe');
  expectEqual(exp.browserMoqBuildLongBlobPeersAfterObjectFirstLenProbe(), 6, 'browserMoqBuildLongBlobPeersAfterObjectFirstLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobObjectCountProbe(), 1, 'browserMoqBuildLongBlobObjectCountProbe');
  if ((exp.browserMoqBuildLongBlobDropletCountProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobDropletCountProbe mismatch');
  }
  expectEqual(exp.browserMoqBuildLongBlobLongPeerRemoteAckCountProbe(), 2, 'browserMoqBuildLongBlobLongPeerRemoteAckCountProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerRemoteAckFirstLenProbe(), 52, 'browserMoqBuildLongBlobLongPeerRemoteAckFirstLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerRemoteAckFirstEqProbe(), 1, 'browserMoqBuildLongBlobLongPeerRemoteAckFirstEqProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerRemoteAckCsvLenProbe(), 105, 'browserMoqBuildLongBlobLongPeerRemoteAckCsvLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerWireRemoteAckCsvLenProbe(), 105, 'browserMoqBuildLongBlobLongPeerWireRemoteAckCsvLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerWireHasRemoteAckCsvProbe(), 1, 'browserMoqBuildLongBlobLongPeerWireHasRemoteAckCsvProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerWireHasObjectFieldLabelsProbe(), 1, 'browserMoqBuildLongBlobLongPeerWireHasObjectFieldLabelsProbe');
  expectEqual(exp.browserMoqBuildLongBlobObjectOnlyFirstDropletBlockIndexesLenProbe(), 1, 'browserMoqBuildLongBlobObjectOnlyFirstDropletBlockIndexesLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobObjectOnlyFirstDropletBlockIndex0ValueProbe(), 0, 'browserMoqBuildLongBlobObjectOnlyFirstDropletBlockIndex0ValueProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerObjectOnlyFirstDropletBlockIndexesLenProbe(), 1, 'browserMoqBuildLongBlobLongPeerObjectOnlyFirstDropletBlockIndexesLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobLongPeerObjectOnlyFirstDropletBlockIndex0ValueProbe(), 0, 'browserMoqBuildLongBlobLongPeerObjectOnlyFirstDropletBlockIndex0ValueProbe');
  expectEqual(exp.browserMoqPassDropletsFirstBlockIndexesLenProbe(), 1, 'browserMoqPassDropletsFirstBlockIndexesLenProbe');
  expectEqual(exp.browserMoqPassDropletsFirstBlockIndex0ValueProbe(), 0, 'browserMoqPassDropletsFirstBlockIndex0ValueProbe');
  expectEqual(exp.browserMoqAppendDropletsFirstBlockIndexesLenProbe(), 1, 'browserMoqAppendDropletsFirstBlockIndexesLenProbe');
  expectEqual(exp.browserMoqAppendDropletsFirstBlockIndex0ValueProbe(), 0, 'browserMoqAppendDropletsFirstBlockIndex0ValueProbe');
  expectEqual(exp.browserMoqAppendLongPeerDropletsFirstBlockIndexesLenProbe(), 1, 'browserMoqAppendLongPeerDropletsFirstBlockIndexesLenProbe');
  expectEqual(exp.browserMoqAppendLongPeerDropletsFirstBlockIndex0ValueProbe(), 0, 'browserMoqAppendLongPeerDropletsFirstBlockIndex0ValueProbe');
  expectEqual(exp.browserMoqBuildLongBlobFirstDropletBlockIndexesEqProbe(), 1, 'browserMoqBuildLongBlobFirstDropletBlockIndexesEqProbe');
  expectEqual(exp.browserMoqBuildLongBlobFirstDropletBlockIndex0ValueProbe(), 0, 'browserMoqBuildLongBlobFirstDropletBlockIndex0ValueProbe');
  expectEqual(exp.browserMoqBuildLongBlobFirstDropletBlockIndexesLocalLenProbe(), 1, 'browserMoqBuildLongBlobFirstDropletBlockIndexesLocalLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobFirstDropletBlockIndexesLocalValue0Probe(), 0, 'browserMoqBuildLongBlobFirstDropletBlockIndexesLocalValue0Probe');
  expectEqual(exp.browserMoqBuildLongBlobContextIntToStrZeroLenProbe(), 1, 'browserMoqBuildLongBlobContextIntToStrZeroLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobIndexedIntToStrLenProbe(), 1, 'browserMoqBuildLongBlobIndexedIntToStrLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobInlineRowsFirstLenProbe(), 1, 'browserMoqBuildLongBlobInlineRowsFirstLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobInlineRowsCountViaCalleeProbe(), 1, 'browserMoqBuildLongBlobInlineRowsCountViaCalleeProbe');
  expectEqual(exp.browserMoqBuildLongBlobInlineRowsFirstLenViaCalleeProbe(), 1, 'browserMoqBuildLongBlobInlineRowsFirstLenViaCalleeProbe');
  expectEqual(exp.browserMoqBuildLongBlobInlineRowsSingleReturnLenProbe(), 1, 'browserMoqBuildLongBlobInlineRowsSingleReturnLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobFirstDropletBlockIndexesLocalInlineCsvLenProbe(), 1, 'browserMoqBuildLongBlobFirstDropletBlockIndexesLocalInlineCsvLenProbe');
  {
    const localCsvLen = exp.browserMoqBuildLongBlobFirstDropletBlockIndexesLocalCsvLenProbe() | 0;
    if (localCsvLen !== 1) {
      const localCsvHandle = exp.browserMoqBuildLongBlobFirstDropletBlockIndexesLocalCsvHandleProbe() | 0;
      throw new Error(`browserMoqBuildLongBlobFirstDropletBlockIndexesLocalCsvLenProbe mismatch: ${bufferHandleText(localCsvHandle)}`);
    }
  }
  expectEqual(exp.browserMoqBuildLongBlobFirstDropletBlockIndexesCsvLenProbe(), 1, 'browserMoqBuildLongBlobFirstDropletBlockIndexesCsvLenProbe');
  expectEqual(exp.browserMoqBuildLongBlobFirstDropletBlockIndexesCsvByte0Probe(), '0'.charCodeAt(0), 'browserMoqBuildLongBlobFirstDropletBlockIndexesCsvByte0Probe');
  {
    const hasDropletLabels = exp.browserMoqBuildLongBlobLongPeerWireHasDropletFieldLabelsProbe() | 0;
    if (hasDropletLabels !== 1) {
      const wireHandle = exp.browserMoqBuildLongBlobLongPeerWireHandleProbe() | 0;
      throw new Error(`browserMoqBuildLongBlobLongPeerWireHasDropletFieldLabelsProbe mismatch: ${bufferHandleText(wireHandle)}`);
    }
  }
  if ((exp.browserMoqBuildLongBlobLongPeerWireLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildLongBlobLongPeerWireLenProbe mismatch');
  }
  if ((exp.browserMoqBuildRemoteAckCsvLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildRemoteAckCsvLenProbe mismatch');
  }
  if ((exp.browserMoqBuildHeaderWireLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildHeaderWireLenProbe mismatch');
  }
  if ((exp.browserMoqBuildObjectRowsWireLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildObjectRowsWireLenProbe mismatch');
  }
  if ((exp.browserMoqBuildDropletRowsWireLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildDropletRowsWireLenProbe mismatch');
  }
  if ((exp.browserMoqBuildWireLenProbe() | 0) <= 0) {
    throw new Error('browserMoqBuildWireLenProbe mismatch');
  }
  const buildErrorHandle = exp.browserMoqBuildErrorHandleProbe() | 0;
  const objectErrorHandle = exp.browserMoqObjectErrorHandleProbe() | 0;
  expectEqual(exp.browserBufferHandleLen(buildErrorHandle), 2, 'browserMoqBuildErrorHandleProbe.len');
  expectEqual(exp.browserBufferHandleLen(objectErrorHandle), 2, 'browserMoqObjectErrorHandleProbe.len');
  expectText(bufferHandleText(buildErrorHandle), 'ok', 'browserMoqBuildErrorHandleProbe.text');
  expectText(bufferHandleText(objectErrorHandle), 'ok', 'browserMoqObjectErrorHandleProbe.text');
  expectEqual(exp.browserBufferHandleByte(buildErrorHandle, 0), 'o'.charCodeAt(0), 'browserMoqBuildErrorHandleProbe.byte0');
  expectEqual(exp.browserBufferHandleByte(buildErrorHandle, 1), 'k'.charCodeAt(0), 'browserMoqBuildErrorHandleProbe.byte1');
  expectEqual(exp.browserBufferHandleRelease(buildErrorHandle), 1, 'browserBufferHandleRelease');
  expectEqual(exp.browserMoqPeerCsvCountProbe(), 2, 'browserMoqPeerCsvCountProbe');
  expectEqual(exp.browserMoqPeerCsvDirectCountProbe(), 2, 'browserMoqPeerCsvDirectCountProbe');
  expectEqual(exp.browserMoqPeerCsvSplitCountProbe(), 2, 'browserMoqPeerCsvSplitCountProbe');
  expectEqual(exp.browserMoqPeerCsvFirstPartLenProbe(), 6, 'browserMoqPeerCsvFirstPartLenProbe');
  expectEqual(exp.browserMoqPeerCsvFirstTrimmedLenProbe(), 6, 'browserMoqPeerCsvFirstTrimmedLenProbe');
  expectEqual(exp.browserMoqDirectSliceLenProbe(), 6, 'browserMoqDirectSliceLenProbe');
  expectEqual(exp.browserMoqDirectSliceEqProbe(), 1, 'browserMoqDirectSliceEqProbe');
  expectEqual(exp.browserMoqLiteralLenProbe(), 6, 'browserMoqLiteralLenProbe');
  expectEqual(exp.browserMoqLiteralEqProbe(), 1, 'browserMoqLiteralEqProbe');
  expectEqual(exp.browserMoqLocalSliceLenProbe(), 6, 'browserMoqLocalSliceLenProbe');
  expectEqual(exp.browserMoqSeqLiteralFirstLenProbe(), 6, 'browserMoqSeqLiteralFirstLenProbe');
  expectEqual(exp.browserMoqSeqSliceFirstLenProbe(), 6, 'browserMoqSeqSliceFirstLenProbe');
  expectEqual(exp.browserRangeLoopSumProbe(), 21, 'browserRangeLoopSumProbe');
  const blobHandle = registerInputBytes(encoder.encode('moq-fountain-smoke'));
  const ownerHandle = registerInputText('12D3KooWmxfnTuP2Gn5mhXTgeKiDHpKKQfVWKHeYruMuAVbkWmk8');
  const remoteHandle = registerInputText('12D3KooWiWLDFUvGDGRGfmTnfEEPzxuPw83ZnvNeSTWCNwQA4vy4,12D3KooW98NzVx7ZavtVbP9bT3hneTyz2j1kYaFZb54n8qbV5m9V');
  const mimeTypeHandle = registerInputText('video/mp4');
  expectEqual(exp.browserInputTextRoundtripLen(remoteHandle), 105, 'browserInputTextRoundtripLen.remoteHandle');
  expectEqual(exp.browserInputRemotePeerCount(remoteHandle), 2, 'browserInputRemotePeerCount.remoteHandle');
  expectEqual(exp.browserMoqFountainInputRemotePeerCount(ownerHandle, remoteHandle, mimeTypeHandle), 2, 'browserMoqFountainInputRemotePeerCount');
  expectEqual(exp.browserMoqFountainBuildRemotePeerCount(blobHandle, ownerHandle, remoteHandle, mimeTypeHandle, 1024, 8, 2), 2, 'browserMoqFountainBuildRemotePeerCount');
  expectEqual(exp.browserMoqFountainBuildWireRemotePeerCsvLen(blobHandle, ownerHandle, remoteHandle, mimeTypeHandle, 1024, 8, 2), 105, 'browserMoqFountainBuildWireRemotePeerCsvLen');
  const externalBuildHandle = exp.browserMoqFountainBuild(blobHandle, ownerHandle, remoteHandle, mimeTypeHandle, 1024, 8, 2) | 0;
  if (externalBuildHandle <= 0) {
    const externalErrorHandle = exp.browserMoqFountainLastErrorHandle() | 0;
    throw new Error(`browserMoqFountainBuild external failed: ${bufferHandleText(externalErrorHandle)}`);
  }
  const externalWire = bufferHandleText(externalBuildHandle);
  if (!externalWire.includes('remote_ack_peer_ids=12D3KooWiWLDFUvGDGRGfmTnfEEPzxuPw83ZnvNeSTWCNwQA4vy4,12D3KooW98NzVx7ZavtVbP9bT3hneTyz2j1kYaFZb54n8qbV5m9V')) {
    throw new Error(`browserMoqFountainBuild external wire mismatch: ${externalWire}`);
  }
  if (!externalWire.includes('object.0.group_id=')) {
    throw new Error(`browserMoqFountainBuild missing object group label: ${externalWire}`);
  }
  if (!externalWire.includes('object.0.object_id=object-0')) {
    throw new Error(`browserMoqFountainBuild missing object id label: ${externalWire}`);
  }
  if (!externalWire.includes('droplet.0.kind=source')) {
    throw new Error(`browserMoqFountainBuild missing droplet kind label: ${externalWire}`);
  }
  if (!externalWire.includes('droplet.0.block_indexes=0')) {
    throw new Error(`browserMoqFountainBuild missing droplet block indexes: ${externalWire}`);
  }
  console.log('v3 browser host wasm smoke: ok');
}).catch((error) => {
  console.error(error);
  process.exit(1);
});
