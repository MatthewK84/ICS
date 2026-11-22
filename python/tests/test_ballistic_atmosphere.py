"""ISA troposphere density (ICS-044)."""

from itertools import pairwise

import pytest

from ics_models.ballistic.atmosphere import LOWEST_HEIGHT_M, TROPOPAUSE_HEIGHT_M, density


def test_sea_level_and_tropopause_densities_are_the_standard_ones() -> None:
    assert density(0.0) == pytest.approx(1.2250, abs=5e-5)
    assert density(TROPOPAUSE_HEIGHT_M) == pytest.approx(0.36392, abs=5e-5)
    assert density(1_500.0) == pytest.approx(1.0581, abs=5e-5)


def test_density_falls_with_height() -> None:
    heights = [LOWEST_HEIGHT_M + 250.0 * step for step in range(47)]
    values = [density(height) for height in heights]
    assert all(lower > upper for lower, upper in pairwise(values))


@pytest.mark.parametrize("height_m", [LOWEST_HEIGHT_M - 1.0, TROPOPAUSE_HEIGHT_M + 1.0, float("nan")])
def test_heights_outside_the_troposphere_are_refused(height_m: float) -> None:
    with pytest.raises(ValueError, match="outside the ISA troposphere"):
        density(height_m)
