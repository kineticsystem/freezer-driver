import { describe, expect, it } from 'vitest';
import { applyShotMessage, MAX_SHOTS, ShotEvent, ShotSource, type ShotMessage } from '../src/freezer/freezer';

const STEPS = [
  { outputs: 3, hold_us: 1_000 },
  { outputs: 0, hold_us: 2_000 },
];

function message(event: number, change: Partial<ShotMessage> = {}): ShotMessage {
  return {
    event,
    source: ShotSource.NODE,
    shot_id: 1,
    started: { sec: 100, nanosec: 500_000_000 },
    steps: STEPS.map((step) => ({ type: 0, ...step })) as ShotMessage['steps'],
    duration_us: 3_000,
    elapsed_us: 0,
    worst_lateness_us: 0,
    message: '',
    ...change,
  };
}

describe('the shots of the topic', () => {
  it('adds a shot when it starts, and ends it with its last event', () => {
    let shots = applyShotMessage([], message(ShotEvent.STARTED), 7);
    expect(shots).toHaveLength(1);
    expect(shots[0]).toMatchObject({ id: 1, source: 'node', startedMs: 100_500, receivedMs: 7, state: 'running' });
    expect(shots[0].steps).toEqual(STEPS);

    shots = applyShotMessage(shots, message(ShotEvent.ENDED, { elapsed_us: 3_000, worst_lateness_us: 5 }), 9);
    expect(shots).toHaveLength(1);
    expect(shots[0]).toMatchObject({ state: 'ended', elapsedUs: 3_000, worstLatenessUs: 5 });
  });

  it('tells a stopped shot and a failed one', () => {
    const started = applyShotMessage([], message(ShotEvent.STARTED), 0);
    expect(applyShotMessage(started, message(ShotEvent.STOPPED, { elapsed_us: 1_500 }), 0)[0]).toMatchObject({
      state: 'stopped',
      elapsedUs: 1_500,
    });
    expect(applyShotMessage(started, message(ShotEvent.FAILED, { message: 'lost' }), 0)[0]).toMatchObject({
      state: 'failed',
      message: 'lost',
    });
  });

  it('tells the shots of the trigger', () => {
    const shots = applyShotMessage([], message(ShotEvent.STARTED, { source: ShotSource.TRIGGER }), 0);
    expect(shots[0].source).toBe('trigger');
  });

  /** The topic keeps its last messages: a page that opens late gets them again. */
  it('ignores a message it already has', () => {
    let shots = applyShotMessage([], message(ShotEvent.STARTED), 0);
    shots = applyShotMessage(shots, message(ShotEvent.ENDED), 0);
    expect(applyShotMessage(shots, message(ShotEvent.ENDED), 0)).toBe(shots);
    expect(applyShotMessage(shots, message(ShotEvent.STARTED), 0)).toHaveLength(1);
  });

  it('adds an ended shot whose start it missed', () => {
    const shots = applyShotMessage([], message(ShotEvent.ENDED, { elapsed_us: 3_000 }), 0);
    expect(shots).toHaveLength(1);
    expect(shots[0]).toMatchObject({ id: 1, state: 'ended', elapsedUs: 3_000 });
  });

  it('keeps the same id apart after the node restarts', () => {
    let shots = applyShotMessage([], message(ShotEvent.STARTED), 0);
    shots = applyShotMessage(shots, message(ShotEvent.ENDED), 0);
    shots = applyShotMessage(shots, message(ShotEvent.STARTED, { started: { sec: 200, nanosec: 0 } }), 0);
    expect(shots).toHaveLength(2);
    expect(shots[1].state).toBe('running');
  });

  it(`keeps the last ${MAX_SHOTS} shots`, () => {
    let shots = applyShotMessage([], message(ShotEvent.STARTED), 0);
    for (let id = 2; id <= MAX_SHOTS + 5; id++) {
      shots = applyShotMessage(shots, message(ShotEvent.STARTED, { shot_id: id }), 0);
    }
    expect(shots).toHaveLength(MAX_SHOTS);
    expect(shots[shots.length - 1].id).toBe(MAX_SHOTS + 5);
  });
});
