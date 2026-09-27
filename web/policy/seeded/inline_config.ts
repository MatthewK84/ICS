// expect: has no effect because you have 'noInlineConfig'
// Seeded violation (ICS-007): a comment that turns a lint rule off. See policy/check-policy.sh.
/* eslint-disable no-var */
export function count(): number {
  let total = 0;
  total += 1;
  return total;
}
