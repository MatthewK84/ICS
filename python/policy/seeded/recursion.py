# expect: ICS101 function 'countdown' is recursive
# Seeded violation (ICS-006): a function that calls itself. Rejected by the
# AST checks. See policy/check-policy.sh.


def countdown(steps: int) -> int:
    if steps <= 0:
        return 0
    return countdown(steps - 1)
