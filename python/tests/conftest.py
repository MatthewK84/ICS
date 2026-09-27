"""Settings shared by every test (ICS-006)."""

from hypothesis import settings

# The same examples on every run, so a failure reproduces exactly, and no
# example database written to disk.
settings.register_profile("ics", derandomize=True, database=None, deadline=None)
settings.load_profile("ics")
