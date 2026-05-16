export const TSCsgSchema = "ts-csg.semantic" as const;
export const TSCsgVersion = 1 as const;

export interface SourceLoc {
  file: string;
  line: number;
  column: number;
  start: number;
  end: number;
}

export interface CsgFact {
  kind: string;
  id?: string;
  loc?: SourceLoc;
  [key: string]: unknown;
}

export interface UnsupportedFact extends CsgFact {
  kind: "ts.unsupported";
  id: string;
  code: string;
  message: string;
}

export interface ExtractOptions {
  project?: string;
  files?: string[];
  rootDir?: string;
  out?: string;
  pretty?: boolean;
}

export interface ExtractResult {
  facts: CsgFact[];
  unsupported: UnsupportedFact[];
  diagnostics: string[];
}
