import { useEffect, useMemo, useSyncExternalStore } from 'react';
import { Rosbridge } from '../ros/rosbridge';
import { Freezer, type FreezerState } from './freezer';

/** The Freezer node behind the rosbridge at this URL, connected while the component lives. */
export function useFreezer(url: string): { freezer: Freezer; state: FreezerState } {
  const freezer = useMemo(() => new Freezer(new Rosbridge(url)), [url]);
  useEffect(
    () => () => {
      freezer.close();
      freezer.ros.close();
    },
    [freezer],
  );
  const state = useSyncExternalStore(freezer.subscribe, freezer.getState);
  return { freezer, state };
}

/** The rosbridge URL when none is set: port 9092 of the host that serves the page. */
export function defaultRosbridgeUrl(): string {
  return `ws://${location.hostname || 'localhost'}:9092`;
}
