// expect: @typescript-eslint/no-non-null-assertion
// Seeded violation (ICS-007): a non-null assertion. See policy/check-policy.sh.
export function first(values: readonly string[]): string {
  return values[0]!;
}
