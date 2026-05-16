import { createHash } from "node:crypto";
import path from "node:path";
import * as ts from "typescript";

import type { CsgFact, ExtractOptions, SourceLoc } from "./schema.js";
import { stableJson } from "./stable-json.js";

export const CsgCoreSchema = "csg-core" as const;
export const CsgCoreVersion = 1 as const;
export const CsgCoreReportSchema = "csg-core.report" as const;

export interface CsgCoreOptions extends ExtractOptions {
  entryRoots?: string[];
  runtime?: string[];
}

export interface CsgCoreIssue {
  id: string;
  code: string;
  message: string;
  loc?: SourceLoc | undefined;
  owner?: string | undefined;
}

export interface CsgCoreRuntimeRequirement {
  id: string;
  kind: string;
  name: string;
  runtime: string;
  source: string;
  loc?: SourceLoc | undefined;
  owner?: string | undefined;
}

export interface CsgCoreExternalSymbol {
  id: string;
  name: string;
  source: string;
  runtime: string;
}

export interface CsgCoreCounts {
  sourceFiles: number;
  modules: number;
  imports: number;
  exports: number;
  symbols: number;
  types: number;
  functions: number;
  blocks: number;
  terms: number;
  ops: number;
  calls: number;
  data: number;
  runtimeRequirements: number;
  externalSymbols: number;
  unsupported: number;
}

export interface CsgCoreReport {
  schema: typeof CsgCoreReportSchema;
  version: typeof CsgCoreVersion;
  complete: boolean;
  projectRoot: string;
  projectFile?: string | undefined;
  runtimes: string[];
  entryRoots: string[];
  counts: CsgCoreCounts;
  unsupported: CsgCoreIssue[];
  runtimeRequirements: CsgCoreRuntimeRequirement[];
  externalSymbols: CsgCoreExternalSymbol[];
  diagnostics: string[];
}

export interface CsgCoreResult {
  facts: CsgFact[];
  diagnostics: string[];
  report: CsgCoreReport;
  text: string;
}

type ProgramBuild = {
  cwd: string;
  projectFile?: string;
  program: ts.Program;
  configDiagnostics: ts.Diagnostic[];
  compilerOptions: ts.CompilerOptions;
};

type FunctionContext = {
  id: string;
  name: string;
  sourceFile: ts.SourceFile;
  nextOp: number;
  nextBlock: number;
};

type RuntimeClassification = {
  runtime: string;
  source: string;
};

type BindingPathSegment =
  | { kind: "property"; name: string }
  | { kind: "index"; index: number }
  | { kind: "rest"; index?: number | undefined };

type BindingLeaf = {
  name: string;
  node: ts.Identifier;
  path: BindingPathSegment[];
  rest: boolean;
  defaultInitializerKind?: string | undefined;
};

const allowedRuntimes = new Set(["node", "browser"]);
const defaultEntryRoots = ["src/index.ts", "src/main.ts", "src/renderer.ts"];
const nodeBuiltins = new Set([
  "assert",
  "buffer",
  "child_process",
  "crypto",
  "events",
  "fs",
  "http",
  "https",
  "net",
  "os",
  "path",
  "process",
  "stream",
  "timers",
  "url",
  "util",
  "worker_threads",
  "zlib",
]);
const browserGlobals = new Set([
  "AbortController",
  "Blob",
  "CustomEvent",
  "Document",
  "Element",
  "Event",
  "EventTarget",
  "File",
  "HTMLElement",
  "HTMLInputElement",
  "HTMLTextAreaElement",
  "KeyboardEvent",
  "MouseEvent",
  "MutationObserver",
  "Node",
  "Request",
  "Response",
  "URL",
  "WebSocket",
  "Window",
  "document",
  "fetch",
  "history",
  "localStorage",
  "location",
  "navigator",
  "sessionStorage",
  "window",
]);
const jsCoreGlobals = new Set([
  "Array",
  "Boolean",
  "Date",
  "Error",
  "JSON",
  "Map",
  "Math",
  "Number",
  "Object",
  "Promise",
  "Reflect",
  "RegExp",
  "Set",
  "String",
  "Symbol",
  "WeakMap",
  "WeakSet",
  "console",
  "parseInt",
  "setInterval",
  "setTimeout",
  "undefined",
]);

export function emitCsgCoreFromTs(options: CsgCoreOptions): CsgCoreResult {
  const runtimes = normalizeRuntime(options.runtime);
  const build = buildProgram(options);
  const diagnostics = [
    ...validateRuntimeOptions(options.runtime),
    ...build.configDiagnostics.map((diag) => formatDiagnostic(build.cwd, diag)),
    ...ts.getPreEmitDiagnostics(build.program).filter((diag) => diag.category === ts.DiagnosticCategory.Error).map((diag) => formatDiagnostic(build.cwd, diag)),
  ];

  if (diagnostics.length > 0) {
    const report = emptyReport(build, runtimes, diagnostics);
    return { facts: [], diagnostics, report, text: "" };
  }

  const checker = build.program.getTypeChecker();
  const sourceFiles = build.program
    .getSourceFiles()
    .filter((sourceFile) => isProjectSourceFile(build.cwd, sourceFile))
    .sort((left, right) => relPath(build.cwd, left.fileName).localeCompare(relPath(build.cwd, right.fileName)));

  const facts: CsgFact[] = [];
  const unsupportedById = new Map<string, CsgCoreIssue>();
  const runtimeById = new Map<string, CsgCoreRuntimeRequirement>();
  const externalById = new Map<string, CsgCoreExternalSymbol>();
  const dataById = new Map<string, CsgFact>();
  const typeById = new Map<string, CsgFact>();
  const functionStack: FunctionContext[] = [];

  emitFact({
    kind: "csg.core.schema",
    id: stableId("csg.core.schema", "typescript"),
    language: "typescript",
    schema: CsgCoreSchema,
    version: CsgCoreVersion,
  });
  emitFact({
    kind: "csg.project",
    id: stableId("csg.project", build.cwd, String(sourceFiles.length)),
    root: toPosix(path.resolve(build.cwd)),
    projectFile: build.projectFile ? relPath(build.cwd, build.projectFile) : undefined,
    sourceFileCount: sourceFiles.length,
    runtimes,
    compilerOptions: compilerOptionsFact(build.compilerOptions),
  });

  for (const sourceFile of sourceFiles) {
    emitModule(sourceFile);
  }
  for (const sourceFile of sourceFiles) {
    visit(sourceFile, sourceFile);
  }

  for (const fact of [...typeById.values()].sort(compareFactId)) {
    facts.push(fact);
  }
  for (const fact of [...dataById.values()].sort(compareFactId)) {
    facts.push(fact);
  }
  const runtimeRequirements = [...runtimeById.values()].sort(compareById);
  const externalSymbols = [...externalById.values()].sort(compareById);
  const unsupported = [...unsupportedById.values()].sort(compareIssue);
  for (const item of runtimeRequirements) {
    facts.push({
      kind: "csg.runtime_requirement",
      id: item.id,
      name: item.name,
      runtime: item.runtime,
      requirementKind: item.kind,
      source: item.source,
      ...(item.loc ? { loc: item.loc } : {}),
      ...(item.owner ? { owner: item.owner } : {}),
    });
  }
  for (const item of externalSymbols) {
    facts.push({
      kind: "csg.external_symbol",
      id: item.id,
      name: item.name,
      runtime: item.runtime,
      source: item.source,
    });
  }
  for (const item of unsupported) {
    facts.push({
      kind: "csg.unsupported",
      id: item.id,
      code: item.code,
      message: item.message,
      ...(item.loc ? { loc: item.loc } : {}),
      ...(item.owner ? { owner: item.owner } : {}),
    });
  }

  const counts = countFacts(facts, sourceFiles.length);
  const report: CsgCoreReport = {
    schema: CsgCoreReportSchema,
    version: CsgCoreVersion,
    complete: unsupported.length === 0 && runtimeRequirements.length === 0,
    projectRoot: toPosix(path.resolve(build.cwd)),
    projectFile: build.projectFile ? relPath(build.cwd, build.projectFile) : undefined,
    runtimes,
    entryRoots: entryRoots(build.cwd, options.entryRoots, sourceFiles),
    counts,
    unsupported,
    runtimeRequirements,
    externalSymbols,
    diagnostics: [],
  };
  const text = facts.map((fact) => stableJson(fact, options.pretty)).join("\n") + "\n";
  return { facts, diagnostics: [], report, text };

  function visit(node: ts.Node, sourceFile: ts.SourceFile): void {
    inspectUnsupported(node, sourceFile);

    if (ts.isImportDeclaration(node)) {
      emitImport(node, sourceFile);
    } else if (ts.isExportDeclaration(node)) {
      emitExport(node, sourceFile);
    } else if (ts.isExportAssignment(node)) {
      emitExportAssignment(node, sourceFile);
    } else if (ts.isVariableDeclaration(node)) {
      emitVariable(node, sourceFile);
    } else if (ts.isClassDeclaration(node)) {
      emitClass(node, sourceFile);
    } else if (ts.isInterfaceDeclaration(node)) {
      emitSymbol("interface", node, sourceFile, node.name.text);
    } else if (ts.isTypeAliasDeclaration(node)) {
      emitSymbol("type_alias", node, sourceFile, node.name.text);
    } else if (ts.isEnumDeclaration(node)) {
      emitSymbol("enum", node, sourceFile, node.name.text);
    } else if (ts.isJsxElement(node) || ts.isJsxSelfClosingElement(node) || ts.isJsxFragment(node)) {
      emitJsx(node, sourceFile);
    }

    if (isFunctionLikeWithBody(node)) {
      emitFunction(node, sourceFile);
      return;
    }

    if (ts.isCallExpression(node)) {
      emitCall(node, sourceFile);
    } else if (ts.isNewExpression(node)) {
      emitNew(node, sourceFile);
    } else if (ts.isPropertyAccessExpression(node)) {
      inspectRuntimeExpression(node, sourceFile);
    } else if (ts.isIdentifier(node)) {
      inspectRuntimeIdentifier(node, sourceFile);
    }

    ts.forEachChild(node, (child) => visit(child, sourceFile));
  }

  function emitModule(sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "csg.module",
      id: fileId(sourceFile),
      path: relPath(build.cwd, sourceFile.fileName),
      sha256: sha256(sourceFile.text),
      isDeclarationFile: sourceFile.isDeclarationFile,
    });
  }

  function emitImport(node: ts.ImportDeclaration, sourceFile: ts.SourceFile): void {
    const moduleName = stringModuleSpecifier(node.moduleSpecifier);
    if (!moduleName) return;
    const classification = classifyModuleSpecifier(moduleName);
    const clause = node.importClause;
    const namedBindings = clause?.namedBindings;
    emitFact({
      kind: "csg.import",
      id: nodeId("csg.import", node, sourceFile, moduleName),
      loc: locFor(sourceFile, node),
      module: moduleName,
      classification: classification.source,
      runtime: classification.runtime,
      typeOnly: clause?.isTypeOnly === true,
      defaultName: clause?.name?.text,
      namespaceName: namedBindings && ts.isNamespaceImport(namedBindings) ? namedBindings.name.text : undefined,
      named: namedBindings && ts.isNamedImports(namedBindings)
        ? namedBindings.elements.map((item) => ({
            name: item.name.text,
            propertyName: item.propertyName?.text,
            typeOnly: item.isTypeOnly,
          }))
        : [],
    });
    if (classification.source !== "project") {
      addRuntimeRequirement(sourceFile, node, "module_import", moduleName, classification, currentOwner());
      addExternalSymbol(moduleName, classification);
    }
  }

  function emitExport(node: ts.ExportDeclaration, sourceFile: ts.SourceFile): void {
    const moduleName = stringModuleSpecifier(node.moduleSpecifier);
    emitFact({
      kind: "csg.export",
      id: nodeId("csg.export", node, sourceFile, moduleName ?? "local"),
      loc: locFor(sourceFile, node),
      module: moduleName,
      typeOnly: node.isTypeOnly,
      named: node.exportClause && ts.isNamedExports(node.exportClause)
        ? node.exportClause.elements.map((item) => ({
            name: item.name.text,
            propertyName: item.propertyName?.text,
            typeOnly: item.isTypeOnly,
          }))
        : [],
      namespace: node.exportClause && ts.isNamespaceExport(node.exportClause) ? node.exportClause.name.text : undefined,
    });
    if (moduleName) {
      const classification = classifyModuleSpecifier(moduleName);
      if (classification.source !== "project") {
        addRuntimeRequirement(sourceFile, node, "module_export", moduleName, classification, currentOwner());
        addExternalSymbol(moduleName, classification);
      }
    }
  }

  function emitExportAssignment(node: ts.ExportAssignment, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "csg.export",
      id: nodeId("csg.export", node, sourceFile, "assignment"),
      loc: locFor(sourceFile, node),
      exportEquals: node.isExportEquals,
      expressionKind: syntaxKindName(node.expression.kind),
      expressionText: node.expression.getText(sourceFile),
    });
  }

  function emitSymbol(symbolKind: string, node: ts.Declaration, sourceFile: ts.SourceFile, name: string): string {
    const symbol = symbolForDeclaration(node);
    const symbolId = stableId("csg.symbol", symbolKind, relPath(build.cwd, sourceFile.fileName), name, String(node.pos), String(node.end));
    emitFact({
      kind: "csg.symbol",
      id: symbolId,
      loc: locFor(sourceFile, node),
      name,
      symbolKind,
      fqName: symbol ? normalizeCheckerText(checker.getFullyQualifiedName(symbol)) : name,
      flags: symbol ? symbolFlags(symbol.flags) : [],
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
    });
    return symbolId;
  }

  function emitClass(node: ts.ClassDeclaration, sourceFile: ts.SourceFile): void {
    const name = node.name?.text ?? "<anonymous>";
    const symbolId = emitSymbol("class", node, sourceFile, name);
    emitFact({
      kind: "csg.class",
      id: nodeId("csg.class", node, sourceFile, name),
      loc: locFor(sourceFile, node),
      name,
      symbol: symbolId,
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
      abstract: hasModifier(node, ts.SyntaxKind.AbstractKeyword),
      typeParameters: typeParameterTexts(node.typeParameters),
      heritageCount: node.heritageClauses?.reduce((sum, clause) => sum + clause.types.length, 0) ?? 0,
    });
    for (const clause of node.heritageClauses ?? []) {
      for (const item of clause.types) {
        const target = checker.getSymbolAtLocation(item.expression);
        emitFact({
          kind: "csg.class_heritage",
          id: nodeId("csg.class_heritage", item, sourceFile, `${name}:${item.expression.getText(sourceFile)}`),
          loc: locFor(sourceFile, item),
          class: nodeId("csg.class", node, sourceFile, name),
          heritageKind: syntaxKindName(clause.token),
          expression: item.expression.getText(sourceFile),
          target: symbolFact(target, sourceFile),
          typeArguments: item.typeArguments?.map((typeArg) => typeArg.getText(sourceFile)) ?? [],
        });
        addRuntimeRequirement(sourceFile, item, "class_heritage", `${name}:${item.expression.getText(sourceFile)}`, { runtime: "js-core", source: "ecmascript_class" }, currentOwner());
      }
    }
  }

  function emitJsx(node: ts.JsxElement | ts.JsxSelfClosingElement | ts.JsxFragment, sourceFile: ts.SourceFile): void {
    const tagName = jsxTagName(node, sourceFile);
    emitFact({
      kind: "csg.jsx",
      id: nodeId("csg.jsx", node, sourceFile, tagName),
      loc: locFor(sourceFile, node),
      owner: currentOwner(),
      tagName,
      jsxKind: syntaxKindName(node.kind),
      attributeCount: jsxAttributeCount(node),
      childCount: ts.isJsxElement(node) || ts.isJsxFragment(node) ? node.children.length : 0,
    });
    addRuntimeRequirement(sourceFile, node, "jsx", tagName || "fragment", { runtime: "js-core", source: "jsx_runtime" }, currentOwner());
  }

  function emitVariable(node: ts.VariableDeclaration, sourceFile: ts.SourceFile): void {
    const name = bindingNameText(node.name, sourceFile);
    const type = checker.getTypeAtLocation(node.name);
    addTypeFact(sourceFile, node.name, "variable", type);
    emitFact({
      kind: "csg.symbol",
      id: nodeId("csg.symbol", node, sourceFile, name),
      loc: locFor(sourceFile, node),
      name,
      symbolKind: "variable",
      declarationKind: variableDeclarationKind(node),
      type: typeText(type),
      initializerKind: node.initializer ? syntaxKindName(node.initializer.kind) : undefined,
      exported: isExportedVariable(node),
    });
    if (!ts.isIdentifier(node.name)) {
      emitBindingFacts(node.name, sourceFile, variableDeclarationKind(node), currentOwner());
    }
    if (containsAny(type, node.name)) {
      addUnsupported(sourceFile, node, "type.any", "variable resolves to any", currentOwner());
    }
  }

  function emitFunction(node: FunctionLikeWithBody, sourceFile: ts.SourceFile): void {
    const name = declarationName(node, sourceFile);
    const signature = checker.getSignatureFromDeclaration(node);
    const returnType = signature ? checker.getReturnTypeOfSignature(signature) : checker.getTypeAtLocation(node);
    const functionId = nodeId("csg.function", node, sourceFile, name);
    const symbolId = emitSymbol("function", node, sourceFile, name);
    const context: FunctionContext = {
      id: functionId,
      name,
      sourceFile,
      nextOp: 0,
      nextBlock: 0,
    };
    emitFact({
      kind: "csg.function",
      id: functionId,
      loc: locFor(sourceFile, node),
      name,
      symbol: symbolId,
      async: hasModifier(node, ts.SyntaxKind.AsyncKeyword),
      generator: "asteriskToken" in node && node.asteriskToken !== undefined,
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
      typeParameters: typeParameterTexts(functionTypeParameters(node)),
      parameters: node.parameters.map((param, index) => parameterFact(param, sourceFile, index)),
      returnType: typeText(returnType),
    });
    if (containsAny(returnType, node)) {
      addUnsupported(sourceFile, node, "type.any", "function return resolves to any", functionId);
    }
    if (hasModifier(node, ts.SyntaxKind.AsyncKeyword)) {
      emitFact({
        kind: "csg.async_function",
        id: stableId("csg.async_function", functionId),
        loc: locFor(sourceFile, node),
        function: functionId,
        runtime: "js-core",
        stateMachine: "promise",
      });
      addRuntimeRequirement(sourceFile, node, "async_function", name, { runtime: "js-core", source: "ecmascript_promise" }, functionId);
    }
    if ("asteriskToken" in node && node.asteriskToken) {
      addUnsupported(sourceFile, node, "function.generator", "generators require iterator state-machine lowering", functionId);
    }

    functionStack.push(context);
    const entryBlock = newBlock(context, node.body, "entry");
    if (ts.isBlock(node.body)) {
      emitStatementList(context, entryBlock, node.body.statements, sourceFile);
      emitTerm(context, entryBlock, terminalKind(node.body.statements), []);
    } else {
      const valueOp = emitExpression(context, entryBlock, node.body, sourceFile);
      emitOp(context, entryBlock, node.body, "return", { value: valueOp });
      emitTerm(context, entryBlock, "return", []);
    }
    functionStack.pop();
  }

  function emitStatementList(context: FunctionContext, blockId: string, statements: readonly ts.Statement[], sourceFile: ts.SourceFile): void {
    for (const statement of statements) {
      emitStatement(context, blockId, statement, sourceFile);
    }
  }

  function emitStatement(context: FunctionContext, blockId: string, node: ts.Statement, sourceFile: ts.SourceFile): void {
    if (ts.isVariableStatement(node)) {
      emitOp(context, blockId, node, "var_statement", { declarationCount: node.declarationList.declarations.length });
      for (const declaration of node.declarationList.declarations) {
        emitVariable(declaration, sourceFile);
        const initializerOp = declaration.initializer ? emitExpression(context, blockId, declaration.initializer, sourceFile) : undefined;
        if (!ts.isIdentifier(declaration.name)) {
          emitBindingExtractOps(context, blockId, declaration.name, sourceFile, initializerOp);
        }
      }
      return;
    }
    if (ts.isFunctionDeclaration(node) && node.body) {
      emitFunction(node as ts.FunctionDeclaration & { body: ts.FunctionBody }, sourceFile);
      emitOp(context, blockId, node, "function_decl", { name: node.name?.text ?? "<anonymous>" });
      return;
    }
    if (ts.isExpressionStatement(node)) {
      emitExpression(context, blockId, node.expression, sourceFile);
      return;
    }
    if (ts.isReturnStatement(node)) {
      const valueOp = node.expression ? emitExpression(context, blockId, node.expression, sourceFile) : undefined;
      emitOp(context, blockId, node, "return", { value: valueOp });
      return;
    }
    if (ts.isIfStatement(node)) {
      const condOp = emitExpression(context, blockId, node.expression, sourceFile);
      const thenBlock = newBlock(context, node.thenStatement, "if.then");
      emitStatement(context, thenBlock, asStatement(node.thenStatement), sourceFile);
      const elseBlock = node.elseStatement ? newBlock(context, node.elseStatement, "if.else") : undefined;
      if (node.elseStatement && elseBlock) emitStatement(context, elseBlock, asStatement(node.elseStatement), sourceFile);
      emitOp(context, blockId, node, "branch_if", { condition: condOp, thenBlock, elseBlock });
      return;
    }
    if (ts.isWhileStatement(node)) {
      const conditionBlock = newBlock(context, node.expression, "while.condition");
      const bodyBlock = newBlock(context, node.statement, "while.body");
      const condOp = emitExpression(context, conditionBlock, node.expression, sourceFile);
      emitStatement(context, bodyBlock, asStatement(node.statement), sourceFile);
      emitTerm(context, conditionBlock, "branch", [bodyBlock, blockId]);
      emitOp(context, blockId, node, "while", { conditionBlock, bodyBlock, condition: condOp });
      return;
    }
    if (ts.isForOfStatement(node) || ts.isForInStatement(node)) {
      const iterableOp = emitExpression(context, blockId, node.expression, sourceFile);
      const bodyBlock = newBlock(context, node.statement, ts.isForOfStatement(node) ? "for_of.body" : "for_in.body");
      emitStatement(context, bodyBlock, asStatement(node.statement), sourceFile);
      addRuntimeRequirement(
        sourceFile,
        node,
        ts.isForOfStatement(node) ? "iterator_loop" : "property_iterator_loop",
        syntaxKindName(node.kind),
        { runtime: "js-core", source: "ecmascript_iterator" },
        context.id,
      );
      emitOp(context, blockId, node, ts.isForOfStatement(node) ? "for_of" : "for_in", {
        initializerKind: syntaxKindName(node.initializer.kind),
        iterable: iterableOp,
        bodyBlock,
      });
      return;
    }
    if (ts.isBlock(node)) {
      const nested = newBlock(context, node, "block");
      emitStatementList(context, nested, node.statements, sourceFile);
      emitOp(context, blockId, node, "block", { block: nested });
      return;
    }
    if (ts.isTryStatement(node)) {
      const tryBlock = newBlock(context, node.tryBlock, "try.body");
      emitStatement(context, tryBlock, node.tryBlock, sourceFile);
      const catchBlock = node.catchClause ? newBlock(context, node.catchClause.block, "try.catch") : undefined;
      if (node.catchClause?.variableDeclaration && !ts.isIdentifier(node.catchClause.variableDeclaration.name)) {
        emitBindingFacts(node.catchClause.variableDeclaration.name, sourceFile, "catch", context.id);
      }
      if (node.catchClause && catchBlock) emitStatement(context, catchBlock, node.catchClause.block, sourceFile);
      const finallyBlock = node.finallyBlock ? newBlock(context, node.finallyBlock, "try.finally") : undefined;
      if (node.finallyBlock && finallyBlock) emitStatement(context, finallyBlock, node.finallyBlock, sourceFile);
      addRuntimeRequirement(sourceFile, node, "exception_region", "try", { runtime: "js-core", source: "ecmascript_exception" }, context.id);
      emitOp(context, blockId, node, "try", { tryBlock, catchBlock, finallyBlock });
      return;
    }
    if (ts.isThrowStatement(node)) {
      const valueOp = node.expression ? emitExpression(context, blockId, node.expression, sourceFile) : undefined;
      emitOp(context, blockId, node, "throw", { value: valueOp });
      return;
    }
    emitOp(context, blockId, node, "statement", { statementKind: syntaxKindName(node.kind) });
  }

  function emitExpression(context: FunctionContext, blockId: string, node: ts.Expression, sourceFile: ts.SourceFile): string {
    if (ts.isParenthesizedExpression(node)) {
      return emitExpression(context, blockId, node.expression, sourceFile);
    }
    if (ts.isSpreadElement(node)) {
      const value = emitExpression(context, blockId, node.expression, sourceFile);
      addRuntimeRequirement(sourceFile, node, "spread", "spread", { runtime: "js-core", source: "ecmascript_iterator" }, currentOwner());
      return emitOp(context, blockId, node, "spread", { value });
    }
    if (ts.isNumericLiteral(node) || ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node)) {
      const value = ts.isNumericLiteral(node) ? Number(node.text) : node.text;
      const dataId = addDataFact(sourceFile, node, ts.isNumericLiteral(node) ? "number" : "string", value);
      return emitOp(context, blockId, node, "literal", { data: dataId, literalKind: ts.isNumericLiteral(node) ? "number" : "string" });
    }
    if (node.kind === ts.SyntaxKind.TrueKeyword || node.kind === ts.SyntaxKind.FalseKeyword || node.kind === ts.SyntaxKind.NullKeyword) {
      const value = node.kind === ts.SyntaxKind.TrueKeyword ? true : node.kind === ts.SyntaxKind.FalseKeyword ? false : null;
      const dataId = addDataFact(sourceFile, node, value === null ? "null" : "boolean", value);
      return emitOp(context, blockId, node, "literal", { data: dataId, literalKind: value === null ? "null" : "boolean" });
    }
    if (ts.isIdentifier(node)) {
      inspectRuntimeIdentifier(node, sourceFile);
      return emitOp(context, blockId, node, "identifier", { name: node.text });
    }
    if (ts.isCallExpression(node)) {
      emitCall(node, sourceFile);
      for (const arg of node.arguments) emitExpression(context, blockId, arg, sourceFile);
      return emitOp(context, blockId, node, "call", { callee: node.expression.getText(sourceFile), argumentCount: node.arguments.length });
    }
    if (ts.isNewExpression(node)) {
      emitNew(node, sourceFile);
      for (const arg of node.arguments ?? []) emitExpression(context, blockId, arg, sourceFile);
      return emitOp(context, blockId, node, "new", { constructor: node.expression.getText(sourceFile), argumentCount: node.arguments?.length ?? 0 });
    }
    if (ts.isJsxElement(node) || ts.isJsxSelfClosingElement(node) || ts.isJsxFragment(node)) {
      emitJsx(node, sourceFile);
      return emitOp(context, blockId, node, "jsx", {
        tagName: jsxTagName(node, sourceFile),
        attributeCount: jsxAttributeCount(node),
      });
    }
    if (ts.isBinaryExpression(node)) {
      const left = emitExpression(context, blockId, node.left, sourceFile);
      const right = emitExpression(context, blockId, node.right, sourceFile);
      return emitOp(context, blockId, node, isAssignmentOperator(node.operatorToken.kind) ? "assign" : "binary", {
        operator: syntaxKindName(node.operatorToken.kind),
        left,
        right,
      });
    }
    if (ts.isPrefixUnaryExpression(node) || ts.isPostfixUnaryExpression(node)) {
      const operand = emitExpression(context, blockId, node.operand, sourceFile);
      return emitOp(context, blockId, node, "unary", { operator: syntaxKindName(node.operator), operand });
    }
    if (ts.isPropertyAccessExpression(node)) {
      inspectRuntimeExpression(node, sourceFile);
      const receiver = emitExpression(context, blockId, node.expression, sourceFile);
      return emitOp(context, blockId, node, "property_read", { receiver, name: node.name.text });
    }
    if (ts.isElementAccessExpression(node)) {
      const receiver = emitExpression(context, blockId, node.expression, sourceFile);
      const argument = node.argumentExpression ? emitExpression(context, blockId, node.argumentExpression, sourceFile) : undefined;
      return emitOp(context, blockId, node, "element_read", { receiver, argument });
    }
    if (ts.isAwaitExpression(node)) {
      const value = emitExpression(context, blockId, node.expression, sourceFile);
      addRuntimeRequirement(sourceFile, node, "await", "Promise.await", { runtime: "js-core", source: "ecmascript_promise" }, currentOwner());
      return emitOp(context, blockId, node, "await", { value });
    }
    if (ts.isObjectLiteralExpression(node)) {
      for (const property of node.properties) {
        if (ts.isPropertyAssignment(property)) emitExpression(context, blockId, property.initializer, sourceFile);
      }
      return emitOp(context, blockId, node, "object_literal", { propertyCount: node.properties.length });
    }
    if (ts.isArrayLiteralExpression(node)) {
      for (const element of node.elements) {
        if (ts.isSpreadElement(element)) emitExpression(context, blockId, element.expression, sourceFile);
        else emitExpression(context, blockId, element, sourceFile);
      }
      return emitOp(context, blockId, node, "array_literal", { elementCount: node.elements.length });
    }
    if (ts.isArrowFunction(node) || ts.isFunctionExpression(node)) {
      if (isFunctionLikeWithBody(node)) emitFunction(node, sourceFile);
      return emitOp(context, blockId, node, "function_value", { functionKind: syntaxKindName(node.kind) });
    }
    if (ts.isTemplateExpression(node)) {
      for (const span of node.templateSpans) emitExpression(context, blockId, span.expression, sourceFile);
      return emitOp(context, blockId, node, "template", { spanCount: node.templateSpans.length });
    }
    return emitOp(context, blockId, node, "expression", { expressionKind: syntaxKindName(node.kind) });
  }

  function emitCall(node: ts.CallExpression, sourceFile: ts.SourceFile): void {
    const signature = checker.getResolvedSignature(node);
    const returnType = node.expression.kind === ts.SyntaxKind.ImportKeyword
      ? checker.getTypeAtLocation(node)
      : signature
        ? checker.getReturnTypeOfSignature(signature)
        : checker.getTypeAtLocation(node);
    const target = signature?.declaration ? symbolForDeclaration(signature.declaration) : checker.getSymbolAtLocation(node.expression);
    const targetFact = symbolFact(target, sourceFile);
    addTypeFact(sourceFile, node, "call.return", returnType);
    emitFact({
      kind: "csg.call",
      id: nodeId("csg.call", node, sourceFile, node.expression.getText(sourceFile)),
      loc: locFor(sourceFile, node),
      owner: currentOwner(),
      calleeText: node.expression.getText(sourceFile),
      calleeKind: syntaxKindName(node.expression.kind),
      target: targetFact,
      argumentCount: node.arguments.length,
      returnType: typeText(returnType),
    });
    if (node.expression.kind === ts.SyntaxKind.ImportKeyword) {
      const moduleName = stringModuleSpecifier(node.arguments[0]);
      if (moduleName) {
        addRuntimeRequirement(sourceFile, node, "dynamic_import", moduleName, { runtime: "js-core", source: "ecmascript_module_loader" }, currentOwner());
      }
    }
    if (containsAny(returnType, node)) {
      addUnsupported(sourceFile, node, "type.any", "call return resolves to any", currentOwner());
    }
    inspectRuntimeSymbol(sourceFile, node, target, node.expression.getText(sourceFile), "call", currentOwner());
  }

  function emitNew(node: ts.NewExpression, sourceFile: ts.SourceFile): void {
    const valueType = checker.getTypeAtLocation(node);
    addTypeFact(sourceFile, node, "new.value", valueType);
    emitFact({
      kind: "csg.call",
      id: nodeId("csg.call", node, sourceFile, `new:${node.expression.getText(sourceFile)}`),
      loc: locFor(sourceFile, node),
      owner: currentOwner(),
      calleeText: node.expression.getText(sourceFile),
      calleeKind: "new",
      argumentCount: node.arguments?.length ?? 0,
      returnType: typeText(valueType),
    });
    inspectRuntimeSymbol(sourceFile, node, checker.getSymbolAtLocation(node.expression), node.expression.getText(sourceFile), "constructor", currentOwner());
  }

  function emitBindingFacts(name: ts.BindingName, sourceFile: ts.SourceFile, declarationKind: string, owner: string | undefined): void {
    for (const leaf of collectBindingLeaves(name, sourceFile)) {
      const type = checker.getTypeAtLocation(leaf.node);
      addTypeFact(sourceFile, leaf.node, "binding", type);
      emitFact({
        kind: "csg.binding",
        id: stableId("csg.binding", relPath(build.cwd, sourceFile.fileName), String(leaf.node.pos), String(leaf.node.end), stableJson(leaf.path)),
        loc: locFor(sourceFile, leaf.node),
        owner,
        declarationKind,
        name: leaf.name,
        path: leaf.path,
        rest: leaf.rest,
        defaultInitializerKind: leaf.defaultInitializerKind,
        type: typeText(type),
        symbol: symbolFact(checker.getSymbolAtLocation(leaf.node), sourceFile),
      });
      if (containsAny(type, leaf.node)) {
        addUnsupported(sourceFile, leaf.node, "type.any", "binding resolves to any", owner);
      }
    }
  }

  function emitBindingExtractOps(
    context: FunctionContext,
    blockId: string,
    name: ts.BindingName,
    sourceFile: ts.SourceFile,
    sourceOp: string | undefined,
  ): void {
    for (const leaf of collectBindingLeaves(name, sourceFile)) {
      emitOp(context, blockId, leaf.node, "binding_extract", {
        source: sourceOp,
        name: leaf.name,
        path: leaf.path,
        rest: leaf.rest,
        defaultInitializerKind: leaf.defaultInitializerKind,
      });
    }
  }

  function collectBindingLeaves(name: ts.BindingName, sourceFile: ts.SourceFile, pathPrefix: BindingPathSegment[] = []): BindingLeaf[] {
    if (ts.isIdentifier(name)) {
      return [{
        name: name.text,
        node: name,
        path: pathPrefix,
        rest: pathPrefix.some((segment) => segment.kind === "rest"),
      }];
    }
    if (ts.isArrayBindingPattern(name)) {
      const leaves: BindingLeaf[] = [];
      name.elements.forEach((element, index) => {
        if (ts.isOmittedExpression(element)) return;
        const segment: BindingPathSegment = element.dotDotDotToken ? { kind: "rest", index } : { kind: "index", index };
        const childPath = [...pathPrefix, segment];
        for (const leaf of collectBindingLeaves(element.name, sourceFile, childPath)) {
          leaves.push({
            ...leaf,
            rest: leaf.rest || element.dotDotDotToken !== undefined,
            defaultInitializerKind: element.initializer ? syntaxKindName(element.initializer.kind) : leaf.defaultInitializerKind,
          });
        }
      });
      return leaves;
    }
    const leaves: BindingLeaf[] = [];
    for (const element of name.elements) {
      let propertyName = "";
      if (element.propertyName) {
        if (ts.isComputedPropertyName(element.propertyName) && !isLiteralExpression(element.propertyName.expression)) {
          addUnsupported(sourceFile, element.propertyName, "binding.computed", "computed binding names require closed literal keys", currentOwner());
          propertyName = element.propertyName.getText(sourceFile);
        } else {
          propertyName = propertyNameText(element.propertyName, sourceFile);
        }
      } else if (ts.isIdentifier(element.name)) {
        propertyName = element.name.text;
      } else {
        propertyName = element.name.getText(sourceFile);
      }
      const segment: BindingPathSegment = element.dotDotDotToken ? { kind: "rest" } : { kind: "property", name: propertyName };
      const childPath = [...pathPrefix, segment];
      for (const leaf of collectBindingLeaves(element.name, sourceFile, childPath)) {
        leaves.push({
          ...leaf,
          rest: leaf.rest || element.dotDotDotToken !== undefined,
          defaultInitializerKind: element.initializer ? syntaxKindName(element.initializer.kind) : leaf.defaultInitializerKind,
        });
      }
    }
    return leaves;
  }

  function inspectUnsupported(node: ts.Node, sourceFile: ts.SourceFile): void {
    if (isDeclarationWithComputedName(node)) {
      const name = node.name;
      if (ts.isComputedPropertyName(name) && !isLiteralExpression(name.expression)) {
        addUnsupported(sourceFile, name, "computed_name.dynamic", "computed declaration names require closed literal keys", currentOwner());
      }
    }
    if (ts.isCallExpression(node)) {
      if (ts.isIdentifier(node.expression) && node.expression.text === "eval") {
        addUnsupported(sourceFile, node, "eval", "eval cannot be represented as closed CSG facts", currentOwner());
      }
      if (ts.isIdentifier(node.expression) && node.expression.text === "require") {
        const arg = node.arguments[0];
        if (!arg || !isStringLiteralLike(arg)) {
          addUnsupported(sourceFile, node, "require.dynamic", "require() module specifier must be a string literal", currentOwner());
        }
      }
      if (node.expression.kind === ts.SyntaxKind.ImportKeyword) {
        const arg = node.arguments[0];
        if (!arg || !isStringLiteralLike(arg)) {
          addUnsupported(sourceFile, node, "import.dynamic", "dynamic import() module specifier must be a string literal", currentOwner());
        }
      }
    }
    if (ts.isBinaryExpression(node) && isAssignmentOperator(node.operatorToken.kind) && isPrototypeMutation(node.left)) {
      addUnsupported(sourceFile, node, "prototype.mutation", "prototype mutation is runtime shape mutation", currentOwner());
    }
    if (ts.isSwitchStatement(node)) {
      addUnsupported(sourceFile, node, "switch", "switch requires explicit dispatch lowering", currentOwner());
    }
    if (ts.canHaveDecorators(node) && (ts.getDecorators(node)?.length ?? 0) > 0) {
      addUnsupported(sourceFile, node, "decorator", "decorators require explicit metadata/runtime mapping", currentOwner());
    }
  }

  function inspectRuntimeExpression(node: ts.PropertyAccessExpression, sourceFile: ts.SourceFile): void {
    const root = rootIdentifierText(node.expression);
    if (!root) return;
    const classification = classifyGlobalName(root);
    if (!classification) return;
    addRuntimeRequirement(sourceFile, node, "property_access", `${root}.${node.name.text}`, classification, currentOwner());
  }

  function inspectRuntimeIdentifier(node: ts.Identifier, sourceFile: ts.SourceFile): void {
    if (isDeclarationName(node)) return;
    const classification = classifyGlobalName(node.text);
    if (!classification) return;
    addRuntimeRequirement(sourceFile, node, "global", node.text, classification, currentOwner());
  }

  function inspectRuntimeSymbol(sourceFile: ts.SourceFile, node: ts.Node, symbol: ts.Symbol | undefined, name: string, kind: string, owner: string | undefined): void {
    if (!symbol) {
      const root = name.split(".")[0] ?? name;
      const globalClassification = classifyGlobalName(root);
      if (globalClassification) addRuntimeRequirement(sourceFile, node, kind, name, globalClassification, owner);
      return;
    }
    const declarations = symbol.declarations ?? [];
    const declaration = declarations[0];
    if (!declaration) return;
    const declarationSource = declaration.getSourceFile();
    if (isProjectSourceFile(build.cwd, declarationSource)) return;
    const classification = classifyDeclarationSource(declarationSource, name);
    addRuntimeRequirement(sourceFile, node, kind, name, classification, owner);
    addExternalSymbol(name, classification);
  }

  function addUnsupported(sourceFile: ts.SourceFile, node: ts.Node, code: string, message: string, owner: string | undefined): void {
    const id = nodeId("csg.unsupported", node, sourceFile, code);
    if (unsupportedById.has(id)) return;
    const item: CsgCoreIssue = {
      id,
      code,
      message,
      loc: locFor(sourceFile, node),
      owner,
    };
    unsupportedById.set(id, item);
  }

  function addRuntimeRequirement(
    sourceFile: ts.SourceFile,
    node: ts.Node,
    kind: string,
    name: string,
    classification: RuntimeClassification,
    owner: string | undefined,
  ): void {
    const id = stableId("csg.runtime_requirement", kind, name, classification.runtime, relPath(build.cwd, sourceFile.fileName), String(node.pos), String(node.end));
    if (runtimeById.has(id)) return;
    runtimeById.set(id, {
      id,
      kind,
      name,
      runtime: classification.runtime,
      source: classification.source,
      loc: locFor(sourceFile, node),
      owner,
    });
  }

  function addExternalSymbol(name: string, classification: RuntimeClassification): void {
    const id = stableId("csg.external_symbol", name, classification.runtime, classification.source);
    if (externalById.has(id)) return;
    externalById.set(id, { id, name, runtime: classification.runtime, source: classification.source });
  }

  function addTypeFact(sourceFile: ts.SourceFile, node: ts.Node, typeKind: string, type: ts.Type): string {
    const text = typeText(type);
    const id = stableId("csg.type", typeKind, text, relPath(build.cwd, sourceFile.fileName), String(node.pos), String(node.end));
    if (!typeById.has(id)) {
      typeById.set(id, {
        kind: "csg.type",
        id,
        loc: locFor(sourceFile, node),
        typeKind,
        text,
        flags: typeFlags(type.flags),
      });
    }
    return id;
  }

  function addDataFact(sourceFile: ts.SourceFile, node: ts.Node, dataKind: string, value: unknown): string {
    const id = stableId("csg.data", dataKind, stableJson(value));
    if (!dataById.has(id)) {
      dataById.set(id, {
        kind: "csg.data",
        id,
        loc: locFor(sourceFile, node),
        dataKind,
        value,
      });
    }
    return id;
  }

  function newBlock(context: FunctionContext, node: ts.Node, blockKind: string): string {
    const blockId = stableId("csg.block", context.id, blockKind, String(context.nextBlock), String(node.pos), String(node.end));
    context.nextBlock += 1;
    emitFact({
      kind: "csg.block",
      id: blockId,
      loc: locFor(context.sourceFile, node),
      function: context.id,
      blockKind,
      ordinal: context.nextBlock - 1,
    });
    return blockId;
  }

  function emitOp(context: FunctionContext, blockId: string, node: ts.Node, opKind: string, fields: Record<string, unknown>): string {
    const opId = stableId("csg.op", context.id, blockId, String(context.nextOp), opKind, String(node.pos), String(node.end));
    const ordinal = context.nextOp;
    context.nextOp += 1;
    emitFact({
      kind: "csg.op",
      id: opId,
      loc: locFor(context.sourceFile, node),
      function: context.id,
      block: blockId,
      opKind,
      ordinal,
      ...fields,
    });
    emitFact({
      kind: "csg.debug_map",
      id: stableId("csg.debug_map", opId),
      loc: locFor(context.sourceFile, node),
      target: opId,
    });
    return opId;
  }

  function emitTerm(context: FunctionContext, blockId: string, termKind: string, targets: string[]): void {
    emitFact({
      kind: "csg.term",
      id: stableId("csg.term", context.id, blockId, termKind, targets.join(",")),
      function: context.id,
      block: blockId,
      termKind,
      targets,
    });
  }

  function emitFact(fact: CsgFact): void {
    facts.push(fact);
  }

  function nodeId(kind: string, node: ts.Node, sourceFile: ts.SourceFile, salt: string): string {
    return stableId(kind, relPath(build.cwd, sourceFile.fileName), String(node.pos), String(node.end), salt);
  }

  function fileId(sourceFile: ts.SourceFile): string {
    return stableId("csg.module", relPath(build.cwd, sourceFile.fileName));
  }

  function locFor(sourceFile: ts.SourceFile, node: ts.Node): SourceLoc {
    const start = node.getStart(sourceFile, false);
    const end = node.getEnd();
    const point = sourceFile.getLineAndCharacterOfPosition(start);
    return {
      file: relPath(build.cwd, sourceFile.fileName),
      line: point.line + 1,
      column: point.character + 1,
      start,
      end,
    };
  }

  function currentOwner(): string | undefined {
    return functionStack[functionStack.length - 1]?.id;
  }

  function typeText(type: ts.Type): string {
    return normalizeCheckerText(checker.typeToString(type, undefined, ts.TypeFormatFlags.NoTruncation | ts.TypeFormatFlags.UseFullyQualifiedType));
  }

  function normalizeCheckerText(value: string): string {
    return value.split(toPosix(build.cwd)).join("$root");
  }

  function symbolForDeclaration(declaration: ts.Declaration): ts.Symbol | undefined {
    const namedDeclaration = declaration as ts.Declaration & { name?: ts.Node };
    return namedDeclaration.name ? checker.getSymbolAtLocation(namedDeclaration.name) : undefined;
  }

  function symbolFact(symbol: ts.Symbol | undefined, sourceFile: ts.SourceFile): Record<string, unknown> | undefined {
    if (!symbol) return undefined;
    const firstDeclaration = symbol.declarations?.[0];
    const fqName = normalizeCheckerText(checker.getFullyQualifiedName(symbol));
    return {
      id: stableId("csg.symbol.ref", fqName, firstDeclaration ? declarationKey(firstDeclaration) : relPath(build.cwd, sourceFile.fileName)),
      name: symbol.getName(),
      fqName,
      flags: symbolFlags(symbol.flags),
    };
  }

  function declarationKey(node: ts.Declaration): string {
    const sourceFile = node.getSourceFile();
    return `${relPath(build.cwd, sourceFile.fileName)}:${node.pos}:${node.end}`;
  }

  function containsAny(root: ts.Type, location: ts.Node): boolean {
    const seen = new Set<ts.Type>();
    const stack: ts.Type[] = [root];
    let steps = 0;
    while (stack.length > 0) {
      const type = stack.pop();
      if (!type) continue;
      if ((type.flags & ts.TypeFlags.Any) !== 0) return true;
      if (seen.has(type)) continue;
      seen.add(type);
      steps += 1;
      if (steps > 256) return false;
      if (type.isUnionOrIntersection()) stack.push(...type.types);
      const reference = type as ts.TypeReference;
      if (reference.target) {
        for (const arg of checker.getTypeArguments(reference)) stack.push(arg);
      }
      for (const prop of type.getProperties().slice(0, 64)) {
        const declaration = prop.valueDeclaration ?? prop.declarations?.[0];
        if (!declaration || !isProjectSourceFile(build.cwd, declaration.getSourceFile())) continue;
        stack.push(checker.getTypeOfSymbolAtLocation(prop, declaration));
      }
    }
    return false;
  }
}

function buildProgram(options: CsgCoreOptions): ProgramBuild {
  const baseCwd = path.resolve(options.rootDir ?? process.cwd());
  if (options.project) {
    const project = path.resolve(baseCwd, options.project);
    const cwd = path.resolve(options.rootDir ?? path.dirname(project));
    const configDiagnostics: ts.Diagnostic[] = [];
    const parsed = ts.getParsedCommandLineOfConfigFile(project, {}, {
      ...ts.sys,
      onUnRecoverableConfigFileDiagnostic: (diagnostic) => {
        configDiagnostics.push(diagnostic);
      },
    });
    if (!parsed) {
      const compilerOptions = defaultCompilerOptions(cwd);
      return {
        cwd,
        projectFile: project,
        program: ts.createProgram({ rootNames: [], options: compilerOptions }),
        configDiagnostics,
        compilerOptions,
      };
    }
    const createOptions: ts.CreateProgramOptions = {
      rootNames: parsed.fileNames,
      options: parsed.options,
    };
    if (parsed.projectReferences) createOptions.projectReferences = parsed.projectReferences;
    return {
      cwd,
      projectFile: project,
      program: ts.createProgram(createOptions),
      configDiagnostics: [...configDiagnostics, ...parsed.errors],
      compilerOptions: parsed.options,
    };
  }

  if (!options.files || options.files.length === 0) {
    const compilerOptions = defaultCompilerOptions(baseCwd);
    return {
      cwd: baseCwd,
      program: ts.createProgram({ rootNames: [], options: compilerOptions }),
      configDiagnostics: [makeDiagnostic("either --project or at least one --file is required")],
      compilerOptions,
    };
  }

  const compilerOptions = defaultCompilerOptions(baseCwd);
  return {
    cwd: baseCwd,
    program: ts.createProgram({ rootNames: options.files.map((file) => path.resolve(baseCwd, file)), options: compilerOptions }),
    configDiagnostics: [],
    compilerOptions,
  };
}

function emptyReport(build: ProgramBuild, runtimes: string[], diagnostics: string[]): CsgCoreReport {
  return {
    schema: CsgCoreReportSchema,
    version: CsgCoreVersion,
    complete: false,
    projectRoot: toPosix(path.resolve(build.cwd)),
    projectFile: build.projectFile ? relPath(build.cwd, build.projectFile) : undefined,
    runtimes,
    entryRoots: entryRoots(build.cwd, undefined, []),
    counts: {
      sourceFiles: 0,
      modules: 0,
      imports: 0,
      exports: 0,
      symbols: 0,
      types: 0,
      functions: 0,
      blocks: 0,
      terms: 0,
      ops: 0,
      calls: 0,
      data: 0,
      runtimeRequirements: 0,
      externalSymbols: 0,
      unsupported: 0,
    },
    unsupported: [],
    runtimeRequirements: [],
    externalSymbols: [],
    diagnostics,
  };
}

function countFacts(facts: readonly CsgFact[], sourceFiles: number): CsgCoreCounts {
  const counts = new Map<string, number>();
  for (const fact of facts) counts.set(fact.kind, (counts.get(fact.kind) ?? 0) + 1);
  return {
    sourceFiles,
    modules: counts.get("csg.module") ?? 0,
    imports: counts.get("csg.import") ?? 0,
    exports: counts.get("csg.export") ?? 0,
    symbols: counts.get("csg.symbol") ?? 0,
    types: counts.get("csg.type") ?? 0,
    functions: counts.get("csg.function") ?? 0,
    blocks: counts.get("csg.block") ?? 0,
    terms: counts.get("csg.term") ?? 0,
    ops: counts.get("csg.op") ?? 0,
    calls: counts.get("csg.call") ?? 0,
    data: counts.get("csg.data") ?? 0,
    runtimeRequirements: counts.get("csg.runtime_requirement") ?? 0,
    externalSymbols: counts.get("csg.external_symbol") ?? 0,
    unsupported: counts.get("csg.unsupported") ?? 0,
  };
}

function validateRuntimeOptions(runtime: string[] | undefined): string[] {
  const diagnostics: string[] = [];
  for (const item of runtime ?? []) {
    if (!allowedRuntimes.has(item)) diagnostics.push(`runtime must be node or browser: ${item}`);
  }
  return diagnostics;
}

function normalizeRuntime(runtime: string[] | undefined): string[] {
  const values = runtime && runtime.length > 0 ? runtime : ["node"];
  return [...new Set(values)].sort();
}

function entryRoots(cwd: string, configured: string[] | undefined, sourceFiles: readonly ts.SourceFile[]): string[] {
  const roots = configured && configured.length > 0 ? configured : defaultEntryRoots;
  const sourceFileSet = new Set(sourceFiles.map((sourceFile) => relPath(cwd, sourceFile.fileName)));
  return [...new Set(roots)]
    .map((item) => toPosix(item))
    .filter((item) => sourceFileSet.has(item))
    .sort();
}

function defaultCompilerOptions(cwd: string): ts.CompilerOptions {
  return {
    target: ts.ScriptTarget.ES2022,
    module: ts.ModuleKind.NodeNext,
    moduleResolution: ts.ModuleResolutionKind.NodeNext,
    strict: true,
    noImplicitAny: true,
    exactOptionalPropertyTypes: true,
    noUncheckedIndexedAccess: true,
    esModuleInterop: true,
    forceConsistentCasingInFileNames: true,
    skipLibCheck: true,
    rootDir: cwd,
  };
}

function makeDiagnostic(message: string): ts.Diagnostic {
  return {
    category: ts.DiagnosticCategory.Error,
    code: 0,
    file: undefined,
    start: undefined,
    length: undefined,
    messageText: message,
  };
}

function formatDiagnostic(cwd: string, diagnostic: ts.Diagnostic): string {
  const message = ts.flattenDiagnosticMessageText(diagnostic.messageText, "\n");
  if (diagnostic.file && diagnostic.start !== undefined) {
    const point = diagnostic.file.getLineAndCharacterOfPosition(diagnostic.start);
    return `${relPath(cwd, diagnostic.file.fileName)}:${point.line + 1}:${point.character + 1} TS${diagnostic.code}: ${message}`;
  }
  return `TS${diagnostic.code}: ${message}`;
}

function compilerOptionsFact(options: ts.CompilerOptions): Record<string, unknown> {
  return {
    target: options.target === undefined ? undefined : ts.ScriptTarget[options.target],
    module: options.module === undefined ? undefined : ts.ModuleKind[options.module],
    moduleResolution: options.moduleResolution === undefined ? undefined : ts.ModuleResolutionKind[options.moduleResolution],
    strict: options.strict === true,
    noImplicitAny: options.noImplicitAny === true,
    exactOptionalPropertyTypes: options.exactOptionalPropertyTypes === true,
    noUncheckedIndexedAccess: options.noUncheckedIndexedAccess === true,
  };
}

function isProjectSourceFile(cwd: string, sourceFile: ts.SourceFile): boolean {
  const resolved = path.resolve(sourceFile.fileName);
  if (sourceFile.isDeclarationFile) return false;
  if (resolved.includes(`${path.sep}node_modules${path.sep}`)) return false;
  return isInside(cwd, resolved);
}

function isInside(root: string, file: string): boolean {
  const relative = path.relative(root, file);
  return relative === "" || (!relative.startsWith("..") && !path.isAbsolute(relative));
}

function relPath(cwd: string, file: string): string {
  return toPosix(path.relative(cwd, path.resolve(file)) || ".");
}

function toPosix(value: string): string {
  return value.split(path.sep).join("/");
}

function sha256(value: string): string {
  return createHash("sha256").update(value).digest("hex");
}

function stableId(...parts: string[]): string {
  return createHash("sha256").update(parts.join("\u0000")).digest("hex").slice(0, 24);
}

function compareFactId(left: CsgFact, right: CsgFact): number {
  return String(left.id ?? "").localeCompare(String(right.id ?? ""));
}

function compareById(left: { id: string }, right: { id: string }): number {
  return left.id.localeCompare(right.id);
}

function compareIssue(left: CsgCoreIssue, right: CsgCoreIssue): number {
  const leftLoc = left.loc ? `${left.loc.file}:${left.loc.start}` : "";
  const rightLoc = right.loc ? `${right.loc.file}:${right.loc.start}` : "";
  return leftLoc.localeCompare(rightLoc) || left.code.localeCompare(right.code) || left.id.localeCompare(right.id);
}

function syntaxKindName(kind: ts.SyntaxKind): string {
  return ts.SyntaxKind[kind] ?? `SyntaxKind${kind}`;
}

function typeFlags(flags: ts.TypeFlags): string[] {
  const names: string[] = [];
  for (const key of Object.keys(ts.TypeFlags)) {
    const value = Number(key);
    if (Number.isNaN(value) || value === 0) continue;
    if ((value & (value - 1)) !== 0) continue;
    if ((flags & value) !== 0) names.push(ts.TypeFlags[value] ?? key);
  }
  return [...new Set(names)].sort();
}

function symbolFlags(flags: ts.SymbolFlags): string[] {
  const names: string[] = [];
  for (const key of Object.keys(ts.SymbolFlags)) {
    const value = Number(key);
    if (Number.isNaN(value) || value === 0) continue;
    if ((value & (value - 1)) !== 0) continue;
    if ((flags & value) !== 0) names.push(ts.SymbolFlags[value] ?? key);
  }
  return [...new Set(names)].sort();
}

function stringModuleSpecifier(node: ts.Expression | undefined): string | undefined {
  return node && isStringLiteralLike(node) ? node.text : undefined;
}

function isStringLiteralLike(node: ts.Node): node is ts.StringLiteral | ts.NoSubstitutionTemplateLiteral {
  return ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node);
}

function isLiteralExpression(node: ts.Expression): boolean {
  return isStringLiteralLike(node) || ts.isNumericLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node);
}

function propertyNameText(name: ts.PropertyName, sourceFile: ts.SourceFile): string {
  if (ts.isIdentifier(name) || ts.isPrivateIdentifier(name) || ts.isStringLiteral(name) || ts.isNumericLiteral(name)) return name.text;
  return name.getText(sourceFile);
}

function bindingNameText(name: ts.BindingName, sourceFile: ts.SourceFile): string {
  return ts.isIdentifier(name) ? name.text : name.getText(sourceFile);
}

function declarationName(node: FunctionLikeWithBody, sourceFile: ts.SourceFile): string {
  const name = functionNameNode(node);
  if (name) return propertyNameText(name, sourceFile);
  if (ts.isConstructorDeclaration(node)) return "constructor";
  return "<anonymous>";
}

function functionNameNode(node: FunctionLikeWithBody): ts.PropertyName | undefined {
  if ("name" in node && node.name) return node.name;
  return undefined;
}

function functionTypeParameters(node: FunctionLikeWithBody): ts.NodeArray<ts.TypeParameterDeclaration> | undefined {
  if ("typeParameters" in node) return node.typeParameters;
  return undefined;
}

function typeParameterTexts(nodes: ts.NodeArray<ts.TypeParameterDeclaration> | undefined): string[] {
  return nodes?.map((node) => node.getText()).sort() ?? [];
}

function jsxTagName(node: ts.JsxElement | ts.JsxSelfClosingElement | ts.JsxFragment, sourceFile: ts.SourceFile): string {
  if (ts.isJsxFragment(node)) return "";
  if (ts.isJsxElement(node)) return node.openingElement.tagName.getText(sourceFile);
  return node.tagName.getText(sourceFile);
}

function jsxAttributeCount(node: ts.JsxElement | ts.JsxSelfClosingElement | ts.JsxFragment): number {
  if (ts.isJsxFragment(node)) return 0;
  if (ts.isJsxElement(node)) return node.openingElement.attributes.properties.length;
  return node.attributes.properties.length;
}

function parameterFact(param: ts.ParameterDeclaration, sourceFile: ts.SourceFile, index: number): Record<string, unknown> {
  return {
    index,
    name: bindingNameText(param.name, sourceFile),
    optional: param.questionToken !== undefined || param.initializer !== undefined,
    rest: param.dotDotDotToken !== undefined,
    typeSource: param.type?.getText(sourceFile),
    initializerKind: param.initializer ? syntaxKindName(param.initializer.kind) : undefined,
  };
}

function variableDeclarationKind(node: ts.VariableDeclaration): string {
  let current: ts.Node = node;
  while (current.parent) {
    current = current.parent;
    if (ts.isVariableDeclarationList(current)) {
      if ((current.flags & ts.NodeFlags.Const) !== 0) return "const";
      if ((current.flags & ts.NodeFlags.Let) !== 0) return "let";
      return "var";
    }
  }
  return "var";
}

function isExportedVariable(node: ts.VariableDeclaration): boolean {
  let current: ts.Node | undefined = node;
  while (current) {
    if (ts.isVariableStatement(current)) return hasModifier(current, ts.SyntaxKind.ExportKeyword);
    current = current.parent;
  }
  return false;
}

function hasModifier(node: ts.Node, kind: ts.SyntaxKind): boolean {
  return ts.canHaveModifiers(node) && (ts.getModifiers(node)?.some((modifier) => modifier.kind === kind) ?? false);
}

function isAssignmentOperator(kind: ts.SyntaxKind): boolean {
  return kind >= ts.SyntaxKind.FirstAssignment && kind <= ts.SyntaxKind.LastAssignment;
}

function isPrototypeMutation(node: ts.Expression): boolean {
  if (ts.isPropertyAccessExpression(node)) return ts.isPropertyAccessExpression(node.expression) && node.expression.name.text === "prototype";
  if (ts.isElementAccessExpression(node)) return ts.isPropertyAccessExpression(node.expression) && node.expression.name.text === "prototype";
  return false;
}

function isDeclarationWithComputedName(node: ts.Node): node is DeclarationWithName {
  return (ts.isMethodDeclaration(node)
    || ts.isPropertyDeclaration(node)
    || ts.isGetAccessorDeclaration(node)
    || ts.isSetAccessorDeclaration(node)
    || ts.isPropertySignature(node)
    || ts.isMethodSignature(node)
    || ts.isEnumMember(node))
    && node.name !== undefined
    && ts.isComputedPropertyName(node.name);
}

function isDeclarationName(node: ts.Identifier): boolean {
  const parent = node.parent;
  if (!parent) return false;
  if ((ts.isVariableDeclaration(parent)
    || ts.isFunctionDeclaration(parent)
    || ts.isClassDeclaration(parent)
    || ts.isInterfaceDeclaration(parent)
    || ts.isTypeAliasDeclaration(parent)
    || ts.isEnumDeclaration(parent)
    || ts.isParameter(parent)
    || ts.isPropertyDeclaration(parent)
    || ts.isMethodDeclaration(parent)
    || ts.isPropertySignature(parent)
    || ts.isMethodSignature(parent))
    && parent.name === node) return true;
  if ((ts.isImportSpecifier(parent) || ts.isImportClause(parent) || ts.isNamespaceImport(parent) || ts.isExportSpecifier(parent)) && parent.name === node) return true;
  return false;
}

function classifyModuleSpecifier(moduleName: string): RuntimeClassification {
  if (moduleName.startsWith(".") || moduleName.startsWith("/")) return { runtime: "project", source: "project" };
  const normalized = moduleName.startsWith("node:") ? moduleName.slice("node:".length) : moduleName;
  if (nodeBuiltins.has(normalized)) return { runtime: "node", source: "node_builtin" };
  return { runtime: "node", source: "external_package" };
}

function classifyGlobalName(name: string): RuntimeClassification | undefined {
  if (browserGlobals.has(name)) return { runtime: "browser", source: "browser_global" };
  if (jsCoreGlobals.has(name)) return { runtime: "js-core", source: "ecmascript_builtin" };
  if (name === "Buffer" || name === "process" || name === "__dirname" || name === "__filename") {
    return { runtime: "node", source: "node_global" };
  }
  return undefined;
}

function classifyDeclarationSource(sourceFile: ts.SourceFile, name: string): RuntimeClassification {
  const file = toPosix(sourceFile.fileName);
  const root = name.split(".")[0] ?? name;
  const global = classifyGlobalName(root);
  if (global) return global;
  if (file.includes("/node_modules/@types/node/") || file.endsWith("/@types/node/index.d.ts")) {
    return { runtime: "node", source: "node_type" };
  }
  if (file.includes("/typescript/lib/lib.dom")) {
    return { runtime: "browser", source: "dom_lib" };
  }
  if (file.includes("/typescript/lib/lib.es")) {
    return { runtime: "js-core", source: "ecmascript_lib" };
  }
  if (file.includes("/node_modules/")) {
    return { runtime: "node", source: "external_package_type" };
  }
  return { runtime: "unknown", source: "external_declaration" };
}

function rootIdentifierText(node: ts.Expression): string | undefined {
  if (ts.isIdentifier(node)) return node.text;
  if (ts.isPropertyAccessExpression(node)) return rootIdentifierText(node.expression);
  if (ts.isElementAccessExpression(node)) return rootIdentifierText(node.expression);
  if (ts.isCallExpression(node)) return rootIdentifierText(node.expression);
  return undefined;
}

function isFunctionLikeWithBody(node: ts.Node): node is FunctionLikeWithBody {
  return (ts.isFunctionDeclaration(node)
    || ts.isFunctionExpression(node)
    || ts.isArrowFunction(node)
    || ts.isMethodDeclaration(node)
    || ts.isConstructorDeclaration(node)
    || ts.isGetAccessorDeclaration(node)
    || ts.isSetAccessorDeclaration(node)) && node.body !== undefined;
}

function blockStatements(body: ts.ConciseBody): readonly ts.Statement[] {
  return ts.isBlock(body) ? body.statements : [];
}

function asStatement(node: ts.Statement): ts.Statement {
  return node;
}

function terminalKind(statements: readonly ts.Statement[]): string {
  const last = statements[statements.length - 1];
  if (!last) return "return_void";
  if (ts.isReturnStatement(last)) return "return";
  if (ts.isThrowStatement(last)) return "throw";
  return "fallthrough";
}

type FunctionLikeWithBody =
  | (ts.FunctionDeclaration & { body: ts.FunctionBody })
  | (ts.FunctionExpression & { body: ts.FunctionBody })
  | (ts.ArrowFunction & { body: ts.ConciseBody })
  | (ts.MethodDeclaration & { body: ts.FunctionBody })
  | (ts.ConstructorDeclaration & { body: ts.FunctionBody })
  | (ts.GetAccessorDeclaration & { body: ts.FunctionBody })
  | (ts.SetAccessorDeclaration & { body: ts.FunctionBody });

type DeclarationWithName =
  | ts.MethodDeclaration
  | ts.PropertyDeclaration
  | ts.GetAccessorDeclaration
  | ts.SetAccessorDeclaration
  | ts.PropertySignature
  | ts.MethodSignature
  | ts.EnumMember;
