import { Accumulator, Mode, type Point, scale, score } from "./helper.js";

export function run(input: number): number {
  const origin: Point = { x: 1, y: 2 };
  const scaled = scale(origin, input);
  const { x: scaledX, y: scaledY } = scaled;
  const [first, second] = [scaledX, scaledY];
  const acc = new Accumulator();
  const current = acc.add(first);
  const checked = score(current);

  if (checked.ok && Mode.Add === 1) {
    return checked.value + acc.value;
  }
  return second;
}

export const answer: number = run(4);
