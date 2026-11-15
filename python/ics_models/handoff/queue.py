"""The engagements in the order they will happen, and the one a station serves (ICS-039).

A run plan lists its engagements. Each update takes what the station knows of
every engagement now, a sighting: its predicted closest approach, when there
is one, and the target's direction, when the station has it.

- Done. An engagement is done when the approach it held has passed, within
  the miss distance that makes it an engagement. A wider pass is a fly-by,
  and the engagement goes on waiting for another approach. A sighting loses
  its approach as the two vehicles pass, so an approach within the margin of
  now is held until it passes.
- Order. Engagements with an approach ahead come first, soonest first; those
  without follow, in the plan's order.
- Serving. The station serves the first in the order. It stays on a done
  engagement for the dwell, to see what the engagement did. It leaves one
  that is not done only for an engagement more than the margin sooner, so
  noise cannot flip it, or for any with an approach when its own has none.
- Flags. An engagement that follows the one before it by less than the
  minimum gap is too close. One the mount cannot reach in the gap, after the
  dwell, the slew and the time to settle, is out of reach. The one before the
  first in the order is the engagement the station last served to its end. A
  done engagement keeps the flags it had.
"""

import itertools
from collections.abc import Sequence
from dataclasses import dataclass, replace

from ics_models.handoff.approach import Approach, HandoffError
from ics_models.handoff.slew import SlewLimits, reach_seconds
from ics_models.pointing.model import AltAz
from ics_models.tracker.estimator import NS_PER_S

MAX_ENGAGEMENTS: int = 8


@dataclass(frozen=True)
class QueueSettings:
    """The queue's times, in nanoseconds, and the miss distance that makes an engagement, in metres."""

    min_gap_ns: int
    dwell_ns: int
    margin_ns: int
    settle_ns: int
    engaged_miss_m: float

    def __post_init__(self) -> None:
        """Refuse a time that is negative, and a miss distance that is not positive."""
        times = (self.min_gap_ns, self.dwell_ns, self.margin_ns, self.settle_ns)
        if min(times) < 0 or not self.engaged_miss_m > 0.0:
            message = f"times of {times} ns and a miss distance of {self.engaged_miss_m} m cannot be used"
            raise HandoffError(message)


@dataclass(frozen=True)
class Sighting:
    """What a station knows of an engagement now: its approach, and its target's direction."""

    approach: Approach | None
    direction: AltAz | None


@dataclass(frozen=True)
class Slot:
    """An engagement's place in the queue. A done engagement's approach is the one it had."""

    done: bool = False
    approach: Approach | None = None
    too_close: bool = False
    out_of_reach: bool = False


@dataclass(frozen=True)
class Plan:
    """The queue after an update: every slot, the engagements not done, next first, and the one served."""

    slots: tuple[Slot, ...]
    order: tuple[int, ...]
    current: int | None
    dwelling: bool


def moved_on(slot: Slot, sighting: Sighting, now_ns: int, settings: QueueSettings) -> Slot:
    """A slot not done, after a sighting: done if its approach has passed near enough, else with the approach ahead."""
    had = slot.approach
    if had is not None and had.time_ns <= now_ns and had.miss_m <= settings.engaged_miss_m:
        return replace(slot, done=True)
    seen = sighting.approach
    ahead = seen if seen is not None and seen.time_ns > now_ns else None
    if ahead is None and had is not None and now_ns < had.time_ns <= now_ns + settings.margin_ns:
        ahead = had
    return replace(slot, approach=ahead)


def ordered(slots: Sequence[Slot]) -> tuple[int, ...]:
    """The engagements not done: those with an approach, soonest first, then the rest in the plan's order."""
    timed = [(slot.approach.time_ns, index) for index, slot in enumerate(slots) if not slot.done and slot.approach]
    waiting = [index for index, slot in enumerate(slots) if not slot.done and slot.approach is None]
    return (*(index for _, index in sorted(timed)), *waiting)


def sooner(first: Slot, second: Slot, margin_ns: int) -> bool:
    """Whether the first engagement's approach is more than a margin before the second's, or the second has none."""
    if first.approach is None:
        return False
    return second.approach is None or first.approach.time_ns + margin_ns < second.approach.time_ns


def flagged(slot: Slot, before: Slot, reach_s: float | None, settings: QueueSettings) -> Slot:
    """A slot with its flags, for the engagement served before it and the seconds the mount takes between them."""
    if slot.approach is None or before.approach is None:
        return replace(slot, too_close=False, out_of_reach=False)
    gap_ns = slot.approach.time_ns - before.approach.time_ns
    needed_s = None if reach_s is None else (settings.dwell_ns + settings.settle_ns) / NS_PER_S + reach_s
    out_of_reach = needed_s is not None and needed_s > gap_ns / NS_PER_S
    return replace(slot, too_close=gap_ns < settings.min_gap_ns, out_of_reach=out_of_reach)


class Queue:
    """A station's queue of a run plan's engagements."""

    def __init__(self, count: int, settings: QueueSettings, limits: SlewLimits) -> None:
        """A queue of count engagements, none with an approach yet."""
        if not 1 <= count <= MAX_ENGAGEMENTS:
            message = f"a run plan has 1 to {MAX_ENGAGEMENTS} engagements, not {count}"
            raise HandoffError(message)
        self._settings = settings
        self._limits = limits
        self._slots: tuple[Slot, ...] = tuple(Slot() for _ in range(count))
        self._current: int | None = None
        self._previous: int | None = None

    def update(self, now_ns: int, sightings: Sequence[Sighting]) -> Plan:
        """The plan at a time, for a sighting of each engagement."""
        if len(sightings) != len(self._slots):
            message = f"{len(sightings)} sightings were given for {len(self._slots)} engagements"
            raise HandoffError(message)
        slots = [
            slot if slot.done else moved_on(slot, sighting, now_ns, self._settings)
            for slot, sighting in zip(self._slots, sightings, strict=True)
        ]
        order = ordered(slots)
        dwelling = self._choose(slots, order, now_ns)
        before = self._current if dwelling else self._previous
        served = order if before is None else (before, *order)
        for first, second in itertools.pairwise(served):
            reach = self._reach(sightings[first], sightings[second])
            slots[second] = flagged(slots[second], slots[first], reach, self._settings)
        if before is None and order:
            slots[order[0]] = replace(slots[order[0]], too_close=False, out_of_reach=False)
        self._slots = tuple(slots)
        return Plan(self._slots, order, self._current, dwelling)

    def _choose(self, slots: Sequence[Slot], order: tuple[int, ...], now_ns: int) -> bool:
        """Sets the engagement served, and returns whether the station is dwelling on one that is done."""
        current = self._current
        if current is not None and slots[current].done:
            passed = slots[current].approach
            if passed is not None and now_ns < passed.time_ns + self._settings.dwell_ns:
                return True
            self._previous = current
            current = None
        head = order[0] if order else None
        if current is None or head is None or sooner(slots[head], slots[current], self._settings.margin_ns):
            current = head
        self._current = current
        return False

    def _reach(self, first: Sighting, second: Sighting) -> float | None:
        """The seconds the mount takes between two engagements' targets, when the station has both directions."""
        if first.direction is None or second.direction is None:
            return None
        return reach_seconds(first.direction, second.direction, self._limits)
