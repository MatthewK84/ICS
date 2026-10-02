"""Missions and commands for the SITL rig (ICS-018).

Both autopilots fly the same mission shape, uploaded with MAVLink's mission
protocol: take off, set the cruise speed, wait at the start point (an
unlimited loiter), then fly the path. The rig starts the engagement by moving
both vehicles' current mission item to the first path waypoint at once
(MISSION_SET_CURRENT, in vehicle.py).
ArduPilot keeps mission item 0 for its home position and ignores what is
uploaded there, so its mission begins with a placeholder.

Commands are resent every RETRY_S until the autopilot accepts them, and a
refused mission upload is started again. A refusal is not final: just after
boot ArduPilot has no mission storage yet, and an autopilot refuses to arm
until its estimator has converged, so the rig keeps asking until the
engagement's time limit and reports the last refusal if it runs out.
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import Final

from ics_sitl.messages import CommandAck, CommandLong, Message, MissionAck, MissionCount, MissionItemInt
from ics_sitl.profiles import Profile, Waypoint

RETRY_S: Final = 1.0
DEGREES_E7: Final = 1.0e7
# MAV_CMD values.
NAV_WAYPOINT: Final = 16
NAV_LOITER_UNLIM: Final = 17
NAV_TAKEOFF: Final = 22
DO_SET_MODE: Final = 176
DO_CHANGE_SPEED: Final = 178
MISSION_START: Final = 300
COMPONENT_ARM_DISARM: Final = 400
SET_MESSAGE_INTERVAL: Final = 511
# MAV_FRAME values: DO commands carry no position; positions are above home.
FRAME_MISSION: Final = 2
FRAME_GLOBAL_RELATIVE_ALT: Final = 3
# MAV_RESULT values.
RESULT_ACCEPTED: Final = 0
RESULT_IN_PROGRESS: Final = 5
# MAV_MISSION_RESULT: the upload was accepted.
MISSION_ACCEPTED: Final = 0
SPEED_TYPE_GROUND: Final = 1.0
NO_CHANGE: Final = -1.0


@dataclass(frozen=True)
class Target:
    """The autopilot a message is for."""

    system: int
    component: int


@dataclass(frozen=True)
class Item:
    """One mission item, before it is addressed to an autopilot."""

    command: int
    frame: int
    params: tuple[float, float, float, float]
    point: Waypoint | None = None


@dataclass(frozen=True)
class Plan:
    """A vehicle's mission, where it waits, and the items of its path."""

    items: tuple[Item, ...]
    start_seq: int
    path_seqs: tuple[int, ...]


@dataclass(frozen=True)
class Command:
    """A MAV_CMD and its first parameters; the rest are zero.

    A command that is not ``required`` is a request: a refusal ends it as an
    acceptance would. The rig asks for message streams this way, because PX4
    refuses an interval for a message it already sends unasked.
    """

    command: int
    params: tuple[float, ...] = ()
    required: bool = True


def plan_for(profile: Profile) -> Plan:
    """The mission a profile flies."""
    items: list[Item] = []
    if profile.autopilot == "ardupilot":
        items.append(Item(NAV_WAYPOINT, FRAME_GLOBAL_RELATIVE_ALT, (0.0, 0.0, 0.0, 0.0), profile.home))
    takeoff_point = Waypoint(profile.home.latitude_deg, profile.home.longitude_deg, profile.start.height_m)
    items.append(Item(NAV_TAKEOFF, FRAME_GLOBAL_RELATIVE_ALT, (0.0, 0.0, 0.0, math.nan), takeoff_point))
    items.append(Item(DO_CHANGE_SPEED, FRAME_MISSION, (SPEED_TYPE_GROUND, profile.speed_m_s, NO_CHANGE, 0.0)))
    start_seq = len(items)
    items.append(Item(NAV_LOITER_UNLIM, FRAME_GLOBAL_RELATIVE_ALT, (0.0, 0.0, 0.0, math.nan), profile.start))
    path_seqs = tuple(range(len(items), len(items) + len(profile.path)))
    items.extend(
        Item(NAV_WAYPOINT, FRAME_GLOBAL_RELATIVE_ALT, (0.0, 0.0, 0.0, math.nan), point) for point in profile.path
    )
    return Plan(tuple(items), start_seq, path_seqs)


def item_message(plan: Plan, seq: int, target: Target) -> MissionItemInt:
    """Mission item ``seq`` addressed to ``target``."""
    item = plan.items[seq]
    point = item.point or Waypoint(0.0, 0.0, 0.0)
    return MissionItemInt(
        target_system=target.system,
        target_component=target.component,
        seq=seq,
        frame=item.frame,
        command=item.command,
        current=0,
        autocontinue=1,
        param1=item.params[0],
        param2=item.params[1],
        param3=item.params[2],
        param4=item.params[3],
        x=round(point.latitude_deg * DEGREES_E7),
        y=round(point.longitude_deg * DEGREES_E7),
        z=point.height_m,
        mission_type=0,
    )


def command_message(command: Command, target: Target, confirmation: int) -> CommandLong:
    """A COMMAND_LONG; ``confirmation`` counts the resends."""
    params = (*command.params, *(0.0,) * (7 - len(command.params)))
    return CommandLong(target.system, target.component, command.command, confirmation % 256, *params)


class MissionUpload:
    """The ground-station side of MAVLink's mission upload."""

    def __init__(self, plan: Plan, target: Target) -> None:
        self.plan = plan
        self.target = target
        self.done = False
        self.refusal = ""
        self.last_activity = -math.inf

    def tick(self, now: float) -> list[Message]:
        """Announce the mission, again if the autopilot has gone quiet or refused it."""
        if self.done or now - self.last_activity < RETRY_S:
            return []
        self.last_activity = now
        return [MissionCount(self.target.system, self.target.component, len(self.plan.items), 0)]

    def requested(self, seq: int, now: float) -> list[Message]:
        """The item the autopilot asked for."""
        if self.done or not 0 <= seq < len(self.plan.items):
            return []
        self.last_activity = now
        return [item_message(self.plan, seq, self.target)]

    def acknowledged(self, ack: MissionAck, now: float) -> None:
        """Finish on acceptance; after a refusal, start again in RETRY_S."""
        if ack.type == MISSION_ACCEPTED:
            self.done = True
            return
        self.refusal = f"mission upload refused with MAV_MISSION_RESULT {ack.type}"
        self.last_activity = now


class CommandSequence:
    """Commands sent in order, each resent until the autopilot accepts it."""

    def __init__(self, commands: tuple[Command, ...], target: Target) -> None:
        self.commands = commands
        self.target = target
        self.index = 0
        self.attempts = 0
        self.sent_at = -math.inf
        self.refusal = ""

    @property
    def done(self) -> bool:
        return self.index >= len(self.commands)

    def tick(self, now: float) -> list[Message]:
        if self.done or now - self.sent_at < RETRY_S:
            return []
        self.sent_at = now
        self.attempts += 1
        return [command_message(self.commands[self.index], self.target, self.attempts - 1)]

    def acknowledged(self, ack: CommandAck) -> None:
        """Move to the next command once this one is accepted, or answered at all if it is a request."""
        if self.done or ack.command != self.commands[self.index].command or ack.result == RESULT_IN_PROGRESS:
            return
        if ack.result != RESULT_ACCEPTED and self.commands[self.index].required:
            self.refusal = f"MAV_CMD {ack.command} refused with MAV_RESULT {ack.result}"
            return
        self.index += 1
        self.attempts = 0
        self.sent_at = -math.inf
