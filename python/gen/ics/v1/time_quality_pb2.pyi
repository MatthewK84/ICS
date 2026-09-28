from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TimeQuality(_message.Message):
    __slots__ = ("station_id", "time_utc_ns", "clock_state", "holdover_duration_ns", "gnss_satellite_count", "ptp_offset_ns", "ptp_path_delay_ns", "irig_b_locked", "error_bound_ns", "camera_offsets")
    class ClockState(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        CLOCK_STATE_UNSPECIFIED: _ClassVar[TimeQuality.ClockState]
        CLOCK_STATE_LOCKED: _ClassVar[TimeQuality.ClockState]
        CLOCK_STATE_HOLDOVER: _ClassVar[TimeQuality.ClockState]
        CLOCK_STATE_FREE_RUNNING: _ClassVar[TimeQuality.ClockState]
    CLOCK_STATE_UNSPECIFIED: TimeQuality.ClockState
    CLOCK_STATE_LOCKED: TimeQuality.ClockState
    CLOCK_STATE_HOLDOVER: TimeQuality.ClockState
    CLOCK_STATE_FREE_RUNNING: TimeQuality.ClockState
    class CameraOffset(_message.Message):
        __slots__ = ("camera_id", "offset_ns", "offset_sigma_ns", "measured_utc_ns")
        CAMERA_ID_FIELD_NUMBER: _ClassVar[int]
        OFFSET_NS_FIELD_NUMBER: _ClassVar[int]
        OFFSET_SIGMA_NS_FIELD_NUMBER: _ClassVar[int]
        MEASURED_UTC_NS_FIELD_NUMBER: _ClassVar[int]
        camera_id: str
        offset_ns: int
        offset_sigma_ns: int
        measured_utc_ns: int
        def __init__(self, camera_id: _Optional[str] = ..., offset_ns: _Optional[int] = ..., offset_sigma_ns: _Optional[int] = ..., measured_utc_ns: _Optional[int] = ...) -> None: ...
    STATION_ID_FIELD_NUMBER: _ClassVar[int]
    TIME_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    CLOCK_STATE_FIELD_NUMBER: _ClassVar[int]
    HOLDOVER_DURATION_NS_FIELD_NUMBER: _ClassVar[int]
    GNSS_SATELLITE_COUNT_FIELD_NUMBER: _ClassVar[int]
    PTP_OFFSET_NS_FIELD_NUMBER: _ClassVar[int]
    PTP_PATH_DELAY_NS_FIELD_NUMBER: _ClassVar[int]
    IRIG_B_LOCKED_FIELD_NUMBER: _ClassVar[int]
    ERROR_BOUND_NS_FIELD_NUMBER: _ClassVar[int]
    CAMERA_OFFSETS_FIELD_NUMBER: _ClassVar[int]
    station_id: str
    time_utc_ns: int
    clock_state: TimeQuality.ClockState
    holdover_duration_ns: int
    gnss_satellite_count: int
    ptp_offset_ns: int
    ptp_path_delay_ns: int
    irig_b_locked: bool
    error_bound_ns: int
    camera_offsets: _containers.RepeatedCompositeFieldContainer[TimeQuality.CameraOffset]
    def __init__(self, station_id: _Optional[str] = ..., time_utc_ns: _Optional[int] = ..., clock_state: _Optional[_Union[TimeQuality.ClockState, str]] = ..., holdover_duration_ns: _Optional[int] = ..., gnss_satellite_count: _Optional[int] = ..., ptp_offset_ns: _Optional[int] = ..., ptp_path_delay_ns: _Optional[int] = ..., irig_b_locked: _Optional[bool] = ..., error_bound_ns: _Optional[int] = ..., camera_offsets: _Optional[_Iterable[_Union[TimeQuality.CameraOffset, _Mapping]]] = ...) -> None: ...
