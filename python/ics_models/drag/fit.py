"""Fit Cd by material class."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

MIN_MASS_KG: float = 0.01
MIN_CD: float = 0.1
MAX_CD: float = 2.0


@dataclass(frozen=True)
class DragTable:
    """Cd values keyed by material class."""

    materials: dict[str, float]


def fit_drag_table(
    masses_kg: npt.NDArray[np.float64],
    betas: npt.NDArray[np.float64],
    materials: list[str],
) -> DragTable:
    """Simple least-squares Cd fit per material class for the reference."""
    table: dict[str, float] = {}
    for mat in set(materials):
        mask = np.array([m == mat for m in materials])
        if not np.any(mask):
            continue
        cd = float(np.mean(betas[mask]) / max(np.mean(masses_kg[mask]), MIN_MASS_KG))
        table[mat] = max(MIN_CD, min(cd, MAX_CD))
    return DragTable(materials=table)
