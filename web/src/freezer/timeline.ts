// The timing of a shot, from its table: when each line is on. The controller
// runs the table exactly, so the times are those of the table, in µs from the
// start of the shot, not the times messages reach the page.

import { isOn, LINES } from './board';

export interface Step {
  outputs: number;
  hold_us: number;
}

/** A time span, in µs from the start of the shot. */
export interface Span {
  start: number;
  end: number;
}

/** The start of each step, and the end of the last one. */
export function boundaries(steps: Step[]): number[] {
  const times = [0];
  for (const step of steps) times.push(times[times.length - 1] + step.hold_us);
  return times;
}

/**
 * When a line is on during the shot, as spans, merged across the steps that
 * keep it on. A shot stopped after `until` µs is cut there: nothing that
 * would have come after it happened.
 */
export function spansOf(steps: Step[], bit: number, until = Infinity): Span[] {
  const times = boundaries(steps);
  const spans: Span[] = [];
  steps.forEach((step, i) => {
    const start = times[i];
    const end = Math.min(times[i + 1], until);
    if (start >= until || end <= start || !isOn(step.outputs, bit)) return;
    const last = spans[spans.length - 1];
    if (last && last.end === start) last.end = end;
    else spans.push({ start, end });
  });
  return spans;
}

/** The bits a shot uses, in the order of LINES. */
export function usedBits(steps: Step[]): number[] {
  const all = steps.reduce((mask, step) => mask | step.outputs, 0);
  return LINES.map((line) => line.bit).filter((bit) => isOn(all, bit));
}

/** The step running at a time, or -1 outside the shot. */
export function stepAt(steps: Step[], time: number): number {
  const times = boundaries(steps);
  for (let i = 0; i < steps.length; i++) {
    if (time >= times[i] && time < times[i + 1]) return i;
  }
  return -1;
}

/**
 * Round tick values for an axis from 0 to `duration` µs: about `count` of
 * them, at 1, 2 or 5 times a power of ten.
 */
export function ticks(duration: number, count = 6): number[] {
  if (!(duration > 0)) return [0];
  const raw = duration / count;
  const power = 10 ** Math.floor(Math.log10(raw));
  const step = [1, 2, 5, 10].map((m) => m * power).find((s) => s >= raw) ?? 10 * power;
  const out: number[] = [];
  for (let t = 0; t <= duration + step * 1e-9; t += step) out.push(Math.round(t * 1e6) / 1e6);
  return out;
}

/** A duration in µs, in the unit that reads best: 40 µs, 2.5 ms, 1.2 s. */
export function formatDuration(us: number): string {
  const trim = (value: number) => String(Number(value.toPrecision(4)));
  if (Math.abs(us) < 1000) return `${trim(us)} µs`;
  if (Math.abs(us) < 1_000_000) return `${trim(us / 1000)} ms`;
  return `${trim(us / 1_000_000)} s`;
}
