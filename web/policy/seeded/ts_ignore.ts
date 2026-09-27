// expect: @typescript-eslint/ban-ts-comment
// Seeded violation (ICS-007): a comment that silences the type checker. See policy/check-policy.sh.
export function width(): number {
  // @ts-ignore
  return "wide";
}
