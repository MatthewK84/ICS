"""The reference engagement queue (ICS-039)."""

import math

import pytest

from ics_models.handoff.approach import Approach, HandoffError
from ics_models.handoff.queue import MAX_ENGAGEMENTS, Plan, Queue, QueueSettings, Sighting
from ics_models.handoff.slew import SlewLimits
from ics_models.pointing.model import AltAz

S: int = 1_000_000_000
SETTINGS = QueueSettings(
    min_gap_ns=3 * S, dwell_ns=S // 2, margin_ns=S // 2, settle_ns=3 * S // 10, engaged_miss_m=10.0
)
LIMITS = SlewLimits(rate_rad_s=math.radians(10.0), acceleration_rad_s2=0.5, gain=6.0)
NEAR = AltAz(1.0, 0.3)
NONE = Sighting(None, None)


def seen(seconds: float, miss_m: float = 1.0, direction: AltAz | None = NEAR) -> Sighting:
    return Sighting(Approach(round(seconds * S), miss_m), direction)


def queue_of(count: int) -> Queue:
    return Queue(count, SETTINGS, LIMITS)


def at(queue: Queue, seconds: float, *sightings: Sighting) -> Plan:
    return queue.update(round(seconds * S), sightings)


def test_engagements_without_an_approach_wait_in_the_plans_order() -> None:
    plan = at(queue_of(3), 0.0, NONE, NONE, NONE)
    assert plan.order == (0, 1, 2)
    assert (plan.current, plan.dwelling) == (0, False)


def test_engagements_with_an_approach_come_first_soonest_first() -> None:
    plan = at(queue_of(4), 0.0, NONE, seen(9.0), seen(7.0), seen(9.0))
    assert (plan.order, plan.current) == ((2, 1, 3, 0), 2)


def test_the_station_leaves_an_engagement_only_for_one_more_than_the_margin_sooner() -> None:
    queue = queue_of(2)
    assert at(queue, 0.0, seen(8.0), seen(9.0)).current == 0
    inside = at(queue, 0.1, seen(8.0), seen(7.5))
    assert (inside.order, inside.current) == ((1, 0), 0)
    assert at(queue, 0.2, seen(8.0), seen(7.4)).current == 1


def test_the_station_leaves_an_engagement_with_no_approach_for_one_with() -> None:
    queue = queue_of(2)
    assert at(queue, 0.0, NONE, NONE).current == 0
    assert at(queue, 0.1, NONE, seen(30.0)).current == 1
    assert at(queue, 0.2, NONE, NONE).current == 1


def test_an_engagement_is_done_when_its_approach_passes_near_enough() -> None:
    queue = queue_of(2)
    at(queue, 7.9, seen(8.0, 2.0), seen(20.0))
    plan = at(queue, 8.0, NONE, seen(20.0))
    assert plan.slots[0].done
    assert plan.slots[0].approach == Approach(8 * S, 2.0)
    assert (plan.order, plan.current, plan.dwelling) == ((1,), 0, True)


def test_the_station_dwells_on_a_done_engagement_then_moves_on() -> None:
    queue = queue_of(2)
    at(queue, 7.9, seen(8.0), seen(20.0))
    assert at(queue, 8.4, NONE, seen(20.0)).dwelling
    moved = at(queue, 8.5, NONE, seen(20.0))
    assert (moved.current, moved.dwelling) == (1, False)
    at(queue, 19.9, NONE, seen(20.0))
    at(queue, 20.0, NONE, NONE)
    idle = at(queue, 20.5, NONE, NONE)
    assert (idle.order, idle.current, idle.dwelling) == ((), None, False)
    assert at(queue, 30.0, seen(40.0), seen(41.0)).current is None


def test_a_fly_by_is_not_an_engagement() -> None:
    queue = queue_of(1)
    at(queue, 7.9, seen(8.0, 10.1))
    plan = at(queue, 8.0, NONE)
    assert not plan.slots[0].done
    assert plan.slots[0].approach is None
    assert at(queue, 9.0, seen(12.0, 10.0)).slots[0].approach == Approach(12 * S, 10.0)
    assert at(queue, 12.0, NONE).slots[0].done


def test_an_approach_lost_as_it_passes_is_held() -> None:
    queue = queue_of(1)
    at(queue, 7.4, seen(8.0))
    assert at(queue, 7.5, NONE).slots[0].approach == Approach(8 * S, 1.0)
    assert not at(queue, 7.9, seen(7.9)).slots[0].done
    assert at(queue, 8.0, NONE).slots[0].done


def test_an_approach_given_up_early_is_dropped() -> None:
    queue = queue_of(1)
    at(queue, 7.0, seen(8.0))
    assert at(queue, 7.4, NONE).slots[0].approach is None
    assert not at(queue, 8.1, NONE).slots[0].done


def test_an_engagement_too_soon_after_the_one_before_is_flagged() -> None:
    queue = queue_of(3)
    plan = at(queue, 0.0, seen(8.0), seen(10.9), seen(13.9))
    assert [slot.too_close for slot in plan.slots] == [False, True, False]
    assert not any(slot.out_of_reach for slot in plan.slots)


def test_an_engagement_the_mount_cannot_reach_in_the_gap_is_flagged() -> None:
    far = AltAz(1.0 + math.radians(60.0), 0.3)
    queue = queue_of(3)
    plan = at(queue, 0.0, seen(8.0), seen(14.0, 1.0, far), seen(30.0, 1.0, None))
    assert [slot.out_of_reach for slot in plan.slots] == [False, True, False]
    assert not at(queue, 0.1, seen(8.0), seen(16.0, 1.0, far), seen(30.0, 1.0, None)).slots[1].out_of_reach
    assert not at(queue, 0.2, seen(8.0, 1.0, None), seen(14.0, 1.0, far), NONE).slots[1].out_of_reach


def test_the_first_engagement_is_flagged_against_the_one_served_before_it() -> None:
    queue = queue_of(2)
    at(queue, 7.9, seen(8.0), seen(10.0))
    dwelling = at(queue, 8.0, NONE, seen(10.0))
    assert dwelling.dwelling
    assert dwelling.slots[1].too_close
    moved = at(queue, 8.5, NONE, seen(10.0))
    assert moved.current == 1
    assert moved.slots[1].too_close
    assert not at(queue, 8.6, NONE, seen(11.1)).slots[1].too_close
    done = at(queue, 11.1, NONE, NONE)
    assert done.slots[1].done
    assert not done.slots[1].too_close


def test_flags_fall_when_an_engagement_comes_first_with_none_served() -> None:
    queue = queue_of(2)
    assert at(queue, 0.0, seen(8.0), seen(9.0)).slots[1].too_close
    plan = at(queue, 0.1, NONE, seen(9.0))
    assert plan.order == (1, 0)
    assert not plan.slots[1].too_close
    assert not plan.slots[0].too_close


def test_a_done_engagement_keeps_its_flags() -> None:
    queue = queue_of(2)
    at(queue, 7.9, seen(8.0), seen(10.0))
    at(queue, 8.5, NONE, seen(10.0))
    at(queue, 9.9, NONE, seen(10.0))
    assert at(queue, 10.0, NONE, NONE).slots[1].too_close
    assert at(queue, 12.0, NONE, NONE).slots[1].too_close


@pytest.mark.parametrize("count", [0, MAX_ENGAGEMENTS + 1])
def test_a_run_plan_has_one_to_eight_engagements(count: int) -> None:
    with pytest.raises(HandoffError, match="engagements"):
        queue_of(count)
    assert at(queue_of(MAX_ENGAGEMENTS), 0.0, *[NONE] * MAX_ENGAGEMENTS).order == tuple(range(MAX_ENGAGEMENTS))


def test_every_engagement_needs_a_sighting() -> None:
    with pytest.raises(HandoffError, match="1 sightings were given for 2"):
        at(queue_of(2), 0.0, NONE)


@pytest.mark.parametrize("values", [(-1, 0, 0, 0, 1.0), (0, -1, 0, 0, 1.0), (0, 0, -1, 0, 1.0), (0, 0, 0, -1, 1.0)])
def test_settings_that_cannot_be_used_are_refused(values: tuple[int, int, int, int, float]) -> None:
    with pytest.raises(HandoffError, match="cannot be used"):
        QueueSettings(*values)
    with pytest.raises(HandoffError, match="cannot be used"):
        QueueSettings(0, 0, 0, 0, 0.0)
