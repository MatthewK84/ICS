// expect: @typescript-eslint/no-floating-promises
// Seeded violation (ICS-007): a promise nobody awaits or handles. See policy/check-policy.sh.
async function save(): Promise<void> {
  await Promise.resolve();
}

export function onClick(): void {
  save();
}
