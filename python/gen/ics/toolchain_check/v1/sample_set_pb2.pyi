from ics.toolchain_check.v1 import sample_pb2 as _sample_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class SampleSet(_message.Message):
    __slots__ = ("station_id", "samples")
    STATION_ID_FIELD_NUMBER: _ClassVar[int]
    SAMPLES_FIELD_NUMBER: _ClassVar[int]
    station_id: str
    samples: _containers.RepeatedCompositeFieldContainer[_sample_pb2.Sample]
    def __init__(self, station_id: _Optional[str] = ..., samples: _Optional[_Iterable[_Union[_sample_pb2.Sample, _Mapping]]] = ...) -> None: ...
