// expect: :5:
// Seeded violation (ICS-007): a comment that hides code from coverage. The
// suppression scan must report line 5. See policy/check-policy.sh.
export function risky(flag: boolean): number {
  /* v8 ignore next */
  return flag ? 1 : 0;
}
