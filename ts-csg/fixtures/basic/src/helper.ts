export interface Point {
  readonly x: number;
  readonly y: number;
}

export type Score =
  | { readonly ok: true; readonly value: number }
  | { readonly ok: false; readonly message: string };

export enum Mode {
  Add = 1,
  Scale = 2,
}

export function scale(point: Point, factor: number): Point {
  return {
    x: point.x * factor,
    y: point.y * factor,
  };
}

export function score(value: number): Score {
  if (value >= 0) {
    return { ok: true, value };
  }
  return { ok: false, message: "negative" };
}

export class Accumulator {
  private total: number = 0;

  add(value: number): number {
    this.total += value;
    return this.total;
  }

  get value(): number {
    return this.total;
  }
}
