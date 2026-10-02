"""Tests for running and judging an engagement (ICS-018), against fake autopilots.

Every message between the controller and the fakes goes through the MAVLink
codec, as it would over UDP.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import replace
from typing import Final

import pytest

from ics_sitl import mission
from ics_sitl.engagement import Controller, Outgoing, Verdict
from ics_sitl.mavlink import Frame, decode, encode
from ics_sitl.messages import Message
from ics_sitl.profiles import Engagement, Profile, Waypoint
from ics_sitl.vehicle import Phase, Record
from tests.sitl_fakes import FakeAutopilot

STEP_S: Final = 0.1
# The two paths run 10 m apart in height.
PLANNED_MISS_M: Final = 10.0
SYSTEMS: Final = (("px4", 1), ("ardupilot", 2))
# Two vehicles that fly the same line north, 10 m apart in height.
TARGET: Final = Profile(
    role="target",
    autopilot="px4",
    home=Waypoint(40.0, -100.0005, 0.0),
    speed_m_s=4.0,
    start=Waypoint(40.0004, -100.0, 30.0),
    path=(Waypoint(40.002, -100.0, 30.0), Waypoint(40.004, -100.0, 30.0)),
)
INTERCEPTOR: Final = replace(
    TARGET,
    role="interceptor",
    autopilot="ardupilot",
    home=Waypoint(39.9995, -99.9995, 0.0),
    start=Waypoint(39.9995, -100.0, 40.0),
    path=(Waypoint(40.002, -100.0, 40.0), Waypoint(40.004, -100.0, 40.0)),
)
ENGAGEMENT: Final = Engagement(
    name="test",
    description="a test engagement",
    ground_msl_m=700.0,
    time_limit_s=60.0,
    miss_distance_m=(6.0, 20.0),
    waypoint_tolerance_m=1.0,
    target=TARGET,
    interceptor=INTERCEPTOR,
)


def carried(message: Message, system: int) -> Frame:
    """The frame the other side would decode."""
    decoded = decode(encode(message, 0, system, 1))
    assert len(decoded.frames) == 1
    return decoded.frames[0]


def deliver(controller: Controller, fakes: Mapping[str, FakeAutopilot], outgoing: list[Outgoing], now: float) -> None:
    """Pass messages to the fakes, and their replies back, until the exchange settles."""
    pending = list(outgoing)
    for _ in range(1000):
        if not pending:
            return
        item = pending.pop(0)
        fake = fakes[item.autopilot]
        for reply in fake.handle(carried(item.message, 255).message):
            pending.extend(controller.receive(item.autopilot, carried(reply, fake.system), now))
    raise AssertionError


def fly(
    engagement: Engagement, fakes: Mapping[str, FakeAutopilot], steps: int = 600
) -> tuple[Controller, Verdict, list[Record]]:
    """Run the controller against the fakes, a tenth of a second per step."""
    records: list[Record] = []
    controller = Controller(engagement, records.append)
    for step in range(steps):
        now = step * STEP_S
        if controller.finished(now):
            break
        for name, fake in fakes.items():
            for message in fake.tick(now):
                deliver(controller, fakes, controller.receive(name, carried(message, fake.system), now), now)
        deliver(controller, fakes, controller.tick(now), now)
    return controller, controller.verdict(), records


def fakes(**options: FakeAutopilot) -> dict[str, FakeAutopilot]:
    autopilots = {name: FakeAutopilot(name, system) for name, system in SYSTEMS}
    return autopilots | options


def test_a_clean_engagement_passes() -> None:
    controller, verdict, records = fly(ENGAGEMENT, fakes())
    assert verdict.failures == ()
    assert verdict.passed
    assert verdict.closest_approach_m is not None
    assert verdict.closest_approach_m == pytest.approx(PLANNED_MISS_M, abs=0.1)
    assert verdict.engaged_at_s is not None
    assert {report.phase for report in verdict.vehicles} == {"done"}
    assert all(vehicle.phase is Phase.DONE for vehicle in controller.vehicles.values())
    events = {record.event for record in records}
    assert {"phase", "position", "reached", "engage"} <= events


def test_uploads_the_mission_each_autopilot_expects() -> None:
    autopilots = fakes()
    fly(ENGAGEMENT, autopilots)
    px4 = [item.command for _, item in sorted(autopilots["px4"].items.items())]
    ardupilot = [item.command for _, item in sorted(autopilots["ardupilot"].items.items())]
    flight = [mission.NAV_TAKEOFF, mission.DO_CHANGE_SPEED, mission.NAV_LOITER_UNLIM]
    assert px4 == [*flight, mission.NAV_WAYPOINT, mission.NAV_WAYPOINT]
    assert ardupilot == [mission.NAV_WAYPOINT, *px4]
    speed = autopilots["px4"].items[1]
    assert (speed.param1, speed.param2, speed.frame) == (1.0, 4.0, mission.FRAME_MISSION)


def test_refusals_are_retried_until_accepted() -> None:
    refusing = FakeAutopilot("ardupilot", 2, refuse_once=frozenset({mission.COMPONENT_ARM_DISARM}), refuse_upload=True)
    _, verdict, records = fly(ENGAGEMENT, fakes(ardupilot=refusing))
    assert verdict.passed
    texts = [record.values["text"] for record in records if record.event == "text"]
    assert "PreArm: waiting for the fake" in texts
    refused = [record.values for record in records if record.event == "refused"]
    assert {"command": mission.COMPONENT_ARM_DISARM, "result": 1} in refused


def test_a_silent_autopilot_fails_with_where_it_stopped() -> None:
    silent = FakeAutopilot("px4", 1)
    silent.silent = True
    _, verdict, _ = fly(replace(ENGAGEMENT, time_limit_s=5.0), fakes(px4=silent))
    assert not verdict.passed
    assert "target (px4) stopped while connecting: no reason given" in verdict.failures
    assert "the vehicles never flew their paths together" in verdict.failures
    assert any("never sent HEARTBEAT" in failure for failure in verdict.failures)


def test_a_refusal_that_never_ends_is_reported() -> None:
    stubborn = FakeAutopilot("px4", 1, refuse_always=frozenset({mission.MISSION_START}))
    _, verdict, _ = fly(replace(ENGAGEMENT, time_limit_s=5.0), fakes(px4=stubborn))
    failure = next(failure for failure in verdict.failures if failure.startswith("target (px4) stopped"))
    assert "while starting" in failure
    assert "MAV_CMD 300 refused with MAV_RESULT 1" in failure
    assert "PreArm: waiting for the fake" in failure


def test_a_heartbeat_from_the_wrong_autopilot_is_not_accepted() -> None:
    crossed = FakeAutopilot("ardupilot", 1)
    _, verdict, _ = fly(replace(ENGAGEMENT, time_limit_s=3.0), fakes(px4=crossed))
    assert any("names MAV_AUTOPILOT 3, not px4" in failure for failure in verdict.failures)


def test_a_miss_outside_the_plan_fails() -> None:
    _, verdict, _ = fly(replace(ENGAGEMENT, miss_distance_m=(0.0, 5.0)), fakes())
    assert verdict.failures == ("closest approach 10.0 m is outside the planned 0.0 to 5.0 m",)


def test_a_track_that_misses_its_waypoints_fails() -> None:
    stray = FakeAutopilot("px4", 1)
    stray.stray_e7 = 200
    _, verdict, _ = fly(ENGAGEMENT, fakes(px4=stray))
    assert verdict.failures == (
        "target (px4) passed 2.2 m from path waypoint 0",
        "target (px4) passed 2.2 m from path waypoint 1",
    )
