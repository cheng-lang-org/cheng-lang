import path from "node:path";
import * as ts from "typescript";

import type { ExtractOptions } from "./schema.js";

export interface ChengCsgV2Options extends ExtractOptions {
  entry?: string;
  target?: string;
}

export interface ChengCsgV2Result {
  diagnostics: string[];
  unsupported: string[];
  text: string;
  functionCount: number;
  wordCount: number;
}

type ProgramBuild = {
  cwd: string;
  program: ts.Program;
  diagnostics: string[];
};

type FunctionInfo = {
  fn: ts.FunctionDeclaration;
  itemId: number;
  name: string;
  symbol: string;
};

type CompiledFunction = {
  bodyKind: string;
  info: FunctionInfo;
  relocs: LocalReloc[];
  words: number[];
  wordOffset: number;
};

type LocalReloc = {
  targetSymbol: string;
  wordIndex: number;
};

export function emitChengCsgV2FromTs(options: ChengCsgV2Options): ChengCsgV2Result {
  const target = options.target ?? "arm64-apple-darwin";
  const entry = options.entry ?? "main";
  const format = objectFormatForTarget(target);
  if (!format) {
    return failUnsupported(`target ${target} is not supported by ts-csg Cheng CSG v2 writer`);
  }

  const build = buildProgram(options);
  if (build.diagnostics.length > 0) {
    return { diagnostics: build.diagnostics, unsupported: [], text: "", functionCount: 0, wordCount: 0 };
  }

  const sourceFiles = build.program
    .getSourceFiles()
    .filter((sourceFile) => isProjectSourceFile(build.cwd, sourceFile))
    .sort((left, right) => relPath(build.cwd, left.fileName).localeCompare(relPath(build.cwd, right.fileName)));

  if (sourceFiles.length !== 1) {
    return failUnsupported(`expected exactly one project source file, got ${sourceFiles.length}`);
  }

  const sourceFile = sourceFiles[0];
  if (!sourceFile) {
    return failUnsupported("missing project source file");
  }

  const checker = build.program.getTypeChecker();
  const collected = collectFunctions(sourceFile, checker, target, entry);
  if (!collected.ok) {
    return { diagnostics: [], unsupported: collected.messages ?? [collected.message], text: "", functionCount: 0, wordCount: 0 };
  }

  const compiledFunctions: CompiledFunction[] = [];
  let wordOffset = 0;
  for (const info of collected.functions) {
    const compiler = new Arm64TsSubsetCompiler(sourceFile, checker, collected.byName, info);
    const compiled = compiler.compileFunction();
    if (!compiled.ok) {
      return failUnsupported(`${info.name}: ${compiled.message}`);
    }
    compiledFunctions.push({
      bodyKind: compiled.bodyKind,
      info,
      relocs: compiled.relocs,
      words: compiled.words,
      wordOffset,
    });
    wordOffset += compiled.words.length;
  }

  const symbol = symbolForEntry(target, entry);
  const writer = new CsgV2Writer();
  writer.stringRecord(1, target);
  writer.stringRecord(2, format);
  writer.stringRecord(3, symbol);
  for (const item of compiledFunctions) {
    writer.functionRecord(item.info.itemId, item.wordOffset, item.words.length, item.info.symbol, item.bodyKind);
  }
  for (const item of compiledFunctions) {
    for (const word of item.words) {
      writer.wordRecord(word);
    }
  }
  for (const item of compiledFunctions) {
    for (const reloc of item.relocs) {
      writer.relocRecord(item.info.itemId, item.wordOffset + reloc.wordIndex, reloc.targetSymbol);
    }
  }

  return {
    diagnostics: [],
    unsupported: [],
    text: writer.toString(),
    functionCount: compiledFunctions.length,
    wordCount: wordOffset,
  };
}

function collectFunctions(
  sourceFile: ts.SourceFile,
  checker: ts.TypeChecker,
  target: string,
  entry: string,
): CompileResult<{ byName: Map<string, FunctionInfo>; functions: FunctionInfo[] }> & { messages?: string[] } {
  const messages: string[] = [];
  const byName = new Map<string, FunctionInfo>();
  const functions: FunctionInfo[] = [];

  for (const statement of sourceFile.statements) {
    if (!ts.isFunctionDeclaration(statement)) {
      messages.push(`top-level statement is not supported: ${ts.SyntaxKind[statement.kind]}`);
      continue;
    }
    if (!statement.name) {
      messages.push("anonymous top-level functions are not supported");
      continue;
    }
    const name = statement.name.text;
    if (byName.has(name)) {
      messages.push(`duplicate function: ${name}`);
      continue;
    }
    const info: FunctionInfo = {
      fn: statement,
      itemId: functions.length + 1,
      name,
      symbol: symbolForEntry(target, name),
    };
    byName.set(name, info);
    functions.push(info);
  }

  const entryInfo = byName.get(entry);
  if (!entryInfo) {
    messages.push(`entry function must be named ${entry}`);
  }

  for (const info of functions) {
    const fn = info.fn;
    if (info.name === entry) {
      if (!hasModifier(fn, ts.SyntaxKind.ExportKeyword)) {
        messages.push(`${entry} must be exported`);
      }
      if (fn.parameters.length !== 0) {
        messages.push(`${entry} must not have parameters`);
      }
    }
    if (fn.typeParameters && fn.typeParameters.length > 0) {
      messages.push(`${info.name} must not be generic`);
    }
    if (hasModifier(fn, ts.SyntaxKind.AsyncKeyword)) {
      messages.push(`${info.name} must not be async`);
    }
    if (fn.asteriskToken) {
      messages.push(`${info.name} must not be a generator`);
    }
    if (!fn.body) {
      messages.push(`${info.name} must have a body`);
    }
    if (!fn.type || fn.type.getText(sourceFile) !== "number") {
      messages.push(`${info.name} must declare return type number`);
    }
    const signature = checker.getSignatureFromDeclaration(fn);
    const returnType = signature ? checker.getReturnTypeOfSignature(signature) : undefined;
    if (!returnType || (returnType.flags & ts.TypeFlags.NumberLike) === 0) {
      messages.push(`${info.name} return type must resolve to number`);
    }
    if (fn.parameters.length > 8) {
      messages.push(`${info.name} must not have more than 8 parameters`);
    }
    for (const parameter of fn.parameters) {
      if (!ts.isIdentifier(parameter.name)) {
        messages.push(`${info.name} parameters must be simple identifiers`);
        continue;
      }
      if (!parameter.type || parameter.type.getText(sourceFile) !== "number") {
        messages.push(`${info.name}.${parameter.name.text} must declare type number`);
      }
      if ((checker.getTypeAtLocation(parameter.name).flags & ts.TypeFlags.NumberLike) === 0) {
        messages.push(`${info.name}.${parameter.name.text} must resolve to number`);
      }
      if (parameter.initializer || parameter.questionToken || parameter.dotDotDotToken) {
        messages.push(`${info.name}.${parameter.name.text} must be a required positional parameter`);
      }
    }
  }

  if (messages.length > 0) {
    return { ok: false, message: messages[0] ?? "unsupported function set", messages };
  }
  return { ok: true, byName, functions };
}

type CompileOk<T> = { ok: true } & T;
type CompileFail = { ok: false; message: string };
type CompileResult<T> = CompileOk<T> | CompileFail;

type VarInfo = {
  reg: number;
  value?: number;
};

type ExprValue = {
  reg: number;
  value?: number;
};

class Arm64TsSubsetCompiler {
  private readonly env = new Map<string, VarInfo>();
  private readonly relocs: LocalReloc[] = [];
  private readonly words: number[] = [];
  private nextVarReg = 19;
  private nextTempReg = 0;

  constructor(
    private readonly sourceFile: ts.SourceFile,
    private readonly checker: ts.TypeChecker,
    private readonly functions: Map<string, FunctionInfo>,
    private readonly current: FunctionInfo,
  ) {}

  compileFunction(): CompileResult<{ bodyKind: string; relocs: LocalReloc[]; words: number[] }> {
    const fn = this.current.fn;
    if (!fn.body) return failCompile("entry function must have a body");
    this.emitPrologue();
    const params = this.bindParameters(fn);
    if (!params.ok) return params;
    const body = this.compileBlock(fn.body.statements);
    if (!body.ok) return body;
    if (!body.terminates) return failCompile("entry function must return on every path");
    return { ok: true, words: [...this.words], relocs: [...this.relocs], bodyKind: "ts_i32_call_cfg_v1" };
  }

  private bindParameters(fn: ts.FunctionDeclaration): CompileResult<{}> {
    for (let index = 0; index < fn.parameters.length; index += 1) {
      const parameter = fn.parameters[index];
      if (!parameter || !ts.isIdentifier(parameter.name)) {
        return failCompile("parameters must be simple identifiers");
      }
      const reg = this.allocVarReg();
      if (!reg.ok) return reg;
      this.emit(movReg(reg.reg, index));
      this.env.set(parameter.name.text, { reg: reg.reg });
    }
    return { ok: true };
  }

  private compileBlock(statements: ts.NodeArray<ts.Statement>): CompileResult<{ terminates: boolean }> {
    for (const statement of statements) {
      this.nextTempReg = 0;
      const result = this.compileStatement(statement);
      if (!result.ok) return result;
      if (result.terminates) return { ok: true, terminates: true };
    }
    return { ok: true, terminates: false };
  }

  private compileStatement(statement: ts.Statement): CompileResult<{ terminates: boolean }> {
    if (ts.isVariableStatement(statement)) {
      const result = this.compileVariableStatement(statement);
      if (!result.ok) return result;
      return { ok: true, terminates: false };
    }

    if (ts.isReturnStatement(statement)) {
      const result = this.compileReturn(statement);
      if (!result.ok) return result;
      return { ok: true, terminates: true };
    }

    if (ts.isIfStatement(statement)) {
      return this.compileIf(statement);
    }

    if (ts.isBlock(statement)) {
      return this.compileBlock(statement.statements);
    }

    return failCompile(`unsupported statement: ${ts.SyntaxKind[statement.kind]}`);
  }

  private compileVariableStatement(statement: ts.VariableStatement): CompileResult<{}> {
    if ((statement.declarationList.flags & ts.NodeFlags.Const) === 0 &&
        (statement.declarationList.flags & ts.NodeFlags.Let) === 0) {
      return failCompile("only const/let local variables are supported");
    }
    if (statement.declarationList.declarations.length !== 1) {
      return failCompile("variable statement must contain exactly one declaration");
    }

    const declaration = statement.declarationList.declarations[0];
    if (!declaration) return failCompile("missing variable declaration");
    if (!ts.isIdentifier(declaration.name)) {
      return failCompile("destructuring declarations are not supported");
    }
    if (this.env.has(declaration.name.text)) {
      return failCompile(`duplicate local variable: ${declaration.name.text}`);
    }
    if (!declaration.initializer) {
      return failCompile(`local variable ${declaration.name.text} must have an initializer`);
    }
    if (!this.isNumber(declaration.name)) {
      return failCompile(`local variable ${declaration.name.text} must resolve to number`);
    }

    const expr = this.compileExpr(declaration.initializer);
    if (!expr.ok) return expr;
    const reg = this.allocVarReg();
    if (!reg.ok) return reg;
    this.emit(movReg(reg.reg, expr.reg));
    const info: VarInfo = { reg: reg.reg };
    if (expr.value !== undefined) info.value = expr.value;
    this.env.set(declaration.name.text, info);
    return { ok: true };
  }

  private compileReturn(statement: ts.ReturnStatement): CompileResult<{}> {
    if (!statement.expression) return failCompile("return statement must return a number expression");
    const expr = this.compileExpr(statement.expression);
    if (!expr.ok) return expr;
    if (expr.reg !== 0) {
      this.emit(movReg(0, expr.reg));
    }
    this.emitEpilogue();
    return { ok: true };
  }

  private compileIf(statement: ts.IfStatement): CompileResult<{ terminates: boolean }> {
    const branch = this.compileBranchIfFalse(statement.expression);
    if (!branch.ok) return branch;

    this.nextTempReg = 0;
    const thenResult = this.compileNestedStatement(statement.thenStatement);
    if (!thenResult.ok) return thenResult;

    if (!statement.elseStatement) {
      this.patchBranch(branch.patchIndex, this.words.length);
      return { ok: true, terminates: false };
    }

    if (thenResult.terminates) {
      this.patchBranch(branch.patchIndex, this.words.length);
      this.nextTempReg = 0;
      const elseResult = this.compileNestedStatement(statement.elseStatement);
      if (!elseResult.ok) return elseResult;
      return { ok: true, terminates: elseResult.terminates };
    }

    const jumpEnd = this.emitBPlaceholder();
    this.patchBranch(branch.patchIndex, this.words.length);
    this.nextTempReg = 0;
    const elseResult = this.compileNestedStatement(statement.elseStatement);
    if (!elseResult.ok) return elseResult;
    this.patchBranch(jumpEnd, this.words.length);
    return { ok: true, terminates: elseResult.terminates };
  }

  private compileNestedStatement(statement: ts.Statement): CompileResult<{ terminates: boolean }> {
    if (ts.isBlock(statement)) return this.compileBlock(statement.statements);
    return this.compileStatement(statement);
  }

  private compileExpr(expression: ts.Expression): CompileResult<ExprValue> {
    if (!this.isNumber(expression)) {
      return failCompile(`expression must resolve to number: ${expression.getText(this.sourceFile)}`);
    }

    if (ts.isNumericLiteral(expression)) {
      const parsed = parseIntegerLiteral(expression.text);
      if (!parsed.ok) return parsed;
      return this.loadImmediate(parsed.value);
    }

    if (ts.isPrefixUnaryExpression(expression) &&
        expression.operator === ts.SyntaxKind.MinusToken &&
        ts.isNumericLiteral(expression.operand)) {
      const parsed = parseIntegerLiteral(expression.operand.text);
      if (!parsed.ok) return parsed;
      return this.loadImmediate(-parsed.value);
    }

    if (ts.isIdentifier(expression)) {
      const item = this.env.get(expression.text);
      if (!item) return failCompile(`unknown local variable: ${expression.text}`);
      if (item.value !== undefined) {
        return { ok: true, reg: item.reg, value: item.value };
      }
      return { ok: true, reg: item.reg };
    }

    if (ts.isParenthesizedExpression(expression)) {
      return this.compileExpr(expression.expression);
    }

    if (ts.isCallExpression(expression)) {
      return this.compileCall(expression);
    }

    if (ts.isBinaryExpression(expression)) {
      return this.compileBinaryExpr(expression);
    }

    return failCompile(`unsupported expression: ${ts.SyntaxKind[expression.kind]}`);
  }

  private compileBinaryExpr(expression: ts.BinaryExpression): CompileResult<ExprValue> {
    const op = expression.operatorToken.kind;
    if (op !== ts.SyntaxKind.PlusToken &&
        op !== ts.SyntaxKind.MinusToken &&
        op !== ts.SyntaxKind.AsteriskToken) {
      return failCompile(`unsupported numeric operator: ${ts.SyntaxKind[op]}`);
    }

    const left = this.compileExpr(expression.left);
    if (!left.ok) return left;
    const leftTemp = this.ensureTemp(left);
    if (!leftTemp.ok) return leftTemp;
    const right = this.compileExpr(expression.right);
    if (!right.ok) return right;

    let value = 0;
    let knownValue: number | undefined = undefined;
    if (op === ts.SyntaxKind.PlusToken) {
      this.emit(addReg(leftTemp.reg, leftTemp.reg, right.reg));
      if (leftTemp.value !== undefined && right.value !== undefined) {
        value = leftTemp.value + right.value;
        knownValue = value;
      }
    } else if (op === ts.SyntaxKind.MinusToken) {
      this.emit(subReg(leftTemp.reg, leftTemp.reg, right.reg));
      if (leftTemp.value !== undefined && right.value !== undefined) {
        value = leftTemp.value - right.value;
        knownValue = value;
      }
    } else {
      this.emit(mulReg(leftTemp.reg, leftTemp.reg, right.reg));
      if (leftTemp.value !== undefined && right.value !== undefined) {
        value = leftTemp.value * right.value;
        knownValue = value;
      }
    }
    if (knownValue !== undefined) {
      const checked = checkI32(knownValue, "numeric expression result");
      if (!checked.ok) return checked;
    }
    if (knownValue !== undefined) {
      return { ok: true, reg: leftTemp.reg, value: knownValue };
    }
    return { ok: true, reg: leftTemp.reg };
  }

  private compileCall(expression: ts.CallExpression): CompileResult<ExprValue> {
    if (!ts.isIdentifier(expression.expression)) {
      return failCompile("only direct calls to same-file functions are supported");
    }
    const target = this.functions.get(expression.expression.text);
    if (!target) {
      return failCompile(`unknown function call target: ${expression.expression.text}`);
    }
    if (target.name === this.current.name) {
      return failCompile("recursive calls are not supported");
    }
    if (expression.arguments.length !== target.fn.parameters.length) {
      return failCompile(`${target.name} expects ${target.fn.parameters.length} arguments`);
    }
    for (const arg of expression.arguments) {
      if (containsCallExpression(arg)) {
        return failCompile("nested call arguments are not supported");
      }
    }
    for (let index = 0; index < expression.arguments.length; index += 1) {
      const arg = expression.arguments[index];
      if (!arg) return failCompile("missing call argument");
      this.nextTempReg = index;
      const compiled = this.compileExpr(arg);
      if (!compiled.ok) return compiled;
      if (compiled.reg !== index) {
        this.emit(movReg(index, compiled.reg));
      }
    }
    this.nextTempReg = 0;
    const relocWord = this.words.length;
    this.emit(bl(0));
    this.relocs.push({ wordIndex: relocWord, targetSymbol: target.symbol });
    return { ok: true, reg: 0 };
  }

  private compileBranchIfFalse(expression: ts.Expression): CompileResult<{ patchIndex: number }> {
    if (!ts.isBinaryExpression(expression)) {
      return failCompile("if condition must be a numeric comparison");
    }
    if (containsCallExpression(expression.left) || containsCallExpression(expression.right)) {
      return failCompile("function calls inside conditions are not supported");
    }

    const condition = conditionForOperator(expression.operatorToken.kind);
    if (!condition.ok) return condition;

    const left = this.compileExpr(expression.left);
    if (!left.ok) return left;
    const right = this.compileExpr(expression.right);
    if (!right.ok) return right;
    this.emit(cmpReg(left.reg, right.reg));
    const patchIndex = this.emitBCondPlaceholder(invertCond(condition.cond));
    return { ok: true, patchIndex };
  }

  private loadImmediate(value: number): CompileResult<ExprValue> {
    const checked = checkI32(value, "integer literal");
    if (!checked.ok) return checked;
    const reg = this.allocTempReg();
    if (!reg.ok) return reg;
    for (const word of movI32(reg.reg, value)) {
      this.emit(word);
    }
    return { ok: true, reg: reg.reg, value };
  }

  private ensureTemp(value: ExprValue): CompileResult<ExprValue> {
    if (value.reg >= 0 && value.reg <= 7) return { ok: true, ...value };
    const reg = this.allocTempReg();
    if (!reg.ok) return reg;
    this.emit(movReg(reg.reg, value.reg));
    if (value.value !== undefined) {
      return { ok: true, reg: reg.reg, value: value.value };
    }
    return { ok: true, reg: reg.reg };
  }

  private isNumber(node: ts.Node): boolean {
    const type = this.checker.getTypeAtLocation(node);
    return (type.flags & ts.TypeFlags.NumberLike) !== 0;
  }

  private allocTempReg(): CompileResult<{ reg: number }> {
    if (this.nextTempReg > 7) return failCompile("expression needs more than 8 temporary registers");
    const reg = this.nextTempReg;
    this.nextTempReg += 1;
    return { ok: true, reg };
  }

  private allocVarReg(): CompileResult<{ reg: number }> {
    if (this.nextVarReg > 26) return failCompile("function needs more than 8 parameter/local registers");
    const reg = this.nextVarReg;
    this.nextVarReg += 1;
    return { ok: true, reg };
  }

  private emitPrologue(): void {
    this.emit(stpPre64(29, 30, -16));
    this.emit(stpPre64(19, 20, -16));
    this.emit(stpPre64(21, 22, -16));
    this.emit(stpPre64(23, 24, -16));
    this.emit(stpPre64(25, 26, -16));
    this.emit(movFpSp());
  }

  private emitEpilogue(): void {
    this.emit(ldpPost64(25, 26, 16));
    this.emit(ldpPost64(23, 24, 16));
    this.emit(ldpPost64(21, 22, 16));
    this.emit(ldpPost64(19, 20, 16));
    this.emit(ldpPost64(29, 30, 16));
    this.emit(ret());
  }

  private emit(word: number): void {
    this.words.push(word >>> 0);
  }

  private emitBCondPlaceholder(cond: number): number {
    const index = this.words.length;
    this.emit(bCond(cond, 0));
    return index;
  }

  private emitBPlaceholder(): number {
    const index = this.words.length;
    this.emit(b(0));
    return index;
  }

  private patchBranch(index: number, target: number): void {
    const current = this.words[index];
    if (current === undefined) {
      throw new Error("branch patch index out of range");
    }
    const offset = target - index;
    if ((current & 0xff000010) === 0x54000000) {
      const cond = current & 0xf;
      this.words[index] = bCond(cond, offset);
    } else {
      this.words[index] = b(offset);
    }
  }
}

function parseIntegerLiteral(text: string): CompileResult<{ value: number }> {
  const value = Number(text);
  if (!Number.isInteger(value)) {
    return failCompile("return literal must be an integer");
  }
  const checked = checkI32(value, "integer literal");
  if (!checked.ok) return checked;
  return { ok: true, value };
}

function checkI32(value: number, label: string): CompileResult<{}> {
  if (!Number.isInteger(value)) return failCompile(`${label} must be an integer`);
  if (value < -2147483648 || value > 2147483647) {
    return failCompile(`${label} must fit int32`);
  }
  return { ok: true };
}

function failCompile(message: string): CompileFail {
  return { ok: false, message };
}

function movI32(reg: number, value: number): number[] {
  const unsigned = value >>> 0;
  const lo = unsigned & 0xffff;
  const hi = (unsigned >>> 16) & 0xffff;
  const words = [movz(reg, lo, 0)];
  if (hi !== 0) {
    words.push(movk(reg, hi, 1));
  }
  return words;
}

function movz(rd: number, imm16: number, hw: number): number {
  return (0x52800000 | ((hw & 0x3) << 21) | ((imm16 & 0xffff) << 5) | (rd & 31)) >>> 0;
}

function movk(rd: number, imm16: number, hw: number): number {
  return (0x72800000 | ((hw & 0x3) << 21) | ((imm16 & 0xffff) << 5) | (rd & 31)) >>> 0;
}

function movReg(rd: number, rm: number): number {
  return (0x2a000000 | ((rm & 31) << 16) | (31 << 5) | (rd & 31)) >>> 0;
}

function addReg(rd: number, rn: number, rm: number): number {
  return (0x0b000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31)) >>> 0;
}

function subReg(rd: number, rn: number, rm: number): number {
  return (0x4b000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | (rd & 31)) >>> 0;
}

function mulReg(rd: number, rn: number, rm: number): number {
  return (0x1b000000 | ((rm & 31) << 16) | (31 << 10) | ((rn & 31) << 5) | (rd & 31)) >>> 0;
}

function cmpReg(rn: number, rm: number): number {
  return (0x6b000000 | ((rm & 31) << 16) | ((rn & 31) << 5) | 31) >>> 0;
}

function stpPre64(rt: number, rt2: number, immBytes: number): number {
  if (immBytes % 8 !== 0) throw new Error("stp pre-index offset must be 8-byte aligned");
  const imm7 = (immBytes / 8) & 0x7f;
  return (0xa9800000 | (imm7 << 15) | ((rt2 & 31) << 10) | (31 << 5) | (rt & 31)) >>> 0;
}

function ldpPost64(rt: number, rt2: number, immBytes: number): number {
  if (immBytes % 8 !== 0) throw new Error("ldp post-index offset must be 8-byte aligned");
  const imm7 = (immBytes / 8) & 0x7f;
  return (0xa8c00000 | (imm7 << 15) | ((rt2 & 31) << 10) | (31 << 5) | (rt & 31)) >>> 0;
}

function movFpSp(): number {
  return 0x910003fd;
}

function ret(): number {
  return 0xd65f03c0;
}

function bl(offsetInstructions: number): number {
  if (offsetInstructions < -33554432 || offsetInstructions > 33554431) {
    throw new Error("call branch offset out of range");
  }
  return (0x94000000 | (offsetInstructions & 0x03ffffff)) >>> 0;
}

function b(offsetInstructions: number): number {
  if (offsetInstructions < -33554432 || offsetInstructions > 33554431) {
    throw new Error("unconditional branch offset out of range");
  }
  return (0x14000000 | (offsetInstructions & 0x03ffffff)) >>> 0;
}

function bCond(cond: number, offsetInstructions: number): number {
  if (offsetInstructions < -262144 || offsetInstructions > 262143) {
    throw new Error("conditional branch offset out of range");
  }
  return (0x54000000 | ((offsetInstructions & 0x7ffff) << 5) | (cond & 0xf)) >>> 0;
}

function conditionForOperator(kind: ts.SyntaxKind): CompileResult<{ cond: number }> {
  switch (kind) {
    case ts.SyntaxKind.EqualsEqualsEqualsToken:
      return { ok: true, cond: 0 };
    case ts.SyntaxKind.ExclamationEqualsEqualsToken:
      return { ok: true, cond: 1 };
    case ts.SyntaxKind.LessThanToken:
      return { ok: true, cond: 11 };
    case ts.SyntaxKind.LessThanEqualsToken:
      return { ok: true, cond: 13 };
    case ts.SyntaxKind.GreaterThanToken:
      return { ok: true, cond: 12 };
    case ts.SyntaxKind.GreaterThanEqualsToken:
      return { ok: true, cond: 10 };
    default:
      return failCompile(`unsupported comparison operator: ${ts.SyntaxKind[kind]}`);
  }
}

function invertCond(cond: number): number {
  return cond ^ 1;
}

function containsCallExpression(node: ts.Node): boolean {
  let found = false;
  function visit(current: ts.Node): void {
    if (found) return;
    if (ts.isCallExpression(current)) {
      found = true;
      return;
    }
    ts.forEachChild(current, visit);
  }
  visit(node);
  return found;
}

class CsgV2Writer {
  private readonly lines: string[] = ["CHENG_CSG_V2"];

  stringRecord(kind: number, value: string): void {
    this.record(kind, encodeString(value));
  }

  functionRecord(itemId: number, wordOffset: number, wordCount: number, symbol: string, bodyKind: string): void {
    this.record(4, concatBytes([
      encodeU32(itemId),
      encodeU32(wordOffset),
      encodeU32(wordCount),
      encodeString(symbol),
      encodeString(bodyKind),
    ]));
  }

  wordRecord(word: number): void {
    this.record(5, encodeU32(word));
  }

  relocRecord(sourceItemId: number, wordOffset: number, targetSymbol: string): void {
    this.record(6, concatBytes([
      encodeU32(sourceItemId),
      encodeU32(wordOffset),
      encodeString(targetSymbol),
    ]));
  }

  toString(): string {
    return `${this.lines.join("\n")}\n`;
  }

  private record(kind: number, payload: Uint8Array): void {
    if (kind <= 0 || kind > 0xffff) {
      throw new Error("CSG v2 record kind out of range");
    }
    this.lines.push(`R${hex(kind, 4)}${hex(payload.length, 8)}${bytesToHex(payload)}`);
  }
}

function encodeString(value: string): Uint8Array {
  const bytes = new TextEncoder().encode(value);
  return concatBytes([encodeU32(bytes.length), bytes]);
}

function encodeU32(value: number): Uint8Array {
  if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
    throw new Error(`u32 out of range: ${value}`);
  }
  const out = new Uint8Array(4);
  out[0] = value & 0xff;
  out[1] = (value >>> 8) & 0xff;
  out[2] = (value >>> 16) & 0xff;
  out[3] = (value >>> 24) & 0xff;
  return out;
}

function concatBytes(parts: Uint8Array[]): Uint8Array {
  let size = 0;
  for (const part of parts) size += part.length;
  const out = new Uint8Array(size);
  let offset = 0;
  for (const part of parts) {
    out.set(part, offset);
    offset += part.length;
  }
  return out;
}

function bytesToHex(bytes: Uint8Array): string {
  let out = "";
  for (const byte of bytes) {
    out += hex(byte, 2);
  }
  return out;
}

function hex(value: number, width: number): string {
  return value.toString(16).toUpperCase().padStart(width, "0");
}

function buildProgram(options: ExtractOptions): ProgramBuild {
  const cwd = path.resolve(options.rootDir ?? process.cwd());
  const diagnostics: string[] = [];

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
        diagnostics: configDiagnostics.map((diagnostic) => formatDiagnostic(cwd, diagnostic)),
      };
    }

    const createOptions: ts.CreateProgramOptions = {
      rootNames: parsed.fileNames,
      options: parsed.options,
    };
    if (parsed.projectReferences) {
      createOptions.projectReferences = parsed.projectReferences;
    }
    const program = ts.createProgram(createOptions);
    diagnostics.push(...configDiagnostics.map((diagnostic) => formatDiagnostic(cwd, diagnostic)));
    diagnostics.push(...parsed.errors.map((diagnostic) => formatDiagnostic(cwd, diagnostic)));
    diagnostics.push(...ts.getPreEmitDiagnostics(program)
      .filter((diagnostic) => diagnostic.category === ts.DiagnosticCategory.Error)
      .map((diagnostic) => formatDiagnostic(cwd, diagnostic)));
    return { cwd, program, diagnostics };
  }

  if (!options.files || options.files.length === 0) {
    return {
      cwd,
      program: ts.createProgram({ rootNames: [], options: defaultCompilerOptions(cwd) }),
      diagnostics: ["either --project or at least one --file is required"],
    };
  }

  const program = ts.createProgram({
    rootNames: options.files.map((file) => path.resolve(cwd, file)),
    options: defaultCompilerOptions(cwd),
  });
  diagnostics.push(...ts.getPreEmitDiagnostics(program)
    .filter((diagnostic) => diagnostic.category === ts.DiagnosticCategory.Error)
    .map((diagnostic) => formatDiagnostic(cwd, diagnostic)));
  return { cwd, program, diagnostics };
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

function formatDiagnostic(cwd: string, diagnostic: ts.Diagnostic): string {
  const message = ts.flattenDiagnosticMessageText(diagnostic.messageText, "\n");
  if (diagnostic.file && diagnostic.start !== undefined) {
    const point = diagnostic.file.getLineAndCharacterOfPosition(diagnostic.start);
    return `${relPath(cwd, diagnostic.file.fileName)}:${point.line + 1}:${point.character + 1} TS${diagnostic.code}: ${message}`;
  }
  return `TS${diagnostic.code}: ${message}`;
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
  return path.relative(cwd, path.resolve(file)).split(path.sep).join("/") || ".";
}

function objectFormatForTarget(target: string): string {
  if (target === "arm64-apple-darwin") return "macho";
  if (target === "aarch64-unknown-linux-gnu") return "elf";
  return "";
}

function symbolForEntry(target: string, entry: string): string {
  return target === "arm64-apple-darwin" ? `_${entry}` : entry;
}

function hasModifier(node: ts.Node, kind: ts.SyntaxKind): boolean {
  return ts.canHaveModifiers(node) && (ts.getModifiers(node)?.some((modifier) => modifier.kind === kind) ?? false);
}

function failUnsupported(message: string): ChengCsgV2Result {
  return { diagnostics: [], unsupported: [message], text: "", functionCount: 0, wordCount: 0 };
}
