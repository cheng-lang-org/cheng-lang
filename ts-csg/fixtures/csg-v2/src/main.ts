function add(a: number, b: number): number {
  return a + b;
}

function choose(value: number): number {
  if (value > 20) {
    return value - 7;
  }
  return add(value, 3);
}

export function main(): number {
  const base = add(5, 8);
  const doubled = base * 2;
  return choose(doubled);
}
