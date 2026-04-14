#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';

const SOURCE_EXTS = new Set(['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs']);
const TEST_FILE_RE = /(?:^|\/)(?:__tests__|.*\.(?:test|spec)\.[^.]+)$/;
const SKIP_DIRS = new Set(['node_modules', 'dist', 'build', 'artifacts', '.git', '.build', '.tmp-deploy']);
const GENERATED_PATH_SEGMENTS = new Set(['pkg']);
const GENERATED_FILE_SUFFIXES = ['_wasm.js', '_bg.js'];
const ASSET_EXTS = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.svg', '.ico', '.woff', '.woff2', '.ttf', '.otf', '.wasm', '.mp3', '.wav', '.ogg', '.mp4', '.webm', '.html', '.css', '.json']);

const IMPORT_FEATURES = new Map([
  ['@capacitor/', 'capacitor'],
  ['zustand', 'zustand'],
  ['@radix-ui/', 'radix'],
  ['@react-three/', 'react_three'],
  ['three', 'react_three'],
]);

const IDENTIFIER_FEATURES = new Map([
  ['localStorage', 'local_storage'],
  ['fetch', 'fetch'],
  ['ResizeObserver', 'resize_observer'],
  ['MutationObserver', 'mutation_observer'],
  ['CustomEvent', 'custom_event'],
  ['BroadcastChannel', 'broadcast_channel'],
  ['RTCPeerConnection', 'rtc'],
  ['WebAssembly', 'webassembly'],
  ['Worker', 'worker'],
]);

const CALL_FEATURES = new Map([
  ['useState', 'use_state'],
  ['useEffect', 'use_effect'],
  ['useMemo', 'use_memo'],
  ['useCallback', 'use_callback'],
  ['useRef', 'use_ref'],
  ['useContext', 'use_context'],
  ['createContext', 'use_context'],
  ['lazy', 'suspense'],
  ['memo', 'memo'],
]);

function parseArgs(argv) {
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
      console.log('Usage: r2c-react-v3-ts-frontend.mjs --repo <path> [--runtime-root <dir>] [--out <file>] [--summary-out <file>]');
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

function normalizeRoots(repo, requested) {
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

function sourceRootForPath(relPath, roots) {
  if (roots.includes('.')) {
    return '.';
  }
  const matches = roots.filter((root) => relPath === root || relPath.startsWith(`${root}/`));
  if (matches.length === 0) {
    return '';
  }
  matches.sort((a, b) => b.length - a.length);
  return matches[0];
}

function isGeneratedFile(relPath) {
  const parts = relPath.split('/');
  if (parts.some((part) => GENERATED_PATH_SEGMENTS.has(part))) {
    return true;
  }
  return GENERATED_FILE_SUFFIXES.some((suffix) => relPath.endsWith(suffix));
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
  if (['.js', '.jsx', '.mjs', '.cjs'].includes(ext)) {
    const stem = path.basename(fullPath, ext);
    const dir = path.dirname(fullPath);
    if (fs.existsSync(path.join(dir, `${stem}.ts`)) || fs.existsSync(path.join(dir, `${stem}.tsx`))) {
      return false;
    }
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

function sha256(text) {
  return crypto.createHash('sha256').update(text, 'utf8').digest('hex');
}

function writeSidecarSummary(summaryPath, values) {
  if (!summaryPath) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${String(value ?? '')}`);
  fs.mkdirSync(path.dirname(summaryPath), { recursive: true });
  fs.writeFileSync(summaryPath, `${lines.join('\n')}\n`, 'utf8');
}

function lineColOf(ts, sourceFile, node) {
  const pos = ts.getLineAndCharacterOfPosition(sourceFile, node.getStart(sourceFile, false));
  return { line: pos.line + 1, column: pos.character + 1 };
}

function spanText(ts, sourceFile, node) {
  const start = lineColOf(ts, sourceFile, node);
  return `line:${start.line}:${start.column}`;
}

function textOf(sourceFile, node) {
  return sourceFile.text.slice(node.getStart(sourceFile, false), node.getEnd());
}

function snippetOf(sourceFile, node) {
  const { line } = lineColOf(tsGlobal, sourceFile, node);
  const lines = sourceFile.text.split(/\r?\n/);
  return (lines[line - 1] || '').trim();
}

function isUpperName(name) {
  return typeof name === 'string' && /^[A-Z]/.test(name);
}

function jsxTagName(ts, node) {
  if (ts.isIdentifier(node)) return node.text;
  if (ts.isPropertyAccessExpression(node)) return node.name.text;
  if (ts.isJsxNamespacedName(node)) return `${node.namespace.text}:${node.name.text}`;
  return node.getText();
}

function callName(ts, expr) {
  if (ts.isIdentifier(expr)) return expr.text;
  if (ts.isPropertyAccessExpression(expr)) return expr.name.text;
  return '';
}

function importFeature(specifier) {
  for (const [prefix, feature] of IMPORT_FEATURES.entries()) {
    if (specifier === prefix || specifier.startsWith(prefix)) {
      return feature;
    }
  }
  return '';
}

function classifyBlocker(ts, sourceFile, node) {
  if (ts.isCallExpression(node)) {
    const name = callName(ts, node.expression);
    if (name === 'eval') {
      return ['js_eval', 'R2C 不接受 eval；模块初始化和闭包边界必须静态化。'];
    }
    if (name === 'require') {
      return ['js_dynamic_require', 'R2C 不接受 CommonJS require；模块依赖必须静态可枚举。'];
    }
    if (
      ts.isPropertyAccessExpression(node.expression)
      && ts.isIdentifier(node.expression.expression)
      && node.expression.expression.text === 'Object'
      && node.expression.name.text === 'setPrototypeOf'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
  }
  if (ts.isNewExpression(node)) {
    const name = callName(ts, node.expression);
    if (name === 'Function') {
      return ['js_new_function', 'R2C 不接受运行时构造函数体；代码必须在编译期可见。'];
    }
    if (name === 'Proxy') {
      return ['js_proxy', 'R2C 不接受 Proxy；对象语义必须显式建模。'];
    }
  }
  if (ts.isWithStatement(node)) {
    return ['js_with_statement', 'R2C 不接受 with；作用域必须静态确定。'];
  }
  if (ts.isBinaryExpression(node)) {
    const op = node.operatorToken.kind;
    const assignmentKinds = new Set([
      ts.SyntaxKind.EqualsToken,
      ts.SyntaxKind.BarBarEqualsToken,
      ts.SyntaxKind.AmpersandAmpersandEqualsToken,
      ts.SyntaxKind.QuestionQuestionEqualsToken,
      ts.SyntaxKind.PlusEqualsToken,
      ts.SyntaxKind.MinusEqualsToken,
      ts.SyntaxKind.AsteriskEqualsToken,
      ts.SyntaxKind.SlashEqualsToken,
      ts.SyntaxKind.PercentEqualsToken,
    ]);
    if (!assignmentKinds.has(op)) {
      return null;
    }
    const left = node.left;
    if (
      ts.isPropertyAccessExpression(left)
      && ts.isIdentifier(left.expression)
      && left.expression.text === 'module'
      && left.name.text === 'exports'
    ) {
      return ['js_cjs_exports', 'R2C 不接受 CommonJS 导出；模块导出必须静态化。'];
    }
    if (
      ts.isIdentifier(left)
      && left.text === 'exports'
    ) {
      return ['js_cjs_exports', 'R2C 不接受 CommonJS 导出；模块导出必须静态化。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && ts.isIdentifier(left.expression)
      && left.expression.text === 'exports'
    ) {
      return ['js_cjs_exports', 'R2C 不接受 CommonJS 导出；模块导出必须静态化。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && left.name.text === '__proto__'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && ts.isPropertyAccessExpression(left.expression)
      && left.expression.name.text === 'prototype'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
    if (
      ts.isPropertyAccessExpression(left)
      && left.name.text === 'prototype'
    ) {
      return ['js_prototype_mutation', 'R2C 不接受 prototype 动态改写；对象形状必须稳定。'];
    }
  }
  return null;
}

function hasJsxReturn(ts, node) {
  let found = false;
  function visit(current) {
    if (found) return;
    if (ts.isJsxElement(current) || ts.isJsxSelfClosingElement(current) || ts.isJsxFragment(current)) {
      found = true;
      return;
    }
    ts.forEachChild(current, visit);
  }
  visit(node);
  return found;
}

function scanModule(ts, repo, runtimeRoots, fullPath) {
  const relPath = path.relative(repo, fullPath).split(path.sep).join('/');
  const text = fs.readFileSync(fullPath, 'utf8');
  const ext = path.extname(fullPath).toLowerCase();
  const sourceFile = ts.createSourceFile(fullPath, text, ts.ScriptTarget.Latest, true, scriptKindFor(ts, ext));
  const featureCounts = Object.fromEntries([
    'class_component',
    'error_boundary',
    'use_state',
    'use_effect',
    'use_memo',
    'use_callback',
    'use_ref',
    'use_context',
    'suspense',
    'memo',
    'strict_mode',
    'local_storage',
    'fetch',
    'resize_observer',
    'mutation_observer',
    'custom_event',
    'rtc',
    'media',
    'broadcast_channel',
    'worker',
    'webassembly',
    'capacitor',
    'zustand',
    'radix',
    'react_three',
  ].map((name) => [name, 0]));
  const imports = [];
  const exports = [];
  const components = [];
  const blockers = [];
  const hookCalls = [];
  const classTokens = [];
  const jsxElements = [];
  const dynamicImports = [];
  const staticImports = [];
  const exportedNames = new Set();
  const componentKeys = new Set();
  const blockerKeys = new Set();
  const componentEntries = new Map();

  function countFeature(name) {
    featureCounts[name] = (featureCounts[name] || 0) + 1;
  }

  function maybeAddBlocker(node) {
    const classified = classifyBlocker(ts, sourceFile, node);
    if (!classified) return;
    const [code, reason] = classified;
    const pos = lineColOf(ts, sourceFile, node);
    const key = `${code}:${pos.line}:${pos.column}`;
    if (blockerKeys.has(key)) return;
    blockerKeys.add(key);
    blockers.push({
      code,
      file: relPath,
      line: pos.line,
      column: pos.column,
      snippet: snippetOf(sourceFile, node),
      reason,
    });
  }

  function addComponent(name, kind, node, extra = {}) {
    if (!name || !isUpperName(name)) return;
    const pos = lineColOf(ts, sourceFile, node);
    const key = `${kind}:${name}:${pos.line}`;
    if (componentKeys.has(key)) return;
    componentKeys.add(key);
    const entry = {
      name,
      kind,
      line: pos.line,
      exported: exportedNames.has(name),
      surface_jsx_count: 0,
      surface_intrinsic_jsx_count: 0,
      surface_component_tag_count: 0,
      surface_fragment_count: 0,
      return_surfaces: [],
      ...extra,
    };
    components.push(entry);
    componentEntries.set(key, entry);
  }

  function countDirectRenderSurface(rootNode) {
    const counts = {
      total: 0,
      intrinsic: 0,
      component: 0,
      fragment: 0,
    };
    if (!rootNode) {
      return counts;
    }
    function walk(node) {
      if (node !== rootNode && (ts.isFunctionLike(node) || ts.isClassLike(node))) {
        return;
      }
      if (ts.isJsxOpeningElement(node) || ts.isJsxSelfClosingElement(node)) {
        const tag = jsxTagName(ts, node.tagName);
        counts.total += 1;
        if (/^[a-z]/.test(tag)) counts.intrinsic += 1;
        else counts.component += 1;
      } else if (ts.isJsxFragment(node)) {
        counts.total += 1;
        counts.fragment += 1;
      }
      ts.forEachChild(node, walk);
    }
    walk(rootNode);
    return counts;
  }

  function collectDirectReturnSurfaces(rootNode) {
    const entries = [];
    if (!rootNode) {
      return entries;
    }
    if (!ts.isBlock(rootNode)) {
      const counts = countDirectRenderSurface(rootNode);
      if (counts.total > 0) {
        const pos = lineColOf(ts, sourceFile, rootNode);
        entries.push({
          line: pos.line,
          surface_jsx_count: counts.total,
          surface_intrinsic_jsx_count: counts.intrinsic,
          surface_component_tag_count: counts.component,
          surface_fragment_count: counts.fragment,
        });
      }
      return entries;
    }
    function walk(node) {
      if (node !== rootNode && (ts.isFunctionLike(node) || ts.isClassLike(node))) {
        return;
      }
      if (ts.isReturnStatement(node) && node.expression) {
        const counts = countDirectRenderSurface(node.expression);
        if (counts.total > 0) {
          const pos = lineColOf(ts, sourceFile, node);
          entries.push({
            line: pos.line,
            surface_jsx_count: counts.total,
            surface_intrinsic_jsx_count: counts.intrinsic,
            surface_component_tag_count: counts.component,
            surface_fragment_count: counts.fragment,
          });
        }
      }
      ts.forEachChild(node, walk);
    }
    walk(rootNode);
    return entries;
  }

  for (const statement of sourceFile.statements) {
    if (ts.canHaveModifiers(statement)) {
      const modifiers = ts.getModifiers(statement) || [];
      if (modifiers.some((item) => item.kind === ts.SyntaxKind.ExportKeyword)) {
        if (statement.name && ts.isIdentifier(statement.name)) {
          exportedNames.add(statement.name.text);
        }
      }
      if (modifiers.some((item) => item.kind === ts.SyntaxKind.DefaultKeyword)) {
        exports.push({
          kind: 'default',
          name: statement.name && ts.isIdentifier(statement.name) ? statement.name.text : 'default',
          line: lineColOf(ts, sourceFile, statement).line,
        });
      }
    }
    if (ts.isExportDeclaration(statement) && statement.moduleSpecifier && ts.isStringLiteralLike(statement.moduleSpecifier)) {
      const specifier = statement.moduleSpecifier.text;
      imports.push({ kind: 'export_from', specifier, line: lineColOf(ts, sourceFile, statement).line });
      staticImports.push(specifier);
      exports.push({
        kind: statement.exportClause ? 'named' : 'all',
        name: '*',
        line: lineColOf(ts, sourceFile, statement).line,
      });
    }
    if (ts.isImportDeclaration(statement) && statement.moduleSpecifier && ts.isStringLiteralLike(statement.moduleSpecifier)) {
      const specifier = statement.moduleSpecifier.text;
      imports.push({
        kind: statement.importClause?.isTypeOnly ? 'type' : 'static',
        specifier,
        line: lineColOf(ts, sourceFile, statement).line,
      });
      staticImports.push(specifier);
      const feature = importFeature(specifier);
      if (feature) countFeature(feature);
    }
    if (ts.isFunctionDeclaration(statement) && statement.name) {
      addComponent(statement.name.text, 'function', statement.name, {
        async: Boolean(statement.modifiers?.some((item) => item.kind === ts.SyntaxKind.AsyncKeyword)),
        returnsJsx: hasJsxReturn(ts, statement),
      });
      const pos = lineColOf(ts, sourceFile, statement.name);
      const key = `function:${statement.name.text}:${pos.line}`;
      const entry = componentEntries.get(key);
      if (entry) {
        const counts = countDirectRenderSurface(statement.body);
        entry.surface_jsx_count = counts.total;
        entry.surface_intrinsic_jsx_count = counts.intrinsic;
        entry.surface_component_tag_count = counts.component;
        entry.surface_fragment_count = counts.fragment;
        entry.return_surfaces = collectDirectReturnSurfaces(statement.body);
      }
    }
    if (ts.isClassDeclaration(statement) && statement.name) {
      let isClassComponent = false;
      if (statement.heritageClauses) {
        for (const clause of statement.heritageClauses) {
          for (const type of clause.types) {
            const baseName = callName(ts, type.expression);
            if (baseName === 'Component') {
              isClassComponent = true;
              countFeature('class_component');
            }
          }
        }
      }
      let isErrorBoundary = false;
      for (const member of statement.members) {
        if (
          ts.isMethodDeclaration(member)
          && member.name
          && ts.isIdentifier(member.name)
          && member.name.text === 'componentDidCatch'
        ) {
          isErrorBoundary = true;
        }
        if (
          ts.isMethodDeclaration(member)
          && member.modifiers?.some((item) => item.kind === ts.SyntaxKind.StaticKeyword)
          && member.name
          && ts.isIdentifier(member.name)
          && member.name.text === 'getDerivedStateFromError'
        ) {
          isErrorBoundary = true;
        }
      }
      if (isErrorBoundary) countFeature('error_boundary');
      addComponent(statement.name.text, isClassComponent ? 'class' : 'class', statement.name, {
        async: false,
        returnsJsx: hasJsxReturn(ts, statement),
        errorBoundary: isErrorBoundary,
      });
      const pos = lineColOf(ts, sourceFile, statement.name);
      const key = `class:${statement.name.text}:${pos.line}`;
      const entry = componentEntries.get(key);
      if (entry) {
        const renderMethod = statement.members.find((member) =>
          ts.isMethodDeclaration(member)
          && member.name
          && ts.isIdentifier(member.name)
          && member.name.text === 'render'
        );
        const counts = countDirectRenderSurface(renderMethod?.body || null);
        entry.surface_jsx_count = counts.total;
        entry.surface_intrinsic_jsx_count = counts.intrinsic;
        entry.surface_component_tag_count = counts.component;
        entry.surface_fragment_count = counts.fragment;
        entry.return_surfaces = collectDirectReturnSurfaces(renderMethod?.body || null);
      }
    }
    if (ts.isVariableStatement(statement)) {
      const exported = Boolean(statement.modifiers?.some((item) => item.kind === ts.SyntaxKind.ExportKeyword));
      for (const decl of statement.declarationList.declarations) {
        if (!decl.name || !ts.isIdentifier(decl.name) || !decl.initializer) continue;
        const name = decl.name.text;
        if (exported) exportedNames.add(name);
        if (ts.isArrowFunction(decl.initializer) || ts.isFunctionExpression(decl.initializer)) {
          addComponent(name, 'function', decl.name, {
            async: Boolean(decl.initializer.modifiers?.some((item) => item.kind === ts.SyntaxKind.AsyncKeyword)),
            returnsJsx: hasJsxReturn(ts, decl.initializer.body),
          });
          const pos = lineColOf(ts, sourceFile, decl.name);
          const key = `function:${name}:${pos.line}`;
          const entry = componentEntries.get(key);
          if (entry) {
            const counts = countDirectRenderSurface(decl.initializer.body);
            entry.surface_jsx_count = counts.total;
            entry.surface_intrinsic_jsx_count = counts.intrinsic;
            entry.surface_component_tag_count = counts.component;
            entry.surface_fragment_count = counts.fragment;
            entry.return_surfaces = collectDirectReturnSurfaces(decl.initializer.body);
          }
        }
      }
    }
  }

  function visit(node) {
    maybeAddBlocker(node);

    if (ts.isCallExpression(node)) {
      if (node.expression.kind === ts.SyntaxKind.ImportKeyword && node.arguments.length > 0 && ts.isStringLiteralLike(node.arguments[0])) {
        const specifier = node.arguments[0].text;
        imports.push({ kind: 'dynamic', specifier, line: lineColOf(ts, sourceFile, node).line });
        dynamicImports.push(specifier);
      }
      const name = callName(ts, node.expression);
      const feature = CALL_FEATURES.get(name);
      if (feature) {
        countFeature(feature);
        hookCalls.push({
          name,
          line: lineColOf(ts, sourceFile, node).line,
          column: lineColOf(ts, sourceFile, node).column,
        });
      }
      if (name === 'dispatchEvent') {
        countFeature('custom_event');
      }
      if (
        ts.isPropertyAccessExpression(node.expression)
        && ts.isPropertyAccessExpression(node.expression.expression)
        && ts.isIdentifier(node.expression.expression.expression)
        && node.expression.expression.expression.text === 'navigator'
        && node.expression.expression.name.text === 'mediaDevices'
        && node.expression.name.text === 'getUserMedia'
      ) {
        countFeature('media');
      }
    }

    if (ts.isIdentifier(node)) {
      const feature = IDENTIFIER_FEATURES.get(node.text);
      if (feature) {
        countFeature(feature);
      }
    }

    if (
      ts.isPropertyAccessExpression(node)
      && ts.isIdentifier(node.expression)
      && node.expression.text === 'navigator'
      && node.name.text === 'mediaDevices'
    ) {
      countFeature('media');
    }

    if (ts.isJsxOpeningElement(node) || ts.isJsxSelfClosingElement(node)) {
      const tag = jsxTagName(ts, node.tagName);
      jsxElements.push({
        tag,
        line: lineColOf(ts, sourceFile, node).line,
      });
      if (tag === 'Suspense') {
        countFeature('suspense');
      }
      if (tag === 'StrictMode') {
        countFeature('strict_mode');
      }
      for (const attr of node.attributes.properties) {
        if (!ts.isJsxAttribute(attr) || !attr.name || attr.name.text !== 'className' || !attr.initializer) continue;
        if (ts.isStringLiteral(attr.initializer)) {
          classTokens.push(...attr.initializer.text.split(/\s+/).filter(Boolean));
        } else if (
          ts.isJsxExpression(attr.initializer)
          && attr.initializer.expression
          && ts.isNoSubstitutionTemplateLiteral(attr.initializer.expression)
        ) {
          classTokens.push(...attr.initializer.expression.text.split(/\s+/).filter(Boolean));
        }
      }
    }

    if (ts.isJsxFragment(node)) {
      jsxElements.push({
        tag: 'Fragment',
        line: lineColOf(ts, sourceFile, node).line,
      });
    }

    ts.forEachChild(node, visit);
  }

  visit(sourceFile);

  const features = Object.fromEntries(Object.entries(featureCounts).map(([name, count]) => [name, count > 0]));
  blockers.sort((a, b) => a.file.localeCompare(b.file) || a.line - b.line || a.column - b.column || a.code.localeCompare(b.code));
  components.sort((a, b) => a.line - b.line || a.name.localeCompare(b.name));
  imports.sort((a, b) => a.line - b.line || a.kind.localeCompare(b.kind) || a.specifier.localeCompare(b.specifier));
  hookCalls.sort((a, b) => a.line - b.line || a.column - b.column || a.name.localeCompare(b.name));
  jsxElements.sort((a, b) => a.line - b.line || a.tag.localeCompare(b.tag));
  const uniqueStaticImports = [...new Set(staticImports)].sort();
  const uniqueDynamicImports = [...new Set(dynamicImports)].sort();
  const assetRefs = [...new Set(imports.map((item) => item.specifier).filter((item) => ASSET_EXTS.has(path.extname(item).toLowerCase())))].sort();

  return {
    path: relPath,
    hash: sha256(text),
    ext,
    runtime_root: sourceRootForPath(relPath, runtimeRoots),
    generated: isGeneratedFile(relPath),
    jsx_like: ext === '.tsx' || ext === '.jsx' || jsxElements.length > 0,
    static_imports: uniqueStaticImports,
    dynamic_imports: uniqueDynamicImports,
    asset_refs: assetRefs,
    features,
    feature_counts: featureCounts,
    components,
    blockers,
    imports,
    hook_calls: hookCalls,
    jsx_elements: jsxElements,
    tailwind_class_tokens: [...new Set(classTokens)].sort(),
    tailwind_class_token_count: classTokens.length,
    export_count: exports.length,
  };
}

let tsGlobal = null;

function main() {
  const args = parseArgs(process.argv);
  const repo = path.resolve(args.repo);
  const runtimeRoots = normalizeRoots(repo, args.runtimeRoots);
  const ts = loadTypeScript(repo);
  tsGlobal = ts;
  const modules = walkSources(repo, runtimeRoots).map((fullPath) => scanModule(ts, repo, runtimeRoots, fullPath));
  const out = {
    format: 'tsx_ast_v1',
    repo_root: repo,
    source_roots: runtimeRoots,
    typescript_version: ts.version,
    modules,
  };
  if (args.outPath) {
    const outPath = path.resolve(args.outPath);
    fs.mkdirSync(path.dirname(outPath), { recursive: true });
    fs.writeFileSync(outPath, `${JSON.stringify(out, null, 2)}\n`, 'utf8');
  }
  writeSidecarSummary(args.summaryOut ? path.resolve(args.summaryOut) : '', {
    format: out.format,
    repo_root: repo,
    source_roots: runtimeRoots.join(','),
    typescript_version: ts.version,
    module_count: modules.length,
    out_path: args.outPath ? path.resolve(args.outPath) : '',
  });
  process.stdout.write(`${JSON.stringify(out)}\n`);
}

main();
