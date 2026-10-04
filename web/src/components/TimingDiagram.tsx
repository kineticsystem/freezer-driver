// The lines of the board and the timing of the latest shot, as a logic
// analyser draws it: a row per line, with its LED, lit while the line is
// closed now, and its trace, high while the shot closed it. During a shot the
// LEDs follow its table at the cursor; otherwise, the outputs the node tells. The times are
// those of the shot's table, which the controller runs exactly, from the start
// of the shot; a running shot has a cursor at the time it has reached.

import { useEffect, useRef, useState } from 'react';
import { hex, isOn, LINES } from '../freezer/board';
import { runningUntil, type Shot } from '../freezer/freezer';
import { boundaries, formatDuration, spansOf, stepAt, ticks, usedBits } from '../freezer/timeline';

const LED_X = 9;
const LED_RADIUS = 6;
const LABEL_X = 24;
const LABEL_WIDTH = 124;
const RIGHT_MARGIN = 36;
// The room a tick label needs, so that the labels never overlap.
const TICK_SPACING = 90;
const AXIS_HEIGHT = 28;
const ROW_HEIGHT = 26;
const TRACE_HEIGHT = 14;

/** The width of an element, followed as it resizes. */
function useWidth<T extends HTMLElement>(): [React.RefObject<T | null>, number] {
  const ref = useRef<T>(null);
  const [width, setWidth] = useState(640);
  useEffect(() => {
    const element = ref.current;
    if (!element) return;
    const observer = new ResizeObserver(([entry]) => setWidth(entry.contentRect.width));
    observer.observe(element);
    return () => observer.disconnect();
  }, []);
  return [ref, width];
}

/**
 * How far a running shot has got, in µs: the time since the page learned it
 * started. The page's clock is used alone, so that a host on another clock
 * does not move the cursor.
 */
function useElapsed(shot?: Shot): { elapsed: number; running: boolean } {
  const [now, setNow] = useState(() => Date.now());
  const running = !!shot && shot.state === 'running' && now < runningUntil(shot);
  useEffect(() => {
    if (!running) return;
    let frame = requestAnimationFrame(function tick() {
      setNow(Date.now());
      frame = requestAnimationFrame(tick);
    });
    return () => cancelAnimationFrame(frame);
  }, [running]);
  if (!shot) return { elapsed: 0, running };
  if (!running) return { elapsed: shot.elapsedUs ?? shot.durationUs, running };
  return { elapsed: Math.min(shot.durationUs, Math.max(0, (now - shot.receivedMs) * 1000)), running };
}

interface Props {
  /** The latest shot, if any. */
  shot?: Shot;
  /** The pattern on the outputs now, as the node last told it. */
  outputs?: number;
  /** Told what is under the mouse, the time and the step, or undefined when nothing is. */
  onHover?(text: string | undefined): void;
}

export function TimingDiagram({ shot, outputs, onHover = () => {} }: Props) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const [hover, setHover] = useState<number>();
  const { elapsed, running } = useElapsed(shot);

  const steps = shot?.steps ?? [];
  const timed = steps.length > 0;
  // While the shot runs, the LEDs show its table at the cursor, so that they
  // always agree with the diagram: the node learns each step only when it
  // polls the controller, at another moment than the page's cursor reaches it.
  const runningStep = timed && running ? stepAt(steps, elapsed) : -1;
  const lights = runningStep >= 0 ? steps[runningStep].outputs : outputs;
  const used = new Set(usedBits(steps));
  // The lines the shot uses, and any line on now, so that no lit LED is hidden;
  // all of them before the first shot.
  const lines = LINES.filter(
    (line) => !timed || used.has(line.bit) || (lights !== undefined && isOn(lights, line.bit)),
  );
  const duration = Math.max(shot?.durationUs ?? 0, 1);
  const plotWidth = Math.max(width - LABEL_WIDTH - RIGHT_MARGIN, 50);
  const x = (time: number) => LABEL_WIDTH + (time / duration) * plotWidth;
  const top = timed ? AXIS_HEIGHT : 4;
  const height = top + lines.length * ROW_HEIGHT + 8;
  const times = boundaries(steps);
  // A stopped shot is drawn up to its stop; a running one up to the cursor.
  const until = running || shot?.state === 'stopped' ? elapsed : Infinity;
  const hoverStep = hover === undefined ? -1 : stepAt(steps, hover);
  const hoverText =
    hoverStep >= 0
      ? `${formatDuration(hover!)} · step ${hoverStep}: ${hex(steps[hoverStep].outputs)} for ${formatDuration(
          steps[hoverStep].hold_us,
        )}`
      : undefined;
  useEffect(() => onHover(hoverText), [hoverText, onHover]);

  const onMove = (event: React.MouseEvent<SVGSVGElement>) => {
    if (!timed) return;
    const box = event.currentTarget.getBoundingClientRect();
    const time = ((event.clientX - box.left - LABEL_WIDTH) / plotWidth) * duration;
    setHover(time >= 0 && time <= duration ? time : undefined);
  };

  return (
    <div className="timing" ref={ref}>
      {!timed && (
        <p className="muted hint">
          {shot
            ? 'The node does not know the steps of this shot.'
            : 'No shot yet. Send a sequence, or press IN1, and its timing shows here.'}
        </p>
      )}
      <svg
        width={width}
        height={height}
        role="img"
        aria-label={shot ? `The lines, and the timing of shot ${shot.id}` : 'The lines of the board'}
        onMouseMove={onMove}
        onMouseLeave={() => setHover(undefined)}
      >
        {timed && (
          <>
            {/* The step boundaries, and the time axis. */}
            {times.map((time, i) => (
              <line key={`b${i}`} className="grid" x1={x(time)} x2={x(time)} y1={top - 4} y2={height - 8} />
            ))}
            {ticks(duration, Math.max(2, Math.floor(plotWidth / TICK_SPACING))).map((tick) => (
              <text key={`t${tick}`} className="axis" x={x(tick)} y={top - 10} textAnchor="middle">
                {formatDuration(tick)}
              </text>
            ))}
          </>
        )}
        {lines.map((line, row) => {
          const rowTop = top + row * ROW_HEIGHT;
          const middle = rowTop + ROW_HEIGHT / 2;
          const low = middle + TRACE_HEIGHT / 2;
          const high = low - TRACE_HEIGHT;
          const lit = lights !== undefined && isOn(lights, line.bit);
          const spans = spansOf(steps, line.bit, until);
          // A digital trace: low, up at each span's start, down at its end.
          let path = `M ${x(0)} ${low}`;
          for (const span of spans) {
            path += ` L ${x(span.start)} ${low} L ${x(span.start)} ${high} L ${x(span.end)} ${high} L ${x(span.end)} ${low}`;
          }
          path += ` L ${x(Math.min(until, duration))} ${low}`;
          return (
            <g key={line.bit}>
              <circle className={`led ${lit ? 'on' : ''}`} cx={LED_X} cy={middle} r={LED_RADIUS}>
                <title>{`${line.label}: ${lit ? 'closed' : 'open'} now`}</title>
              </circle>
              <text className="label" x={LABEL_X} y={middle + 4}>
                {line.label}
              </text>
              {timed && (
                <>
                  {spans.map((span) => (
                    <rect
                      key={span.start}
                      className="high"
                      x={x(span.start)}
                      y={high}
                      width={Math.max(x(span.end) - x(span.start), 1)}
                      height={TRACE_HEIGHT}
                    />
                  ))}
                  <path className="trace" d={path} />
                </>
              )}
            </g>
          );
        })}
        {timed && running && (
          <line className="cursor" x1={x(elapsed)} x2={x(elapsed)} y1={top - 4} y2={height - 8} />
        )}
        {timed && shot?.state === 'stopped' && (
          <line className="stop" x1={x(elapsed)} x2={x(elapsed)} y1={top - 4} y2={height - 8} />
        )}
        {hover !== undefined && <line className="hover" x1={x(hover)} x2={x(hover)} y1={top - 4} y2={height - 8} />}
      </svg>
    </div>
  );
}
