// expect: @typescript-eslint/explicit-function-return-type
// Seeded violation (ICS-007): a function without a declared return type. See policy/check-policy.sh.
export function double(value: number) {
  return value * 2;
}
