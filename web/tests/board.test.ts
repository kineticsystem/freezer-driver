import { describe, expect, it } from 'vitest';
import { bitOf, hex, isOn, LINES } from '../src/freezer/board';

describe('the board', () => {
  it('gives each jack its shutter bit, then its focus bit', () => {
    expect(bitOf(1, 'shutter')).toBe(0);
    expect(bitOf(1, 'focus')).toBe(1);
    expect(bitOf(8, 'shutter')).toBe(14);
    expect(bitOf(8, 'focus')).toBe(15);
  });

  it('refuses a jack the board does not have', () => {
    expect(() => bitOf(0, 'focus')).toThrow(RangeError);
    expect(() => bitOf(9, 'focus')).toThrow(RangeError);
  });

  it('lists the 16 lines, focus before shutter', () => {
    expect(LINES).toHaveLength(16);
    expect(LINES[0]).toEqual({ jack: 1, kind: 'focus', bit: 1, label: 'OUT1 focus' });
    expect(LINES[1]).toEqual({ jack: 1, kind: 'shutter', bit: 0, label: 'OUT1 shutter' });
    expect(new Set(LINES.map((line) => line.bit)).size).toBe(16);
  });

  it('reads bits, and writes the outputs in hexadecimal', () => {
    expect(isOn(0xc000, 15)).toBe(true);
    expect(isOn(0xc000, 13)).toBe(false);
    expect(hex(0xc000)).toBe('0xC000');
    expect(hex(3)).toBe('0x0003');
  });
});
