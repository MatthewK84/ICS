// expect: Function 'countdown' is recursive.*ics/no-recursion
// Seeded violation (ICS-007): a function that calls itself. See policy/check-policy.sh.
export function countdown(steps: number): number {
  return steps <= 0 ? 0 : countdown(steps - 1);
}
