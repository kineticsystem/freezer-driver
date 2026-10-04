// A connection to rosbridge: a WebSocket that speaks JSON, so that the browser
// needs no ROS. It reconnects by itself, and subscribes again to every topic
// after a reconnection.
//
//   → subscribe          a topic and its type
//   ← publish            each message of the topic
//   → call_service       a service, its type and the request
//   ← service_response   the response, or result: false and the error
//   → send_action_goal   an action, its type and the goal
//   ← action_feedback    the feedback of the goal
//   ← action_result      the result, with the status of the goal

export type Status = 'connecting' | 'connected' | 'disconnected';

/** The status of a goal, from action_msgs/GoalStatus. */
const GOAL_STATUS: Record<number, ActionOutcome> = { 4: 'succeeded', 5: 'canceled', 6: 'aborted' };

export type ActionOutcome = 'succeeded' | 'canceled' | 'aborted' | 'failed';

export interface ActionResult<R> {
  outcome: ActionOutcome;
  /** The result of the goal; undefined when rosbridge could not send it. */
  values?: R;
  /** Why rosbridge failed, when outcome is failed. */
  error?: string;
}

/** The constructor of the WebSocket, replaced by a fake in tests. */
export type SocketFactory = (url: string) => WebSocket;

interface Subscription {
  topic: string;
  type: string;
  listener(message: unknown): void;
}

interface Pending {
  resolve(message: Record<string, unknown>): void;
  reject(error: Error): void;
}

const RECONNECT_MS = 2000;

let nextId = 0;

export class Rosbridge {
  private socket?: WebSocket;
  private status: Status = 'connecting';
  private readonly statusListeners = new Set<(status: Status) => void>();
  private readonly subscriptions = new Map<string, Subscription>();
  private readonly pending = new Map<string, Pending>();
  private readonly goals = new Map<string, (message: Record<string, unknown>) => void>();
  private reconnect?: ReturnType<typeof setTimeout>;
  private closed = false;

  constructor(
    readonly url: string,
    private readonly factory: SocketFactory = (u) => new WebSocket(u),
  ) {
    this.open();
  }

  getStatus(): Status {
    return this.status;
  }

  /** Calls the listener at every change of status. Returns a function that stops it. */
  onStatus(listener: (status: Status) => void): () => void {
    this.statusListeners.add(listener);
    return () => this.statusListeners.delete(listener);
  }

  /** Receive the messages of a topic. Returns a function that unsubscribes. */
  subscribe<T>(topic: string, type: string, listener: (message: T) => void): () => void {
    const id = `subscribe:${topic}:${nextId++}`;
    this.subscriptions.set(id, { topic, type, listener: listener as (message: unknown) => void });
    this.send({ op: 'subscribe', id, topic, type });
    return () => {
      this.subscriptions.delete(id);
      this.send({ op: 'unsubscribe', id, topic });
    };
  }

  /** Call a service; the promise fails when rosbridge or the service does. */
  async callService<Req, Res>(service: string, type: string, args: Req): Promise<Res> {
    const id = `call_service:${service}:${nextId++}`;
    const response = await new Promise<Record<string, unknown>>((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      if (!this.send({ op: 'call_service', id, service, type, args })) {
        this.pending.delete(id);
        reject(new Error(`Not connected to rosbridge at ${this.url}.`));
      }
    });
    if (response.result === false) {
      throw new Error(String(response.values ?? `${service} failed.`));
    }
    return response.values as Res;
  }

  /** Send the goal of an action, and wait for its result. */
  sendGoal<G, R, F>(
    action: string,
    type: string,
    goal: G,
    onFeedback: (feedback: F) => void = () => {},
  ): Promise<ActionResult<R>> {
    const id = `send_action_goal:${action}:${nextId++}`;
    return new Promise((resolve) => {
      this.goals.set(id, (message) => {
        if (message.op === 'action_feedback') {
          onFeedback(message.values as F);
          return;
        }
        this.goals.delete(id);
        if (message.op === 'status' || message.result === false) {
          resolve({ outcome: 'failed', error: String(message.msg ?? message.values ?? `${action} failed.`) });
          return;
        }
        resolve({ outcome: GOAL_STATUS[Number(message.status)] ?? 'failed', values: message.values as R });
      });
      if (!this.send({ op: 'send_action_goal', id, action, action_type: type, args: goal, feedback: true })) {
        this.goals.delete(id);
        resolve({ outcome: 'failed', error: `Not connected to rosbridge at ${this.url}.` });
      }
    });
  }

  close(): void {
    this.closed = true;
    clearTimeout(this.reconnect);
    this.socket?.close();
    this.setStatus('disconnected');
  }

  private open(): void {
    this.setStatus('connecting');
    let socket: WebSocket;
    try {
      socket = this.factory(this.url);
    } catch {
      this.scheduleReconnect();
      return;
    }
    this.socket = socket;
    socket.onopen = () => {
      this.setStatus('connected');
      for (const [id, { topic, type }] of this.subscriptions) {
        this.send({ op: 'subscribe', id, topic, type });
      }
    };
    socket.onmessage = (event) => this.receive(String(event.data));
    socket.onclose = () => {
      if (this.socket !== socket) return;
      this.socket = undefined;
      this.failPending();
      if (!this.closed) this.scheduleReconnect();
    };
  }

  private scheduleReconnect(): void {
    this.setStatus('disconnected');
    clearTimeout(this.reconnect);
    this.reconnect = setTimeout(() => this.open(), RECONNECT_MS);
  }

  /** The calls waiting for an answer that the closed connection will never bring. */
  private failPending(): void {
    const error = new Error(`The connection to rosbridge at ${this.url} closed.`);
    for (const { reject } of this.pending.values()) reject(error);
    this.pending.clear();
    for (const finish of this.goals.values()) finish({ op: 'action_result', result: false, values: error.message });
    this.goals.clear();
  }

  private send(message: Record<string, unknown>): boolean {
    if (this.socket?.readyState !== WebSocket.OPEN) return false;
    this.socket.send(JSON.stringify(message));
    return true;
  }

  private receive(data: string): void {
    let message: Record<string, unknown>;
    try {
      message = JSON.parse(data);
    } catch {
      return;
    }
    const id = typeof message.id === 'string' ? message.id : undefined;
    if (message.op === 'publish') {
      for (const subscription of this.subscriptions.values()) {
        if (subscription.topic === message.topic) subscription.listener(message.msg);
      }
    } else if (message.op === 'service_response' && id) {
      this.pending.get(id)?.resolve(message);
      this.pending.delete(id);
    } else if (id && this.goals.has(id)) {
      // action_feedback, action_result, or a status reporting a goal rosbridge refused.
      if (message.op !== 'status' || message.level === 'error') this.goals.get(id)!(message);
    } else if (message.op === 'status' && id && this.pending.has(id) && message.level === 'error') {
      this.pending.get(id)!.reject(new Error(String(message.msg)));
      this.pending.delete(id);
    }
  }

  private setStatus(status: Status): void {
    if (status === this.status) return;
    this.status = status;
    for (const listener of this.statusListeners) listener(status);
  }
}
