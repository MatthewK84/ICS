# expect: ICS103 mutable value at module level
# Seeded violation (ICS-006): mutable state shared through a module. Rejected
# by the AST checks. See policy/check-policy.sh.

frames_seen: list[int] = []
