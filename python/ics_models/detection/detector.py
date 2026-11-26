"""Negative- and positive-contrast blob detection."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt

STD_EPS: float = 1e-6
DEFAULT_AREA_PX: float = 4.0


@dataclass(frozen=True)
class Detection:
    row: float
    col: float
    area_px: float
    contrast: float


def detect_blobs(image: npt.NDArray[np.float64], threshold: float = 3.0) -> list[Detection]:
    """Simple threshold blob detector for the reference."""
    mean = float(np.mean(image))
    std = float(np.std(image)) + STD_EPS
    mask = np.abs(image - mean) > threshold * std
    if not np.any(mask):
        return []
    idx = np.argmax(np.abs(image - mean))
    r, c = np.unravel_index(idx, image.shape)
    return [Detection(float(r), float(c), DEFAULT_AREA_PX, float((image[r, c] - mean) / std))]
