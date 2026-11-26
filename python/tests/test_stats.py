"""Basic tests for the statistics package."""

import numpy as np

from ics_models.stats.miss import miss_distance_summary
from ics_models.stats.pk import clopper_pearson_pk
from ics_models.stats.timeline import timeline_gates


def test_pk_interval() -> None:
    interval = clopper_pearson_pk(5, 10)
    assert 0.0 <= interval.lower <= interval.point <= interval.upper <= 1.0


def test_miss_summary() -> None:
    summary = miss_distance_summary(np.array([1.0, 2.0, 3.0]))
    assert summary.median_m == 2.0


def test_timeline_pass() -> None:
    result = timeline_gates({"acq": 1.0}, {"acq": 2.0})
    assert result.passed
