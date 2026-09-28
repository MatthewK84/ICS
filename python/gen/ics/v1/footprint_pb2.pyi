from ics.v1 import common_pb2 as _common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Footprint(_message.Message):
    __slots__ = ("run_id", "sample_count", "fragment_ids", "regions", "inputs")
    class Polygon(_message.Message):
        __slots__ = ("vertices",)
        VERTICES_FIELD_NUMBER: _ClassVar[int]
        vertices: _containers.RepeatedCompositeFieldContainer[_common_pb2.GeodeticPoint]
        def __init__(self, vertices: _Optional[_Iterable[_Union[_common_pb2.GeodeticPoint, _Mapping]]] = ...) -> None: ...
    class Region(_message.Message):
        __slots__ = ("probability", "polygons")
        PROBABILITY_FIELD_NUMBER: _ClassVar[int]
        POLYGONS_FIELD_NUMBER: _ClassVar[int]
        probability: float
        polygons: _containers.RepeatedCompositeFieldContainer[Footprint.Polygon]
        def __init__(self, probability: _Optional[float] = ..., polygons: _Optional[_Iterable[_Union[Footprint.Polygon, _Mapping]]] = ...) -> None: ...
    RUN_ID_FIELD_NUMBER: _ClassVar[int]
    SAMPLE_COUNT_FIELD_NUMBER: _ClassVar[int]
    FRAGMENT_IDS_FIELD_NUMBER: _ClassVar[int]
    REGIONS_FIELD_NUMBER: _ClassVar[int]
    INPUTS_FIELD_NUMBER: _ClassVar[int]
    run_id: str
    sample_count: int
    fragment_ids: _containers.RepeatedScalarFieldContainer[str]
    regions: _containers.RepeatedCompositeFieldContainer[Footprint.Region]
    inputs: _containers.RepeatedCompositeFieldContainer[_common_pb2.FileDigest]
    def __init__(self, run_id: _Optional[str] = ..., sample_count: _Optional[int] = ..., fragment_ids: _Optional[_Iterable[str]] = ..., regions: _Optional[_Iterable[_Union[Footprint.Region, _Mapping]]] = ..., inputs: _Optional[_Iterable[_Union[_common_pb2.FileDigest, _Mapping]]] = ...) -> None: ...
