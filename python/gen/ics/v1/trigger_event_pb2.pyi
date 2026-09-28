from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TriggerEvent(_message.Message):
    __slots__ = ("sequence_id", "time_utc_ns", "kind", "target_entity_id", "interceptor_entity_id", "predicted_closest_approach_utc_ns", "predicted_miss_distance_m", "channels", "detail")
    class Kind(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        KIND_UNSPECIFIED: _ClassVar[TriggerEvent.Kind]
        KIND_ARMED: _ClassVar[TriggerEvent.Kind]
        KIND_PREDICTED: _ClassVar[TriggerEvent.Kind]
        KIND_FIRED: _ClassVar[TriggerEvent.Kind]
        KIND_DISARMED: _ClassVar[TriggerEvent.Kind]
        KIND_FAULT: _ClassVar[TriggerEvent.Kind]
    KIND_UNSPECIFIED: TriggerEvent.Kind
    KIND_ARMED: TriggerEvent.Kind
    KIND_PREDICTED: TriggerEvent.Kind
    KIND_FIRED: TriggerEvent.Kind
    KIND_DISARMED: TriggerEvent.Kind
    KIND_FAULT: TriggerEvent.Kind
    SEQUENCE_ID_FIELD_NUMBER: _ClassVar[int]
    TIME_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    KIND_FIELD_NUMBER: _ClassVar[int]
    TARGET_ENTITY_ID_FIELD_NUMBER: _ClassVar[int]
    INTERCEPTOR_ENTITY_ID_FIELD_NUMBER: _ClassVar[int]
    PREDICTED_CLOSEST_APPROACH_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    PREDICTED_MISS_DISTANCE_M_FIELD_NUMBER: _ClassVar[int]
    CHANNELS_FIELD_NUMBER: _ClassVar[int]
    DETAIL_FIELD_NUMBER: _ClassVar[int]
    sequence_id: str
    time_utc_ns: int
    kind: TriggerEvent.Kind
    target_entity_id: str
    interceptor_entity_id: str
    predicted_closest_approach_utc_ns: int
    predicted_miss_distance_m: float
    channels: _containers.RepeatedScalarFieldContainer[str]
    detail: str
    def __init__(self, sequence_id: _Optional[str] = ..., time_utc_ns: _Optional[int] = ..., kind: _Optional[_Union[TriggerEvent.Kind, str]] = ..., target_entity_id: _Optional[str] = ..., interceptor_entity_id: _Optional[str] = ..., predicted_closest_approach_utc_ns: _Optional[int] = ..., predicted_miss_distance_m: _Optional[float] = ..., channels: _Optional[_Iterable[str]] = ..., detail: _Optional[str] = ...) -> None: ...
