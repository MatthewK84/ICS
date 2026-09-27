# expect: :6:
# Seeded violation (ICS-006): an inline comment that silences mypy. The
# suppression scan must report line 6. See policy/check-policy.sh.


count: int = "three"  # type: ignore[assignment]
