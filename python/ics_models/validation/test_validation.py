"""Basic test for the validation scorer."""

from ics_models.validation.scorer import score_criteria


def test_score_returns_results() -> None:
    results = score_criteria({"pointing_rms_urad": 5.0, "timetag_error_us": 30.0})
    assert len(results) == 2
    assert all(r.passed for r in results)
