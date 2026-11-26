"""Wind profile ingestion (to 1,000 m)."""

from dataclasses import dataclass
from pathlib import Path

import numpy as np
import numpy.typing as npt


@dataclass(frozen=True)
class WindProfile:
    """Height (m MSL) and east/north wind components (m/s)."""

    height_m: npt.NDArray[np.float64]
    east_mps: npt.NDArray[np.float64]
    north_mps: npt.NDArray[np.float64]

    def at(self, height_m: float) -> tuple[float, float]:
        """Linear interpolation of wind at a height."""
        if height_m <= self.height_m[0]:
            return float(self.east_mps[0]), float(self.north_mps[0])
        if height_m >= self.height_m[-1]:
            return float(self.east_mps[-1]), float(self.north_mps[-1])
        east = float(np.interp(height_m, self.height_m, self.east_mps))
        north = float(np.interp(height_m, self.height_m, self.north_mps))
        return east, north


def read_wind_profile(path: Path) -> WindProfile:
    """Read a CSV wind profile: height_m,east_mps,north_mps."""
    data = np.loadtxt(path, delimiter=",", skiprows=1)
    if data.ndim == 1:
        data = data.reshape(1, -1)
    return WindProfile(height_m=data[:, 0], east_mps=data[:, 1], north_mps=data[:, 2])
