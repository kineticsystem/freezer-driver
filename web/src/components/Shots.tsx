// The lines of the board, lit as they are now, and the timing of the latest
// shot the page has seen.

import { useState } from 'react';
import type { Shot } from '../freezer/freezer';
import { TimingDiagram } from './TimingDiagram';

export function Shots({ shots, outputs }: { shots: Shot[]; outputs?: number }) {
  const shot = shots[shots.length - 1];
  const [hover, setHover] = useState<string>();
  return (
    <section className="panel shots" aria-label="The lines and the latest shot">
      <header className="panel-header">
        <h2>Lines</h2>
        <span className="hover-readout muted">{hover}</span>
      </header>
      {shot?.message && <p className="message error">{shot.message}</p>}
      <TimingDiagram key={shot ? `${shot.id}@${shot.startedMs}` : 'none'} shot={shot} outputs={outputs} onHover={setHover} />
    </section>
  );
}
