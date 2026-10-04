// The timing of one shot, as a logic analyser draws it: a row per line, high
// while the line is closed. The times are those of the shot's table, which
// the controller runs exactly, from the start of the shot; a running shot has
// a cursor at the time it has reached.

import { useEffect, useRef, useState } from 'react';
import { hex, LINES } from '../freezer/board';
import type { Shot } from '../freezer/freezer';
import { boundaries, formatDuration, spansOf, stepAt, ticks, usedBits } from '../freezer/timeline';

const LABEL_WIDTH = 104;
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
function useElapsed(shot: Shot): number {
  const running = shot.state === 'running';
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!running) return;
    let frame = requestAnimationFrame(function tick() {
      setNow(Date.now());
      frame = requestAnimationFrame(tick);
    });
    return () => cancelAnimationFrame(frame);
  }, [running]);
  if (!running) return shot.elapsedUs ?? shot.durationUs;
  return Math.min(shot.durationUs, Math.max(0, (now - shot.receivedMs) * 1000));
}

export function TimingDiagram({ shot }: { shot: Shot }) {
  const [ref, width] = useWidth<HTMLDivElement>();
  const [allLines, setAllLines] = useState(false);
  const [hover, setHover] = useState<number>();
  const elapsed = useElapsed(shot);

  const used = new Set(usedBits(shot.steps));
  const lines = LINES.filter((line) => allLines || used.has(line.bit));
  const duration = Math.max(shot.durationUs, 1);
  const plotWidth = Math.max(width - LABEL_WIDTH - RIGHT_MARGIN, 50);
  const x = (time: number) => LABEL_WIDTH + (time / duration) * plotWidth;
  const height = AXIS_HEIGHT + lines.length * ROW_HEIGHT + 8;
  const times = boundaries(shot.steps);
  // A stopped shot is drawn up to its stop; a running one up to the cursor.
  const until = shot.state === 'running' || shot.state === 'stopped' ? elapsed : Infinity;
  const hoverStep = hover === undefined ? -1 : stepAt(shot.steps, hover);

  const onMove = (event: React.MouseEvent<SVGSVGElement>) => {
    const box = event.currentTarget.getBoundingClientRect();
    const time = ((event.clientX - box.left - LABEL_WIDTH) / plotWidth) * duration;
    setHover(time >= 0 && time <= duration ? time : undefined);
  };

  return (
    <div className="timing" ref={ref}>
      <div className="timing-toolbar">
        <label className="check">
          <input type="checkbox" checked={allLines} onChange={(e) => setAllLines(e.target.checked)} />
          All 16 lines
        </label>
        <span className="muted">
          {hoverStep >= 0
            ? `${formatDuration(hover!)} · step ${hoverStep}: ${hex(shot.steps[hoverStep].outputs)} for ${formatDuration(
                shot.steps[hoverStep].hold_us,
              )}`
            : `${shot.steps.length} steps · ${formatDuration(shot.durationUs)}`}
        </span>
      </div>
      {shot.steps.length === 0 ? (
        <p className="muted">The node does not know the steps of this shot.</p>
      ) : (
        <svg
          width={width}
          height={height}
          role="img"
          aria-label={`Timing of shot ${shot.id}`}
          onMouseMove={onMove}
          onMouseLeave={() => setHover(undefined)}
        >
          {/* The step boundaries, and the time axis. */}
          {times.map((time, i) => (
            <line key={`b${i}`} className="grid" x1={x(time)} x2={x(time)} y1={AXIS_HEIGHT - 4} y2={height - 8} />
          ))}
          {ticks(duration, Math.max(2, Math.floor(plotWidth / TICK_SPACING))).map((tick) => (
            <text key={`t${tick}`} className="axis" x={x(tick)} y={AXIS_HEIGHT - 10} textAnchor="middle">
              {formatDuration(tick)}
            </text>
          ))}
          {lines.map((line, row) => {
            const top = AXIS_HEIGHT + row * ROW_HEIGHT;
            const low = top + ROW_HEIGHT / 2 + TRACE_HEIGHT / 2;
            const high = low - TRACE_HEIGHT;
            const spans = spansOf(shot.steps, line.bit, until);
            // A digital trace: low, up at each span's start, down at its end.
            let path = `M ${x(0)} ${low}`;
            for (const span of spans) {
              path += ` L ${x(span.start)} ${low} L ${x(span.start)} ${high} L ${x(span.end)} ${high} L ${x(span.end)} ${low}`;
            }
            path += ` L ${x(Math.min(until, duration))} ${low}`;
            return (
              <g key={line.bit} className={used.has(line.bit) ? '' : 'unused'}>
                <text className="label" x={0} y={top + ROW_HEIGHT / 2 + 4}>
                  {line.label}
                </text>
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
              </g>
            );
          })}
          {shot.state === 'running' && (
            <line className="cursor" x1={x(elapsed)} x2={x(elapsed)} y1={AXIS_HEIGHT - 4} y2={height - 8} />
          )}
          {shot.state === 'stopped' && (
            <line className="stop" x1={x(elapsed)} x2={x(elapsed)} y1={AXIS_HEIGHT - 4} y2={height - 8} />
          )}
          {hover !== undefined && (
            <line className="hover" x1={x(hover)} x2={x(hover)} y1={AXIS_HEIGHT - 4} y2={height - 8} />
          )}
        </svg>
      )}
    </div>
  );
}
