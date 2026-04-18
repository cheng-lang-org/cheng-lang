#!/usr/bin/env node
import fs from 'node:fs'
import os from 'node:os'
import path from 'node:path'
import { spawn } from 'node:child_process'

function parseArgs(argv) {
  const out = {
    chrome: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    baseUrl: 'http://127.0.0.1:4173/',
    route: '',
    outDir: '',
    width: 360,
    height: 800,
    truthFlag: '1',
    debugPort: 0,
    waitMs: 20000,
    summaryOut: '',
    screenshotOut: '',
  }
  for (let i = 2; i < argv.length; i += 1) {
    const a = argv[i]
    if (a === '--chrome') out.chrome = String(argv[++i] || out.chrome)
    else if (a === '--base-url') out.baseUrl = String(argv[++i] || out.baseUrl)
    else if (a === '--route') out.route = String(argv[++i] || '')
    else if (a === '--out-dir') out.outDir = String(argv[++i] || '')
    else if (a === '--width') out.width = Number(argv[++i] || out.width)
    else if (a === '--height') out.height = Number(argv[++i] || out.height)
    else if (a === '--truth-flag') out.truthFlag = String(argv[++i] || out.truthFlag)
    else if (a === '--debug-port') out.debugPort = Number(argv[++i] || out.debugPort)
    else if (a === '--wait-ms') out.waitMs = Number(argv[++i] || out.waitMs)
    else if (a === '--summary-out') out.summaryOut = String(argv[++i] || '')
    else if (a === '--screenshot-out') out.screenshotOut = String(argv[++i] || '')
    else if (a === '-h' || a === '--help') {
      console.log(
        'Usage: r2c-react-v3-truth-runtime.mjs --route <state> --out-dir <dir> ' +
        '[--base-url <url>] [--chrome <bin>] [--width <n>] [--height <n>] ' +
        '[--truth-flag <0|1>] [--debug-port <n>] [--wait-ms <n>] [--summary-out <file>] [--screenshot-out <file>]'
      )
      process.exit(0)
    }
  }
  return out
}

function writeSidecarSummary(summaryPath, values) {
  if (!summaryPath) return
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`)
  fs.mkdirSync(path.dirname(summaryPath), { recursive: true })
  fs.writeFileSync(summaryPath, `${lines.join('\n')}\n`, 'utf8')
}

async function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

async function assertBaseUrlReachable(baseUrl, waitMs) {
  const timeoutMs = Math.max(1000, Math.min(Number(waitMs) || 0, 5000))
  const controller = new AbortController()
  const timer = setTimeout(() => controller.abort(), timeoutMs)
  try {
    await fetch(baseUrl, {
      method: 'GET',
      redirect: 'manual',
      signal: controller.signal,
    })
  } catch (error) {
    const reason = error instanceof Error ? error.message : String(error)
    throw new Error(`truth_base_url_unreachable:${baseUrl}:${reason}`)
  } finally {
    clearTimeout(timer)
  }
}

function buildUrl(baseUrl, route, truthFlag) {
  const query = new URLSearchParams({
    r2c_truth: truthFlag,
    r2c_route: route,
  })
  return `${baseUrl}${baseUrl.includes('?') ? '&' : '?'}${query.toString()}`
}

async function waitForTarget(port, baseUrl, waitMs) {
  const deadline = Date.now() + waitMs
  while (Date.now() < deadline) {
    try {
      const res = await fetch(`http://127.0.0.1:${port}/json/list`)
      const arr = await res.json()
      const target = arr.find((item) => item.type === 'page' && String(item.url).startsWith(baseUrl))
      if (target) return target
    } catch {
    }
    await sleep(250)
  }
  throw new Error('cdp_target_not_found')
}

async function waitForDevToolsPort(userDataDir, chromeProc, waitMs, chromeStderrRef) {
  const deadline = Date.now() + waitMs
  const activePortPath = path.join(userDataDir, 'DevToolsActivePort')
  while (Date.now() < deadline) {
    if (chromeProc.exitCode !== null) {
      const extra = chromeStderrRef.value ? ` ${chromeStderrRef.value}` : ''
      throw new Error(`chrome_exited_before_devtools${extra}`.trim())
    }
    try {
      const raw = fs.readFileSync(activePortPath, 'utf8')
      const lines = raw.split(/\r?\n/)
      const parsed = Number(lines[0] || 0)
      if (parsed > 0) return parsed
    } catch {
    }
    await sleep(100)
  }
  const extra = chromeStderrRef.value ? ` ${chromeStderrRef.value}` : ''
  throw new Error(`devtools_active_port_missing${extra}`.trim())
}

async function waitForProcessExit(proc, waitMs) {
  if (proc.exitCode !== null) return true
  const exitPromise = new Promise((resolve) => {
    proc.once('exit', () => resolve(true))
  })
  const timeoutPromise = new Promise((resolve) => {
    setTimeout(() => resolve(false), waitMs)
  })
  return await Promise.race([exitPromise, timeoutPromise])
}

async function shutdownChrome(proc) {
  if (proc.exitCode !== null) return
  try {
    proc.kill('SIGTERM')
  } catch {
  }
  const exited = await waitForProcessExit(proc, 1500)
  if (exited) return
  if (proc.exitCode !== null) return
  try {
    proc.kill('SIGKILL')
  } catch {
  }
  await waitForProcessExit(proc, 1500)
}

async function captureRuntime(args) {
  if (!args.route) throw new Error('missing --route')
  if (!args.outDir) throw new Error('missing --out-dir')
  if (!fs.existsSync(args.chrome)) throw new Error(`chrome not found: ${args.chrome}`)
  fs.mkdirSync(args.outDir, { recursive: true })
  await assertBaseUrlReachable(args.baseUrl, args.waitMs)

  const url = buildUrl(args.baseUrl, args.route, args.truthFlag)
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'r2c-truth-runtime-'))
  const chromeProc = spawn(args.chrome, [
    `--remote-debugging-port=${args.debugPort > 0 ? args.debugPort : 0}`,
    '--headless=new',
    '--disable-gpu',
    '--hide-scrollbars',
    '--no-first-run',
    `--user-data-dir=${userDataDir}`,
    `--window-size=${args.width},${args.height}`,
    url,
  ], { stdio: ['ignore', 'ignore', 'pipe'] })
  let chromeStderr = ''
  const chromeStderrRef = { value: '' }
  chromeProc.stderr.on('data', (chunk) => {
    chromeStderr += chunk.toString()
    chromeStderrRef.value = chromeStderr
  })

  let ws = null
  try {
    const debugPort = args.debugPort > 0
      ? args.debugPort
      : await waitForDevToolsPort(userDataDir, chromeProc, args.waitMs, chromeStderrRef)
    const target = await waitForTarget(debugPort, args.baseUrl, args.waitMs)
    ws = new WebSocket(target.webSocketDebuggerUrl)
    let seq = 0
    const pending = new Map()
    const consoleMessages = []
    const exceptionMessages = []
    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data)
      if (msg.method === 'Runtime.consoleAPICalled') {
        consoleMessages.push({
          type: msg.params?.type || '',
          args: Array.isArray(msg.params?.args)
            ? msg.params.args.map((entry) => ({
                type: entry?.type || '',
                value: Object.prototype.hasOwnProperty.call(entry || {}, 'value') ? entry.value : null,
                description: entry?.description || '',
              }))
            : [],
          timestamp: msg.params?.timestamp || 0,
        })
        return
      }
      if (msg.method === 'Runtime.exceptionThrown') {
        exceptionMessages.push({
          text: msg.params?.exceptionDetails?.text || '',
          url: msg.params?.exceptionDetails?.url || '',
          lineNumber: msg.params?.exceptionDetails?.lineNumber ?? -1,
          columnNumber: msg.params?.exceptionDetails?.columnNumber ?? -1,
          description: msg.params?.exceptionDetails?.exception?.description || '',
        })
        return
      }
      if (!msg.id) return
      const entry = pending.get(msg.id)
      if (!entry) return
      pending.delete(msg.id)
      if (msg.error) entry.reject(new Error(msg.error.message || 'cdp_error'))
      else entry.resolve(msg.result)
    }
    await new Promise((resolve, reject) => {
      ws.onopen = resolve
      ws.onerror = reject
    })
    const call = (method, params = {}) => {
      const id = ++seq
      ws.send(JSON.stringify({ id, method, params }))
      return new Promise((resolve, reject) => pending.set(id, { resolve, reject }))
    }
    await call('Page.enable')
    await call('Runtime.enable')
    await call('Emulation.setDeviceMetricsOverride', {
      width: args.width,
      height: args.height,
      deviceScaleFactor: 1,
      mobile: false,
      screenWidth: args.width,
      screenHeight: args.height,
    })
    await call('Page.navigate', {
      url,
    })

    let runtimeState = null
    let domHtml = ''
    let screenshotPng = null
    let semanticNodeCount = 0
    let semanticNodesDoc = null
    let documentNodeCount = 0
    let locationDoc = null
    const collectFailureContext = async () => {
      const evaluateText = async (expression) => {
        try {
          const result = await call('Runtime.evaluate', {
            expression,
            returnByValue: true,
          })
          return result?.result?.value ?? null
        } catch (error) {
          return `cdp_eval_failed:${error instanceof Error ? error.message : String(error)}`
        }
      }
      const debugContext = {
        route: args.route,
        url,
        readyState: await evaluateText('document.readyState'),
        location: await evaluateText('JSON.stringify({ pathname: window.location.pathname, search: window.location.search, hash: window.location.hash, href: window.location.href })'),
        title: await evaluateText('document.title'),
        rootState: await evaluateText(`(() => {
          const root = document.getElementById("root");
          if (!root) {
            return JSON.stringify({
              exists: false,
              bodyChildCount: document.body ? document.body.children.length : -1,
              bodyTextPreview: String(document.body?.textContent || "").replace(/\\s+/g, " ").trim().slice(0, 512),
              bodyHtmlPreview: String(document.body?.innerHTML || "").slice(0, 1024),
            });
          }
          return JSON.stringify({
            exists: true,
            childCount: root.children.length,
            textPreview: String(root.textContent || "").replace(/\\s+/g, " ").trim().slice(0, 512),
            htmlPreview: String(root.innerHTML || "").slice(0, 2048),
          });
        })()`),
        runtimeStateText: await evaluateText('JSON.stringify(window.__UNIMAKER_R2C_RUNTIME_STATE || null)'),
        consoleMessages,
        exceptionMessages,
      }
      const debugPath = path.join(args.outDir, `${args.route}.runtime_failure_v1.json`)
      fs.writeFileSync(debugPath, `${JSON.stringify(debugContext, null, 2)}\n`, 'utf8')
      writeSidecarSummary(args.summaryOut ? path.resolve(args.summaryOut) : '', {
        ok: false,
        route: args.route,
        runtime_failure_path: debugPath,
      })
      return debugPath
    }
    const deadline = Date.now() + args.waitMs
    while (Date.now() < deadline) {
      const stateRes = await call('Runtime.evaluate', {
        expression: 'JSON.stringify(window.__UNIMAKER_R2C_RUNTIME_STATE || null)',
        returnByValue: true,
      })
      const stateText = stateRes?.result?.value
      if (stateText && stateText !== 'null') {
        runtimeState = JSON.parse(stateText)
        const domRes = await call('Runtime.evaluate', {
          expression: 'document.documentElement.outerHTML',
          returnByValue: true,
        })
        domHtml = String(domRes?.result?.value || '')
        const screenshotRes = await call('Page.captureScreenshot', {
          format: 'png',
          fromSurface: true,
        })
        const screenshotBase64 = String(screenshotRes?.data || '')
        screenshotPng = screenshotBase64 ? Buffer.from(screenshotBase64, 'base64') : null
        const semanticCountRes = await call('Runtime.evaluate', {
          expression: '(() => { const root = document.getElementById(\"root\"); if (!root) return -1; return root.querySelectorAll(\"*\").length + 1; })()',
          returnByValue: true,
        })
        semanticNodeCount = Number(semanticCountRes?.result?.value || -1)
        if (semanticNodeCount < 0) {
          throw new Error('missing_root_container')
        }
        const semanticNodesRes = await call('Runtime.evaluate', {
          expression: `(() => {
            const root = document.getElementById("root");
            if (!root) return "";
            const preview = (text, maxLen) => {
              const normalized = String(text || "").replace(/\\s+/g, " ").trim();
              if (!normalized) return "";
              return normalized.length > maxLen ? normalized.slice(0, maxLen) : normalized;
            };
            const classPreview = (element) => {
              const raw = String(element.getAttribute("class") || "").trim();
              if (!raw) return "";
              return raw.split(/\\s+/).filter(Boolean).slice(0, 3).join(" ");
            };
            const makeLabel = (tag, classText) => {
              if (!classText) return "<" + tag + ">";
              return "<" + tag + "> ." + classText.split(/\\s+/).join(".");
            };
            const nodes = [];
            const walk = (element, depth, pathText) => {
              if (!(element instanceof Element)) return;
              const tag = String(element.tagName || "").toLowerCase();
              const classes = classPreview(element);
              nodes.push({
                kind: "element",
                tag,
                depth,
                path: pathText,
                classPreview: classes,
                textPreview: preview(element.textContent || "", 96),
                label: makeLabel(tag, classes),
              });
              const children = Array.from(element.children || []);
              for (let index = 0; index < children.length; index += 1) {
                walk(children[index], depth + 1, pathText + "." + String(index));
              }
            };
            walk(root, 0, "0");
            return JSON.stringify({
              format: "semantic_dom_nodes_v1",
              route_state: ${JSON.stringify(args.route)},
              node_count: nodes.length,
              source: "dom_root_elements_v1",
              nodes,
            });
          })()`,
          returnByValue: true,
        })
        const semanticNodesText = semanticNodesRes?.result?.value
        semanticNodesDoc = semanticNodesText ? JSON.parse(semanticNodesText) : null
        if (!semanticNodesDoc || Number(semanticNodesDoc.node_count || 0) !== semanticNodeCount) {
          throw new Error('semantic_dom_nodes_missing')
        }
        const documentCountRes = await call('Runtime.evaluate', {
          expression: 'document.querySelectorAll(\"*\").length',
          returnByValue: true,
        })
        documentNodeCount = Number(documentCountRes?.result?.value || 0)
        const locationRes = await call('Runtime.evaluate', {
          expression: 'JSON.stringify({ pathname: window.location.pathname, search: window.location.search, hash: window.location.hash })',
          returnByValue: true,
        })
        const locationText = locationRes?.result?.value
        locationDoc = locationText ? JSON.parse(locationText) : null
        break
      }
      await sleep(250)
    }
    if (!runtimeState) {
      const debugPath = await collectFailureContext()
      throw new Error(`runtime_state_missing debug=${debugPath} ${chromeStderr}`.trim())
    }
    const runtimeStatePath = path.join(args.outDir, `${args.route}.runtime_state.json`)
    const domPath = path.join(args.outDir, `${args.route}.dom.html`)
    const metaPath = path.join(args.outDir, `${args.route}.runtime_meta.json`)
    const truthTracePath = path.join(args.outDir, `${args.route}.truth_trace_v2.json`)
    const semanticNodesPath = path.join(args.outDir, `${args.route}.semantic_dom_nodes_v1.json`)
    const screenshotPath = path.resolve(args.screenshotOut || path.join(args.outDir, `${args.route}.screenshot.png`))
    const crypto = await import('node:crypto')
    const domHash = crypto.createHash('sha256').update(domHtml, 'utf8').digest('hex')
    let screenshotHash = ''
    if (screenshotPng && screenshotPng.length > 0) {
      fs.mkdirSync(path.dirname(screenshotPath), { recursive: true })
      fs.writeFileSync(screenshotPath, screenshotPng)
      screenshotHash = crypto.createHash('sha256').update(screenshotPng).digest('hex')
    }
    const locationDocResolved = locationDoc || { pathname: '/', search: '', hash: '' }
    const timestampMs = Date.now()
    const capturedAt = new Date(timestampMs).toISOString()
    const truthTraceDoc = {
      format: 'truth_trace_v2',
      traceId: `capture_truth_${args.route}`,
      platform: 'browser_chrome_headless',
      capturedAt,
      sourceRoot: args.baseUrl,
      states: [args.route],
      snapshots: [
        {
          stateId: args.route,
          route: {
            routeId: args.route,
            pathname: String(locationDocResolved.pathname || '/'),
            search: String(locationDocResolved.search || ''),
            hash: String(locationDocResolved.hash || ''),
            width: args.width,
            height: args.height,
          },
          domPath,
          domHash,
          runtimeStatePath,
        semanticNodesPath,
        semanticNodesCount: semanticNodeCount,
        screenshotPath,
        screenshotHash,
        renderReady: true,
        storage: [],
        hostEvents: [],
          timestampMs,
        },
      ],
      sideEffects: [],
    }
    fs.writeFileSync(runtimeStatePath, `${JSON.stringify(runtimeState, null, 2)}\n`, 'utf8')
    fs.writeFileSync(domPath, domHtml, 'utf8')
    fs.writeFileSync(semanticNodesPath, `${JSON.stringify(semanticNodesDoc, null, 2)}\n`, 'utf8')
    fs.writeFileSync(metaPath, `${JSON.stringify({
        route: args.route,
        url,
        width: args.width,
        height: args.height,
        runtime_state_path: runtimeStatePath,
        dom_path: domPath,
        dom_hash: domHash,
        screenshot_path: screenshotPath,
        screenshot_hash: screenshotHash,
        semantic_nodes_path: semanticNodesPath,
        semantic_nodes_count: semanticNodeCount,
        document_nodes_count: documentNodeCount,
        location: locationDocResolved,
      }, null, 2)}\n`, 'utf8')
    fs.writeFileSync(truthTracePath, `${JSON.stringify(truthTraceDoc, null, 2)}\n`, 'utf8')
    writeSidecarSummary(args.summaryOut ? path.resolve(args.summaryOut) : '', {
      ok: true,
      route: args.route,
      truth_trace_path: truthTracePath,
      truth_trace_format: 'truth_trace_v2',
      truth_trace_state_count: 1,
      runtime_state_path: runtimeStatePath,
      dom_path: domPath,
      screenshot_path: screenshotPath,
      screenshot_sha256: screenshotHash,
      semantic_nodes_path: semanticNodesPath,
      runtime_meta_path: metaPath,
      dom_hash: domHash,
      semantic_nodes_count: semanticNodeCount,
      document_nodes_count: documentNodeCount,
      pathname: String(locationDocResolved.pathname || '/'),
      search: String(locationDocResolved.search || ''),
      hash: String(locationDocResolved.hash || ''),
      debug_port: debugPort,
    })
    console.log(JSON.stringify({
      ok: true,
      route: args.route,
      truth_trace_path: truthTracePath,
      runtime_state_path: runtimeStatePath,
      dom_path: domPath,
      screenshot_path: screenshotPath,
      screenshot_sha256: screenshotHash,
      semantic_nodes_path: semanticNodesPath,
      dom_hash: domHash,
      semantic_nodes_count: semanticNodeCount,
      document_nodes_count: documentNodeCount,
      debug_port: debugPort,
      location: locationDocResolved,
    }))
  } finally {
    try {
      if (ws) ws.close()
    } catch {
    }
    await shutdownChrome(chromeProc)
    fs.rmSync(userDataDir, { recursive: true, force: true })
  }
}

const args = parseArgs(process.argv)
captureRuntime(args).catch((err) => {
  console.error(`[r2c-react-v3-truth-runtime] ${err instanceof Error ? err.message : String(err)}`)
  process.exit(2)
})
