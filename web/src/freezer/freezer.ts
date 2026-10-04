// The Freezer node, as the page sees it through rosbridge: the shots, from the
// topic shots, and the commands, the Shoot action and the services
// set_outputs, stop and fake/press_trigger.

import { Rosbridge, type ActionResult, type Status } from '../ros/rosbridge';
import type { Step } from './timeline';

export const NODE = '/freezer';

/** builtin_interfaces/Time. */
export interface Time {
  sec: number;
  nanosec: number;
}

export const ShotEvent = { STARTED: 0, ENDED: 1, STOPPED: 2, FAILED: 3 } as const;
export const ShotSource = { NODE: 0, TRIGGER: 1 } as const;

/** freezer_msgs/Shot. */
export interface ShotMessage {
  event: number;
  source: number;
  shot_id: number;
  started: Time;
  steps: Step[];
  duration_us: number;
  elapsed_us: number;
  worst_lateness_us: number;
  message: string;
}

export type ShotState = 'running' | 'ended' | 'stopped' | 'failed';

/** A shot, from its STARTED message and, once it is over, its last one. */
export interface Shot {
  id: number;
  source: 'node' | 'trigger';
  /** When the controller started it, in ms since the epoch, host time. */
  startedMs: number;
  /** When the page learned it had started, in ms since the epoch, browser time. */
  receivedMs: number;
  steps: Step[];
  durationUs: number;
  state: ShotState;
  /** How long it ran, once it is over. */
  elapsedUs?: number;
  worstLatenessUs?: number;
  message?: string;
}

/** The result of the Shoot action. */
export interface ShootResult {
  message: string;
  shot_id: number;
  duration_us: number;
  worst_lateness_us: number;
}

export interface FreezerState {
  status: Status;
  /** The shots received, oldest first. */
  shots: Shot[];
}

/** How many shots the page keeps. */
export const MAX_SHOTS = 50;

const STATES: Record<number, ShotState> = {
  [ShotEvent.ENDED]: 'ended',
  [ShotEvent.STOPPED]: 'stopped',
  [ShotEvent.FAILED]: 'failed',
};

export function timeToMs(time: Time): number {
  return time.sec * 1000 + time.nanosec / 1e6;
}

/**
 * The shots after a message of the topic shots: a STARTED adds a shot, any
 * other event ends the one with its id. A shot whose STARTED the page missed,
 * e.g. before it opened, is added as it ended.
 */
export function applyShotMessage(shots: Shot[], message: ShotMessage, receivedMs: number): Shot[] {
  const index = shots.findIndex((shot) => shot.id === message.shot_id && shot.state === 'running');
  if (message.event === ShotEvent.STARTED) {
    const shot: Shot = {
      id: message.shot_id,
      source: message.source === ShotSource.TRIGGER ? 'trigger' : 'node',
      startedMs: timeToMs(message.started),
      receivedMs,
      steps: message.steps.map(({ outputs, hold_us }) => ({ outputs, hold_us })),
      durationUs: message.duration_us,
      state: 'running',
    };
    return [...shots.filter((s) => !(s.id === shot.id && s.startedMs === shot.startedMs)), shot].slice(-MAX_SHOTS);
  }
  const end = {
    state: STATES[message.event] ?? 'failed',
    elapsedUs: message.elapsed_us,
    worstLatenessUs: message.worst_lateness_us,
    message: message.message,
  } satisfies Partial<Shot>;
  if (index >= 0) {
    return shots.map((shot, i) => (i === index ? { ...shot, ...end } : shot));
  }
  if (shots.some((shot) => shot.id === message.shot_id && shot.startedMs === timeToMs(message.started))) {
    return shots;
  }
  const missed = applyShotMessage(shots, { ...message, event: ShotEvent.STARTED }, receivedMs);
  return missed.map((shot, i) => (i === missed.length - 1 ? { ...shot, ...end } : shot));
}

/** The Freezer node behind a rosbridge, as a store React subscribes to. */
export class Freezer {
  private state: FreezerState;
  private readonly listeners = new Set<() => void>();
  private readonly stops: (() => void)[] = [];

  constructor(readonly ros: Rosbridge) {
    this.state = { status: ros.getStatus(), shots: [] };
    this.stops.push(ros.onStatus((status) => this.update({ status })));
    this.stops.push(
      ros.subscribe<ShotMessage>(`${NODE}/shots`, 'freezer_msgs/msg/Shot', (message) =>
        this.update({ shots: applyShotMessage(this.state.shots, message, Date.now()) }),
      ),
    );
  }

  getState = (): FreezerState => this.state;

  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };

  /** Fire a sequence of the node's parameters, the default one when empty, or a raw table. */
  shoot(goal: { sequence?: string; steps?: Step[] }): Promise<ActionResult<ShootResult>> {
    const steps = (goal.steps ?? []).map((step) => ({ type: 0, ...step }));
    return this.ros.sendGoal(`${NODE}/shoot`, 'freezer_msgs/action/Shoot', { sequence: goal.sequence ?? '', steps });
  }

  setOutputs(outputs: number): Promise<{ success: boolean; message: string }> {
    return this.ros.callService(`${NODE}/set_outputs`, 'freezer_msgs/srv/SetOutputs', { outputs });
  }

  stop(): Promise<{ success: boolean; message: string }> {
    return this.ros.callService(`${NODE}/stop`, 'std_srvs/srv/Trigger', {});
  }

  /** Press IN1 of a fake controller. The service exists only with use_fake. */
  pressTrigger(): Promise<{ success: boolean; message: string }> {
    return this.ros.callService(`${NODE}/fake/press_trigger`, 'std_srvs/srv/Trigger', {});
  }

  /** The names of the sequences of the node's parameters, the default one first. */
  async sequenceNames(): Promise<string[]> {
    const response = await this.ros.callService<
      { names: string[] },
      { values: { string_value?: string; string_array_value?: string[] }[] }
    >(`${NODE}/get_parameters`, 'rcl_interfaces/srv/GetParameters', { names: ['default_sequence', 'sequence_names'] });
    const [defaultSequence, names] = response.values;
    const all = names?.string_array_value ?? [];
    const first = defaultSequence?.string_value;
    return first && all.includes(first) ? [first, ...all.filter((name) => name !== first)] : all;
  }

  close(): void {
    for (const stop of this.stops) stop();
  }

  private update(change: Partial<FreezerState>): void {
    this.state = { ...this.state, ...change };
    for (const listener of this.listeners) listener();
  }
}
