from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class MountSample(_message.Message):
    __slots__ = ("station_id", "time_utc_ns", "azimuth_rad", "elevation_rad", "azimuth_rate_rad_per_s", "elevation_rate_rad_per_s", "mode", "sun_interlock_active", "sun_separation_rad")
    class Mode(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        MODE_UNSPECIFIED: _ClassVar[MountSample.Mode]
        MODE_IDLE: _ClassVar[MountSample.Mode]
        MODE_SLEWING: _ClassVar[MountSample.Mode]
        MODE_TRACKING: _ClassVar[MountSample.Mode]
        MODE_PARKED: _ClassVar[MountSample.Mode]
        MODE_FAULT: _ClassVar[MountSample.Mode]
    MODE_UNSPECIFIED: MountSample.Mode
    MODE_IDLE: MountSample.Mode
    MODE_SLEWING: MountSample.Mode
    MODE_TRACKING: MountSample.Mode
    MODE_PARKED: MountSample.Mode
    MODE_FAULT: MountSample.Mode
    STATION_ID_FIELD_NUMBER: _ClassVar[int]
    TIME_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    AZIMUTH_RAD_FIELD_NUMBER: _ClassVar[int]
    ELEVATION_RAD_FIELD_NUMBER: _ClassVar[int]
    AZIMUTH_RATE_RAD_PER_S_FIELD_NUMBER: _ClassVar[int]
    ELEVATION_RATE_RAD_PER_S_FIELD_NUMBER: _ClassVar[int]
    MODE_FIELD_NUMBER: _ClassVar[int]
    SUN_INTERLOCK_ACTIVE_FIELD_NUMBER: _ClassVar[int]
    SUN_SEPARATION_RAD_FIELD_NUMBER: _ClassVar[int]
    station_id: str
    time_utc_ns: int
    azimuth_rad: float
    elevation_rad: float
    azimuth_rate_rad_per_s: float
    elevation_rate_rad_per_s: float
    mode: MountSample.Mode
    sun_interlock_active: bool
    sun_separation_rad: float
    def __init__(self, station_id: _Optional[str] = ..., time_utc_ns: _Optional[int] = ..., azimuth_rad: _Optional[float] = ..., elevation_rad: _Optional[float] = ..., azimuth_rate_rad_per_s: _Optional[float] = ..., elevation_rate_rad_per_s: _Optional[float] = ..., mode: _Optional[_Union[MountSample.Mode, str]] = ..., sun_interlock_active: _Optional[bool] = ..., sun_separation_rad: _Optional[float] = ...) -> None: ...
