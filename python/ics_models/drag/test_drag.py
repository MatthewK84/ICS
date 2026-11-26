"""Basic test for drag-table fitting."""

import numpy as np

from ics_models.drag.fit import fit_drag_table


def test_fit_returns_table() -> None:
    table = fit_drag_table(
        np.array([0.1, 0.2]),
        np.array([0.5, 0.6]),
        ["steel", "steel"],
    )
    assert "steel" in table.materials
