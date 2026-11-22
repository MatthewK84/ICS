"""Settings shared by every test (ICS-006)."""

import os

from hypothesis import settings

# OpenBLAS picks its matrix kernels by CPU, and they round differently, so the
# golden files are written and checked with the Haswell kernels, which every
# x86-64 CI runner has (see ics_golden/__init__.py). numpy is not loaded yet.
os.environ.setdefault("OPENBLAS_CORETYPE", "Haswell")

# The same examples on every run, so a failure reproduces exactly, and no
# example database written to disk.
settings.register_profile("ics", derandomize=True, database=None, deadline=None)
settings.load_profile("ics")
