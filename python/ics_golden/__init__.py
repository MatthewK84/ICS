"""Golden test vectors shared by the Python, C++ and TypeScript tests (golden/ at the repository root).

OpenBLAS picks its matrix kernels by CPU, and they round differently: an
iterative fit can take another path and end in another last digit. So every
golden file is written, and checked, with the Haswell kernels, which every
x86-64 machine CI runs on has. Importing this package asks for them unless
OPENBLAS_CORETYPE is already set; it must come before numpy is loaded.
"""

import os

os.environ.setdefault("OPENBLAS_CORETYPE", "Haswell")
