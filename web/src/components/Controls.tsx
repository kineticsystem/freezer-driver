// The commands: fire a sequence of the node's parameters, stop, switch every
// output off, and press IN1 of a fake controller.

import { useEffect, useState } from 'react';
import type { Freezer } from '../freezer/freezer';

interface Props {
  freezer: Freezer;
  connected: boolean;
  running: boolean;
}

export function Controls({ freezer, connected, running }: Props) {
  const [names, setNames] = useState<string[]>([]);
  const [sequence, setSequence] = useState('');
  const [message, setMessage] = useState<{ text: string; error: boolean }>();

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
    return () => {
      current = false;
    };
  }, [freezer, connected]);

  const report = async (action: () => Promise<{ success: boolean; message: string }>, done: string) => {
    try {
      const response = await action();
      setMessage(response.success ? { text: done, error: false } : { text: response.message, error: true });
    } catch (e) {
      setMessage({ text: e instanceof Error ? e.message : String(e), error: true });
    }
  };

  const shoot = async () => {
    setMessage({ text: `Shooting ${sequence || 'the default sequence'}…`, error: false });
    const result = await freezer.shoot({ sequence });
    if (result.outcome === 'succeeded') {
      setMessage({ text: `Shot ${result.values?.shot_id} done.`, error: false });
    } else {
      setMessage({ text: result.values?.message || result.error || `The shot ${result.outcome}.`, error: true });
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
          {names.map((name) => (
            <option key={name} value={name}>
              {name}
            </option>
          ))}
        </select>
        <button className="primary" disabled={!connected || running} onClick={shoot}>
          Shoot
        </button>
        <button className="danger" disabled={!connected} onClick={() => report(() => freezer.stop(), 'Stopped.')}>
          Stop
        </button>
      </div>
      <div className="control-row">
        <button
          disabled={!connected || running}
          onClick={() => report(() => freezer.setOutputs(0), 'Every output is off.')}
        >
          All off
        </button>
        <button
          disabled={!connected}
          title="Press the remote trigger of a fake controller: it fires the sequence loaded last"
          onClick={() => report(() => freezer.pressTrigger(), 'IN1 pressed.')}
        >
          Press IN1
        </button>
      </div>
      <p className={`message ${message?.error ? 'error' : ''}`} role="status">
        {message?.text ?? ' '}
      </p>
    </section>
  );
}
