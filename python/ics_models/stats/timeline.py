"""Timeline gate checks."""

from dataclasses import dataclass


@dataclass(frozen=True)
class TimelineResult:
    passed: bool
    deltas_s: dict[str, float]


def timeline_gates(observed_s: dict[str, float], limits_s: dict[str, float]) -> TimelineResult:
    """Check whether observed times meet the gate limits."""
    deltas = {k: observed_s.get(k, 0.0) - limits_s.get(k, 0.0) for k in limits_s}
    passed = all(d <= 0.0 for d in deltas.values())
    return TimelineResult(passed=passed, deltas_s=deltas)
