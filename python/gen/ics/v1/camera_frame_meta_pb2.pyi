from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class CameraFrameMeta(_message.Message):
    __slots__ = ("station_id", "camera_id", "camera_kind", "segment_id", "frame_index", "exposure_start_utc_ns", "exposure_duration_ns", "time_source", "time_offset_applied_ns", "width_px", "height_px", "bits_per_pixel", "window_x_px", "window_y_px")
    class CameraKind(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        CAMERA_KIND_UNSPECIFIED: _ClassVar[CameraFrameMeta.CameraKind]
        CAMERA_KIND_HIGH_SPEED_VISIBLE: _ClassVar[CameraFrameMeta.CameraKind]
        CAMERA_KIND_MWIR: _ClassVar[CameraFrameMeta.CameraKind]
        CAMERA_KIND_TRACKING: _ClassVar[CameraFrameMeta.CameraKind]
    CAMERA_KIND_UNSPECIFIED: CameraFrameMeta.CameraKind
    CAMERA_KIND_HIGH_SPEED_VISIBLE: CameraFrameMeta.CameraKind
    CAMERA_KIND_MWIR: CameraFrameMeta.CameraKind
    CAMERA_KIND_TRACKING: CameraFrameMeta.CameraKind
    class TimeSource(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        TIME_SOURCE_UNSPECIFIED: _ClassVar[CameraFrameMeta.TimeSource]
        TIME_SOURCE_IRIG: _ClassVar[CameraFrameMeta.TimeSource]
        TIME_SOURCE_HOST: _ClassVar[CameraFrameMeta.TimeSource]
    TIME_SOURCE_UNSPECIFIED: CameraFrameMeta.TimeSource
    TIME_SOURCE_IRIG: CameraFrameMeta.TimeSource
    TIME_SOURCE_HOST: CameraFrameMeta.TimeSource
    STATION_ID_FIELD_NUMBER: _ClassVar[int]
    CAMERA_ID_FIELD_NUMBER: _ClassVar[int]
    CAMERA_KIND_FIELD_NUMBER: _ClassVar[int]
    SEGMENT_ID_FIELD_NUMBER: _ClassVar[int]
    FRAME_INDEX_FIELD_NUMBER: _ClassVar[int]
    EXPOSURE_START_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    EXPOSURE_DURATION_NS_FIELD_NUMBER: _ClassVar[int]
    TIME_SOURCE_FIELD_NUMBER: _ClassVar[int]
    TIME_OFFSET_APPLIED_NS_FIELD_NUMBER: _ClassVar[int]
    WIDTH_PX_FIELD_NUMBER: _ClassVar[int]
    HEIGHT_PX_FIELD_NUMBER: _ClassVar[int]
    BITS_PER_PIXEL_FIELD_NUMBER: _ClassVar[int]
    WINDOW_X_PX_FIELD_NUMBER: _ClassVar[int]
    WINDOW_Y_PX_FIELD_NUMBER: _ClassVar[int]
    station_id: str
    camera_id: str
    camera_kind: CameraFrameMeta.CameraKind
    segment_id: str
    frame_index: int
    exposure_start_utc_ns: int
    exposure_duration_ns: int
    time_source: CameraFrameMeta.TimeSource
    time_offset_applied_ns: int
    width_px: int
    height_px: int
    bits_per_pixel: int
    window_x_px: int
    window_y_px: int
    def __init__(self, station_id: _Optional[str] = ..., camera_id: _Optional[str] = ..., camera_kind: _Optional[_Union[CameraFrameMeta.CameraKind, str]] = ..., segment_id: _Optional[str] = ..., frame_index: _Optional[int] = ..., exposure_start_utc_ns: _Optional[int] = ..., exposure_duration_ns: _Optional[int] = ..., time_source: _Optional[_Union[CameraFrameMeta.TimeSource, str]] = ..., time_offset_applied_ns: _Optional[int] = ..., width_px: _Optional[int] = ..., height_px: _Optional[int] = ..., bits_per_pixel: _Optional[int] = ..., window_x_px: _Optional[int] = ..., window_y_px: _Optional[int] = ...) -> None: ...
