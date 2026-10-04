// The 16 outputs of the Freezer board. Each jack, OUT1 to OUT8, has two lines:
// for jack k, bit 2(k-1) closes the shutter line, the tip of the plug, and bit
// 2(k-1)+1 the focus line, its ring. A flash or a light closes both.

export const JACK_COUNT = 8;

export type LineKind = 'shutter' | 'focus';

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
  return 2 * (jack - 1) + (kind === 'focus' ? 1 : 0);
}

/** The 16 lines, jack by jack, focus before shutter: the order a camera closes them. */
export const LINES: Line[] = Array.from({ length: JACK_COUNT }, (_, i) => i + 1).flatMap((jack) =>
  (['focus', 'shutter'] as const).map((kind) => ({ jack, kind, bit: bitOf(jack, kind), label: `OUT${jack} ${kind}` })),
);

export function isOn(outputs: number, bit: number): boolean {
  return ((outputs >> bit) & 1) === 1;
}

/** The outputs as four hexadecimal digits, e.g. 0xC000. */
export function hex(outputs: number): string {
  return `0x${outputs.toString(16).toUpperCase().padStart(4, '0')}`;
}
