"""Basic test for the reference detector."""

import numpy as np

from ics_models.detection.detector import detect_blobs


def test_detect_finds_blob() -> None:
    image = np.zeros((10, 10))
    image[5, 5] = 10.0
    detections = detect_blobs(image)
    assert len(detections) == 1
    assert detections[0].row == 5.0
