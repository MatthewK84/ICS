"""Debris footprint Monte Carlo reference (ICS-051 / PY-06).

Ingests wind profiles, propagates fragments with sampled uncertainty,
and produces 50/90/99% containment regions. Coverage is checked on
synthetic truth generated from the ballistic model.
"""

from ics_models.footprint.propagate import FootprintResult, run_monte_carlo
from ics_models.footprint.regions import ContainmentRegions, compute_regions
from ics_models.footprint.wind import WindProfile, read_wind_profile

__all__ = (
    "ContainmentRegions",
    "FootprintResult",
    "WindProfile",
    "compute_regions",
    "read_wind_profile",
    "run_monte_carlo",
)
