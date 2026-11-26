"""Fit Cd by material class."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt


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
        # Placeholder: Cd ~ beta / (area * rho) approximated from mass proxy
        cd = float(np.mean(betas[mask]) / max(np.mean(masses_kg[mask]), 0.01))
        table[mat] = max(0.1, min(cd, 2.0))
    return DragTable(materials=table)
