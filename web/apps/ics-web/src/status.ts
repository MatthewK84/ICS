// Station link status as shown by the toolchain canary (ICS-007). The real
// status model arrives with the ics-api contracts (ICS-011 onward).
export type LinkState = "connected" | "degraded" | "lost";

export interface LinkStatus {
  readonly station: string;
  readonly state: LinkState;
  readonly lastUpdateMs: number;
}

const LABELS: Readonly<Record<LinkState, string>> = {
  connected: "Connected",
  degraded: "Degraded",
  lost: "Lost",
};

const MS_PER_SECOND = 1000;

// "Station 1: Connected (5 s ago)". A last update in the future, from clock
// skew between hosts, reads as 0 s.
export function describeLink(status: LinkStatus, nowMs: number): string {
  const ageSeconds = Math.max(0, Math.floor((nowMs - status.lastUpdateMs) / MS_PER_SECOND));
  return `${status.station}: ${LABELS[status.state]} (${String(ageSeconds)} s ago)`;
}
