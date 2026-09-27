// expect: no-var
// Seeded violation (ICS-007): var instead of const or let. See policy/check-policy.sh.
export function count(): number {
  var total = 0;
  total += 1;
  return total;
}
