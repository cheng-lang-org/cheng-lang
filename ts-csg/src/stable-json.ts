function normalize(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(normalize);
  if (!value || typeof value !== "object") return value;

  const input = value as Record<string, unknown>;
  const output: Record<string, unknown> = {};
  for (const key of Object.keys(input).sort()) {
    const item = input[key];
    if (item !== undefined) output[key] = normalize(item);
  }
  return output;
}

export function stableJson(value: unknown, pretty = false): string {
  return JSON.stringify(normalize(value), null, pretty ? 2 : 0);
}
