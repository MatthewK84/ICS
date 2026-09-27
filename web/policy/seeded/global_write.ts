// expect: Do not attach state to the global object
// Seeded violation (ICS-007): state attached to the global object. See policy/check-policy.sh.
declare global {
  interface Window {
    lastValue: number;
  }
}

export function remember(value: number): void {
  window.lastValue = value;
}
