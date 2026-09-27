# expect: :6:
# Seeded violation (ICS-006): an inline comment that silences ruff. The
# suppression scan must report line 6. See policy/check-policy.sh.


value = eval("1")  # noqa: S307
