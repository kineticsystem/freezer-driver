import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { Rosbridge } from '../src/ros/rosbridge';

/** A WebSocket the test drives: it records what the page sends, and delivers what the test says. */
class FakeSocket {
  static readonly OPEN = 1;
  readyState = 0;
  sent: Record<string, unknown>[] = [];
  onopen?: () => void;
  onclose?: () => void;
  onmessage?: (event: { data: string }) => void;

  send(data: string) {
    this.sent.push(JSON.parse(data));
  }

  close() {
    this.readyState = 3;
    this.onclose?.();
  }

  open() {
    this.readyState = FakeSocket.OPEN;
    this.onopen?.();
  }

  receive(message: Record<string, unknown>) {
    this.onmessage?.({ data: JSON.stringify(message) });
  }

  last() {
    return this.sent[this.sent.length - 1];
  }
}

describe('Rosbridge', () => {
  let sockets: FakeSocket[];
  let ros: Rosbridge;

  beforeEach(() => {
    vi.useFakeTimers();
    vi.stubGlobal('WebSocket', FakeSocket);
    sockets = [];
    ros = new Rosbridge('ws://test:9092', () => {
      const socket = new FakeSocket();
      sockets.push(socket);
      return socket as unknown as WebSocket;
    });
  });

  afterEach(() => {
    ros.close();
    vi.unstubAllGlobals();
    vi.useRealTimers();
  });

  it('reports its status', () => {
    const statuses: string[] = [];
    ros.onStatus((status) => statuses.push(status));
    sockets[0].open();
    expect(ros.getStatus()).toBe('connected');
    expect(statuses).toEqual(['connected']);
  });

  it('subscribes, and delivers the messages of the topic', () => {
    const received: unknown[] = [];
    ros.subscribe('/freezer/outputs', 'freezer_msgs/msg/Outputs', (message) => received.push(message));
    sockets[0].open();
    expect(sockets[0].last()).toMatchObject({ op: 'subscribe', topic: '/freezer/outputs', type: 'freezer_msgs/msg/Outputs' });

    sockets[0].receive({ op: 'publish', topic: '/freezer/outputs', msg: { outputs: 3 } });
    sockets[0].receive({ op: 'publish', topic: '/other', msg: { outputs: 4 } });
    expect(received).toEqual([{ outputs: 3 }]);
  });

  it('subscribes again after reconnecting', () => {
    ros.subscribe('/freezer/shots', 'freezer_msgs/msg/Shot', () => {});
    sockets[0].open();
    sockets[0].close();
    expect(ros.getStatus()).toBe('disconnected');

    vi.advanceTimersByTime(2_000);
    expect(sockets).toHaveLength(2);
    sockets[1].open();
    expect(sockets[1].sent).toEqual([expect.objectContaining({ op: 'subscribe', topic: '/freezer/shots' })]);
  });

  it('calls a service and resolves with its response', async () => {
    sockets[0].open();
    const response = ros.callService('/freezer/stop', 'std_srvs/srv/Trigger', {});
    const request = sockets[0].last();
    expect(request).toMatchObject({ op: 'call_service', service: '/freezer/stop', type: 'std_srvs/srv/Trigger' });

    sockets[0].receive({ op: 'service_response', id: request.id, result: true, values: { success: true, message: '' } });
    await expect(response).resolves.toEqual({ success: true, message: '' });
  });

  it('fails a service call that rosbridge could not make', async () => {
    sockets[0].open();
    const response = ros.callService('/freezer/fake/press_trigger', 'std_srvs/srv/Trigger', {});
    sockets[0].receive({ op: 'service_response', id: sockets[0].last().id, result: false, values: 'no such service' });
    await expect(response).rejects.toThrow('no such service');
  });

  it('fails a call at once when not connected', async () => {
    await expect(ros.callService('/freezer/stop', 'std_srvs/srv/Trigger', {})).rejects.toThrow('Not connected');
  });

  it('fails the calls waiting when the connection closes', async () => {
    sockets[0].open();
    const response = ros.callService('/freezer/stop', 'std_srvs/srv/Trigger', {});
    sockets[0].close();
    await expect(response).rejects.toThrow('closed');
  });

  it('sends a goal, delivers its feedback and resolves with its result', async () => {
    sockets[0].open();
    const feedback: unknown[] = [];
    const result = ros.sendGoal('/freezer/shoot', 'freezer_msgs/action/Shoot', { sequence: '' }, (f) => feedback.push(f));
    const goal = sockets[0].last();
    expect(goal).toMatchObject({
      op: 'send_action_goal',
      action: '/freezer/shoot',
      action_type: 'freezer_msgs/action/Shoot',
      args: { sequence: '' },
      feedback: true,
    });

    sockets[0].receive({ op: 'action_feedback', id: goal.id, values: { state: 1 } });
    sockets[0].receive({ op: 'action_result', id: goal.id, status: 6, result: true, values: { message: 'Stopped.' } });
    expect(feedback).toEqual([{ state: 1 }]);
    await expect(result).resolves.toEqual({ outcome: 'aborted', values: { message: 'Stopped.' } });
  });

  it('reports a goal rosbridge refused', async () => {
    sockets[0].open();
    const result = ros.sendGoal('/freezer/shoot', 'freezer_msgs/action/Nope', {});
    sockets[0].receive({ op: 'status', id: sockets[0].last().id, level: 'error', msg: 'unknown action type' });
    await expect(result).resolves.toEqual({ outcome: 'failed', error: 'unknown action type' });
  });
});
