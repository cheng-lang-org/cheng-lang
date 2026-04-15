#!/usr/bin/env node
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { discoverTruthRoutes, normalizeRoots } from './r2c-react-v3-discover-truth-routes.mjs';
import { buildCodegenRouteCatalog } from './r2c-react-v3-route-matrix-shared.mjs';

function parseArgs(argv) {
  const out = {
    repo: '',
    tsxAstPath: '',
    outDir: '',
    summaryOut: '',
    runtimeRoots: [],
  };
  for (let i = 2; i < argv.length; i += 1) {
    const arg = argv[i];
    if (arg === '--repo') out.repo = String(argv[++i] || '');
    else if (arg === '--tsx-ast') out.tsxAstPath = String(argv[++i] || '');
    else if (arg === '--out-dir') out.outDir = String(argv[++i] || '');
    else if (arg === '--summary-out') out.summaryOut = String(argv[++i] || '');
    else if (arg === '--runtime-root') out.runtimeRoots.push(String(argv[++i] || ''));
    else if (arg === '-h' || arg === '--help') {
      console.log('Usage: r2c-react-v3-codegen-surface.mjs --repo <path> [--tsx-ast <file>] [--out-dir <dir>] [--runtime-root <dir>] [--summary-out <file>]');
      process.exit(0);
    }
  }
  if (!out.repo) throw new Error('missing --repo');
  return out;
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

function makeSafeIdent(text) {
  let out = String(text || '').replace(/[^A-Za-z0-9]+/g, '_').replace(/^_+|_+$/g, '');
  if (!out) out = 'module';
  if (/^[0-9]/.test(out)) out = `m_${out}`;
  return out;
}

function chengStr(text) {
  return `"${String(text || '').replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n').replace(/\t/g, '\\t')}"`;
}

function chengBool(value) {
  return value ? 'true' : 'false';
}

function renderStrArrayFn(name, values) {
  if (values.length === 0) {
    return `fn ${name}(): str[] =\n    return []\n`;
  }
  const lines = [`fn ${name}(): str[] =`, '    return ['];
  for (const value of values) lines.push(`        ${chengStr(value)},`);
  lines.push('    ]');
  return `${lines.join('\n')}\n`;
}

function renderRuntimeCheng() {
  return [
    'import std/strutils as strutil',
    'type',
    '    R2cGeneratedModuleSurface =',
    '        sourcePath: str',
    '        sourceHash: str',
    '        runtimeRoot: str',
    '        exportCount: int32',
    '        tailwindClassTokenCount: int32',
    '        componentCount: int32',
    '        hookCount: int32',
    '        effectCount: int32',
    '        stateSlotCount: int32',
    '        jsxElementCount: int32',
    '        generated: bool',
    '        jsxLike: bool',
    '',
    '    R2cGeneratedProjectSurface =',
    '        packageId: str',
    '        routeCatalogModule: str',
    '        routeCount: int32',
    '        routeIds: str[]',
    '        projectModule: str',
    '        mainModule: str',
    '        entryModule: str',
    '        moduleSourcePaths: str[]',
    '        modules: R2cGeneratedModuleSurface[]',
    '',
    'fn r2cGeneratedModuleSurfaceZero(): R2cGeneratedModuleSurface =',
    '    var out: R2cGeneratedModuleSurface',
    '    out.sourcePath = ""',
    '    out.sourceHash = ""',
    '    out.runtimeRoot = ""',
    '    out.exportCount = 0',
    '    out.tailwindClassTokenCount = 0',
    '    out.componentCount = 0',
    '    out.hookCount = 0',
    '    out.effectCount = 0',
    '    out.stateSlotCount = 0',
    '    out.jsxElementCount = 0',
    '    out.generated = false',
    '    out.jsxLike = false',
    '    return out',
    '',
    'fn r2cGeneratedProjectSurfaceZero(): R2cGeneratedProjectSurface =',
    '    var out: R2cGeneratedProjectSurface',
    '    out.packageId = ""',
    '    out.routeCatalogModule = ""',
    '    out.routeCount = 0',
    '    out.routeIds = []',
    '    out.projectModule = ""',
    '    out.mainModule = ""',
    '    out.entryModule = ""',
    '    out.moduleSourcePaths = []',
    '    out.modules = []',
    '    return out',
    '',
  ].join('\n');
}

function renderStubModule(name) {
  return `fn ${name}(): int32 =\n    return 0\n`;
}

function renderModuleCheng(module, runtimeModule) {
  const hookCalls = Array.isArray(module.hook_calls) ? module.hook_calls : [];
  const effectCount = hookCalls.filter((item) => String(item.name || '') === 'useEffect').length;
  const stateSlotCount = hookCalls.filter((item) => String(item.name || '') === 'useState').length;
  const jsxElementCount = Array.isArray(module.jsx_elements) ? module.jsx_elements.length : 0;
  const componentCount = Array.isArray(module.components) ? module.components.length : 0;
  return [
    `import ${runtimeModule} as runtime`,
    '',
    'fn r2cGeneratedModuleSurface(): runtime.R2cGeneratedModuleSurface =',
    '    var out = runtime.r2cGeneratedModuleSurfaceZero()',
    `    out.sourcePath = ${chengStr(module.path)}`,
    `    out.sourceHash = ${chengStr(module.hash || '')}`,
    `    out.runtimeRoot = ${chengStr(module.runtime_root || '')}`,
    `    out.exportCount = ${Number(module.export_count || 0)}`,
    `    out.tailwindClassTokenCount = ${Number(module.tailwind_class_token_count || 0)}`,
    `    out.componentCount = ${componentCount}`,
    `    out.hookCount = ${hookCalls.length}`,
    `    out.effectCount = ${effectCount}`,
    `    out.stateSlotCount = ${stateSlotCount}`,
    `    out.jsxElementCount = ${jsxElementCount}`,
    `    out.generated = ${chengBool(Boolean(module.generated))}`,
    `    out.jsxLike = ${chengBool(Boolean(module.jsx_like))}`,
    '    return out',
    '',
  ].join('\n');
}

function renderProjectCheng(manifest) {
  const lines = [`import ${manifest.runtime_module} as runtime`];
  for (const item of manifest.modules) {
    lines.push(`import ${item.import_path} as ${item.module_name}`);
  }
  lines.push('');
  lines.push(renderStrArrayFn('r2cGeneratedRouteIds', manifest.route_ids || []).trimEnd());
  lines.push('');
  lines.push(renderStrArrayFn('r2cGeneratedModuleSourcePaths', manifest.modules.map((item) => item.source_path)).trimEnd());
  lines.push('');
  lines.push('fn r2cGeneratedAllModules(): runtime.R2cGeneratedModuleSurface[] =');
  lines.push('    return [');
  for (const item of manifest.modules) {
    lines.push(`        ${item.module_name}.r2cGeneratedModuleSurface(),`);
  }
  lines.push('    ]');
  lines.push('');
  lines.push('fn r2cGeneratedProjectSurface(): runtime.R2cGeneratedProjectSurface =');
  lines.push('    var out = runtime.r2cGeneratedProjectSurfaceZero()');
  lines.push(`    out.packageId = ${chengStr(manifest.package_id)}`);
  lines.push(`    out.routeCatalogModule = ${chengStr(manifest.route_catalog_module)}`);
  lines.push(`    out.routeCount = ${Number(manifest.route_count || 0)}`);
  lines.push('    out.routeIds = r2cGeneratedRouteIds()');
  lines.push(`    out.projectModule = ${chengStr(manifest.project_module)}`);
  lines.push(`    out.mainModule = ${chengStr(manifest.main_module)}`);
  lines.push(`    out.entryModule = ${chengStr(manifest.entry_module)}`);
  lines.push('    out.moduleSourcePaths = r2cGeneratedModuleSourcePaths()');
  lines.push('    out.modules = r2cGeneratedAllModules()');
  lines.push('    return out');
  lines.push('');
  return lines.join('\n');
}

function renderProjectEntryCheng(manifest) {
  return [
    `import ${manifest.project_module} as project`,
    `import ${manifest.runtime_module} as runtime`,
    '',
    'fn r2cGeneratedProjectSurface(): runtime.R2cGeneratedProjectSurface =',
    '    return project.r2cGeneratedProjectSurface()',
    '',
  ].join('\n');
}

function renderAppRunnerCheng(manifest) {
  return [
    `import ${manifest.runtime_module} as runtime`,
    'import std/strutils as strutil',
    '',
    'type',
    '    R2cExecSnapshot =',
    '        routeState: str',
    '        mountPhase: str',
    '        commitPhase: str',
    '        renderReady: bool',
    '        semanticNodesLoaded: bool',
    '        semanticNodesCount: int32',
    '        moduleCount: int32',
    '        componentCount: int32',
    '        effectCount: int32',
    '        stateSlotCount: int32',
    '',
    'fn v3R2cExecSnapshotZero(): R2cExecSnapshot =',
    '    var out: R2cExecSnapshot',
    '    out.routeState = ""',
    '    out.mountPhase = ""',
    '    out.commitPhase = ""',
    '    out.renderReady = false',
    '    out.semanticNodesLoaded = false',
    '    out.semanticNodesCount = 0',
    '    out.moduleCount = 0',
    '    out.componentCount = 0',
    '    out.effectCount = 0',
    '    out.stateSlotCount = 0',
    '    return out',
    '',
    'fn v3R2cBuildHomeDefaultSnapshot(surface: runtime.R2cGeneratedProjectSurface): R2cExecSnapshot =',
    '    var out = v3R2cExecSnapshotZero()',
    '    out.routeState = "home_default"',
    '    out.mountPhase = "prepared"',
    '    out.commitPhase = "committed"',
    '    out.renderReady = true',
    '    if surface.modules.len > 0:',
    '        let entry = surface.modules[0]',
    '        out.semanticNodesLoaded = entry.jsxElementCount > 0',
    '        out.semanticNodesCount = entry.jsxElementCount',
    '        out.moduleCount = 1',
    '        out.componentCount = entry.componentCount',
    '        out.effectCount = entry.effectCount',
    '        out.stateSlotCount = entry.stateSlotCount',
    '    return out',
    '',
    'fn v3R2cBoolJson(value: bool): str =',
    '    if value:',
    '        return "true"',
    '    return "false"',
    '',
    'fn v3R2cSnapshotJson(snapshot: R2cExecSnapshot): str =',
    '    return "{" +',
    '        "\\"format\\":\\"cheng_codegen_exec_snapshot_v1\\"," +',
    '        "\\"route_state\\":\\"" + snapshot.routeState + "\\"," +',
    '        "\\"mount_phase\\":\\"" + snapshot.mountPhase + "\\"," +',
    '        "\\"commit_phase\\":\\"" + snapshot.commitPhase + "\\"," +',
    '        "\\"render_ready\\":" + v3R2cBoolJson(snapshot.renderReady) + "," +',
    '        "\\"semantic_nodes_loaded\\":" + v3R2cBoolJson(snapshot.semanticNodesLoaded) + "," +',
    '        "\\"semantic_nodes_count\\":" + strutil.intToStr(snapshot.semanticNodesCount) + "," +',
    '        "\\"module_count\\":" + strutil.intToStr(snapshot.moduleCount) + "," +',
    '        "\\"component_count\\":" + strutil.intToStr(snapshot.componentCount) + "," +',
    '        "\\"effect_count\\":" + strutil.intToStr(snapshot.effectCount) + "," +',
    '        "\\"state_slot_count\\":" + strutil.intToStr(snapshot.stateSlotCount) +',
    '        "}"',
    '',
    'fn v3R2cSmokeExitCode(surface: runtime.R2cGeneratedProjectSurface): int32 =',
    '    if surface.entryModule == "":',
    '        return 11',
    '    if surface.modules.len <= 0:',
    '        return 12',
    '    return 0',
    '',
  ].join('\n');
}

function renderMainCheng(manifest) {
  return [
    `import ${manifest.project_module} as project`,
    `import ${manifest.app_runner_module} as app_runner`,
    'import std/os',
    '',
    'fn main(): int32 =',
    '    let surface = project.r2cGeneratedProjectSurface()',
    '    let snapshot = app_runner.v3R2cBuildHomeDefaultSnapshot(surface)',
    '    os.writeLine(os.get_stdout(), app_runner.v3R2cSnapshotJson(snapshot))',
    '    return app_runner.v3R2cSmokeExitCode(surface)',
    '',
  ].join('\n');
}

function resolveEntryModule(modules) {
  const preferred = ['app/App.tsx', 'app/main.tsx', 'app/main.ts', 'app/index.tsx', 'app/index.ts'];
  const modulePaths = new Set(modules.map((module) => String(module.path || '')));
  for (const candidate of preferred) {
    if (modulePaths.has(candidate)) return candidate;
  }
  return modules[0]?.path || '';
}

function buildManifest(repo, outDir, modules, routeCatalog) {
  const packageRoot = path.resolve(outDir, 'cheng_codegen');
  const packageId = 'pkg://cheng/cheng_codegen';
  const modulePrefix = 'cheng_codegen';
  const packageImportPrefix = 'cheng/cheng_codegen';
  const entryModule = resolveEntryModule(modules);
  const usedNames = new Set();
  const generatedModules = [];
  for (const module of modules) {
    let safe = makeSafeIdent(module.path);
    if (usedNames.has(safe)) safe = `${safe}_${String(module.hash || '').slice(0, 8)}`;
    usedNames.add(safe);
    generatedModules.push({
      source_path: module.path,
      module_name: safe,
      import_path: `${packageImportPrefix}/modules/${safe}`,
      output_path: path.join(packageRoot, 'src', 'modules', `${safe}.cheng`),
    });
  }
  return {
    format: 'cheng_codegen_v1',
    repo_root: repo,
    package_root: packageRoot,
    package_id: packageId,
    module_prefix: modulePrefix,
    package_import_prefix: packageImportPrefix,
    runtime_module: `${packageImportPrefix}/runtime`,
    ui_host_module: `${packageImportPrefix}/ui_host`,
    react18_compat_module: `${packageImportPrefix}/react18_compat`,
    app_runner_module: `${packageImportPrefix}/app_runner`,
    route_catalog_module: `${packageImportPrefix}/project`,
    route_catalog_path: path.join(outDir, 'cheng_codegen_route_catalog_v1.json'),
    project_module: `${packageImportPrefix}/project`,
    entry_project_module: `${packageImportPrefix}/project_entry`,
    main_module: `${packageImportPrefix}/main`,
    entry_module: entryModule,
    runner_mode: 'codegen_surface_v1',
    route_count: Number(routeCatalog?.routeCount || 0),
    route_ids: Array.isArray(routeCatalog?.routes) ? routeCatalog.routes : [],
    modules: generatedModules,
  };
}

function writePackage(manifest, modules, routeCatalog) {
  const packageRoot = manifest.package_root;
  const srcRoot = path.join(packageRoot, 'src');
  const modulesRoot = path.join(srcRoot, 'modules');
  fs.rmSync(packageRoot, { recursive: true, force: true });
  fs.mkdirSync(modulesRoot, { recursive: true });
  writeText(path.join(packageRoot, 'cheng-package.toml'), `package_id = "${manifest.package_id}"\nmodule_prefix = "${manifest.module_prefix}"\n`);
  writeText(path.join(srcRoot, 'runtime.cheng'), renderRuntimeCheng());
  writeText(path.join(srcRoot, 'ui_host.cheng'), renderStubModule('r2cUiHostStub'));
  writeText(path.join(srcRoot, 'react18_compat.cheng'), renderStubModule('r2cReact18CompatStub'));
  writeText(path.join(srcRoot, 'app_runner.cheng'), renderAppRunnerCheng(manifest));
  for (const item of manifest.modules) {
    const moduleDoc = modules.find((module) => module.path === item.source_path);
    writeText(item.output_path, renderModuleCheng(moduleDoc, manifest.runtime_module));
  }
  writeText(path.join(srcRoot, 'project.cheng'), renderProjectCheng(manifest));
  writeText(path.join(srcRoot, 'project_entry.cheng'), renderProjectEntryCheng(manifest));
  writeText(path.join(srcRoot, 'main.cheng'), renderMainCheng(manifest));
}

function runCodegenSmoke(workspaceRoot, manifest) {
  const tool = process.env.R2C_REACT_V3_TOOLING_BIN || path.join(workspaceRoot, 'artifacts', 'v3_backend_driver', 'cheng');
  const packageRoot = manifest.package_root;
  const mainPath = path.join(packageRoot, 'src', 'main.cheng');
  const reportPath = path.join(packageRoot, 'emit_csg_report.txt');
  const logPath = path.join(packageRoot, 'emit_csg.log');
  const summary = {
    format: 'cheng_codegen_smoke_v1',
    ok: false,
    reason: '',
    tool,
    main_path: mainPath,
    report_path: reportPath,
    log_path: logPath,
    returncode: -1,
    package_id: '',
    source_bundle_cid: '',
    canonical_graph_cid: '',
    graph_cid: '',
    version: 0,
    node_count: 0,
    edge_count: 0,
  };
  if (!fs.existsSync(tool)) {
    summary.reason = 'missing_tooling_bin';
    return summary;
  }
  const result = spawnSync(tool, ['emit-csg', `--in:${mainPath}`, `--root:${packageRoot}`, `--report-out:${reportPath}`], {
    encoding: 'utf8',
  });
  const logChunks = [];
  if (result.stdout) logChunks.push(result.stdout);
  if (result.stderr) logChunks.push(result.stderr);
  writeText(logPath, logChunks.join('\n'));
  summary.returncode = Number(result.status ?? -1);
  if (summary.returncode !== 0) {
    summary.reason = 'emit_csg_failed';
    return summary;
  }
  summary.ok = true;
  summary.reason = 'ok';
  if (fs.existsSync(reportPath)) {
    const reportText = fs.readFileSync(reportPath, 'utf8');
    for (const line of reportText.split(/\r?\n/)) {
      const index = line.indexOf('=');
      if (index < 0) continue;
      const key = line.slice(0, index);
      const value = line.slice(index + 1);
      if (['package_id', 'source_bundle_cid', 'canonical_graph_cid', 'graph_cid'].includes(key)) {
        summary[key] = value;
      } else if (['version', 'node_count', 'edge_count'].includes(key)) {
        summary[key] = Number(value);
      }
    }
  }
  return summary;
}

const HOME_DEFAULT_EXCLUDED_TAGS = new Set([
  'PwaWanDmSmokePage',
  'PwaWanSessionSmokePage',
  'PwaContentMediaSyncSmokePage',
]);

function countHookCalls(moduleDoc, name) {
  const hookCalls = Array.isArray(moduleDoc?.hook_calls) ? moduleDoc.hook_calls : [];
  return hookCalls.filter((item) => String(item?.name || '') === name).length;
}

function buildBaseExecSnapshot(doc, manifest) {
  const modules = Array.isArray(doc?.modules) ? doc.modules : [];
  const entryModulePath = String(manifest.entry_module || modules[0]?.path || '');
  const entryModule = modules.find((item) => String(item?.path || '') === entryModulePath) || modules[0] || {};
  const hookCalls = Array.isArray(entryModule.hook_calls) ? entryModule.hook_calls : [];
  const jsxElements = (Array.isArray(entryModule.jsx_elements) ? entryModule.jsx_elements : [])
    .filter((item) => !HOME_DEFAULT_EXCLUDED_TAGS.has(String(item?.tag || '')));
  const components = Array.isArray(entryModule.components) ? entryModule.components : [];
  const effectCount = countHookCalls(entryModule, 'useEffect');
  const stateSlotCount = countHookCalls(entryModule, 'useState');
  const hookSlotCount = hookCalls.length;
  const semanticNodesCount = jsxElements.length;
  const componentCount = components.length;
  return {
    format: 'cheng_codegen_exec_snapshot_v1',
    route_state: 'home_default',
    mount_phase: 'prepared',
    commit_phase: 'committed',
    render_ready: true,
    semantic_nodes_loaded: semanticNodesCount > 0,
    semantic_nodes_count: semanticNodesCount,
    module_count: modules.length,
    component_count: componentCount,
    effect_count: effectCount,
    state_slot_count: stateSlotCount,
    passive_effects_flushed: false,
    passive_effect_flush_count: 0,
    hook_slot_count: hookSlotCount,
    effect_slot_count: effectCount,
    component_instance_count: componentCount,
    tree_node_count: semanticNodesCount,
    root_node_count: semanticNodesCount > 0 ? 1 : 0,
    module_node_count: modules.length,
    component_node_count: componentCount,
    element_node_count: semanticNodesCount,
    hook_node_count: hookSlotCount,
    effect_node_count: effectCount,
  };
}

function main() {
  const args = parseArgs(process.argv);
  const workspaceRoot = resolveWorkspaceRoot();
  const repo = path.resolve(args.repo);
  const outDir = path.resolve(args.outDir || path.join(repo, 'build', 'r2c_react_v3_cheng'));
  const tsxAstPath = path.resolve(args.tsxAstPath || path.join(outDir, 'tsx_ast_v1.json'));
  const doc = readJson(tsxAstPath);
  if (String(doc.format || '') !== 'tsx_ast_v1') {
    throw new Error(`unexpected tsx ast format: ${String(doc.format || '')}`);
  }
  const modules = Array.isArray(doc.modules) ? doc.modules : [];
  const runtimeRoots = normalizeRoots(repo, args.runtimeRoots);
  const truthRoutesDoc = discoverTruthRoutes(repo, runtimeRoots);
  const routeIds = Array.isArray(truthRoutesDoc.routes) ? truthRoutesDoc.routes.map((entry) => String(entry?.routeId || '').trim()).filter(Boolean) : [];
  const manifestStub = buildManifest(repo, outDir, modules, { routeCount: routeIds.length });
  const baseSnapshot = buildBaseExecSnapshot(doc, manifestStub);
  const routeCatalog = buildCodegenRouteCatalog(baseSnapshot, routeIds, 'react_surface_runner_v1', modules, repo);
  routeCatalog.outPath = path.join(outDir, 'cheng_codegen_route_catalog_v1.json');
  const manifest = buildManifest(repo, outDir, modules, routeCatalog);
  writePackage(manifest, modules, routeCatalog);
  const smoke = runCodegenSmoke(workspaceRoot, manifest);
  if (!smoke.ok) {
    writeJson(path.join(outDir, 'cheng_codegen_v1.json'), manifest);
    writeJson(path.join(outDir, 'cheng_codegen_route_catalog_v1.json'), routeCatalog);
    writeJson(path.join(outDir, 'cheng_codegen_smoke_v1.json'), smoke);
    writeSummary(args.summaryOut, {
      module_count: modules.length,
      codegen_module_count: manifest.modules.length,
      route_count: routeCatalog.routeCount,
      package_root: manifest.package_root,
      package_id: manifest.package_id,
      runner_mode: manifest.runner_mode,
      route_catalog_path: routeCatalog.outPath,
      smoke_ok: false,
      smoke_reason: smoke.reason,
      smoke_report_path: smoke.report_path,
      smoke_log_path: smoke.log_path,
      typescript_version: String(doc.typescript_version || ''),
      codegen_manifest_path: path.join(outDir, 'cheng_codegen_v1.json'),
    });
    process.exit(1);
  }
  const manifestPath = path.join(outDir, 'cheng_codegen_v1.json');
  const routeCatalogPath = path.join(outDir, 'cheng_codegen_route_catalog_v1.json');
  const smokePath = path.join(outDir, 'cheng_codegen_smoke_v1.json');
  writeJson(manifestPath, manifest);
  writeJson(routeCatalogPath, routeCatalog);
  writeJson(smokePath, smoke);
  writeSummary(args.summaryOut, {
    module_count: modules.length,
    codegen_module_count: manifest.modules.length,
    route_count: routeCatalog.routeCount,
    package_root: manifest.package_root,
    package_id: manifest.package_id,
    runner_mode: manifest.runner_mode,
    route_catalog_path: routeCatalogPath,
    smoke_ok: true,
    smoke_reason: smoke.reason,
    smoke_report_path: smoke.report_path,
    smoke_log_path: smoke.log_path,
    smoke_node_count: smoke.node_count,
    smoke_edge_count: smoke.edge_count,
    typescript_version: String(doc.typescript_version || ''),
    codegen_manifest_path: manifestPath,
    route_catalog_path: routeCatalogPath,
    codegen_smoke_path: smokePath,
  });
  console.log(JSON.stringify({
    ok: true,
    repo_root: repo,
    out_dir: outDir,
    module_count: modules.length,
    codegen_module_count: manifest.modules.length,
    route_count: routeCatalog.routeCount,
    package_root: manifest.package_root,
    smoke_ok: true,
    smoke_node_count: smoke.node_count,
    smoke_edge_count: smoke.edge_count,
  }, null, 2));
}

main();
