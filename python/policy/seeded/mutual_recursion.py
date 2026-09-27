# expect: ICS101 function 'is_even' is recursive
# Seeded violation (ICS-006): two functions that call each other. Rejected by
# the AST checks. See policy/check-policy.sh.


def is_even(value: int) -> bool:
    return True if value == 0 else is_odd(value - 1)


def is_odd(value: int) -> bool:
    return False if value == 0 else is_even(value - 1)
