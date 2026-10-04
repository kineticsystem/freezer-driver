// The shots the page has seen, newest first, and the timing of the one
// selected: the latest, until another is picked.

import { useState } from 'react';
import type { Shot } from '../freezer/freezer';
import { formatDuration } from '../freezer/timeline';
import { TimingDiagram } from './TimingDiagram';

const STATE_LABELS: Record<Shot['state'], string> = {
  running: 'Running',
  ended: 'Ended',
  stopped: 'Stopped',
  failed: 'Failed',
};

function keyOf(shot: Shot): string {
  return `${shot.id}@${shot.startedMs}`;
}

export function Shots({ shots }: { shots: Shot[] }) {
  const [selectedKey, setSelectedKey] = useState<string>();
  const latest = shots[shots.length - 1];
  const selected = shots.find((shot) => keyOf(shot) === selectedKey) ?? latest;

  return (
    <section className="panel shots" aria-label="Shots">
      <header className="panel-header">
        <h2>{selected ? `Shot ${selected.id}` : 'Shots'}</h2>
        {selected && (
          <span className={`badge state-${selected.state}`}>
            {STATE_LABELS[selected.state]}
            {selected.source === 'trigger' ? ' · IN1' : ''}
          </span>
        )}
        {selected?.worstLatenessUs !== undefined && selected.state === 'ended' && (
          <span className="muted">worst lateness {formatDuration(selected.worstLatenessUs)}</span>
        )}
        {selected && selectedKey && keyOf(selected) !== keyOf(latest) && (
          <button className="link" onClick={() => setSelectedKey(undefined)}>
            Follow the latest
          </button>
        )}
      </header>
      {selected ? (
        <>
          {selected.message && <p className="message error">{selected.message}</p>}
          <TimingDiagram key={keyOf(selected)} shot={selected} />
          <table className="shot-list">
            <thead>
              <tr>
                <th>Shot</th>
                <th>From</th>
                <th>Started</th>
                <th>Duration</th>
                <th>State</th>
              </tr>
            </thead>
            <tbody>
              {[...shots].reverse().map((shot) => (
                <tr
                  key={keyOf(shot)}
                  className={keyOf(shot) === keyOf(selected) ? 'selected' : ''}
                  onClick={() => setSelectedKey(keyOf(shot))}
                >
                  <td>{shot.id}</td>
                  <td>{shot.source === 'trigger' ? 'IN1' : 'node'}</td>
                  <td>{new Date(shot.startedMs).toLocaleTimeString()}</td>
                  <td>{formatDuration(shot.state === 'stopped' ? (shot.elapsedUs ?? 0) : shot.durationUs)}</td>
                  <td>{STATE_LABELS[shot.state]}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </>
      ) : (
        <p className="muted">No shot yet. Shoot, or press IN1, and its timing shows here.</p>
      )}
    </section>
  );
}
