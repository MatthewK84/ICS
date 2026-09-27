// expect: implicit_any\.ts\([0-9]+,[0-9]+\): error TS7006
// Seeded violation (ICS-007): a parameter with no type, which strict mode rejects. See policy/check-policy.sh.
export function scale(value): number {
  return Number(value) * 2;
}
