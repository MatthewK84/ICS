// expect: Function 'isEven' is recursive.*ics/no-recursion
// Seeded violation (ICS-007): two functions that call each other. See policy/check-policy.sh.
export const isEven = (value: number): boolean => value === 0 || isOdd(value - 1);
export const isOdd = (value: number): boolean => value !== 0 && isEven(value - 1);
