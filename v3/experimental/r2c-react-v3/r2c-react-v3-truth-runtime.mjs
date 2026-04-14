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
    debugPort: 9223,
    waitMs: 20000,
    summaryOut: '',
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
    else if (a === '-h' || a === '--help') {
      console.log(
        'Usage: r2c-react-v3-truth-runtime.mjs --route <state> --out-dir <dir> ' +
        '[--base-url <url>] [--chrome <bin>] [--width <n>] [--height <n>] ' +
        '[--truth-flag <0|1>] [--debug-port <n>] [--wait-ms <n>] [--summary-out <file>]'
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

async function captureRuntime(args) {
  if (!args.route) throw new Error('missing --route')
  if (!args.outDir) throw new Error('missing --out-dir')
  if (!fs.existsSync(args.chrome)) throw new Error(`chrome not found: ${args.chrome}`)
  fs.mkdirSync(args.outDir, { recursive: true })

  const url = buildUrl(args.baseUrl, args.route, args.truthFlag)
  const userDataDir = fs.mkdtempSync(path.join(os.tmpdir(), 'r2c-truth-runtime-'))
  const chromeProc = spawn(args.chrome, [
    `--remote-debugging-port=${args.debugPort}`,
    '--headless=new',
    '--disable-gpu',
    '--hide-scrollbars',
    '--no-first-run',
    `--user-data-dir=${userDataDir}`,
    `--window-size=${args.width},${args.height}`,
    url,
  ], { stdio: ['ignore', 'ignore', 'pipe'] })
  let chromeStderr = ''
  chromeProc.stderr.on('data', (chunk) => {
    chromeStderr += chunk.toString()
  })

  let ws = null
  try {
    const target = await waitForTarget(args.debugPort, args.baseUrl, args.waitMs)
    ws = new WebSocket(target.webSocketDebuggerUrl)
    let seq = 0
    const pending = new Map()
    ws.onmessage = (event) => {
      const msg = JSON.parse(event.data)
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

    let runtimeState = null
    let domHtml = ''
    let semanticNodeCount = 0
    let documentNodeCount = 0
    let locationDoc = null
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
        const semanticCountRes = await call('Runtime.evaluate', {
          expression: '(() => { const root = document.getElementById(\"root\"); if (!root) return -1; return root.querySelectorAll(\"*\").length + 1; })()',
          returnByValue: true,
        })
        semanticNodeCount = Number(semanticCountRes?.result?.value || -1)
        if (semanticNodeCount < 0) {
          throw new Error('missing_root_container')
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
      throw new Error(`runtime_state_missing ${chromeStderr}`.trim())
    }
    const runtimeStatePath = path.join(args.outDir, `${args.route}.runtime_state.json`)
    const domPath = path.join(args.outDir, `${args.route}.dom.html`)
    const metaPath = path.join(args.outDir, `${args.route}.runtime_meta.json`)
    const crypto = await import('node:crypto')
    const domHash = crypto.createHash('sha256').update(domHtml, 'utf8').digest('hex')
    fs.writeFileSync(runtimeStatePath, `${JSON.stringify(runtimeState, null, 2)}\n`, 'utf8')
    fs.writeFileSync(domPath, domHtml, 'utf8')
    fs.writeFileSync(metaPath, `${JSON.stringify({
        route: args.route,
        url,
        width: args.width,
        height: args.height,
        runtime_state_path: runtimeStatePath,
        dom_path: domPath,
        dom_hash: domHash,
        semantic_nodes_count: semanticNodeCount,
        document_nodes_count: documentNodeCount,
        location: locationDoc || { pathname: '/', search: '', hash: '' },
      }, null, 2)}\n`, 'utf8')
    writeSidecarSummary(args.summaryOut ? path.resolve(args.summaryOut) : '', {
      ok: true,
      route: args.route,
      runtime_state_path: runtimeStatePath,
      dom_path: domPath,
      runtime_meta_path: metaPath,
      dom_hash: domHash,
      semantic_nodes_count: semanticNodeCount,
      document_nodes_count: documentNodeCount,
      pathname: (locationDoc && locationDoc.pathname) || '/',
      search: (locationDoc && locationDoc.search) || '',
      hash: (locationDoc && locationDoc.hash) || '',
    })
    console.log(JSON.stringify({
      ok: true,
      route: args.route,
      runtime_state_path: runtimeStatePath,
      dom_path: domPath,
      dom_hash: domHash,
      semantic_nodes_count: semanticNodeCount,
      document_nodes_count: documentNodeCount,
      location: locationDoc || { pathname: '/', search: '', hash: '' },
    }))
  } finally {
    try {
      if (ws) ws.close()
    } catch {
    }
    chromeProc.kill('SIGTERM')
    await sleep(250)
    fs.rmSync(userDataDir, { recursive: true, force: true })
  }
}

const args = parseArgs(process.argv)
captureRuntime(args).catch((err) => {
  console.error(`[r2c-react-v3-truth-runtime] ${err instanceof Error ? err.message : String(err)}`)
  process.exit(2)
})
