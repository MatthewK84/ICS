"""Negative- and positive-contrast blob detection."""

from dataclasses import dataclass

import numpy as np
import numpy.typing as npt


@dataclass(frozen=True)
class Detection:
    row: float
    col: float
    area_px: float
    contrast: float


def detect_blobs(image: npt.NDArray[np.float64], threshold: float = 3.0) -> list[Detection]:
    """Simple threshold blob detector for the reference."""
    mean = float(np.mean(image))
    std = float(np.std(image)) + 1e-6
    mask = np.abs(image - mean) > threshold * std
    # Placeholder: return a single detection at the strongest pixel
    if not np.any(mask):
        return []
    idx = np.argmax(np.abs(image - mean))
    r, c = np.unravel_index(idx, image.shape)
    return [Detection(float(r), float(c), 4.0, float((image[r, c] - mean) / std))]
