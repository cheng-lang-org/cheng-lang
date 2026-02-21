const DEFAULT_IMPORT_MODULE = "cheng_web";

export class ChengWasmHost {
  constructor(options = {}) {
    this.importModule = options.importModule || DEFAULT_IMPORT_MODULE;
    this.root = options.root || (typeof document !== "undefined" ? document.body : null);
    this.defaultRootSelector = options.defaultRootSelector || "";
    this.nodes = [null];
    this.nodeHandles = new WeakMap();
    this.eventHandlers = new Map();
    this.timers = new Map();
    this.fetches = new Map();
    this.historyHandlers = new Map();
    this.nextTimer = 1;
    this.nextFetch = 1;
    this.textDecoder = new TextDecoder("utf-8");
    this.textEncoder = new TextEncoder();
    this.instance = null;
    this.exports = null;
    this.exportCache = new Map();
    this.rootHandle = 0;
  }

  async load(url, importOverrides = {}) {
    const imports = this._mergeImports(this._buildImports(), importOverrides);
    let source = url;
    if (typeof source === "string") {
      const res = await fetch(source);
      if (!res.ok) {
        throw new Error(`WASM fetch failed: ${res.status}`);
      }
      if (WebAssembly.instantiateStreaming) {
        try {
          const streamed = await WebAssembly.instantiateStreaming(res, imports);
          this._setInstance(streamed.instance);
          return this;
        } catch {
          source = await res.arrayBuffer();
        }
      } else {
        source = await res.arrayBuffer();
      }
    }
    if (source instanceof WebAssembly.Module) {
      const result = await WebAssembly.instantiate(source, imports);
      this._setInstance(result);
      return this;
    }
    const result = await WebAssembly.instantiate(source, imports);
    this._setInstance(result.instance);
    return this;
  }

  setRoot(root) {
    this.root = root || null;
    this.rootHandle = 0;
  }

  setRootFromSelector(selector) {
    if (!selector || typeof document === "undefined") {
      return;
    }
    const node = document.querySelector(selector);
    if (node) {
      this.root = node;
      this.rootHandle = 0;
    }
  }

  mount(selector = "") {
    const mountFn = this._resolveExport("cwc_mount");
    if (!mountFn) {
      return 0;
    }
    const finalSelector = selector || this.defaultRootSelector;
    if (finalSelector) {
      this.setRootFromSelector(finalSelector);
    }
    const { ptr, len } = this.allocString(finalSelector);
    const handle = mountFn(ptr, len);
    this.free(ptr, len);
    return handle;
  }

  init() {
    const initFn = this._resolveExport("cwc_init");
    if (initFn) {
      initFn();
    }
  }

  update(handle) {
    const updateFn = this._resolveExport("cwc_update");
    if (updateFn) {
      updateFn(handle || 0);
    }
  }

  unmount(handle) {
    const unmountFn = this._resolveExport("cwc_unmount");
    if (unmountFn) {
      unmountFn(handle || 0);
    }
  }

  allocString(text) {
    const bytes = this.textEncoder.encode(text || "");
    const len = bytes.length;
    const ptr = len > 0 ? this.alloc(len) : 0;
    if (len > 0) {
      this._memoryView().set(bytes, ptr);
    }
    return { ptr, len };
  }

  readString(ptr, len) {
    if (!ptr || !len) {
      return "";
    }
    const slice = this._memoryView().subarray(ptr, ptr + len);
    return this.textDecoder.decode(slice);
  }

  alloc(size) {
    if (!this.exports) {
      throw new Error("WASM exports not ready");
    }
    const fn = this._resolveExport("alloc") || this._resolveExport("malloc");
    if (!fn) {
      throw new Error("alloc is not exported");
    }
    return fn(size);
  }

  free(ptr, size) {
    if (!this.exports || !ptr) {
      return;
    }
    const fn = this._resolveExport("free") || this._resolveExport("dealloc");
    if (fn) {
      fn(ptr, size || 0);
    }
  }

  _memoryView() {
    if (!this.exports) {
      throw new Error("memory export missing");
    }
    const memory = this.exports.memory || this.exports["memory.0"];
    if (!memory) {
      throw new Error("memory export missing");
    }
    return new Uint8Array(memory.buffer);
  }

  _ensureDom() {
    if (typeof document === "undefined") {
      throw new Error("DOM is not available in this environment");
    }
  }

  _currentPath() {
    if (typeof location === "undefined") {
      return "/";
    }
    const path = `${location.pathname || ""}${location.search || ""}${location.hash || ""}`;
    return path.length > 0 ? path : "/";
  }

  _encodeEvent(event) {
    if (!event) {
      return "";
    }
    const payload = {
      type: event.type,
      timeStamp: event.timeStamp
    };
    if ("key" in event) {
      payload.key = event.key;
      payload.code = event.code;
      payload.altKey = event.altKey;
      payload.ctrlKey = event.ctrlKey;
      payload.shiftKey = event.shiftKey;
      payload.metaKey = event.metaKey;
    }
    if ("clientX" in event) {
      payload.clientX = event.clientX;
      payload.clientY = event.clientY;
      payload.button = event.button;
      payload.buttons = event.buttons;
    }
    if ("deltaY" in event) {
      payload.deltaX = event.deltaX;
      payload.deltaY = event.deltaY;
      payload.deltaMode = event.deltaMode;
    }
    if ("path" in event) {
      payload.path = event.path;
    }
    const target = event.target;
    if (target && typeof target === "object") {
      if ("value" in target) {
        payload.value = target.value;
      }
      if ("checked" in target) {
        payload.checked = target.checked;
      }
    }
    return JSON.stringify(payload);
  }

  _dispatchEvent(handlerId, event) {
    const dispatchFn = this._resolveExport("host_event_dispatch");
    if (!dispatchFn) {
      return;
    }
    const payload = this._encodeEvent(event);
    if (!payload) {
      dispatchFn(handlerId, 0, 0);
      return;
    }
    const { ptr, len } = this.allocString(payload);
    dispatchFn(handlerId, ptr, len);
    this.free(ptr, len);
  }

  _mergeImports(base, overrides) {
    const out = { ...base };
    for (const [key, value] of Object.entries(overrides)) {
      out[key] = { ...(out[key] || {}), ...value };
    }
    return out;
  }

  _buildImports() {
    const api = {
      dom_create_element: (tagPtr, tagLen) => {
        this._ensureDom();
        const tag = this.readString(tagPtr, tagLen);
        const node = document.createElement(tag);
        return this._internNode(node);
      },
      dom_create_text: (textPtr, textLen) => {
        this._ensureDom();
        const text = this.readString(textPtr, textLen);
        const node = document.createTextNode(text);
        return this._internNode(node);
      },
      dom_get_root: () => {
        this._ensureDom();
        const node = this.root || document.body;
        if (!node) return 0;
        if (this.rootHandle) {
          return this.rootHandle;
        }
        this.rootHandle = this._internNode(node);
        return this.rootHandle;
      },
      dom_query_selector: (selectorPtr, selectorLen) => {
        this._ensureDom();
        const selector = this.readString(selectorPtr, selectorLen);
        if (!selector) return 0;
        const scope = this.root && this.root.querySelector ? this.root : document;
        const node = scope.querySelector(selector);
        return node ? this._internNode(node) : 0;
      },
      dom_set_attr: (nodeId, namePtr, nameLen, valuePtr, valueLen) => {
        const node = this._resolveNode(nodeId);
        if (!node) return;
        const name = this.readString(namePtr, nameLen);
        const value = this.readString(valuePtr, valueLen);
        node.setAttribute(name, value);
      },
      dom_set_prop: (nodeId, namePtr, nameLen, valuePtr, valueLen) => {
        const node = this._resolveNode(nodeId);
        if (!node) return;
        const name = this.readString(namePtr, nameLen);
        const value = this.readString(valuePtr, valueLen);
        node[name] = value;
      },
      dom_set_style: (nodeId, namePtr, nameLen, valuePtr, valueLen) => {
        const node = this._resolveNode(nodeId);
        if (!node || !node.style) return;
        const name = this.readString(namePtr, nameLen);
        const value = this.readString(valuePtr, valueLen);
        node.style.setProperty(name, value);
      },
      dom_text_set: (nodeId, textPtr, textLen) => {
        const node = this._resolveNode(nodeId);
        if (!node) return;
        const text = this.readString(textPtr, textLen);
        node.nodeValue = text;
      },
      dom_insert: (parentId, nodeId, anchorId) => {
        const parent = this._resolveNode(parentId) || this.root || (typeof document !== "undefined" ? document.body : null);
        const node = this._resolveNode(nodeId);
        const anchor = this._resolveNode(anchorId);
        if (!parent || !node) return;
        parent.insertBefore(node, anchor || null);
      },
      dom_remove: (nodeId) => {
        const node = this._resolveNode(nodeId);
        if (!node || !node.parentNode) return;
        node.parentNode.removeChild(node);
      },
      dom_replace: (oldId, newId) => {
        const oldNode = this._resolveNode(oldId);
        const newNode = this._resolveNode(newId);
        if (!oldNode || !oldNode.parentNode || !newNode) return;
        oldNode.parentNode.replaceChild(newNode, oldNode);
      },
      dom_add_event: (nodeId, namePtr, nameLen, handlerId) => {
        const node = this._resolveNode(nodeId);
        if (!node) return;
        const name = this.readString(namePtr, nameLen);
        this._removeHandler(handlerId);
        const listener = (event) => {
          this._dispatchEvent(handlerId, event);
        };
        node.addEventListener(name, listener);
        this.eventHandlers.set(handlerId, { node, name, listener });
      },
      dom_remove_event: (nodeId, namePtr, nameLen, handlerId) => {
        const node = this._resolveNode(nodeId);
        if (!node) return;
        const name = this.readString(namePtr, nameLen);
        this._removeHandler(handlerId, node, name);
      },
      host_log: (level, msgPtr, msgLen) => {
        const msg = this.readString(msgPtr, msgLen);
        if (level >= 2) {
          console.error(msg);
        } else if (level === 1) {
          console.warn(msg);
        } else {
          console.log(msg);
        }
      },
      host_now: () => {
        if (typeof performance !== "undefined" && performance.now) {
          return performance.now();
        }
        return Date.now();
      },
      host_set_timeout: (handlerId, ms) => {
        const id = this.nextTimer++;
        const timer = setTimeout(() => {
          this._dispatchEvent(handlerId, null);
          this.timers.delete(id);
        }, ms);
        this.timers.set(id, timer);
        return id;
      },
      host_clear_timeout: (timerId) => {
        const timer = this.timers.get(timerId);
        if (timer) {
          clearTimeout(timer);
          this.timers.delete(timerId);
        }
      },
      host_fetch: (reqPtr, reqLen) => {
        const raw = this.readString(reqPtr, reqLen);
        let url = raw;
        let init = {};
        if (raw && raw[0] === "{") {
          try {
            const parsed = JSON.parse(raw);
            if (parsed && parsed.url) {
              url = parsed.url;
            }
            if (parsed && parsed.method) {
              init.method = parsed.method;
            }
            if (parsed && parsed.headers) {
              init.headers = parsed.headers;
            }
            if (parsed && parsed.body) {
              init.body = parsed.body;
            }
          } catch {
            url = raw;
          }
        }
        const id = this.nextFetch++;
        const entry = { status: 0, body: null, done: false, error: null, offset: 0 };
        this.fetches.set(id, entry);
        fetch(url, init)
          .then((res) => res.arrayBuffer().then((buf) => {
            entry.status = res.status;
            entry.body = new Uint8Array(buf);
            entry.done = true;
          }))
          .catch((err) => {
            entry.error = err;
            entry.done = true;
          });
        return id;
      },
      host_fetch_status: (reqId) => {
        const entry = this.fetches.get(reqId);
        if (!entry) return -1;
        if (!entry.done) return 0;
        if (entry.error) return -1;
        return entry.status || 0;
      },
      host_fetch_body: (reqId, outPtr, outLen) => {
        const entry = this.fetches.get(reqId);
        if (!entry || !entry.done || entry.error || !entry.body) return 0;
        const bytes = entry.body;
        const offset = entry.offset || 0;
        const remaining = bytes.length - offset;
        if (remaining <= 0) {
          this.fetches.delete(reqId);
          return 0;
        }
        const writeLen = Math.min(outLen, remaining);
        this._memoryView().set(bytes.subarray(offset, offset + writeLen), outPtr);
        entry.offset = offset + writeLen;
        if (entry.offset >= bytes.length) {
          this.fetches.delete(reqId);
        }
        return writeLen;
      },
      host_history_listen: (handlerId) => {
        if (typeof window === "undefined") return;
        if (!handlerId) return;
        if (this.historyHandlers.has(handlerId)) return;
        const listener = () => {
          this._dispatchEvent(handlerId, { type: "popstate", path: this._currentPath() });
        };
        window.addEventListener("popstate", listener);
        window.addEventListener("hashchange", listener);
        this.historyHandlers.set(handlerId, { listener });
        listener();
      },
      host_history_unlisten: (handlerId) => {
        if (typeof window === "undefined") return;
        const entry = this.historyHandlers.get(handlerId);
        if (!entry) return;
        window.removeEventListener("popstate", entry.listener);
        window.removeEventListener("hashchange", entry.listener);
        this.historyHandlers.delete(handlerId);
      },
      host_history_push: (pathPtr, pathLen) => {
        if (typeof history === "undefined") return;
        const next = this.readString(pathPtr, pathLen) || "/";
        history.pushState({}, "", next);
      },
      host_history_replace: (pathPtr, pathLen) => {
        if (typeof history === "undefined") return;
        const next = this.readString(pathPtr, pathLen) || "/";
        history.replaceState({}, "", next);
      }
    };

    return { [this.importModule]: api };
  }

  _internNode(node) {
    const existing = this.nodeHandles.get(node);
    if (existing) {
      return existing;
    }
    const handle = this.nodes.length;
    this.nodes.push(node);
    this.nodeHandles.set(node, handle);
    return handle;
  }

  _resolveNode(handle) {
    if (!handle) return null;
    return this.nodes[handle] || null;
  }

  _removeHandler(handlerId, expectedNode, expectedName) {
    const entry = this.eventHandlers.get(handlerId);
    if (!entry) return;
    if (expectedNode && entry.node !== expectedNode) return;
    if (expectedName && entry.name !== expectedName) return;
    entry.node.removeEventListener(entry.name, entry.listener);
    this.eventHandlers.delete(handlerId);
  }

  _resolveExport(name) {
    if (!this.exports) {
      return null;
    }
    if (this.exportCache.has(name)) {
      return this.exportCache.get(name);
    }
    let fn = this.exports[name] || this.exports[`${name}.0`];
    if (!fn) {
      const suffix = `__${name}.0`;
      for (const key of Object.keys(this.exports)) {
        if (key === `${name}.0` || key.startsWith(`${name}__`) || key.endsWith(suffix)) {
          fn = this.exports[key];
          break;
        }
      }
    }
    this.exportCache.set(name, fn || null);
    return fn;
  }

  _setInstance(instance) {
    this.instance = instance;
    this.exports = instance.exports || instance;
    this.exportCache.clear();
  }
}
