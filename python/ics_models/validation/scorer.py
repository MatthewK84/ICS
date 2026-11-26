"""Compute C1–C19 accreditation criteria."""

from dataclasses import dataclass

POINTING_THRESHOLD_URAD: float = 10.0
TIMETAG_THRESHOLD_US: float = 50.0


@dataclass(frozen=True)
class CriterionResult:
    id: str
    passed: bool
    value: float
    threshold: float


def score_criteria(data: dict[str, float]) -> list[CriterionResult]:
    """Placeholder scorer that checks a few representative criteria."""
    results = [
        CriterionResult(
            "C17",
            data.get("pointing_rms_urad", 20.0) <= POINTING_THRESHOLD_URAD,
            data.get("pointing_rms_urad", 20.0),
            POINTING_THRESHOLD_URAD,
        ),
        CriterionResult(
            "C18",
            data.get("timetag_error_us", 100.0) <= TIMETAG_THRESHOLD_US,
            data.get("timetag_error_us", 100.0),
            TIMETAG_THRESHOLD_US,
        ),
    ]
    return results
