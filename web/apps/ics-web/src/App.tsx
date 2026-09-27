import type { ReactElement } from "react";

import { describeLink, type LinkStatus } from "./status.ts";

export interface AppProps {
  readonly nowMs: number;
}

const CANARY_STATUS: LinkStatus = { station: "Station 1", state: "connected", lastUpdateMs: 0 };

// Placeholder page for the toolchain canary; ICS-082 scaffolds the real app.
export function App({ nowMs }: AppProps): ReactElement {
  return (
    <main>
      <h1>ICS</h1>
      <p>{describeLink(CANARY_STATUS, nowMs)}</p>
    </main>
  );
}
