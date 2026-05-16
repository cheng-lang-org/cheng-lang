import { createHash } from "node:crypto";
import path from "node:path";
import * as ts from "typescript";

import { TSCsgSchema, TSCsgVersion, type CsgFact, type ExtractOptions, type ExtractResult, type SourceLoc, type UnsupportedFact } from "./schema.js";

type ProgramBuild = {
  cwd: string;
  program: ts.Program;
  configDiagnostics: ts.Diagnostic[];
  compilerOptions: ts.CompilerOptions;
};

type FactFields = Record<string, unknown>;

export function extractTsCsg(options: ExtractOptions): ExtractResult {
  const build = buildProgram(options);
  const diagnostics = [
    ...build.configDiagnostics,
    ...ts.getPreEmitDiagnostics(build.program).filter((diag) => diag.category === ts.DiagnosticCategory.Error),
  ].map((diag) => formatDiagnostic(build.cwd, diag));

  if (diagnostics.length > 0) {
    return { facts: [], unsupported: [], diagnostics };
  }

  const checker = build.program.getTypeChecker();
  const projectFiles = build.program
    .getSourceFiles()
    .filter((sourceFile) => isProjectSourceFile(build.cwd, sourceFile))
    .sort((left, right) => relPath(build.cwd, left.fileName).localeCompare(relPath(build.cwd, right.fileName)));

  const facts: CsgFact[] = [];
  const unsupported: UnsupportedFact[] = [];
  const unsupportedKeys = new Set<string>();

  emitFact({
    kind: "csg.schema",
    id: stableId("csg.schema", "typescript"),
    language: "typescript",
    schema: TSCsgSchema,
    version: TSCsgVersion,
  });
  emitFact({
    kind: "ts.project",
    id: stableId("ts.project", build.cwd, projectFiles.length.toString()),
    root: toPosix(path.resolve(build.cwd)),
    sourceFileCount: projectFiles.length,
    compilerOptions: compilerOptionsFact(build.compilerOptions),
  });

  for (const sourceFile of projectFiles) {
    emitSourceFile(sourceFile);
    visit(sourceFile, sourceFile);
  }

  for (const item of unsupported) {
    facts.push(item);
  }

  return { facts, unsupported, diagnostics: [] };

  function visit(node: ts.Node, sourceFile: ts.SourceFile): void {
    inspectUnsupported(node, sourceFile);

    if (ts.isImportDeclaration(node)) {
      emitImportDeclaration(node, sourceFile);
    } else if (ts.isExportDeclaration(node)) {
      emitExportDeclaration(node, sourceFile);
    } else if (ts.isExportAssignment(node)) {
      emitExportAssignment(node, sourceFile);
    } else if (ts.isFunctionDeclaration(node)) {
      emitFunctionLike("ts.function", node, sourceFile);
    } else if (ts.isFunctionExpression(node)) {
      emitFunctionLike("ts.function_expr", node, sourceFile);
    } else if (ts.isArrowFunction(node)) {
      emitFunctionLike("ts.arrow_function", node, sourceFile);
    } else if (ts.isClassDeclaration(node)) {
      emitClassDeclaration(node, sourceFile);
    } else if (ts.isInterfaceDeclaration(node)) {
      emitInterfaceDeclaration(node, sourceFile);
    } else if (ts.isTypeAliasDeclaration(node)) {
      emitTypeAliasDeclaration(node, sourceFile);
    } else if (ts.isEnumDeclaration(node)) {
      emitEnumDeclaration(node, sourceFile);
    } else if (ts.isVariableDeclaration(node)) {
      emitVariableDeclaration(node, sourceFile);
    } else if (ts.isMethodDeclaration(node)) {
      emitClassMember("method", node, sourceFile);
      emitFunctionLike("ts.method", node, sourceFile);
    } else if (ts.isConstructorDeclaration(node)) {
      emitClassMember("constructor", node, sourceFile);
      emitFunctionLike("ts.constructor", node, sourceFile);
    } else if (ts.isPropertyDeclaration(node)) {
      emitClassMember("property", node, sourceFile);
    } else if (ts.isGetAccessorDeclaration(node)) {
      emitClassMember("getter", node, sourceFile);
      emitFunctionLike("ts.getter", node, sourceFile);
    } else if (ts.isSetAccessorDeclaration(node)) {
      emitClassMember("setter", node, sourceFile);
      emitFunctionLike("ts.setter", node, sourceFile);
    } else if (ts.isCallExpression(node)) {
      emitCallExpression(node, sourceFile);
    } else if (ts.isNewExpression(node)) {
      emitNewExpression(node, sourceFile);
    } else if (isControlNode(node)) {
      emitControlFact(node, sourceFile);
    } else if (isJsxNode(node)) {
      emitJsxFact(node, sourceFile);
    }

    ts.forEachChild(node, (child) => visit(child, sourceFile));
  }

  function emitSourceFile(sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.source_file",
      id: fileId(sourceFile),
      path: relPath(build.cwd, sourceFile.fileName),
      sha256: sha256(sourceFile.text),
      isDeclarationFile: sourceFile.isDeclarationFile,
    });
  }

  function emitImportDeclaration(node: ts.ImportDeclaration, sourceFile: ts.SourceFile): void {
    const specifier = stringModuleSpecifier(node.moduleSpecifier);
    if (!specifier) return;

    const clause = node.importClause;
    const namedBindings = clause?.namedBindings;
    emitFact({
      kind: "ts.module_import",
      id: nodeId("ts.module_import", node, sourceFile, specifier),
      loc: locFor(sourceFile, node),
      module: specifier,
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
  }

  function emitExportDeclaration(node: ts.ExportDeclaration, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.module_export",
      id: nodeId("ts.module_export", node, sourceFile, stringModuleSpecifier(node.moduleSpecifier) ?? "local"),
      loc: locFor(sourceFile, node),
      module: stringModuleSpecifier(node.moduleSpecifier),
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
  }

  function emitExportAssignment(node: ts.ExportAssignment, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.module_export_assignment",
      id: nodeId("ts.module_export_assignment", node, sourceFile, ""),
      loc: locFor(sourceFile, node),
      exportEquals: node.isExportEquals,
      expressionKind: syntaxKindName(node.expression.kind),
      expressionText: node.expression.getText(sourceFile),
    });
  }

  function emitFunctionLike(kind: string, node: SupportedFunctionLike, sourceFile: ts.SourceFile): void {
    const signature = checker.getSignatureFromDeclaration(node);
    const returnType = signature ? checker.getReturnTypeOfSignature(signature) : undefined;
    if (returnType) {
      assertNoAnyType(sourceFile, node, returnType, `${kind}.return`);
    }

    emitFact({
      kind,
      id: nodeId(kind, node, sourceFile, declarationName(node, sourceFile)),
      loc: locFor(sourceFile, node),
      name: declarationName(node, sourceFile),
      symbol: symbolFact(symbolForNodeName(functionNameNode(node)), node, sourceFile),
      async: hasModifier(node, ts.SyntaxKind.AsyncKeyword),
      generator: "asteriskToken" in node && node.asteriskToken !== undefined,
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
      defaultExport: hasModifier(node, ts.SyntaxKind.DefaultKeyword),
      typeParameters: typeParameterFacts(functionTypeParameters(node)),
      parameters: node.parameters.map((param) => parameterFact(param, sourceFile)),
      returnType: returnType ? typeText(returnType) : "void",
    });
  }

  function emitClassDeclaration(node: ts.ClassDeclaration, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.class",
      id: nodeId("ts.class", node, sourceFile, node.name?.text ?? ""),
      loc: locFor(sourceFile, node),
      name: node.name?.text ?? "<anonymous>",
      symbol: symbolFact(node.name ? checker.getSymbolAtLocation(node.name) : undefined, node, sourceFile),
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
      defaultExport: hasModifier(node, ts.SyntaxKind.DefaultKeyword),
      abstract: hasModifier(node, ts.SyntaxKind.AbstractKeyword),
      typeParameters: typeParameterFacts(node.typeParameters),
      heritage: heritageFacts(node.heritageClauses),
    });
  }

  function emitInterfaceDeclaration(node: ts.InterfaceDeclaration, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.interface",
      id: nodeId("ts.interface", node, sourceFile, node.name.text),
      loc: locFor(sourceFile, node),
      name: node.name.text,
      symbol: symbolFact(checker.getSymbolAtLocation(node.name), node, sourceFile),
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
      typeParameters: typeParameterFacts(node.typeParameters),
      heritage: heritageFacts(node.heritageClauses),
      members: node.members.map((member) => memberSignatureFact(member, sourceFile)),
    });
  }

  function emitTypeAliasDeclaration(node: ts.TypeAliasDeclaration, sourceFile: ts.SourceFile): void {
    const aliasType = checker.getTypeFromTypeNode(node.type);
    assertNoAnyType(sourceFile, node, aliasType, "ts.type_alias");
    emitFact({
      kind: "ts.type_alias",
      id: nodeId("ts.type_alias", node, sourceFile, node.name.text),
      loc: locFor(sourceFile, node),
      name: node.name.text,
      symbol: symbolFact(checker.getSymbolAtLocation(node.name), node, sourceFile),
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
      typeParameters: typeParameterFacts(node.typeParameters),
      type: typeText(aliasType),
      typeSource: node.type.getText(sourceFile),
    });
  }

  function emitEnumDeclaration(node: ts.EnumDeclaration, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.enum",
      id: nodeId("ts.enum", node, sourceFile, node.name.text),
      loc: locFor(sourceFile, node),
      name: node.name.text,
      symbol: symbolFact(checker.getSymbolAtLocation(node.name), node, sourceFile),
      exported: hasModifier(node, ts.SyntaxKind.ExportKeyword),
      constEnum: hasModifier(node, ts.SyntaxKind.ConstKeyword),
      members: node.members.map((member) => ({
        name: propertyNameText(member.name, sourceFile),
        value: constantValue(member),
      })),
    });
  }

  function emitVariableDeclaration(node: ts.VariableDeclaration, sourceFile: ts.SourceFile): void {
    const type = checker.getTypeAtLocation(node.name);
    assertNoAnyType(sourceFile, node, type, "ts.variable");
    emitFact({
      kind: "ts.variable",
      id: nodeId("ts.variable", node, sourceFile, node.name.getText(sourceFile)),
      loc: locFor(sourceFile, node),
      name: bindingNameText(node.name, sourceFile),
      symbol: symbolFact(symbolForNodeName(node.name), node, sourceFile),
      declarationKind: variableDeclarationKind(node),
      type: typeText(type),
      initializerKind: node.initializer ? syntaxKindName(node.initializer.kind) : undefined,
      exported: isExportedVariable(node),
    });
  }

  function emitClassMember(memberKind: string, node: ClassMemberWithName, sourceFile: ts.SourceFile): void {
    const typeNode = ts.isPropertyDeclaration(node) ? node : undefined;
    const valueType = typeNode ? checker.getTypeAtLocation(typeNode.name) : undefined;
    if (valueType) {
      assertNoAnyType(sourceFile, node, valueType, `ts.class_member.${memberKind}`);
    }

    emitFact({
      kind: "ts.class_member",
      id: nodeId("ts.class_member", node, sourceFile, `${memberKind}:${memberName(node, sourceFile)}`),
      loc: locFor(sourceFile, node),
      memberKind,
      name: memberName(node, sourceFile),
      visibility: visibility(node),
      static: hasModifier(node, ts.SyntaxKind.StaticKeyword),
      readonly: hasModifier(node, ts.SyntaxKind.ReadonlyKeyword),
      abstract: hasModifier(node, ts.SyntaxKind.AbstractKeyword),
      optional: "questionToken" in node && node.questionToken !== undefined,
      type: valueType ? typeText(valueType) : undefined,
    });
  }

  function emitCallExpression(node: ts.CallExpression, sourceFile: ts.SourceFile): void {
    const signature = checker.getResolvedSignature(node);
    const returnType = signature ? checker.getReturnTypeOfSignature(signature) : undefined;
    if (returnType) {
      assertNoAnyType(sourceFile, node, returnType, "ts.call.return");
    }

    emitFact({
      kind: "ts.call",
      id: nodeId("ts.call", node, sourceFile, node.expression.getText(sourceFile)),
      loc: locFor(sourceFile, node),
      calleeText: node.expression.getText(sourceFile),
      calleeKind: syntaxKindName(node.expression.kind),
      resolvedSymbol: symbolFact(signature?.declaration ? symbolForDeclaration(signature.declaration) : checker.getSymbolAtLocation(node.expression), node, sourceFile),
      argumentCount: node.arguments.length,
      returnType: returnType ? typeText(returnType) : undefined,
      typeArguments: node.typeArguments?.map((item) => checker.typeToString(checker.getTypeFromTypeNode(item))) ?? [],
    });
  }

  function emitNewExpression(node: ts.NewExpression, sourceFile: ts.SourceFile): void {
    const type = checker.getTypeAtLocation(node);
    assertNoAnyType(sourceFile, node, type, "ts.new");
    emitFact({
      kind: "ts.new",
      id: nodeId("ts.new", node, sourceFile, node.expression.getText(sourceFile)),
      loc: locFor(sourceFile, node),
      constructorText: node.expression.getText(sourceFile),
      argumentCount: node.arguments?.length ?? 0,
      type: typeText(type),
    });
  }

  function emitControlFact(node: ts.Node, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.control",
      id: nodeId("ts.control", node, sourceFile, syntaxKindName(node.kind)),
      loc: locFor(sourceFile, node),
      controlKind: syntaxKindName(node.kind),
    });
  }

  function emitJsxFact(node: ts.Node, sourceFile: ts.SourceFile): void {
    emitFact({
      kind: "ts.jsx",
      id: nodeId("ts.jsx", node, sourceFile, syntaxKindName(node.kind)),
      loc: locFor(sourceFile, node),
      jsxKind: syntaxKindName(node.kind),
    });
  }

  function inspectUnsupported(node: ts.Node, sourceFile: ts.SourceFile): void {
    if (isDeclarationWithComputedName(node)) {
      const name = node.name;
      if (ts.isComputedPropertyName(name) && !isLiteralExpression(name.expression)) {
        addUnsupported(sourceFile, name, "computed_name.dynamic", "computed declaration names must be literal");
      }
    }

    if (ts.isCallExpression(node)) {
      if (ts.isIdentifier(node.expression) && node.expression.text === "eval") {
        addUnsupported(sourceFile, node, "eval", "eval cannot be represented as closed CSG facts");
      }

      if (ts.isIdentifier(node.expression) && node.expression.text === "require") {
        const arg = node.arguments[0];
        if (!arg || !isStringLiteralLike(arg)) {
          addUnsupported(sourceFile, node, "require.dynamic", "require() module specifier must be a string literal");
        }
      }

      if (node.expression.kind === ts.SyntaxKind.ImportKeyword) {
        const arg = node.arguments[0];
        if (!arg || !isStringLiteralLike(arg)) {
          addUnsupported(sourceFile, node, "import.dynamic", "dynamic import() module specifier must be a string literal");
        }
      }
    }

    if (ts.isBinaryExpression(node) && isAssignmentOperator(node.operatorToken.kind) && isPrototypeMutation(node.left)) {
      addUnsupported(sourceFile, node, "prototype.mutation", "prototype mutation is runtime shape mutation and is not closed CSG");
    }
  }

  function addUnsupported(sourceFile: ts.SourceFile, node: ts.Node, code: string, message: string): void {
    const key = `${code}:${relPath(build.cwd, sourceFile.fileName)}:${node.pos}:${node.end}`;
    if (unsupportedKeys.has(key)) return;
    unsupportedKeys.add(key);
    unsupported.push({
      kind: "ts.unsupported",
      id: nodeId("ts.unsupported", node, sourceFile, code),
      loc: locFor(sourceFile, node),
      code,
      message,
    });
  }

  function assertNoAnyType(sourceFile: ts.SourceFile, node: ts.Node, type: ts.Type, context: string): void {
    if (!containsAny(type, node)) return;
    addUnsupported(sourceFile, node, "type.any", `${context} resolves to any; explicit closed static type is required`);
  }

  function emitFact(fact: CsgFact): void {
    facts.push(fact);
  }

  function nodeId(kind: string, node: ts.Node, sourceFile: ts.SourceFile, salt: string): string {
    return stableId(kind, relPath(build.cwd, sourceFile.fileName), String(node.pos), String(node.end), salt);
  }

  function fileId(sourceFile: ts.SourceFile): string {
    return stableId("ts.source_file", relPath(build.cwd, sourceFile.fileName));
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

  function parameterFact(param: ts.ParameterDeclaration, sourceFile: ts.SourceFile): FactFields {
    const type = checker.getTypeAtLocation(param.name);
    assertNoAnyType(sourceFile, param, type, "ts.parameter");
    return {
      name: bindingNameText(param.name, sourceFile),
      optional: param.questionToken !== undefined || param.initializer !== undefined,
      rest: param.dotDotDotToken !== undefined,
      type: typeText(type),
      initializerKind: param.initializer ? syntaxKindName(param.initializer.kind) : undefined,
    };
  }

  function memberSignatureFact(member: ts.TypeElement, sourceFile: ts.SourceFile): FactFields {
    const name = "name" in member && member.name ? propertyNameText(member.name, sourceFile) : "";
    const memberType = checker.getTypeAtLocation(member);
    assertNoAnyType(sourceFile, member, memberType, "ts.interface_member");
    return {
      kind: syntaxKindName(member.kind),
      name,
      optional: "questionToken" in member && member.questionToken !== undefined,
      type: typeText(memberType),
    };
  }

  function symbolFact(symbol: ts.Symbol | undefined, node: ts.Node, sourceFile: ts.SourceFile): FactFields | undefined {
    if (!symbol) return undefined;
    const declarations = symbol.declarations ?? [];
    const firstDeclaration = declarations[0];
    const fqName = normalizeCheckerText(checker.getFullyQualifiedName(symbol));
    return {
      id: stableId("ts.symbol", fqName, firstDeclaration ? declarationKey(firstDeclaration) : nodeId("symbol", node, sourceFile, fqName)),
      name: symbol.getName(),
      fqName,
      flags: symbolFlags(symbol.flags),
    };
  }

  function typeText(type: ts.Type): string {
    return normalizeCheckerText(checker.typeToString(type, undefined, ts.TypeFormatFlags.NoTruncation | ts.TypeFormatFlags.UseFullyQualifiedType));
  }

  function symbolForNodeName(name: ts.Node | undefined): ts.Symbol | undefined {
    if (!name) return undefined;
    return checker.getSymbolAtLocation(name);
  }

  function symbolForDeclaration(declaration: ts.Declaration): ts.Symbol | undefined {
    const namedDeclaration = declaration as ts.Declaration & { name?: ts.Node };
    if (namedDeclaration.name) {
      return checker.getSymbolAtLocation(namedDeclaration.name);
    }
    return undefined;
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
      if (steps > 512) return false;

      if (type.isUnionOrIntersection()) {
        stack.push(...type.types);
      }

      const typeReference = type as ts.TypeReference;
      if (typeReference.target) {
        for (const arg of checker.getTypeArguments(typeReference)) stack.push(arg);
      }

      for (const prop of type.getProperties().slice(0, 128)) {
        const propDeclaration = prop.valueDeclaration ?? prop.declarations?.[0];
        if (!propDeclaration || !isProjectSourceFile(build.cwd, propDeclaration.getSourceFile())) continue;
        const propType = checker.getTypeOfSymbolAtLocation(prop, propDeclaration);
        stack.push(propType);
      }
    }

    return false;
  }

  function declarationKey(node: ts.Declaration): string {
    const sourceFile = node.getSourceFile();
    return `${relPath(build.cwd, sourceFile.fileName)}:${node.pos}:${node.end}`;
  }

  function normalizeCheckerText(value: string): string {
    return value.split(toPosix(build.cwd)).join("$root");
  }
}

function buildProgram(options: ExtractOptions): ProgramBuild {
  const cwd = path.resolve(options.rootDir ?? process.cwd());

  if (options.project) {
    const configDiagnostics: ts.Diagnostic[] = [];
    const project = path.resolve(cwd, options.project);
    const parsed = ts.getParsedCommandLineOfConfigFile(project, {}, {
      ...ts.sys,
      onUnRecoverableConfigFileDiagnostic: (diagnostic) => {
        configDiagnostics.push(diagnostic);
      },
    });

    if (!parsed) {
      return {
        cwd,
        program: ts.createProgram({ rootNames: [], options: defaultCompilerOptions(cwd) }),
        configDiagnostics,
        compilerOptions: defaultCompilerOptions(cwd),
      };
    }

    const createOptions: ts.CreateProgramOptions = {
      rootNames: parsed.fileNames,
      options: parsed.options,
    };
    if (parsed.projectReferences) {
      createOptions.projectReferences = parsed.projectReferences;
    }

    return {
      cwd,
      program: ts.createProgram(createOptions),
      configDiagnostics: [...configDiagnostics, ...parsed.errors],
      compilerOptions: parsed.options,
    };
  }

  if (!options.files || options.files.length === 0) {
    const diagnostic = makeDiagnostic("either --project or at least one --file is required");
    return {
      cwd,
      program: ts.createProgram({ rootNames: [], options: defaultCompilerOptions(cwd) }),
      configDiagnostics: [diagnostic],
      compilerOptions: defaultCompilerOptions(cwd),
    };
  }

  const rootNames = options.files.map((file) => path.resolve(cwd, file));
  const compilerOptions = defaultCompilerOptions(cwd);
  return {
    cwd,
    program: ts.createProgram({ rootNames, options: compilerOptions }),
    configDiagnostics: [],
    compilerOptions,
  };
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

function compilerOptionsFact(options: ts.CompilerOptions): FactFields {
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

function syntaxKindName(kind: ts.SyntaxKind): string {
  return ts.SyntaxKind[kind] ?? `SyntaxKind${kind}`;
}

function hasModifier(node: ts.Node, kind: ts.SyntaxKind): boolean {
  return ts.canHaveModifiers(node) && (ts.getModifiers(node)?.some((modifier) => modifier.kind === kind) ?? false);
}

function typeParameterFacts(nodes: ts.NodeArray<ts.TypeParameterDeclaration> | undefined): FactFields[] {
  return nodes?.map((node) => ({
    name: node.name.text,
    constraint: node.constraint?.getText(),
    default: node.default?.getText(),
  })) ?? [];
}

function heritageFacts(nodes: ts.NodeArray<ts.HeritageClause> | undefined): FactFields[] {
  const facts: FactFields[] = [];
  for (const clause of nodes ?? []) {
    for (const type of clause.types) {
      facts.push({
        kind: syntaxKindName(clause.token),
        expression: type.expression.getText(),
        typeArguments: type.typeArguments?.map((item) => item.getText()) ?? [],
      });
    }
  }
  return facts;
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

function declarationName(node: SupportedFunctionLike, sourceFile: ts.SourceFile): string {
  const name = functionNameNode(node);
  if (name) return propertyNameText(name, sourceFile);
  if (ts.isConstructorDeclaration(node)) return "constructor";
  return "<anonymous>";
}

function memberName(node: ClassMemberWithName, sourceFile: ts.SourceFile): string {
  if (ts.isConstructorDeclaration(node)) return "constructor";
  return "name" in node && node.name ? propertyNameText(node.name, sourceFile) : "";
}

function functionNameNode(node: SupportedFunctionLike): ts.PropertyName | undefined {
  if ("name" in node && node.name) return node.name;
  return undefined;
}

function functionTypeParameters(node: SupportedFunctionLike): ts.NodeArray<ts.TypeParameterDeclaration> | undefined {
  if ("typeParameters" in node) return node.typeParameters;
  return undefined;
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

function visibility(node: ts.Node): string {
  if (hasModifier(node, ts.SyntaxKind.PrivateKeyword)) return "private";
  if (hasModifier(node, ts.SyntaxKind.ProtectedKeyword)) return "protected";
  if (hasModifier(node, ts.SyntaxKind.PublicKeyword)) return "public";
  return "default";
}

function constantValue(member: ts.EnumMember): string | number | undefined {
  if (!member.initializer) return undefined;
  if (ts.isStringLiteral(member.initializer)) return member.initializer.text;
  if (ts.isNumericLiteral(member.initializer)) return Number(member.initializer.text);
  return member.initializer.getText();
}

function isControlNode(node: ts.Node): boolean {
  return ts.isIfStatement(node)
    || ts.isForStatement(node)
    || ts.isForInStatement(node)
    || ts.isForOfStatement(node)
    || ts.isWhileStatement(node)
    || ts.isDoStatement(node)
    || ts.isSwitchStatement(node)
    || ts.isTryStatement(node)
    || ts.isCatchClause(node)
    || ts.isReturnStatement(node)
    || ts.isThrowStatement(node)
    || ts.isBreakStatement(node)
    || ts.isContinueStatement(node)
    || ts.isAwaitExpression(node);
}

function isJsxNode(node: ts.Node): boolean {
  return ts.isJsxElement(node)
    || ts.isJsxSelfClosingElement(node)
    || ts.isJsxFragment(node)
    || ts.isJsxOpeningElement(node)
    || ts.isJsxClosingElement(node);
}

function isAssignmentOperator(kind: ts.SyntaxKind): boolean {
  return kind >= ts.SyntaxKind.FirstAssignment && kind <= ts.SyntaxKind.LastAssignment;
}

function isPrototypeMutation(node: ts.Expression): boolean {
  if (ts.isPropertyAccessExpression(node)) {
    return ts.isPropertyAccessExpression(node.expression) && node.expression.name.text === "prototype";
  }
  if (ts.isElementAccessExpression(node)) {
    return ts.isPropertyAccessExpression(node.expression) && node.expression.name.text === "prototype";
  }
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

type SupportedFunctionLike =
  | ts.FunctionDeclaration
  | ts.FunctionExpression
  | ts.ArrowFunction
  | ts.MethodDeclaration
  | ts.ConstructorDeclaration
  | ts.GetAccessorDeclaration
  | ts.SetAccessorDeclaration;

type ClassMemberWithName =
  | ts.MethodDeclaration
  | ts.ConstructorDeclaration
  | ts.PropertyDeclaration
  | ts.GetAccessorDeclaration
  | ts.SetAccessorDeclaration;

type DeclarationWithName =
  | ts.MethodDeclaration
  | ts.PropertyDeclaration
  | ts.GetAccessorDeclaration
  | ts.SetAccessorDeclaration
  | ts.PropertySignature
  | ts.MethodSignature
  | ts.EnumMember;
