from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Sample(_message.Message):
    __slots__ = ("time_utc_ns", "kind", "values", "position", "note", "reading_count")
    class Kind(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        KIND_UNSPECIFIED: _ClassVar[Sample.Kind]
        KIND_RANGE: _ClassVar[Sample.Kind]
        KIND_ANGLE: _ClassVar[Sample.Kind]
    KIND_UNSPECIFIED: Sample.Kind
    KIND_RANGE: Sample.Kind
    KIND_ANGLE: Sample.Kind
    class Position(_message.Message):
        __slots__ = ("east_m", "north_m", "up_m")
        EAST_M_FIELD_NUMBER: _ClassVar[int]
        NORTH_M_FIELD_NUMBER: _ClassVar[int]
        UP_M_FIELD_NUMBER: _ClassVar[int]
        east_m: float
        north_m: float
        up_m: float
        def __init__(self, east_m: _Optional[float] = ..., north_m: _Optional[float] = ..., up_m: _Optional[float] = ...) -> None: ...
    TIME_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    KIND_FIELD_NUMBER: _ClassVar[int]
    VALUES_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    NOTE_FIELD_NUMBER: _ClassVar[int]
    READING_COUNT_FIELD_NUMBER: _ClassVar[int]
    time_utc_ns: int
    kind: Sample.Kind
    values: _containers.RepeatedScalarFieldContainer[float]
    position: Sample.Position
    note: str
    reading_count: int
    def __init__(self, time_utc_ns: _Optional[int] = ..., kind: _Optional[_Union[Sample.Kind, str]] = ..., values: _Optional[_Iterable[float]] = ..., position: _Optional[_Union[Sample.Position, _Mapping]] = ..., note: _Optional[str] = ..., reading_count: _Optional[int] = ...) -> None: ...
