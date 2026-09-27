// expect: @typescript-eslint/no-explicit-any
// Seeded violation (ICS-007): the any type. See policy/check-policy.sh.
export function parse(text: string): any {
  return JSON.parse(text);
}
