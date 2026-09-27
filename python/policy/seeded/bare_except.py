# expect: E722
# Seeded violation (ICS-006): a bare except, which hides every error. Rejected
# by ruff. See policy/check-policy.sh.


def parse(text: str) -> int:
    try:
        return int(text)
    except:
        return 0
