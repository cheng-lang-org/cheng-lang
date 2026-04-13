(function () {
  const textEncoder = new TextEncoder();
  const textDecoder = new TextDecoder();

  const SIGNAL_OFFER = 1;
  const SIGNAL_ANSWER = 2;
  const SIGNAL_CANDIDATE = 3;
  const SIGNAL_POLICY = 4;

  function ensureUint8Array(value) {
    if (value instanceof Uint8Array) {
      return value;
    }
    if (Array.isArray(value)) {
      return Uint8Array.from(value);
    }
    if (value instanceof ArrayBuffer) {
      return new Uint8Array(value);
    }
    if (ArrayBuffer.isView(value)) {
      return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    }
    return new Uint8Array(0);
  }

  function bytesEqual(left, right) {
    if (left.length !== right.length) {
      return false;
    }
    for (let i = 0; i < left.length; i += 1) {
      if (left[i] !== right[i]) {
        return false;
      }
    }
    return true;
  }

  function pushU32(out, value) {
    out.push((value >>> 24) & 255);
    out.push((value >>> 16) & 255);
    out.push((value >>> 8) & 255);
    out.push(value & 255);
  }

  function readU32(data, offset) {
    if (offset + 4 > data.length) {
      throw new Error("v3 browser webrtc: short u32");
    }
    return (((data[offset] << 24) >>> 0) |
            (data[offset + 1] << 16) |
            (data[offset + 2] << 8) |
            data[offset + 3]) >>> 0;
  }

  function pushBytes(out, bytes) {
    for (let i = 0; i < bytes.length; i += 1) {
      out.push(bytes[i]);
    }
  }

  function pushStr(out, text) {
    const bytes = textEncoder.encode(text);
    pushU32(out, bytes.length);
    pushBytes(out, bytes);
  }

  function readStr(data, offset) {
    const size = readU32(data, offset);
    const start = offset + 4;
    const end = start + size;
    if (end > data.length) {
      throw new Error("v3 browser webrtc: short string");
    }
    return {
      text: textDecoder.decode(data.slice(start, end)),
      next: end
    };
  }

  function decodePolicyMessage(policyBytes) {
    const data = ensureUint8Array(policyBytes);
    if (data.length < 36) {
      throw new Error("v3 browser webrtc: short policy");
    }
    let offset = 0;
    const kind = readU32(data, offset);
    offset += 4;
    if (kind !== SIGNAL_POLICY) {
      throw new Error("v3 browser webrtc: policy kind mismatch");
    }
    const sessionId = data.slice(offset, offset + 32);
    offset += 32;
    offset = readStr(data, offset).next;
    offset = readStr(data, offset).next;
    offset += 4;
    offset = readStr(data, offset).next;
    const relayOnly = readU32(data, offset) !== 0;
    offset += 4;
    const stunCount = readU32(data, offset);
    offset += 4;
    const stunUrls = [];
    for (let i = 0; i < stunCount; i += 1) {
      const stun = readStr(data, offset);
      stunUrls.push(stun.text);
      offset = stun.next;
    }
    const turnCount = readU32(data, offset);
    offset += 4;
    const turnServers = [];
    for (let i = 0; i < turnCount; i += 1) {
      const url = readStr(data, offset);
      offset = url.next;
      const username = readStr(data, offset);
      offset = username.next;
      const credential = readStr(data, offset);
      offset = credential.next;
      turnServers.push({
        url: url.text,
        username: username.text,
        credential: credential.text
      });
    }
    if (offset !== data.length) {
      throw new Error("v3 browser webrtc: policy trailing bytes");
    }
    return {
      kind,
      sessionId,
      relayOnly,
      stunUrls,
      turnServers
    };
  }

  function encodeSignalMessage(message) {
    const out = [];
    pushU32(out, message.kind >>> 0);
    pushBytes(out, ensureUint8Array(message.sessionId));
    pushStr(out, message.sdp || "");
    pushStr(out, message.candidateMid || "");
    pushU32(out, message.candidateMLineIndex || 0);
    pushStr(out, message.candidateText || "");
    pushU32(out, message.relayOnly ? 1 : 0);
    const stunUrls = message.stunUrls || [];
    pushU32(out, stunUrls.length);
    for (const stun of stunUrls) {
      pushStr(out, stun);
    }
    const turnServers = message.turnServers || [];
    pushU32(out, turnServers.length);
    for (const turn of turnServers) {
      pushStr(out, turn.url || "");
      pushStr(out, turn.username || "");
      pushStr(out, turn.credential || "");
    }
    return Uint8Array.from(out);
  }

  function encodeTranscript(messages) {
    const out = [];
    pushU32(out, messages.length);
    for (const message of messages) {
      const payload = encodeSignalMessage(message);
      pushU32(out, payload.length);
      pushBytes(out, payload);
    }
    return Uint8Array.from(out);
  }

  function parseCandidateLines(sdp) {
    if (!sdp) {
      return [];
    }
    const out = [];
    const lines = sdp.split(/\r?\n/);
    for (const line of lines) {
      if (line.startsWith("a=candidate:")) {
        out.push(line.slice(2));
      }
    }
    return out;
  }

  function buildIceServers(policy) {
    const out = [];
    if (policy.stunUrls.length > 0) {
      out.push({ urls: policy.stunUrls });
    }
    for (const server of policy.turnServers) {
      out.push({
        urls: server.url,
        username: server.username,
        credential: server.credential
      });
    }
    return out;
  }

  function rewriteCandidateLoopback(candidate) {
    if (!candidate || !candidate.candidate) {
      return {
        raw: "",
        rtc: candidate
      };
    }
    const parts = candidate.candidate.split(" ");
    if (parts.length < 8) {
      return {
        raw: candidate.candidate,
        rtc: candidate
      };
    }
    parts[4] = "127.0.0.1";
    const raw = parts.join(" ");
    return {
      raw,
      rtc: new RTCIceCandidate({
        candidate: raw,
        sdpMid: candidate.sdpMid,
        sdpMLineIndex: candidate.sdpMLineIndex,
        usernameFragment: candidate.usernameFragment
      })
    };
  }

  async function waitForIceGatheringComplete(peer) {
    if (peer.iceGatheringState === "complete") {
      return;
    }
    await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        cleanup();
        reject(new Error("v3 browser webrtc: ice gathering timeout"));
      }, 10000);
      function cleanup() {
        clearTimeout(timeout);
        peer.removeEventListener("icegatheringstatechange", onChange);
      }
      function onChange() {
        if (peer.iceGatheringState === "complete") {
          cleanup();
          resolve();
        }
      }
      peer.addEventListener("icegatheringstatechange", onChange);
    });
  }

  async function waitForChannelOpen(channel, label) {
    if (channel.readyState === "open") {
      return;
    }
    await new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        cleanup();
        reject(new Error(`v3 browser webrtc: ${label} open timeout`));
      }, 10000);
      function cleanup() {
        clearTimeout(timeout);
        channel.removeEventListener("open", onOpen);
        channel.removeEventListener("error", onError);
      }
      function onOpen() {
        cleanup();
        resolve();
      }
      function onError() {
        cleanup();
        reject(new Error(`v3 browser webrtc: ${label} open failed`));
      }
      channel.addEventListener("open", onOpen);
      channel.addEventListener("error", onError);
    });
  }

  function messageToBytes(data) {
    if (data instanceof ArrayBuffer) {
      return new Uint8Array(data);
    }
    if (ArrayBuffer.isView(data)) {
      return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }
    if (typeof data === "string") {
      return textEncoder.encode(data);
    }
    return new Uint8Array(0);
  }

  async function readNextMessage(channel, label) {
    return new Promise((resolve, reject) => {
      const timeout = setTimeout(() => {
        cleanup();
        reject(new Error(`v3 browser webrtc: ${label} message timeout`));
      }, 10000);
      function cleanup() {
        clearTimeout(timeout);
        channel.removeEventListener("message", onMessage);
        channel.removeEventListener("error", onError);
      }
      function onMessage(event) {
        cleanup();
        resolve(messageToBytes(event.data));
      }
      function onError() {
        cleanup();
        reject(new Error(`v3 browser webrtc: ${label} channel error`));
      }
      channel.addEventListener("message", onMessage);
      channel.addEventListener("error", onError);
    });
  }

  async function createSession(input) {
    const policyBytes = ensureUint8Array(input.policyBytes);
    const label = input.label && input.label.length > 0 ? input.label : "cheng-libp2p";

    const policy = decodePolicyMessage(policyBytes);
    const rtcConfig = {
      iceServers: buildIceServers(policy),
      iceTransportPolicy: policy.relayOnly ? "relay" : "all"
    };

    const clientPeer = new RTCPeerConnection(rtcConfig);
    const serverPeer = new RTCPeerConnection(rtcConfig);
    const clientCandidates = [];
    const serverCandidates = [];

    clientPeer.onicecandidate = (event) => {
      if (event.candidate) {
        const rewritten = rewriteCandidateLoopback(event.candidate);
        clientCandidates.push(rewritten.raw);
        serverPeer.addIceCandidate(rewritten.rtc).catch(() => {});
      }
    };
    serverPeer.onicecandidate = (event) => {
      if (event.candidate) {
        const rewritten = rewriteCandidateLoopback(event.candidate);
        serverCandidates.push(rewritten.raw);
        clientPeer.addIceCandidate(rewritten.rtc).catch(() => {});
      }
    };

    let resolveServerChannel;
    const serverChannelPromise = new Promise((resolve) => {
      resolveServerChannel = resolve;
    });
    serverPeer.ondatachannel = (event) => {
      resolveServerChannel(event.channel);
    };

    const clientChannel = clientPeer.createDataChannel(label);
    clientChannel.binaryType = "arraybuffer";

    const offer = await clientPeer.createOffer();
    await clientPeer.setLocalDescription(offer);
    await serverPeer.setRemoteDescription(offer);
    const answer = await serverPeer.createAnswer();
    await serverPeer.setLocalDescription(answer);
    await clientPeer.setRemoteDescription(answer);

    await Promise.all([
      waitForIceGatheringComplete(clientPeer),
      waitForIceGatheringComplete(serverPeer)
    ]);

    const serverChannel = await Promise.race([
      serverChannelPromise,
      new Promise((_, reject) => setTimeout(() => reject(new Error("v3 browser webrtc: server channel timeout")), 10000))
    ]);
    serverChannel.binaryType = "arraybuffer";

    await Promise.all([
      waitForChannelOpen(clientChannel, "client channel"),
      waitForChannelOpen(serverChannel, "server channel")
    ]);

    if (clientCandidates.length === 0) {
      clientCandidates.push(...parseCandidateLines(clientPeer.localDescription && clientPeer.localDescription.sdp));
    }
    if (serverCandidates.length === 0) {
      serverCandidates.push(...parseCandidateLines(serverPeer.localDescription && serverPeer.localDescription.sdp));
    }
    if (clientCandidates.length === 0 || serverCandidates.length === 0) {
      throw new Error("v3 browser webrtc: candidate missing");
    }
    if (policy.relayOnly) {
      const relayReady = clientCandidates[0].includes(" typ relay") && serverCandidates[0].includes(" typ relay");
      if (!relayReady) {
        throw new Error("v3 browser webrtc: relay-only requires working TURN");
      }
    }

    const transcript = encodeTranscript([
      {
        kind: SIGNAL_POLICY,
        sessionId: policy.sessionId,
        sdp: "",
        candidateMid: "",
        candidateMLineIndex: 0,
        candidateText: "",
        relayOnly: policy.relayOnly,
        stunUrls: policy.stunUrls,
        turnServers: policy.turnServers
      },
      {
        kind: SIGNAL_OFFER,
        sessionId: policy.sessionId,
        sdp: clientPeer.localDescription ? clientPeer.localDescription.sdp || "" : "",
        candidateMid: "",
        candidateMLineIndex: 0,
        candidateText: "",
        relayOnly: false,
        stunUrls: [],
        turnServers: []
      },
      {
        kind: SIGNAL_ANSWER,
        sessionId: policy.sessionId,
        sdp: serverPeer.localDescription ? serverPeer.localDescription.sdp || "" : "",
        candidateMid: "",
        candidateMLineIndex: 0,
        candidateText: "",
        relayOnly: false,
        stunUrls: [],
        turnServers: []
      },
      {
        kind: SIGNAL_CANDIDATE,
        sessionId: policy.sessionId,
        sdp: "",
        candidateMid: "0",
        candidateMLineIndex: 0,
        candidateText: clientCandidates[0],
        relayOnly: false,
        stunUrls: [],
        turnServers: []
      },
      {
        kind: SIGNAL_CANDIDATE,
        sessionId: policy.sessionId,
        sdp: "",
        candidateMid: "0",
        candidateMLineIndex: 0,
        candidateText: serverCandidates[0],
        relayOnly: false,
        stunUrls: [],
        turnServers: []
      }
    ]);

    return {
      signalBytes: transcript,
      async requestResponse(protocolBytesInput, requestBytesInput, responseBytesInput) {
        const protocolBytes = ensureUint8Array(protocolBytesInput);
        const requestBytes = ensureUint8Array(requestBytesInput);
        const responseBytes = ensureUint8Array(responseBytesInput);
        if (protocolBytes.length > 0) {
          const serverProtocolPromise = readNextMessage(serverChannel, "server protocol");
          clientChannel.send(protocolBytes);
          const serverProtocol = await serverProtocolPromise;
          if (!bytesEqual(serverProtocol, protocolBytes)) {
            throw new Error("v3 browser webrtc: protocol request mismatch");
          }
          const clientProtocolPromise = readNextMessage(clientChannel, "client protocol");
          serverChannel.send(protocolBytes);
          const clientProtocol = await clientProtocolPromise;
          if (!bytesEqual(clientProtocol, protocolBytes)) {
            throw new Error("v3 browser webrtc: protocol echo mismatch");
          }
        }
        const serverRequestPromise = readNextMessage(serverChannel, "server request");
        clientChannel.send(requestBytes);
        const serverRequest = await serverRequestPromise;
        if (!bytesEqual(serverRequest, requestBytes)) {
          throw new Error("v3 browser webrtc: request payload mismatch");
        }
        const clientResponsePromise = readNextMessage(clientChannel, "client response");
        serverChannel.send(responseBytes);
        return clientResponsePromise;
      },
      async close() {
        try {
          clientChannel.close();
        } catch {}
        try {
          serverChannel.close();
        } catch {}
        try {
          clientPeer.close();
        } catch {}
        try {
          serverPeer.close();
        } catch {}
      }
    };
  }

  let activeSession = null;

  async function openSession(input) {
    if (activeSession) {
      await activeSession.close();
      activeSession = null;
    }
    activeSession = await createSession(input);
    return {
      signalBytes: Array.from(activeSession.signalBytes)
    };
  }

  async function exchangeSession(input) {
    if (!activeSession) {
      throw new Error("v3 browser webrtc: session missing");
    }
    const responseBytes = await activeSession.requestResponse(input.protocolBytes,
                                                              input.requestBytes,
                                                              input.responseBytes);
    return {
      responseBytes: Array.from(responseBytes)
    };
  }

  async function closeSession() {
    if (!activeSession) {
      return { ok: true };
    }
    await activeSession.close();
    activeSession = null;
    return { ok: true };
  }

  async function runBridge(input) {
    const openResult = await openSession({
      policyBytes: input.policyBytes,
      label: input.label
    });
    let clientResponse;
    try {
      const exchangeResult = await exchangeSession({
        protocolBytes: input.protocolBytes,
        requestBytes: input.requestBytes,
        responseBytes: input.responseBytes
      });
      clientResponse = exchangeResult.responseBytes;
    } finally {
      await closeSession();
    }
    return {
      signalBytes: openResult.signalBytes,
      responseBytes: clientResponse
    };
  }

  window.v3BrowserWebrtcDatachannelRuntime = {
    createSession,
    openSession,
    exchangeSession,
    closeSession,
    runBridge
  };
})();
