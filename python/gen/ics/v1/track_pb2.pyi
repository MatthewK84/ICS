from ics.v1 import common_pb2 as _common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Track(_message.Message):
    __slots__ = ("track_id", "role", "source", "entity_id", "states")
    class Source(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        SOURCE_UNSPECIFIED: _ClassVar[Track.Source]
        SOURCE_ENDGAME: _ClassVar[Track.Source]
        SOURCE_ASSOCIATION: _ClassVar[Track.Source]
        SOURCE_PLI: _ClassVar[Track.Source]
    SOURCE_UNSPECIFIED: Track.Source
    SOURCE_ENDGAME: Track.Source
    SOURCE_ASSOCIATION: Track.Source
    SOURCE_PLI: Track.Source
    TRACK_ID_FIELD_NUMBER: _ClassVar[int]
    ROLE_FIELD_NUMBER: _ClassVar[int]
    SOURCE_FIELD_NUMBER: _ClassVar[int]
    ENTITY_ID_FIELD_NUMBER: _ClassVar[int]
    STATES_FIELD_NUMBER: _ClassVar[int]
    track_id: str
    role: _common_pb2.EntityRole
    source: Track.Source
    entity_id: str
    states: _containers.RepeatedCompositeFieldContainer[TrackState]
    def __init__(self, track_id: _Optional[str] = ..., role: _Optional[_Union[_common_pb2.EntityRole, str]] = ..., source: _Optional[_Union[Track.Source, str]] = ..., entity_id: _Optional[str] = ..., states: _Optional[_Iterable[_Union[TrackState, _Mapping]]] = ...) -> None: ...

class TrackState(_message.Message):
    __slots__ = ("time_utc_ns", "position_enu_m", "velocity_enu_mps", "position_covariance_m2", "velocity_covariance_m2_per_s2")
    TIME_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    POSITION_ENU_M_FIELD_NUMBER: _ClassVar[int]
    VELOCITY_ENU_MPS_FIELD_NUMBER: _ClassVar[int]
    POSITION_COVARIANCE_M2_FIELD_NUMBER: _ClassVar[int]
    VELOCITY_COVARIANCE_M2_PER_S2_FIELD_NUMBER: _ClassVar[int]
    time_utc_ns: int
    position_enu_m: _common_pb2.EnuVector
    velocity_enu_mps: _common_pb2.EnuVector
    position_covariance_m2: _common_pb2.EnuCovariance
    velocity_covariance_m2_per_s2: _common_pb2.EnuCovariance
    def __init__(self, time_utc_ns: _Optional[int] = ..., position_enu_m: _Optional[_Union[_common_pb2.EnuVector, _Mapping]] = ..., velocity_enu_mps: _Optional[_Union[_common_pb2.EnuVector, _Mapping]] = ..., position_covariance_m2: _Optional[_Union[_common_pb2.EnuCovariance, _Mapping]] = ..., velocity_covariance_m2_per_s2: _Optional[_Union[_common_pb2.EnuCovariance, _Mapping]] = ...) -> None: ...
