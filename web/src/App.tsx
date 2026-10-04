import { useState } from 'react';
import { Controls } from './components/Controls';
import { Shots } from './components/Shots';
import { defaultRosbridgeUrl, useFreezer } from './freezer/useFreezer';

const URL_KEY = 'freezer.rosbridge';

function storedUrl(): string {
  try {
    return localStorage.getItem(URL_KEY) || defaultRosbridgeUrl();
  } catch {
    return defaultRosbridgeUrl();
  }
}

const STATUS_LABELS = { connected: 'Connected', connecting: 'Connecting…', disconnected: 'Disconnected' };

export function App() {
  const [url, setUrl] = useState(storedUrl);
  const [draft, setDraft] = useState(url);
  const { freezer, state } = useFreezer(url);
  const connected = state.status === 'connected';
  const running = state.shots.some((shot) => shot.state === 'running');

  const apply = () => {
    setUrl(draft);
    try {
      localStorage.setItem(URL_KEY, draft);
    } catch {
      // The page works without remembering it.
    }
  };

  return (
    <div className="app">
      <header className="topbar">
        <h1>Freezer</h1>
        <span className={`status status-${state.status}`}>
          <span className="dot" aria-hidden="true" />
          {STATUS_LABELS[state.status]}
        </span>
        <form
          className="url"
          onSubmit={(e) => {
            e.preventDefault();
            apply();
          }}
        >
          <label htmlFor="rosbridge-url">rosbridge</label>
          <input id="rosbridge-url" value={draft} onChange={(e) => setDraft(e.target.value)} spellCheck={false} />
          {draft !== url && <button type="submit">Connect</button>}
        </form>
      </header>
      <main className="layout">
        <Controls freezer={freezer} connected={connected} running={running} />
        <Shots shots={state.shots} />
      </main>
    </div>
  );
}
