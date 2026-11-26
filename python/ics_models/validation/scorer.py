"""Compute C1–C19 accreditation criteria."""

from dataclasses import dataclass


@dataclass(frozen=True)
class CriterionResult:
    id: str
    passed: bool
    value: float
    threshold: float


def score_criteria(data: dict[str, float]) -> list[CriterionResult]:
    """Placeholder scorer that checks a few representative criteria."""
    results = [
        CriterionResult("C17", data.get("pointing_rms_urad", 20.0) <= 10.0, data.get("pointing_rms_urad", 20.0), 10.0),
        CriterionResult("C18", data.get("timetag_error_us", 100.0) <= 50.0, data.get("timetag_error_us", 100.0), 50.0),
    ]
    return results
