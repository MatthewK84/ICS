"""The PPS edge a pulse is timed from, and the delay from it (ICS-040).

The pulse generator is triggered by the station's PPS, and puts its pulse a
programmed delay after the edge. So the pulse's time does not depend on the
network or the host: only on the edge, which is a whole UTC second plus the
PPS's known offset, and on the delay.

A pulse is timed from the last edge at or before it, so its delay is under a
period. The generator fires on the first edge after it is armed, so it may be
armed for an edge only after the edge before has passed, by a guard, and it
must be armed a command margin before its own. When it is too late to arm for
a pulse's own edge, the pulse goes out late: on the first edge that can still
be armed for, with no delay.
"""

from dataclasses import dataclass

from ics_models.handoff.approach import HandoffError


@dataclass(frozen=True)
class ShotSettings:
    """The PPS and the margins around it, in nanoseconds."""

    period_ns: int
    offset_ns: int
    command_margin_ns: int
    guard_ns: int

    def __post_init__(self) -> None:
        """Refuse a period the margins do not fit in."""
        fits = self.command_margin_ns >= 0 and self.guard_ns >= 0
        if not (fits and self.period_ns > self.command_margin_ns + self.guard_ns):
            message = f"a period of {self.period_ns} ns does not hold the margins"
            raise HandoffError(message)


@dataclass(frozen=True)
class Shot:
    """A pulse: the edge it is timed from, the delay from it, and whether it is later than asked."""

    edge_ns: int
    delay_ns: int
    late: bool

    @property
    def fire_ns(self) -> int:
        """When the pulse goes out."""
        return self.edge_ns + self.delay_ns


def edge_at_or_before(time_ns: int, settings: ShotSettings) -> int:
    """The last PPS edge at or before a time."""
    return (time_ns - settings.offset_ns) // settings.period_ns * settings.period_ns + settings.offset_ns


def shot_for(fire_ns: int, now_ns: int, settings: ShotSettings) -> Shot:
    """The shot for a pulse asked for at fire_ns, as it can be made from now."""
    edge_ns = edge_at_or_before(fire_ns, settings)
    if now_ns + settings.command_margin_ns <= edge_ns:
        return Shot(edge_ns, fire_ns - edge_ns, late=False)
    reachable = edge_at_or_before(now_ns + settings.command_margin_ns - 1, settings) + settings.period_ns
    return Shot(reachable, 0, late=True)


def window_open(shot: Shot, now_ns: int, settings: ShotSettings) -> bool:
    """Whether the generator may be armed for a shot now: after the edge before it, and in time for its own."""
    opens_ns = shot.edge_ns - settings.period_ns + settings.guard_ns
    return opens_ns <= now_ns <= shot.edge_ns - settings.command_margin_ns
