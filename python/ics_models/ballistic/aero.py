"""Lift and speed-dependent drag ("flutter") models (ICS-045, PY-04).

Both extend the 7-state drag model of dynamics.py with parameters after κ,
under the same ISA density, flat-Earth gravity and RK4 propagation:

- lift, 9 states (κ_L, κ_S after κ, both m²/kg): a plate gliding or sliding
  at a fixed attitude to its path. The extra acceleration is
  ½·rho·(κ_L·(|v|²·up - v_up·v) + κ_S·|v|·cross(v, up)). The lift term lies in
  the vertical plane through v and the side term across it, both
  perpendicular to v, and both have magnitude ½·rho·κ·|v|·|v_horizontal|:
  they fade to nothing as the path turns vertical, where their directions are
  undefined, so nothing is ever divided by a speed.
- flutter, 8 states (κ₁ after κ, in m²/kg per m/s, so s·m/kg): a tumbling piece whose
  drag grows with speed, κ_effective = κ + κ₁·|v|.

A model whose extra parameters are zero moves exactly as the drag model does.
"""

import math

import numpy as np

from ics_models.ballistic.atmosphere import density
from ics_models.ballistic.dynamics import HALF, KAPPA, STATE_SIZE, UP, Law, Matrix, Model
from ics_models.ballistic.fit import DERIVATIVE_STEPS, STEP_FLOORS, FitModel

LIFT_SIZE: int = STATE_SIZE + 2
FLUTTER_SIZE: int = STATE_SIZE + 1
LIFT_KAPPA: int = STATE_SIZE
SIDE_KAPPA: int = STATE_SIZE + 1
KAPPA_SLOPE: int = STATE_SIZE
# Central-difference steps and convergence floors for the extra parameters.
LIFT_STEP_M2_KG: float = 1e-6
LIFT_FLOOR_M2_KG: float = 1e-3
SLOPE_STEP: float = 1e-7
SLOPE_FLOOR: float = 1e-4


def lift_derivative(states: Matrix, model: Model) -> Matrix:
    """The time derivative of each 9-element lift state row."""
    rates = np.zeros_like(states)
    for row in range(states.shape[0]):
        east, north, up = float(states[row, 3]), float(states[row, 4]), float(states[row, 5])
        speed_squared = east * east + north * north + up * up
        speed = math.sqrt(speed_squared)
        pressure = HALF * density(model.origin_height_m + float(states[row, UP]))
        drag = pressure * float(states[row, KAPPA]) * speed
        lift = pressure * float(states[row, LIFT_KAPPA])
        side = pressure * float(states[row, SIDE_KAPPA]) * speed
        rates[row, 0:3] = (east, north, up)
        rates[row, 3] = -drag * east - lift * up * east + side * north
        rates[row, 4] = -drag * north - lift * up * north - side * east
        rates[row, 5] = -drag * up + lift * (speed_squared - up * up) - model.gravity_m_s2
    return rates


def flutter_derivative(states: Matrix, model: Model) -> Matrix:
    """The time derivative of each 8-element flutter state row."""
    rates = np.zeros_like(states)
    for row in range(states.shape[0]):
        east, north, up = float(states[row, 3]), float(states[row, 4]), float(states[row, 5])
        speed = math.sqrt(east * east + north * north + up * up)
        kappa = float(states[row, KAPPA]) + float(states[row, KAPPA_SLOPE]) * speed
        drag = HALF * density(model.origin_height_m + float(states[row, UP])) * kappa * speed
        rates[row, 0:3] = (east, north, up)
        rates[row, 3:6] = (-drag * east, -drag * north, -drag * up - model.gravity_m_s2)
    return rates


LIFT_LAW = Law(rates=lift_derivative, size=LIFT_SIZE)
FLUTTER_LAW = Law(rates=flutter_derivative, size=FLUTTER_SIZE)

LIFT = FitModel(
    name="lift",
    law=LIFT_LAW,
    derivative_steps=(*DERIVATIVE_STEPS, LIFT_STEP_M2_KG, LIFT_STEP_M2_KG),
    step_floors=(*STEP_FLOORS, LIFT_FLOOR_M2_KG, LIFT_FLOOR_M2_KG),
    extra_seeds=(0.0, 0.0),
)
FLUTTER = FitModel(
    name="flutter",
    law=FLUTTER_LAW,
    derivative_steps=(*DERIVATIVE_STEPS, SLOPE_STEP),
    step_floors=(*STEP_FLOORS, SLOPE_FLOOR),
    extra_seeds=(0.0,),
)
