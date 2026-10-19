"""The target in a window of a frame (ICS-038).

The window's border gives the sky background and its noise: the median, and
1.4826 times the median absolute deviation. A pixel stands out when its
contrast with the background, by the target's polarity, passes the larger of
k_sigma noise sigmas and min_contrast counts. Pixels that stand out and touch,
across a side or a corner, form a blob. Of the blobs whose area is within
bounds, the one whose centroid is nearest the prediction is the target; its
centroid weights each pixel by its contrast. Pixel centres are at whole
numbers, x to the right and y down.
"""

import math
from dataclasses import dataclass
from enum import StrEnum

import numpy as np
import numpy.typing as npt

Image = npt.NDArray[np.float64]

# The median absolute deviation of Gaussian noise, in sigmas.
MAD_TO_SIGMA: float = 1.4826
# The smallest window: its border needs pixels around an interior.
MIN_SIDE: int = 3
# The eight neighbours of a pixel, as (row, column) steps.
NEIGHBOURS: tuple[tuple[int, int], ...] = ((-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1))


class TrackerError(ValueError):
    """An input the tracker cannot use, or a target it cannot find."""


class Polarity(StrEnum):
    """How the target differs from the sky."""

    DARK = "dark"
    BRIGHT = "bright"
    EITHER = "either"


@dataclass(frozen=True)
class Window:
    """A rectangle of a frame: its top-left pixel and its size."""

    x: int
    y: int
    width: int
    height: int


@dataclass(frozen=True)
class CentroidSettings:
    """How a target is told from the sky."""

    polarity: Polarity
    k_sigma: float
    min_contrast: float
    min_area: int
    max_area: int


@dataclass(frozen=True)
class Detection:
    """A target found: its centroid in frame pixels, area, peak contrast, and the background it stood out from."""

    x: float
    y: float
    area: int
    peak: float
    background: float
    sigma: float


def window_around(centre: tuple[float, float], size: tuple[int, int], frame: tuple[int, int]) -> Window:
    """A window of size (width, height) centred on the pixel nearest centre (x, y), moved inside the frame."""
    (x, y), (width, height), (frame_width, frame_height) = centre, size, frame
    if width < MIN_SIDE or height < MIN_SIDE or width > frame_width or height > frame_height:
        message = f"a {width} by {height} window does not fit a {frame_width} by {frame_height} frame"
        raise TrackerError(message)
    if not (math.isfinite(x) and math.isfinite(y)) or not (0.0 <= x < frame_width and 0.0 <= y < frame_height):
        message = f"({x}, {y}) is outside the frame"
        raise TrackerError(message)
    left = min(max(round(x) - width // 2, 0), frame_width - width)
    top = min(max(round(y) - height // 2, 0), frame_height - height)
    return Window(left, top, width, height)


def border_of(patch: Image) -> Image:
    """The window's edge pixels: its top and bottom rows, then its sides between them."""
    return np.concatenate((patch[0, :], patch[-1, :], patch[1:-1, 0], patch[1:-1, -1]))


def background_of(patch: Image) -> tuple[float, float]:
    """The sky's level and its noise, from the window's border."""
    edge = border_of(patch)
    level = float(np.median(edge))
    return level, MAD_TO_SIGMA * float(np.median(np.abs(edge - level)))


def contrast_of(patch: Image, background: float, polarity: Polarity) -> Image:
    """How far each pixel stands out from the background, in the target's polarity."""
    if polarity is Polarity.DARK:
        return background - patch
    if polarity is Polarity.BRIGHT:
        return patch - background
    return np.abs(patch - background)


def blob_at(mask: npt.NDArray[np.bool_], seen: npt.NDArray[np.bool_], row: int, column: int) -> list[tuple[int, int]]:
    """The pixels of the blob that holds (row, column), in the order a stack visits them; marks them seen."""
    rows, columns = mask.shape
    pixels: list[tuple[int, int]] = []
    stack = [(row, column)]
    seen[row, column] = True
    while stack:
        here = stack.pop()
        pixels.append(here)
        for step_row, step_column in NEIGHBOURS:
            near_row, near_column = here[0] + step_row, here[1] + step_column
            inside = 0 <= near_row < rows and 0 <= near_column < columns
            if inside and mask[near_row, near_column] and not seen[near_row, near_column]:
                seen[near_row, near_column] = True
                stack.append((near_row, near_column))
    return pixels


def blobs_of(mask: npt.NDArray[np.bool_]) -> list[list[tuple[int, int]]]:
    """Every blob in the mask, in the order of its first pixel, row by row."""
    seen = np.zeros(mask.shape, dtype=np.bool_)
    found: list[list[tuple[int, int]]] = []
    for row, column in zip(*np.nonzero(mask), strict=True):
        if not seen[row, column]:
            found.append(blob_at(mask, seen, int(row), int(column)))
    return found


def centroid_of(pixels: list[tuple[int, int]], contrast: Image) -> tuple[float, float, float]:
    """A blob's contrast-weighted centroid, in window pixels, and its peak contrast."""
    total = weighted_x = weighted_y = 0.0
    peak = -math.inf
    for row, column in pixels:
        weight = float(contrast[row, column])
        total += weight
        weighted_x += weight * column
        weighted_y += weight * row
        peak = max(peak, weight)
    return weighted_x / total, weighted_y / total, peak


def find_target(frame: Image, window: Window, prediction: tuple[float, float], settings: CentroidSettings) -> Detection:
    """The target in a window of a frame: the blob in bounds whose centroid is nearest the prediction."""
    frame_height, frame_width = frame.shape
    if window.width < MIN_SIDE or window.height < MIN_SIDE or window.x < 0 or window.y < 0:
        message = f"window {window} is too small or starts outside the frame"
        raise TrackerError(message)
    if window.x + window.width > frame_width or window.y + window.height > frame_height:
        message = f"window {window} ends outside a {frame_width} by {frame_height} frame"
        raise TrackerError(message)
    patch = frame[window.y : window.y + window.height, window.x : window.x + window.width].astype(np.float64)
    background, sigma = background_of(patch)
    contrast = contrast_of(patch, background, settings.polarity)
    threshold = max(settings.k_sigma * sigma, settings.min_contrast)
    best: Detection | None = None
    best_distance = math.inf
    for pixels in blobs_of(contrast > threshold):
        if not settings.min_area <= len(pixels) <= settings.max_area:
            continue
        column, row, peak = centroid_of(pixels, contrast)
        x, y = window.x + column, window.y + row
        distance = math.hypot(x - prediction[0], y - prediction[1])
        if distance < best_distance:
            best, best_distance = Detection(x, y, len(pixels), peak, background, sigma), distance
    if best is None:
        message = f"no target in window {window}"
        raise TrackerError(message)
    return best
