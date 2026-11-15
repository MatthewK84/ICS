"""The reference shot schedule (ICS-040)."""

import pytest

from ics_models.handoff.approach import HandoffError
from ics_models.trigger.shot import Shot, ShotSettings, edge_at_or_before, shot_for, window_open

S: int = 1_000_000_000
MS: int = 1_000_000
SETTINGS = ShotSettings(period_ns=S, offset_ns=1_500, command_margin_ns=200 * MS, guard_ns=50 * MS)
EDGE: int = 100 * S + 1_500


def test_an_edge_is_a_whole_second_plus_the_offset() -> None:
    assert edge_at_or_before(EDGE, SETTINGS) == EDGE
    assert edge_at_or_before(EDGE - 1, SETTINGS) == EDGE - S
    assert edge_at_or_before(EDGE + S - 1, SETTINGS) == EDGE
    assert edge_at_or_before(0, SETTINGS) == 1_500 - S


def test_a_pulse_is_timed_from_the_last_edge_before_it() -> None:
    shot = shot_for(EDGE + 123_456_789, EDGE - 500 * MS, SETTINGS)
    assert shot == Shot(EDGE, 123_456_789, late=False)
    assert shot.fire_ns == EDGE + 123_456_789
    assert shot_for(EDGE, EDGE - 200 * MS, SETTINGS) == Shot(EDGE, 0, late=False)


def test_a_pulse_too_late_for_its_edge_goes_out_on_the_next_that_can_be_armed_for() -> None:
    assert shot_for(EDGE + 300 * MS, EDGE - 200 * MS + 1, SETTINGS) == Shot(EDGE + S, 0, late=True)
    assert shot_for(EDGE + 300 * MS, EDGE + 250 * MS, SETTINGS) == Shot(EDGE + S, 0, late=True)
    assert shot_for(EDGE + 300 * MS, EDGE + 800 * MS, SETTINGS) == Shot(EDGE + S, 0, late=True)
    assert shot_for(EDGE + 300 * MS, EDGE + 800 * MS + 1, SETTINGS) == Shot(EDGE + 2 * S, 0, late=True)


def test_the_generator_may_be_armed_only_between_the_edge_before_and_the_margin() -> None:
    shot = Shot(EDGE, 300 * MS, late=False)
    assert not window_open(shot, EDGE - S + 50 * MS - 1, SETTINGS)
    assert window_open(shot, EDGE - S + 50 * MS, SETTINGS)
    assert window_open(shot, EDGE - 200 * MS, SETTINGS)
    assert not window_open(shot, EDGE - 200 * MS + 1, SETTINGS)


@pytest.mark.parametrize("values", [(S, 0, -1, 0), (S, 0, 0, -1), (250 * MS, 0, 200 * MS, 50 * MS)])
def test_a_period_must_hold_the_margins(values: tuple[int, int, int, int]) -> None:
    with pytest.raises(HandoffError, match="does not hold the margins"):
        ShotSettings(*values)
