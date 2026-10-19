"""The reference centroider (ICS-038)."""

import numpy as np
import pytest

from ics_models.tracker.centroid import (
    CentroidSettings,
    Polarity,
    TrackerError,
    Window,
    background_of,
    blobs_of,
    contrast_of,
    find_target,
    window_around,
)

SKY: float = 100.0
PAIR_X: float = 7.25
PAIR_AREA: int = 2
NEAR_X: float = 25.0
FAR_X: float = 3.0
BORDER_LEVEL: float = 10.0
SETTINGS = CentroidSettings(Polarity.BRIGHT, 5.0, 10.0, 1, 50)


def flat(width: int, height: int) -> np.ndarray:
    """A frame of plain sky."""
    return np.full((height, width), SKY)


def test_a_bright_pixel_is_found_where_it_is() -> None:
    frame = flat(20, 10)
    frame[4, 7] = SKY + 50.0
    found = find_target(frame, Window(0, 0, 20, 10), (0.0, 0.0), SETTINGS)
    assert (found.x, found.y, found.area, found.peak) == (7.0, 4.0, 1, 50.0)
    assert (found.background, found.sigma) == (SKY, 0.0)


def test_the_centroid_weighs_each_pixel_by_its_contrast() -> None:
    frame = flat(20, 10)
    frame[4, 7] = SKY + 30.0
    frame[4, 8] = SKY + 10.0
    found = find_target(frame, Window(0, 0, 20, 10), (0.0, 0.0), CentroidSettings(Polarity.BRIGHT, 5.0, 5.0, 1, 50))
    assert found.x == pytest.approx(PAIR_X)
    assert found.area == PAIR_AREA


def test_the_blob_nearest_the_prediction_wins() -> None:
    frame = flat(30, 10)
    frame[2, 3] = frame[7, 25] = SKY + 40.0
    assert find_target(frame, Window(0, 0, 30, 10), (24.0, 6.0), SETTINGS).x == NEAR_X
    assert find_target(frame, Window(0, 0, 30, 10), (2.0, 2.0), SETTINGS).x == FAR_X


def test_blobs_join_across_corners_and_stop_at_the_window() -> None:
    mask = np.zeros((4, 4), dtype=np.bool_)
    mask[0, 0] = mask[1, 1] = mask[3, 3] = True
    assert [len(blob) for blob in blobs_of(mask)] == [2, 1]


def test_polarity_decides_what_stands_out() -> None:
    patch = np.array([[90.0, 110.0]])
    assert contrast_of(patch, SKY, Polarity.DARK).tolist() == [[10.0, -10.0]]
    assert contrast_of(patch, SKY, Polarity.BRIGHT).tolist() == [[-10.0, 10.0]]
    assert contrast_of(patch, SKY, Polarity.EITHER).tolist() == [[10.0, 10.0]]


def test_the_background_comes_from_the_border() -> None:
    patch = np.full((5, 5), 10.0)
    patch[0, :] = 12.0
    patch[2, 2] = 1000.0
    level, sigma = background_of(patch)
    assert level == BORDER_LEVEL
    assert sigma == 0.0


def test_blobs_outside_the_area_bounds_are_left_out() -> None:
    frame = flat(20, 10)
    frame[4, 7] = SKY + 50.0
    with pytest.raises(TrackerError, match="no target"):
        find_target(frame, Window(0, 0, 20, 10), (0.0, 0.0), CentroidSettings(Polarity.BRIGHT, 5.0, 10.0, 2, 50))


def test_a_window_must_lie_in_the_frame() -> None:
    frame = flat(20, 10)
    for window in (Window(0, 0, 2, 10), Window(0, 0, 20, 2), Window(-1, 0, 5, 5), Window(0, -1, 5, 5)):
        with pytest.raises(TrackerError, match="too small or starts outside"):
            find_target(frame, window, (0.0, 0.0), SETTINGS)
    for window in (Window(16, 0, 5, 5), Window(0, 6, 5, 5)):
        with pytest.raises(TrackerError, match="ends outside"):
            find_target(frame, window, (0.0, 0.0), SETTINGS)


def test_windows_centre_on_the_prediction_and_stay_in_the_frame() -> None:
    assert window_around((10.4, 5.6), (5, 3), (20, 10)) == Window(8, 5, 5, 3)
    assert window_around((0.0, 9.9), (5, 3), (20, 10)) == Window(0, 7, 5, 3)
    assert window_around((19.9, 0.0), (20, 10), (20, 10)) == Window(0, 0, 20, 10)


def test_a_window_must_fit_and_its_centre_be_in_the_frame() -> None:
    for size in ((2, 5), (5, 2), (21, 5), (5, 11)):
        with pytest.raises(TrackerError, match="does not fit"):
            window_around((5.0, 5.0), size, (20, 10))
    for centre in ((-0.5, 5.0), (5.0, 10.0), (20.0, 5.0), (float("nan"), 5.0), (5.0, float("inf"))):
        with pytest.raises(TrackerError, match="outside the frame"):
            window_around(centre, (5, 5), (20, 10))
