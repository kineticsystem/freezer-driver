// The 16 outputs of the Freezer board. Each jack, OUT1 to OUT8, has two lines,
// all jacks alike: for jack k, bit 2(k-1) closes the tip of the plug, and bit
// 2(k-1)+1 its ring. What a line does depends on what is plugged in, which the
// board does not know: for a camera the ring is the focus and the tip the
// shutter, and a flash or a light closes both. So the lines are named as the
// plug's contacts, never as a device's.

export const JACK_COUNT = 8;

export type LineKind = 'tip' | 'ring';

export interface Line {
  jack: number;
  kind: LineKind;
  bit: number;
  label: string;
}

/** The bit of a line of a jack, 1 to 8. */
export function bitOf(jack: number, kind: LineKind): number {
  if (!Number.isInteger(jack) || jack < 1 || jack > JACK_COUNT) {
    throw new RangeError(`There is no jack ${jack}: the board has OUT1 to OUT${JACK_COUNT}.`);
  }
  return 2 * (jack - 1) + (kind === 'ring' ? 1 : 0);
}

/** The 16 lines, jack by jack, ring before tip: a camera closes its focus, the ring, first. */
export const LINES: Line[] = Array.from({ length: JACK_COUNT }, (_, i) => i + 1).flatMap((jack) =>
  (['ring', 'tip'] as const).map((kind) => ({ jack, kind, bit: bitOf(jack, kind), label: `OUT${jack} ${kind}` })),
);

export function isOn(outputs: number, bit: number): boolean {
  return ((outputs >> bit) & 1) === 1;
}

/** The outputs as four hexadecimal digits, e.g. 0xC000. */
export function hex(outputs: number): string {
  return `0x${outputs.toString(16).toUpperCase().padStart(4, '0')}`;
}
