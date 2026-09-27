# expect: \[no-untyped-def\]
# Seeded violation (ICS-006): a function without type annotations. Rejected by
# mypy --strict. See policy/check-policy.sh.


def scale(value, factor):
    return value * factor
