import { describe, expect, it } from 'vitest';
import { bitOf, hex, isOn, LINES } from '../src/freezer/board';

describe('the board', () => {
  it('gives each jack its tip bit, then its ring bit', () => {
    expect(bitOf(1, 'tip')).toBe(0);
    expect(bitOf(1, 'ring')).toBe(1);
    expect(bitOf(8, 'tip')).toBe(14);
    expect(bitOf(8, 'ring')).toBe(15);
  });

  it('refuses a jack the board does not have', () => {
    expect(() => bitOf(0, 'ring')).toThrow(RangeError);
    expect(() => bitOf(9, 'ring')).toThrow(RangeError);
  });

  it('lists the 16 lines, ring before tip, named after the contacts of the plug', () => {
    expect(LINES).toHaveLength(16);
    expect(LINES[0]).toEqual({ jack: 1, kind: 'ring', bit: 1, label: 'OUT1 ring' });
    expect(LINES[1]).toEqual({ jack: 1, kind: 'tip', bit: 0, label: 'OUT1 tip' });
    expect(LINES[15]).toEqual({ jack: 8, kind: 'tip', bit: 14, label: 'OUT8 tip' });
    expect(new Set(LINES.map((line) => line.bit)).size).toBe(16);
  });

  it('reads bits, and writes the outputs in hexadecimal', () => {
    expect(isOn(0xc000, 15)).toBe(true);
    expect(isOn(0xc000, 13)).toBe(false);
    expect(hex(0xc000)).toBe('0xC000');
    expect(hex(3)).toBe('0x0003');
  });
});
