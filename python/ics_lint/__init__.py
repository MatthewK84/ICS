"""Power-of-Ten checks that ruff and mypy do not cover (ICS-006).

  ICS100  the file cannot be parsed
  ICS101  a function is recursive, directly or through other functions of its module
  ICS102  a function is longer than 50 lines
  ICS103  module-level mutable state: a mutable value bound at module level, or a global statement

Run with ``python -m ics_lint PATH... [--exclude DIR]`` from ``python/``.
"""
