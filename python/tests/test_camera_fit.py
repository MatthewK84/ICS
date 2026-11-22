"""The camera fits (ICS-042): C3 on synthetic calibrations, parameter recovery, and what the fits refuse.

On 20 synthetic calibrations of each camera, each 12 views of the board and
40 sightings with 0.2 px of noise on each axis, a quarter of each held out,
the worst reprojection RMS on the target points held out, and on the
sightings held out, is at most C3's 0.5 px. Every parameter the joint fit
frees lies within 5 sigma of the truth, and at least 98% of them within 3 sigma.

Without the sightings the board alone cannot fix a narrow camera's focal
length, and the camera misses directions on the sky by pixels.
"""

from dataclasses import dataclass

import numpy as np
import pytest

from ics_models.camera.boresight import Boresight, Camera
from ics_models.camera.fit import (
    Setup,
    camera_target_rms,
    fit_boresight,
    fit_camera,
    fit_intrinsics,
    free_mask,
    intrinsics_vector,
    sighting_rms,
    target_rms,
    view_pixels,
)
from ics_models.camera.model import CameraError, Distortion, Intrinsics
from ics_models.camera.observations import TargetPoint, split_sightings, split_targets
from ics_models.camera.pose import Pose, View, homography, rotation_matrix, rotation_vector, seed_camera
from ics_models.camera.solve import covariance_at, solve
from ics_models.camera.synth import CAMERAS, synthetic_sightings, synthetic_targets, terms_for

RUNS: int = 20
VIEWS: int = 12
SIGHTINGS: int = 40
NOISE_PX: float = 0.2
C3_PX: float = 0.5
HELD_OUT: float = 0.25
WORST_SIGMA: float = 5.0
MOST_SIGMA: float = 3.0
MOST_SHARE: float = 0.98


@dataclass(frozen=True)
class Run:
    """One synthetic calibration: held-out RMS on the board and the sky, and each free parameter's error in sigma."""

    points_rms_px: float
    sightings_rms_px: float
    scores: tuple[float, ...]


def truth_vector(camera: Camera) -> list[float]:
    """A camera's parameters, in the joint fit's order."""
    b = camera.boresight
    return [*intrinsics_vector(camera.intrinsics), b.yaw, b.pitch, b.roll]


def calibrate(name: str, camera: Camera, seed: int) -> Run:
    """Calibrate a camera from synthetic data."""
    generator = np.random.default_rng(seed)
    points, _ = synthetic_targets(generator, camera.intrinsics, VIEWS, NOISE_PX)
    sightings = synthetic_sightings(generator, camera, SIGHTINGS, NOISE_PX)
    kept_points, held_points = split_targets(points, HELD_OUT, seed)
    kept_sightings, held_sightings = split_sightings(sightings, HELD_OUT, seed)
    setup = Setup(camera.intrinsics.width, camera.intrinsics.height, camera.boresight.mirrored, terms_for(name))
    fitted = fit_camera(kept_points, kept_sightings, setup)
    errors = np.array(truth_vector(fitted.camera)) - np.array(truth_vector(camera))
    scores = tuple(float(e / s) for e, s in zip(errors, fitted.sigmas, strict=True) if s > 0.0)
    return Run(camera_target_rms(held_points, fitted), sighting_rms(held_sightings, fitted.camera), scores)


@pytest.fixture(scope="module")
def runs() -> list[Run]:
    return [
        calibrate(name, camera, 1000 * index + run)
        for index, (name, camera) in enumerate(CAMERAS)
        for run in range(RUNS)
    ]


def test_c3_holds_on_every_calibration(runs: list[Run]) -> None:
    assert max(run.points_rms_px for run in runs) <= C3_PX
    assert max(run.sightings_rms_px for run in runs) <= C3_PX


def test_every_parameter_is_recovered_within_its_sigma(runs: list[Run]) -> None:
    scores = np.abs(np.concatenate([run.scores for run in runs]))
    assert float(scores.max()) < WORST_SIGMA
    assert float(np.mean(scores < MOST_SIGMA)) >= MOST_SHARE


def test_held_terms_stay_at_zero() -> None:
    name, camera = CAMERAS[0]
    generator = np.random.default_rng(7)
    points, _ = synthetic_targets(generator, camera.intrinsics, VIEWS, NOISE_PX)
    sightings = synthetic_sightings(generator, camera, SIGHTINGS, NOISE_PX)
    setup = Setup(camera.intrinsics.width, camera.intrinsics.height, False, terms_for(name))
    fitted = fit_camera(points, sightings, setup)
    held = fitted.camera.intrinsics.distortion
    assert (held.k2, held.k3, held.p1, held.p2) == (0.0, 0.0, 0.0, 0.0)
    assert fitted.sigmas[5:9] == (0.0, 0.0, 0.0, 0.0)


def test_the_board_alone_misses_the_sky_on_a_narrow_camera() -> None:
    name, camera = CAMERAS[0]
    generator = np.random.default_rng(1)
    points, _ = synthetic_targets(generator, camera.intrinsics, VIEWS, NOISE_PX)
    sightings = synthetic_sightings(generator, camera, SIGHTINGS, NOISE_PX)
    alone = fit_intrinsics(points, camera.intrinsics.width, camera.intrinsics.height, terms_for(name))
    assert target_rms(points, alone) <= C3_PX
    bore = fit_boresight(sightings, alone.intrinsics, False)
    assert sighting_rms(sightings, Camera(alone.intrinsics, bore.boresight)) > 1.0


def test_unknown_distortion_terms_are_refused() -> None:
    with pytest.raises(CameraError, match="not Brown-Conrady terms: k4"):
        free_mask(("k1", "k4"), 9)


def test_a_sighting_behind_the_camera_is_refused() -> None:
    camera = CAMERAS[1][1]
    sightings = synthetic_sightings(np.random.default_rng(2), camera, 5, NOISE_PX)
    flipped = Camera(camera.intrinsics, Boresight(3.0, 0.0, 0.0, camera.boresight.mirrored))
    with pytest.raises(CameraError, match="sighting behind"):
        sighting_rms(sightings, flipped)


def test_a_fit_needs_sightings() -> None:
    with pytest.raises(CameraError, match="needs sightings"):
        fit_boresight([], CAMERAS[1][1].intrinsics, False)


def test_rotation_vectors_round_trip() -> None:
    for rotation in ((0.0, 0.0, 0.0), (0.3, -0.2, 0.1), (1e-14, 0.0, 0.0), (0.0, 2.5, 0.0)):
        back = rotation_vector(rotation_matrix(rotation))
        assert back == pytest.approx(rotation, abs=1e-12)


def test_too_few_views_are_refused() -> None:
    with pytest.raises(CameraError, match="at least 3 views"):
        seed_camera([np.eye(3), np.eye(3)], 640, 512)


def test_views_all_square_on_cannot_seed_the_focal_lengths() -> None:
    camera = dict(CAMERAS)["wide"].intrinsics
    flat = Intrinsics(camera.width, camera.height, camera.fx, camera.fy, camera.cx, camera.cy, Distortion())
    board = np.array([[x, y] for y in (-0.3, 0.0, 0.3) for x in (-0.4, 0.0, 0.4)])
    views = [
        View(board, view_pixels(View(board, board), Pose((0.0, 0.0, spin), (0.0, 0.0, 3.0)), flat))
        for spin in (0.0, 0.5, 1.0)
    ]
    with pytest.raises(CameraError, match="too alike"):
        seed_camera([homography(view) for view in views], flat.width, flat.height)


def test_a_view_in_one_place_is_refused() -> None:
    with pytest.raises(CameraError, match="one place"):
        homography(View(np.zeros((4, 2)), np.zeros((4, 2))))


def test_held_out_points_need_their_view_fitted() -> None:
    camera = dict(CAMERAS)["mwir"]
    points, _ = synthetic_targets(np.random.default_rng(3), camera.intrinsics, 4, NOISE_PX)
    fitted = fit_intrinsics(points, camera.intrinsics.width, camera.intrinsics.height, ("k1",))
    stray = [TargetPoint("unseen", 0, 0.0, 0.0, 10.0, 10.0)]
    with pytest.raises(CameraError, match="did not see: unseen"):
        target_rms(stray, fitted)


def test_a_fit_without_spare_residuals_is_refused() -> None:
    with pytest.raises(CameraError, match="2 residuals for 2 parameters"):
        covariance_at(np.zeros(2), np.eye(2))


def test_a_fit_that_will_not_settle_is_refused() -> None:
    # Each Gauss-Newton step adds 1 to s and cuts the cost by e squared: from
    # 1e300 it falls by most of itself for far more than 200 iterations.
    with pytest.raises(CameraError, match="did not converge"):
        solve(
            lambda s: np.array([1e150 * np.exp(-s[0]), 0.0]),
            lambda s: np.array([[-1e150 * np.exp(-s[0])], [0.0]]),
            np.zeros(1),
        )
