"""Source-term table for M&S."""

from dataclasses import dataclass


@dataclass(frozen=True)
class SourceTerm:
    closing_speed_mps: float
    geometry: str
    target_class: str
    fragment_count: int
    mean_mass_kg: float


def source_term_table() -> list[SourceTerm]:
    """Reference table of representative source terms."""
    return [
        SourceTerm(50.0, "head-on", "group1", 12, 0.05),
        SourceTerm(100.0, "oblique", "group1", 20, 0.03),
        SourceTerm(30.0, "head-on", "group2", 8, 0.15),
    ]
