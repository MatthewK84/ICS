"""Running one scripted engagement and judging it (ICS-018).

The controller keeps both vehicles' state, sends the ground station's
heartbeat, starts the engagement when both vehicles wait at their start
points, and tracks the closest approach between them from then on. It never
blocks or reads the clock: ``rig.run`` feeds it frames and the time.

An engagement passes when both vehicles fly every path waypoint before the
time limit (each autopilot reports reaching each one, and its track passes
within the waypoint tolerance), the closest approach falls within the planned
miss distance, and each autopilot sent every message the MAVLink adapter
(ICS-021) reads.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Final

from ics_sitl.geometry import distance_m
from ics_sitl.mavlink import Frame
from ics_sitl.messages import (
    AttitudeQuaternion,
    CommandAck,
    GlobalPositionInt,
    GpsRawInt,
    Heartbeat,
    Message,
    SystemTime,
)
from ics_sitl.profiles import Engagement
from ics_sitl.vehicle import Phase, Record, Recorder, Vehicle

HEARTBEAT_S: Final = 1.0
# The ground station's heartbeat: MAV_TYPE_GCS, MAV_AUTOPILOT_INVALID, MAV_STATE_ACTIVE, MAVLink 2.
GROUND_STATION: Final = Heartbeat(type=6, autopilot=8, base_mode=0, custom_mode=0, system_status=4, mavlink_version=3)
REQUIRED_MESSAGES: Final = tuple(
    kind.SPEC.name for kind in (Heartbeat, GlobalPositionInt, GpsRawInt, SystemTime, AttitudeQuaternion, CommandAck)
)


@dataclass(frozen=True)
class Outgoing:
    """A message for one autopilot."""

    autopilot: str
    message: Message


@dataclass(frozen=True)
class VehicleReport:
    """How far one vehicle got."""

    role: str
    autopilot: str
    phase: str
    reached: tuple[int, ...]
    nearest_m: tuple[float, ...]
    messages: tuple[str, ...]
    refusal: str


@dataclass(frozen=True)
class Verdict:
    """Whether the engagement passed, and why not if it did not."""

    engagement: str
    passed: bool
    failures: tuple[str, ...]
    closest_approach_m: float | None
    engaged_at_s: float | None
    vehicles: tuple[VehicleReport, ...]


class Controller:
    """Both vehicles of one engagement."""

    def __init__(self, engagement: Engagement, record: Recorder) -> None:
        self.engagement = engagement
        self.record = record
        self.vehicles = {profile.autopilot: Vehicle(profile, record) for profile in engagement.profiles()}
        self.next_heartbeat = 0.0
        self.engaged_at: float | None = None
        self.closest_m = math.inf

    def receive(self, autopilot: str, frame: Frame, now: float) -> list[Outgoing]:
        """Take a frame from one autopilot; return any immediate reply."""
        vehicle = self.vehicles[autopilot]
        replies = vehicle.receive(frame, now)
        if isinstance(frame.message, GlobalPositionInt) and self.engaged_at is not None:
            self.update_closest()
        return [Outgoing(autopilot, message) for message in replies]

    def tick(self, now: float) -> list[Outgoing]:
        """What is due to be sent now."""
        outgoing: list[Outgoing] = []
        if now >= self.next_heartbeat:
            self.next_heartbeat = now + HEARTBEAT_S
            outgoing.extend(Outgoing(autopilot, GROUND_STATION) for autopilot in self.vehicles)
        if self.engaged_at is None and all(vehicle.phase is Phase.READY for vehicle in self.vehicles.values()):
            self.engaged_at = now
            self.record(Record(now, "both", "both", "engage", {}))
            for vehicle in self.vehicles.values():
                vehicle.engage(now)
        for autopilot, vehicle in self.vehicles.items():
            outgoing.extend(Outgoing(autopilot, message) for message in vehicle.tick(now))
        return outgoing

    def finished(self, now: float) -> bool:
        """True when both vehicles are done or time is up."""
        if now >= self.engagement.time_limit_s:
            return True
        return all(vehicle.phase is Phase.DONE for vehicle in self.vehicles.values())

    def update_closest(self) -> None:
        positions = [vehicle.position for vehicle in self.vehicles.values()]
        first, second = positions
        if first is not None and second is not None:
            self.closest_m = min(self.closest_m, distance_m(first, second))

    def verdict(self) -> Verdict:
        """The engagement's result."""
        failures = [failure for vehicle in self.vehicles.values() for failure in vehicle_failures(self, vehicle)]
        failures.extend(approach_failures(self))
        closest = None if math.isinf(self.closest_m) else self.closest_m
        return Verdict(
            engagement=self.engagement.name,
            passed=not failures,
            failures=tuple(failures),
            closest_approach_m=closest,
            engaged_at_s=self.engaged_at,
            vehicles=tuple(report(vehicle) for vehicle in self.vehicles.values()),
        )


def vehicle_failures(controller: Controller, vehicle: Vehicle) -> list[str]:
    """What one vehicle failed to do."""
    who = f"{vehicle.profile.role} ({vehicle.profile.autopilot})"
    failures: list[str] = []
    if vehicle.phase is not Phase.DONE:
        reason = vehicle.refusal() or "no reason given"
        failures.append(f"{who} stopped while {vehicle.phase.value}: {reason}")
    tolerance = controller.engagement.waypoint_tolerance_m
    for index, seq in enumerate(vehicle.plan.path_seqs):
        if seq not in vehicle.reached:
            failures.append(f"{who} did not report reaching path waypoint {index}")
        if vehicle.nearest_m[index] > tolerance:
            failures.append(f"{who} passed {vehicle.nearest_m[index]:.1f} m from path waypoint {index}")
    missing = [name for name in REQUIRED_MESSAGES if name not in vehicle.messages_seen]
    if missing:
        failures.append(f"{who} never sent {', '.join(missing)}")
    return failures


def approach_failures(controller: Controller) -> list[str]:
    low, high = controller.engagement.miss_distance_m
    if math.isinf(controller.closest_m):
        return ["the vehicles never flew their paths together"]
    if not low <= controller.closest_m <= high:
        return [f"closest approach {controller.closest_m:.1f} m is outside the planned {low:.1f} to {high:.1f} m"]
    return []


def report(vehicle: Vehicle) -> VehicleReport:
    return VehicleReport(
        role=vehicle.profile.role,
        autopilot=vehicle.profile.autopilot,
        phase=vehicle.phase.value,
        reached=tuple(sorted(vehicle.reached)),
        nearest_m=tuple(vehicle.nearest_m),
        messages=tuple(sorted(vehicle.messages_seen)),
        refusal=vehicle.refusal(),
    )
