import { describe, expect, it } from 'vitest';
import { boundaries, formatDuration, spansOf, stepAt, ticks, usedBits } from '../src/freezer/timeline';

// A camera on OUT1: the ring 100 ms, ring and tip 100 ms, release 200 ms.
const SHOT = [
  { outputs: 0x0002, hold_us: 100_000 },
  { outputs: 0x0003, hold_us: 100_000 },
  { outputs: 0x0000, hold_us: 200_000 },
];

describe('the timeline of a shot', () => {
  it('starts each step where the previous ends', () => {
    expect(boundaries(SHOT)).toEqual([0, 100_000, 200_000, 400_000]);
  });

  it('merges the steps that keep a line on', () => {
    expect(spansOf(SHOT, 1)).toEqual([{ start: 0, end: 200_000 }]);
    expect(spansOf(SHOT, 0)).toEqual([{ start: 100_000, end: 200_000 }]);
    expect(spansOf(SHOT, 15)).toEqual([]);
  });

  it('cuts a stopped shot where it stopped', () => {
    expect(spansOf(SHOT, 1, 150_000)).toEqual([{ start: 0, end: 150_000 }]);
    expect(spansOf(SHOT, 0, 150_000)).toEqual([{ start: 100_000, end: 150_000 }]);
    expect(spansOf(SHOT, 0, 50_000)).toEqual([]);
  });

  it('keeps separate the spans of a line that goes off and on again', () => {
    const blink = [
      { outputs: 0x8000, hold_us: 10 },
      { outputs: 0x0000, hold_us: 10 },
      { outputs: 0x8000, hold_us: 10 },
      { outputs: 0x0000, hold_us: 10 },
    ];
    expect(spansOf(blink, 15)).toEqual([
      { start: 0, end: 10 },
      { start: 20, end: 30 },
    ]);
  });

  it('lists the lines the shot uses, ring first', () => {
    expect(usedBits(SHOT)).toEqual([1, 0]);
  });

  it('finds the step running at a time', () => {
    expect(stepAt(SHOT, 0)).toBe(0);
    expect(stepAt(SHOT, 150_000)).toBe(1);
    expect(stepAt(SHOT, 399_999)).toBe(2);
    expect(stepAt(SHOT, 400_000)).toBe(-1);
  });

  it('puts round ticks on the axis', () => {
    expect(ticks(400_000)).toEqual([0, 100_000, 200_000, 300_000, 400_000]);
    expect(ticks(1_000, 5)).toEqual([0, 200, 400, 600, 800, 1_000]);
    expect(ticks(0)).toEqual([0]);
  });

  it('writes a duration in the unit that reads best', () => {
    expect(formatDuration(40)).toBe('40 µs');
    expect(formatDuration(2_500)).toBe('2.5 ms');
    expect(formatDuration(100_000)).toBe('100 ms');
    expect(formatDuration(1_200_000)).toBe('1.2 s');
  });
});
