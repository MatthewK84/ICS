"""The target's sky direction and rate, as an alpha-beta track (ICS-038).

Each axis is tracked on its own. A measurement first predicts the track to its
time at the track's rate; the residual, the measurement less the prediction
(the shorter way round in azimuth), then moves the angle by alpha times
itself and the rate by beta times itself over the time since the last. Alpha
in (0, 1] and beta in [0, 4 - 2 alpha) keep the track stable.
"""

from dataclasses import dataclass, replace

from ics_models.pointing.model import AltAz, wrap_pi
from ics_models.tracker.centroid import TrackerError
from ics_models.tracker.geometry import wrap_azimuth

NS_PER_S: float = 1e9
MAX_BETA_PLUS_TWO_ALPHA: float = 4.0


@dataclass(frozen=True)
class EstimatorSettings:
    """The track's gains."""

    alpha: float
    beta: float

    def __post_init__(self) -> None:
        """Refuse gains that would not keep the track stable."""
        if not (0.0 < self.alpha <= 1.0 and 0.0 <= self.beta < MAX_BETA_PLUS_TWO_ALPHA - 2.0 * self.alpha):
            message = f"alpha {self.alpha} and beta {self.beta} do not make a stable track"
            raise TrackerError(message)


@dataclass(frozen=True)
class AngleState:
    """A direction and its rates, in radians and radians a second, at a UTC time in nanoseconds."""

    time_ns: int
    azimuth: float
    elevation: float
    azimuth_rate: float
    elevation_rate: float

    def at(self, time_ns: int) -> "AngleState":
        """The state carried to another time at its rates."""
        seconds = (time_ns - self.time_ns) / NS_PER_S
        return replace(
            self,
            time_ns=time_ns,
            azimuth=wrap_azimuth(self.azimuth + self.azimuth_rate * seconds),
            elevation=self.elevation + self.elevation_rate * seconds,
        )


class AngleEstimator:
    """An alpha-beta track of a direction."""

    def __init__(self, settings: EstimatorSettings) -> None:
        """A track not yet started."""
        self._settings = settings
        self._state: AngleState | None = None

    def start(self, state: AngleState) -> None:
        """Start, or restart, the track from a state."""
        self._state = state

    def state(self) -> AngleState:
        """The track as of its last measurement."""
        if self._state is None:
            message = "the track has not started"
            raise TrackerError(message)
        return self._state

    def update(self, time_ns: int, measured: AltAz) -> AngleState:
        """Take a measurement later than the last, and return the track at its time."""
        last = self.state()
        if time_ns <= last.time_ns:
            message = f"a measurement at {time_ns} ns is not after the last, at {last.time_ns} ns"
            raise TrackerError(message)
        seconds = (time_ns - last.time_ns) / NS_PER_S
        predicted = last.at(time_ns)
        azimuth_residual = wrap_pi(measured.azimuth - predicted.azimuth)
        elevation_residual = measured.elevation - predicted.elevation
        alpha, beta = self._settings.alpha, self._settings.beta
        self._state = AngleState(
            time_ns,
            wrap_azimuth(predicted.azimuth + alpha * azimuth_residual),
            predicted.elevation + alpha * elevation_residual,
            predicted.azimuth_rate + beta * azimuth_residual / seconds,
            predicted.elevation_rate + beta * elevation_residual / seconds,
        )
        return self._state
