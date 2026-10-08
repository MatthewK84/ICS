from ics.v1 import pli_pb2 as _pli_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class QueryPliRequest(_message.Message):
    __slots__ = ("kind", "start_utc_ns", "end_utc_ns", "entity_id", "limit_count")
    class Kind(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        KIND_UNSPECIFIED: _ClassVar[QueryPliRequest.Kind]
        KIND_RECORDS: _ClassVar[QueryPliRequest.Kind]
        KIND_EVENTS: _ClassVar[QueryPliRequest.Kind]
    KIND_UNSPECIFIED: QueryPliRequest.Kind
    KIND_RECORDS: QueryPliRequest.Kind
    KIND_EVENTS: QueryPliRequest.Kind
    KIND_FIELD_NUMBER: _ClassVar[int]
    START_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    END_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    ENTITY_ID_FIELD_NUMBER: _ClassVar[int]
    LIMIT_COUNT_FIELD_NUMBER: _ClassVar[int]
    kind: QueryPliRequest.Kind
    start_utc_ns: int
    end_utc_ns: int
    entity_id: str
    limit_count: int
    def __init__(self, kind: _Optional[_Union[QueryPliRequest.Kind, str]] = ..., start_utc_ns: _Optional[int] = ..., end_utc_ns: _Optional[int] = ..., entity_id: _Optional[str] = ..., limit_count: _Optional[int] = ...) -> None: ...

class QueryPliResponse(_message.Message):
    __slots__ = ("records", "events", "done", "truncated", "error")
    RECORDS_FIELD_NUMBER: _ClassVar[int]
    EVENTS_FIELD_NUMBER: _ClassVar[int]
    DONE_FIELD_NUMBER: _ClassVar[int]
    TRUNCATED_FIELD_NUMBER: _ClassVar[int]
    ERROR_FIELD_NUMBER: _ClassVar[int]
    records: _containers.RepeatedCompositeFieldContainer[_pli_pb2.PliRecord]
    events: _containers.RepeatedCompositeFieldContainer[_pli_pb2.PliEvent]
    done: bool
    truncated: bool
    error: str
    def __init__(self, records: _Optional[_Iterable[_Union[_pli_pb2.PliRecord, _Mapping]]] = ..., events: _Optional[_Iterable[_Union[_pli_pb2.PliEvent, _Mapping]]] = ..., done: _Optional[bool] = ..., truncated: _Optional[bool] = ..., error: _Optional[str] = ...) -> None: ...
