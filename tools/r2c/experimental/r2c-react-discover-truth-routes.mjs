#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { pathToFileURL } from 'node:url';

const SOURCE_EXTS = new Set(['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs']);
const SKIP_DIRS = new Set(['node_modules', 'dist', 'build', 'artifacts', '.git', '.build', '.tmp-deploy']);
const TEST_FILE_RE = /(?:^|\/)(?:__tests__|.*\.(?:test|spec)\.[^.]+)$/;
const ROUTE_RE = /^[a-z]+(?:_[a-z0-9]+)+$/;
const ROUTE_FUNCTION_ALLOWLIST = new Set([
  'truthInitialTab',
  'truthInitialPublishMode',
  'truthInitialApp',
  'resolveCurrentRouteState',
  'deriveHomeRouteState',
  'resolveInitialHomeCategory',
]);

export function parseArgs(argv) {
  const out = {
    repo: '',
    runtimeRoots: [],
    outPath: '',
    summaryOut: '',
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--runtime-root') out.runtimeRoots.push(String(argv[++i] || ''));
    else if (arg === '--out') out.outPath = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-discover-truth-routes.mjs --repo <path> [--runtime-root <dir>] [--out <file>] [--summary-out <file>]');
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

export function normalizeRoots(repo, requested) {
  const picks = requested.length > 0 ? requested : ['app'];
  const out = [];
  for (const item of picks) {
    const root = normalizeRootValue(item);
    if (root !== '.' && !fs.existsSync(path.join(repo, root))) {
      throw new Error(`missing runtime root: ${root}`);
    }
    if (!out.includes(root)) {
      out.push(root);
    }
  }
  return out.length > 0 ? out : ['.'];
}

function pathUnderRoots(relPath, roots) {
  if (roots.includes('.')) {
    return true;
  }
  return roots.some((root) => relPath === root || relPath.startsWith(`${root}/`));
}

function shouldIncludeFile(fullPath, relPath) {
  const ext = path.extname(fullPath).toLowerCase();
  if (!SOURCE_EXTS.has(ext)) {
    return false;
  }
  if (fullPath.endsWith('.d.ts')) {
    return false;
  }
  if (TEST_FILE_RE.test(relPath)) {
    return false;
  }
  return true;
}

function walkSources(repo, roots) {
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
      if (!pathUnderRoots(relPath, roots)) continue;
      if (shouldIncludeFile(fullPath, relPath)) {
        out.push(fullPath);
      }
    }
  }
  walk(repo);
  out.sort();
  return out;
}

export function writeSidecarSummary(summaryPath, values) {
  if (!summaryPath) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`);
  fs.mkdirSync(path.dirname(summaryPath), { recursive: true });
  fs.writeFileSync(summaryPath, `${lines.join('\n')}\n`, 'utf8');
}

function loadTypeScript(repo) {
  const requireFromRepo = createRequire(path.join(repo, 'package.json'));
  return requireFromRepo('typescript');
}

function scriptKindFor(ts, ext) {
  switch (ext) {
    case '.tsx': return ts.ScriptKind.TSX;
    case '.jsx': return ts.ScriptKind.JSX;
    case '.js': return ts.ScriptKind.JS;
    case '.mjs': return ts.ScriptKind.JS;
    case '.cjs': return ts.ScriptKind.JS;
    default: return ts.ScriptKind.TS;
  }
}

function lineColOf(ts, sourceFile, node) {
  const pos = ts.getLineAndCharacterOfPosition(sourceFile, node.getStart(sourceFile, false));
  return { line: pos.line + 1, column: pos.character + 1 };
}

function functionNameOf(ts, node) {
  let current = node.parent;
  while (current) {
    if ((ts.isFunctionDeclaration(current) || ts.isMethodDeclaration(current)) && current.name) {
      return current.name.getText(sourceFileOf(current) || undefined);
    }
    if ((ts.isArrowFunction(current) || ts.isFunctionExpression(current)) && current.parent) {
      const parent = current.parent;
      if (ts.isVariableDeclaration(parent) && ts.isIdentifier(parent.name)) {
        return parent.name.text;
      }
      if (ts.isPropertyAssignment(parent)) {
        return parent.name.getText(sourceFileOf(parent) || undefined);
      }
    }
    current = current.parent;
  }
  return '';
}

function sourceFileOf(node) {
  let current = node;
  while (current) {
    if (tsGlobal.isSourceFile(current)) {
      return current;
    }
    current = current.parent;
  }
  return null;
}

let tsGlobal = null;

function propertyNameText(name) {
  if (!name) return '';
  if ('text' in name && typeof name.text === 'string') return name.text;
  if ('escapedText' in name && typeof name.escapedText === 'string') return name.escapedText;
  return name.getText ? name.getText() : '';
}

function identifierName(ts, node) {
  if (!node) return '';
  if (ts.isIdentifier(node)) return node.text;
  if (ts.isPropertyAccessExpression(node)) return node.name.text;
  return '';
}

function binaryIsEquality(ts, kind) {
  return kind === ts.SyntaxKind.EqualsEqualsEqualsToken || kind === ts.SyntaxKind.EqualsEqualsToken;
}

function reasonForRouteLiteral(ts, node) {
  if (!ts.isStringLiteralLike(node)) {
    return '';
  }
  const value = node.text.trim();
  if (!ROUTE_RE.test(value)) {
    return '';
  }
  const parent = node.parent;
  const fnName = functionNameOf(ts, node);
  if (ts.isPropertyAssignment(parent) && propertyNameText(parent.name) === 'routeKey') {
    return `route_key:${fnName || 'object'}`;
  }
  if (ts.isVariableDeclaration(parent)) {
    const varName = parent.name.getText();
    if (/^[A-Z0-9_]+_ROUTE$/.test(varName)) {
      return `route_const:${varName}`;
    }
  }
  if (ts.isCaseClause(parent) && ROUTE_FUNCTION_ALLOWLIST.has(fnName)) {
    return fnName ? `route_case:${fnName}` : 'route_case';
  }
  if (ts.isReturnStatement(parent) && ROUTE_FUNCTION_ALLOWLIST.has(fnName)) {
    return `route_return:${fnName}`;
  }
  if (ts.isBinaryExpression(parent) && binaryIsEquality(ts, parent.operatorToken.kind)) {
    const leftName = identifierName(ts, parent.left);
    const rightName = identifierName(ts, parent.right);
    if (leftName === 'truthRoute' || leftName === 'route') {
      return `route_compare:${leftName}:${fnName || 'scope'}`;
    }
    if (rightName === 'truthRoute' || rightName === 'route') {
      return `route_compare:${rightName}:${fnName || 'scope'}`;
    }
  }
  return '';
}

export function discoverTruthRoutes(repo, runtimeRoots) {
  const ts = loadTypeScript(repo);
  tsGlobal = ts;
  const routeMap = new Map();
  const files = walkSources(repo, runtimeRoots);

  function addRoute(routeId, relPath, line, reason) {
    if (!ROUTE_RE.test(routeId)) {
      return;
    }
    let entry = routeMap.get(routeId);
    if (!entry) {
      entry = {
        routeId,
        firstSourcePath: relPath,
        firstLine: line,
        reasons: [],
        sources: [],
      };
      routeMap.set(routeId, entry);
    }
    if (!entry.reasons.includes(reason)) {
      entry.reasons.push(reason);
    }
    if (!entry.sources.some((item) => item.path === relPath && item.line === line && item.reason === reason)) {
      entry.sources.push({ path: relPath, line, reason });
    }
  }

  for (const fullPath of files) {
    const relPath = path.relative(repo, fullPath).split(path.sep).join('/');
    const sourceText = fs.readFileSync(fullPath, 'utf8');
    const sourceFile = ts.createSourceFile(fullPath, sourceText, ts.ScriptTarget.Latest, true, scriptKindFor(ts, path.extname(fullPath).toLowerCase()));

    function visit(node) {
      if (ts.isStringLiteralLike(node)) {
        const reason = reasonForRouteLiteral(ts, node);
        if (reason) {
          const pos = lineColOf(ts, sourceFile, node);
          addRoute(node.text.trim(), relPath, pos.line, reason);
        }
      }
      ts.forEachChild(node, visit);
    }

    visit(sourceFile);
  }

  const routes = Array.from(routeMap.values())
    .sort((a, b) => a.routeId.localeCompare(b.routeId))
    .map((entry) => ({
      ...entry,
      reasons: entry.reasons.sort(),
      sources: entry.sources.sort((a, b) => a.path.localeCompare(b.path) || a.line - b.line || a.reason.localeCompare(b.reason)),
    }));

  return {
    format: 'truth_routes_v1',
    repoRoot: repo,
    sourceRoots: runtimeRoots,
    typescriptVersion: ts.version,
    routes,
  };
}

export function runCli(argv = process.argv) {
  const args = parseArgs(argv);
  const repo = path.resolve(args.repo);
  const runtimeRoots = normalizeRoots(repo, args.runtimeRoots);
  const doc = discoverTruthRoutes(repo, runtimeRoots);
  if (args.outPath) {
    const outPath = path.resolve(args.outPath);
    fs.mkdirSync(path.dirname(outPath), { recursive: true });
    fs.writeFileSync(outPath, `${JSON.stringify(doc, null, 2)}\n`, 'utf8');
  }
  writeSidecarSummary(args.summaryOut ? path.resolve(args.summaryOut) : '', {
    format: doc.format,
    repo_root: repo,
    source_roots: runtimeRoots.join(','),
    typescript_version: doc.typescriptVersion,
    route_count: doc.routes.length,
    out_path: args.outPath ? path.resolve(args.outPath) : '',
  });
  process.stdout.write(`${JSON.stringify(doc, null, 2)}\n`);
}

const entryHref = process.argv[1] ? pathToFileURL(path.resolve(process.argv[1])).href : '';
if (import.meta.url === entryHref) {
  runCli(process.argv);
}
