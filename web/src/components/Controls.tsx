// The commands: fire a sequence of the node's parameters, stop, switch every
// output on or off, and press IN1 of a fake controller.

import { useEffect, useState } from 'react';
import type { Freezer } from '../freezer/freezer';

interface Props {
  freezer: Freezer;
  connected: boolean;
  running: boolean;
}

/** What the menu shows for the sequence at this index: the default one first. */
export function sequenceLabel(index: number): string {
  return `example ${index + 1}`;
}

export function Controls({ freezer, connected, running }: Props) {
  const [names, setNames] = useState<string[]>([]);
  const [sequence, setSequence] = useState('');
  // Whether the node runs a fake controller: IN1 of the board can only be
  // pressed by hand.
  const [fake, setFake] = useState(false);
  // Why the last command failed; a command that succeeds says nothing, its
  // effect shows on the lines.
  const [error, setError] = useState<string>();

  // The sequences of the node, once connected.
  useEffect(() => {
    if (!connected) return;
    let current = true;
    freezer
      .sequenceNames()
      .then((found) => {
        if (!current) return;
        setNames(found);
        setSequence((selected) => (found.includes(selected) ? selected : (found[0] ?? '')));
      })
      .catch(() => current && setNames([]));
    freezer
      .usesFake()
      .then((found) => current && setFake(found))
      .catch(() => current && setFake(false));
    return () => {
      current = false;
    };
  }, [freezer, connected]);

  const report = async (action: () => Promise<{ success: boolean; message: string }>) => {
    try {
      const response = await action();
      setError(response.success ? undefined : response.message);
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e));
    }
  };

  const shoot = async () => {
    setError(undefined);
    const result = await freezer.shoot({ sequence });
    const message = result.values?.message || result.error || `The shot ${result.outcome}.`;
    // A shot ended by Stop is what was asked for, not an error.
    if (result.outcome !== 'succeeded' && !message.startsWith('Stopped')) {
      setError(message);
    }
  };

  return (
    <section className="panel controls" aria-label="Commands">
      <header className="panel-header">
        <h2>Commands</h2>
      </header>
      <div className="control-row">
        <select
          value={sequence}
          disabled={!connected || names.length === 0}
          onChange={(e) => setSequence(e.target.value)}
          aria-label="Sequence"
        >
          {names.length === 0 && <option value="">default sequence</option>}
          {names.map((name, index) => (
            <option key={name} value={name} title={name}>
              {sequenceLabel(index)}
            </option>
          ))}
        </select>
        <button className="primary" disabled={!connected || running} onClick={shoot}>
          Send
        </button>
        <button className="danger" disabled={!connected} onClick={() => report(() => freezer.stop())}>
          Stop
        </button>
      </div>
      <div className="control-row">
        <button
          disabled={!connected || running}
          title="Close every line of every jack, until All off: a camera plugged in focuses and holds its shutter open"
          onClick={() => report(() => freezer.setOutputs(0xffff))}
        >
          All on
        </button>
        <button
          disabled={!connected || running}
          onClick={() => report(() => freezer.setOutputs(0))}
        >
          All off
        </button>
        <button
          disabled={!connected || !fake}
          title={
            fake
              ? 'Press the remote trigger of the fake controller: it fires the sequence loaded last'
              : 'Only the fake controller can be pressed from here: on the board, close the IN1 jack'
          }
          onClick={() => report(() => freezer.pressTrigger())}
        >
          Press IN1
        </button>
      </div>
      <p className="message error" role="status">
        {error ?? '\u00a0'}
      </p>
    </section>
  );
}
