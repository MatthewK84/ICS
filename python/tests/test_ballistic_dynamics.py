"""The point-mass drag model and its RK4 propagation (ICS-044)."""

import math

import numpy as np
import pytest

from ics_models.ballistic.atmosphere import density
from ics_models.ballistic.dynamics import STATE_SIZE, Model, propagate, trajectory

START_NS: int = 5_000_000_000
NS_PER_S: int = 1_000_000_000
MODEL = Model(origin_height_m=1_200.0)
MICROMETRE: float = 1e-6


def state(kappa: float) -> np.ndarray:
    return np.array([10.0, -20.0, 150.0, 6.0, -3.0, 4.0, kappa])


def test_without_drag_a_piece_follows_the_parabola() -> None:
    times = [START_NS + step * 37_000_000 for step in range(100)]
    path = trajectory(state(0.0), START_NS, times, MODEL)
    for time, row in zip(times, path, strict=True):
        seconds = (time - START_NS) / NS_PER_S
        assert row[0] == pytest.approx(10.0 + 6.0 * seconds, abs=1e-9)
        assert row[2] == pytest.approx(150.0 + 4.0 * seconds - 0.5 * MODEL.gravity_m_s2 * seconds**2, abs=1e-9)
        assert row[5] == pytest.approx(4.0 - MODEL.gravity_m_s2 * seconds, abs=1e-9)


def test_the_step_is_small_enough_that_a_finer_one_agrees_within_a_micrometre() -> None:
    times = [START_NS + 5 * NS_PER_S]
    coarse = trajectory(state(1.0), START_NS, times, MODEL)
    fine = trajectory(state(1.0), START_NS, times, Model(origin_height_m=1_200.0, step_ns=1_000_000))
    assert np.max(np.abs(coarse - fine)) < MICROMETRE


def test_a_light_piece_falls_at_its_terminal_speed() -> None:
    path = trajectory(np.array([0.0, 0.0, 300.0, 0.0, 0.0, 0.0, 1.0]), START_NS, [START_NS + 20 * NS_PER_S], MODEL)
    terminal = math.sqrt(2.0 * MODEL.gravity_m_s2 / (density(MODEL.origin_height_m + path[0, 2]) * 1.0))
    assert -path[0, 5] == pytest.approx(terminal, rel=1e-4)
    assert path[0, 6] == 1.0


def test_stopping_on_the_way_changes_nothing() -> None:
    middle, end = START_NS + 1_234_567_890, START_NS + 3 * NS_PER_S
    through = trajectory(state(0.3), START_NS, [middle, end], MODEL)
    resumed = trajectory(through[0], middle, [end], MODEL)
    direct = trajectory(state(0.3), START_NS, [end], MODEL)
    assert np.array_equal(through[1], resumed[0])
    assert np.allclose(direct[0], resumed[0], rtol=1e-12, atol=1e-12)


def test_rows_propagate_as_they_would_alone() -> None:
    rows = np.vstack([state(0.0), state(0.08), state(1.0)])
    times = [START_NS, START_NS + NS_PER_S, START_NS + 2 * NS_PER_S]
    together = propagate(rows, START_NS, times, MODEL)
    for row in range(3):
        assert np.array_equal(together[:, row, :], trajectory(rows[row], START_NS, times, MODEL))


def test_bad_inputs_are_refused() -> None:
    with pytest.raises(ValueError, match="rows of 7"):
        propagate(np.zeros(STATE_SIZE), START_NS, [START_NS], MODEL)
    with pytest.raises(ValueError, match="step must be positive"):
        trajectory(state(0.0), START_NS, [START_NS], Model(origin_height_m=0.0, step_ns=0))
    with pytest.raises(ValueError, match="precedes"):
        trajectory(state(0.0), START_NS, [START_NS + NS_PER_S, START_NS], MODEL)
