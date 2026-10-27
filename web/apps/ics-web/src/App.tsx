import type { ReactElement } from "react";

import { describeLink, type LinkStatus } from "./status.ts";

export interface AppProps {
  readonly nowMs: number;
}

const CANARY_STATUS: LinkStatus = { station: "Station 1", state: "connected", lastUpdateMs: 0 };

// Placeholder page for the toolchain canary, styled from src/styles/tokens.css
// (docs/ui-design.md). ICS-082 builds the real app shell; this page has no
// operator yet, so it shows only the top bar and an empty state.
export function App({ nowMs }: AppProps): ReactElement {
  return (
    <div className="shell">
      <header className="topbar">
        <h1>ICS</h1>
        <p className="link" data-state={CANARY_STATUS.state}>
          <span className="dot" aria-hidden="true" />
          {describeLink(CANARY_STATUS, nowMs)}
        </p>
      </header>
      <main className="workspace">
        <p className="empty">No views yet. ICS-082 builds the operator interface; this page is the toolchain canary.</p>
      </main>
    </div>
  );
}
