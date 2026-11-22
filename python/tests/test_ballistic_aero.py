"""The lift and speed-dependent drag ("flutter") models (ICS-045)."""

import numpy as np
import pytest

from ics_models.ballistic.aero import FLUTTER, FLUTTER_LAW, LIFT_LAW, flutter_derivative, lift_derivative
from ics_models.ballistic.atmosphere import density
from ics_models.ballistic.dynamics import DRAG, Model, derivative, propagate_with
from ics_models.ballistic.fit import FitModel, FitSettings, checked_problem

START_NS: int = 5_000_000_000
MODEL = Model(origin_height_m=1_200.0)
WEIGHTLESS = Model(origin_height_m=1_200.0, gravity_m_s2=0.0)
HEIGHT_M: float = 150.0


def kinematics(v_east: float, v_north: float, v_up: float) -> list[float]:
    return [10.0, -20.0, HEIGHT_M, v_east, v_north, v_up]


def pressure() -> float:
    return 0.5 * density(MODEL.origin_height_m + HEIGHT_M)


def test_with_no_extra_parameters_both_models_move_as_the_drag_model_does() -> None:
    times = [START_NS + step * 37_000_000 for step in range(60)]
    drag_state = np.array([[*kinematics(6.0, -3.0, 4.0), 0.2]])
    expected = propagate_with(DRAG, drag_state, START_NS, times, MODEL)[:, 0, :]
    lift = propagate_with(LIFT_LAW, np.array([[*drag_state[0], 0.0, 0.0]]), START_NS, times, MODEL)[:, 0, :]
    flutter = propagate_with(FLUTTER_LAW, np.array([[*drag_state[0], 0.0]]), START_NS, times, MODEL)[:, 0, :]
    np.testing.assert_array_equal(lift[:, 0:7], expected)
    np.testing.assert_array_equal(flutter[:, 0:7], expected)


def test_lift_and_side_force_are_perpendicular_to_the_velocity() -> None:
    rows = np.array([[*kinematics(*velocity), 0.0, 0.3, -0.2] for velocity in ((6.0, -3.0, 4.0), (-9.0, 2.0, -12.0))])
    rates = lift_derivative(rows, WEIGHTLESS)
    for row, rate in zip(rows, rates, strict=True):
        assert float(rate[3:6] @ row[3:6]) == pytest.approx(0.0, abs=1e-12)


def test_in_level_flight_lift_points_up_and_side_force_to_the_right() -> None:
    rates = lift_derivative(np.array([[*kinematics(10.0, 0.0, 0.0), 0.0, 0.3, 0.2]]), WEIGHTLESS)[0]
    assert rates[3:6] == pytest.approx([0.0, -pressure() * 0.2 * 100.0, pressure() * 0.3 * 100.0], rel=1e-12)


def test_in_vertical_flight_lift_and_side_force_vanish() -> None:
    lift = lift_derivative(np.array([[*kinematics(0.0, 0.0, -7.0), 0.1, 0.3, 0.2]]), MODEL)[0]
    drag = derivative(np.array([[*kinematics(0.0, 0.0, -7.0), 0.1]]), MODEL)[0]
    np.testing.assert_array_equal(lift[0:6], drag[0:6])


def test_flutter_drag_grows_with_speed() -> None:
    velocity = (6.0, -3.0, 4.0)
    speed = float(np.linalg.norm(velocity))
    flutter = flutter_derivative(np.array([[*kinematics(*velocity), 0.05, 0.01]]), MODEL)[0]
    drag = derivative(np.array([[*kinematics(*velocity), 0.05 + 0.01 * speed]]), MODEL)[0]
    assert flutter[0:6] == pytest.approx(drag[0:6], rel=1e-12)


def test_a_model_without_a_step_for_every_element_is_refused() -> None:
    broken = FitModel("broken", FLUTTER.law, FLUTTER.derivative_steps[:-1], FLUTTER.step_floors)
    times = [START_NS + step * 10_000_000 for step in range(5)]
    with pytest.raises(ValueError, match="derivative step"):
        checked_problem(times, np.zeros((5, 3)), FitSettings(model=MODEL, sigma_m=0.02), broken)
