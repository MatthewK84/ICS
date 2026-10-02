"""One vehicle's progress through an engagement (ICS-018).

A vehicle moves through its phases in order: wait for its autopilot's
heartbeat, ask for the messages the rig records, upload its mission, take off,
climb to its start point and wait there. When both vehicles wait, the
engagement starts them together; each then flies its path and is done when
its autopilot reports reaching the last waypoint.

``receive`` takes each frame from the autopilot and returns any immediate
reply; ``tick`` returns what is due to be sent. Neither blocks or reads the
clock, so the whole sequence can be driven by tests.
"""

from __future__ import annotations

import math
from collections.abc import Callable, Mapping
from dataclasses import dataclass
from enum import Enum
from types import MappingProxyType
from typing import Final

from ics_sitl.geometry import Position, distance_m
from ics_sitl.mavlink import Frame
from ics_sitl.messages import (
    AttitudeQuaternion,
    CommandAck,
    GlobalPositionInt,
    GpsRawInt,
    Heartbeat,
    Message,
    MissionAck,
    MissionCurrent,
    MissionItemReached,
    MissionRequest,
    MissionRequestInt,
    MissionSetCurrent,
    StatusText,
    SystemTime,
)
from ics_sitl.mission import (
    COMPONENT_ARM_DISARM,
    DEGREES_E7,
    DO_SET_MODE,
    MISSION_START,
    RESULT_ACCEPTED,
    RESULT_IN_PROGRESS,
    RETRY_S,
    SET_MESSAGE_INTERVAL,
    Command,
    CommandSequence,
    MissionUpload,
    Target,
    plan_for,
)
from ics_sitl.profiles import Profile, Waypoint

# MAV_AUTOPILOT values in each autopilot's heartbeat.
AUTOPILOT_IDS: Final = MappingProxyType({"px4": 12, "ardupilot": 3})
MAV_AUTOPILOT_INVALID: Final = 8
# ArduCopter's GUIDED flight mode, and MAV_MODE_FLAG_CUSTOM_MODE_ENABLED.
ARDUCOPTER_GUIDED: Final = 4.0
CUSTOM_MODE_ENABLED: Final = 1.0
# The messages the rig asks for, and their intervals in microseconds: the
# messages the MAVLink adapter (ICS-021) reads, and MISSION_CURRENT, which
# shows the engagement has started.
STREAMS: Final = (
    (GlobalPositionInt, 100_000.0),
    (GpsRawInt, 200_000.0),
    (AttitudeQuaternion, 100_000.0),
    (SystemTime, 1_000_000.0),
    (MissionCurrent, 1_000_000.0),
)
# PX4's MISSION_START switches to mission mode and arms. ArduCopter will not
# arm in AUTO, so it arms in GUIDED and MISSION_START then switches to AUTO.
START_COMMANDS: Final = MappingProxyType(
    {
        "px4": (Command(MISSION_START, (0.0, 0.0)),),
        "ardupilot": (
            Command(DO_SET_MODE, (CUSTOM_MODE_ENABLED, ARDUCOPTER_GUIDED)),
            Command(COMPONENT_ARM_DISARM, (1.0,)),
            Command(MISSION_START, (0.0, 0.0)),
        ),
    }
)
# Waiting at the start point: within this distance of it, and nearly still.
READY_DISTANCE_M: Final = 3.0
READY_SPEED_M_S: Final = 1.0
CM_PER_M: Final = 100.0
MM_PER_M: Final = 1000.0


class Phase(Enum):
    CONNECTING = "connecting"
    CONFIGURING = "configuring"
    UPLOADING = "uploading"
    STARTING = "starting"
    CLIMBING = "climbing"
    READY = "ready"
    ENGAGING = "engaging"
    FLYING = "flying"
    DONE = "done"


@dataclass(frozen=True)
class Record:
    """One line of the truth log: seconds since the run started, which vehicle, and what happened."""

    time_s: float
    role: str
    autopilot: str
    event: str
    values: Mapping[str, float | int | str]


type Recorder = Callable[[Record], None]


class Vehicle:
    """A vehicle's state through one engagement."""

    def __init__(self, profile: Profile, record: Recorder) -> None:
        self.profile = profile
        self.plan = plan_for(profile)
        self.record = record
        self.phase = Phase.CONNECTING
        self.target: Target | None = None
        self.commands: CommandSequence | None = None
        self.upload: MissionUpload | None = None
        self.position: Position | None = None
        self.speed_m_s = math.inf
        self.reached: set[int] = set()
        self.nearest_m = [math.inf] * len(profile.path)
        self.messages_seen: set[str] = set()
        self.last_text = ""
        self.current_seq = -1
        self.engage_sent_at = -math.inf

    def receive(self, frame: Frame, now: float) -> list[Message]:
        """Take one frame from the autopilot; return any immediate reply."""
        message = frame.message
        self.messages_seen.add(message.SPEC.name)
        if isinstance(message, Heartbeat):
            self.heard(frame, message, now)
        elif isinstance(message, GlobalPositionInt):
            self.moved(message, now)
        elif isinstance(message, CommandAck):
            self.answered(message, now)
        elif isinstance(message, MissionRequest | MissionRequestInt) and self.upload is not None:
            return self.upload.requested(message.seq, now)
        elif isinstance(message, MissionAck) and self.upload is not None:
            self.upload.acknowledged(message, now)
        elif isinstance(message, MissionCurrent):
            self.current_seq = message.seq
        elif isinstance(message, MissionItemReached):
            self.reached.add(message.seq)
            self.log(now, "reached", {"seq": message.seq})
        elif isinstance(message, StatusText):
            self.last_text = message.string()
            self.log(now, "text", {"severity": message.severity, "text": self.last_text})
        return []

    def tick(self, now: float) -> list[Message]:
        """What is due to be sent, after moving to the next phase if this one is finished."""
        self.advance(now)
        if self.upload is not None and self.phase is Phase.UPLOADING:
            return self.upload.tick(now)
        if self.commands is not None and self.phase in (Phase.CONFIGURING, Phase.STARTING):
            return self.commands.tick(now)
        if self.phase is Phase.ENGAGING:
            return self.set_current(now)
        return []

    def engage(self, now: float) -> None:
        """Leave the start point for the first path waypoint."""
        if self.phase is Phase.READY:
            self.enter(Phase.ENGAGING, now)

    def set_current(self, now: float) -> list[Message]:
        """MISSION_SET_CURRENT to the first path waypoint, resent until MISSION_CURRENT shows it.

        Both autopilots take this message; PX4 refuses MAV_CMD_DO_SET_MISSION_CURRENT as unsupported.
        """
        if self.target is None or now - self.engage_sent_at < RETRY_S:
            return []
        self.engage_sent_at = now
        return [MissionSetCurrent(self.target.system, self.target.component, self.plan.path_seqs[0])]

    def heard(self, frame: Frame, heartbeat: Heartbeat, now: float) -> None:
        if self.phase is not Phase.CONNECTING or heartbeat.autopilot == MAV_AUTOPILOT_INVALID:
            return
        if heartbeat.autopilot != AUTOPILOT_IDS[self.profile.autopilot]:
            self.last_text = f"heartbeat names MAV_AUTOPILOT {heartbeat.autopilot}, not {self.profile.autopilot}"
            return
        self.target = Target(frame.system, frame.component)
        streams = tuple(
            Command(SET_MESSAGE_INTERVAL, (float(kind.SPEC.message_id), every), required=False)
            for kind, every in STREAMS
        )
        self.run_commands(Phase.CONFIGURING, streams, now)

    def answered(self, ack: CommandAck, now: float) -> None:
        """Pass an acknowledgement on, and log any refusal."""
        if ack.result not in (RESULT_ACCEPTED, RESULT_IN_PROGRESS):
            self.log(now, "refused", {"command": ack.command, "result": ack.result})
        if self.commands is not None:
            self.commands.acknowledged(ack)

    def moved(self, message: GlobalPositionInt, now: float) -> None:
        position = Position(message.lat / DEGREES_E7, message.lon / DEGREES_E7, message.relative_alt / MM_PER_M)
        self.position = position
        self.speed_m_s = math.hypot(message.vx, message.vy) / CM_PER_M
        values = {
            "time_boot_ms": message.time_boot_ms,
            "latitude_deg": position.latitude_deg,
            "longitude_deg": position.longitude_deg,
            "height_m": position.height_m,
            "msl_m": message.alt / MM_PER_M,
        }
        self.log(now, "position", values)
        # The track counts from the moment the engagement starts: the vehicle may move before it acknowledges.
        if self.phase in (Phase.ENGAGING, Phase.FLYING, Phase.DONE):
            for index, waypoint in enumerate(self.profile.path):
                self.nearest_m[index] = min(self.nearest_m[index], distance_m(position, as_position(waypoint)))

    def advance(self, now: float) -> None:
        """Move on from a phase whose work is finished."""
        commands_done = self.commands is not None and self.commands.done
        if self.phase is Phase.CONFIGURING and commands_done and self.target is not None:
            self.upload = MissionUpload(self.plan, self.target)
            self.enter(Phase.UPLOADING, now)
        elif self.phase is Phase.UPLOADING and self.upload is not None and self.upload.done:
            self.run_commands(Phase.STARTING, START_COMMANDS[self.profile.autopilot], now)
        elif self.phase is Phase.STARTING and commands_done:
            self.enter(Phase.CLIMBING, now)
        elif self.phase is Phase.CLIMBING and self.waiting_at_start():
            self.enter(Phase.READY, now)
        elif self.phase is Phase.ENGAGING and self.current_seq >= self.plan.path_seqs[0]:
            self.enter(Phase.FLYING, now)
        elif self.phase is Phase.FLYING and self.plan.path_seqs[-1] in self.reached:
            self.enter(Phase.DONE, now)

    def waiting_at_start(self) -> bool:
        if self.position is None:
            return False
        near = distance_m(self.position, as_position(self.profile.start)) <= READY_DISTANCE_M
        return near and self.speed_m_s <= READY_SPEED_M_S

    def run_commands(self, phase: Phase, commands: tuple[Command, ...], now: float) -> None:
        if self.target is not None:
            self.commands = CommandSequence(commands, self.target)
            self.enter(phase, now)

    def enter(self, phase: Phase, now: float) -> None:
        self.phase = phase
        self.log(now, "phase", {"phase": phase.value})

    def log(self, now: float, event: str, values: Mapping[str, float | int | str]) -> None:
        self.record(Record(now, self.profile.role, self.profile.autopilot, event, values))

    def refusal(self) -> str:
        """The last reason the autopilot gave for refusing, for a failure report."""
        refused = self.commands.refusal if self.commands is not None else ""
        upload = self.upload.refusal if self.upload is not None and not self.upload.done else ""
        return "; ".join(part for part in (upload, refused, self.last_text) if part)


def as_position(waypoint: Waypoint) -> Position:
    return Position(waypoint.latitude_deg, waypoint.longitude_deg, waypoint.height_m)
