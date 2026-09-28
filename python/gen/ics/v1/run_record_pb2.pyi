from ics.v1 import common_pb2 as _common_pb2
from ics.v1 import footprint_pb2 as _footprint_pb2
from ics.v1 import kill_assessment_pb2 as _kill_assessment_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class RunRecord(_message.Message):
    __slots__ = ("run_id", "event_id", "start_utc_ns", "end_utc_ns", "range_origin", "stations", "software_revision", "parameter_files", "models", "artifacts", "kill_assessment", "footprint", "measurements", "flags")
    class Flag(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        FLAG_UNSPECIFIED: _ClassVar[RunRecord.Flag]
        FLAG_REDUCED_TRUTH: _ClassVar[RunRecord.Flag]
        FLAG_INCOMPLETE_COVERAGE: _ClassVar[RunRecord.Flag]
        FLAG_TIME_DEGRADED: _ClassVar[RunRecord.Flag]
    FLAG_UNSPECIFIED: RunRecord.Flag
    FLAG_REDUCED_TRUTH: RunRecord.Flag
    FLAG_INCOMPLETE_COVERAGE: RunRecord.Flag
    FLAG_TIME_DEGRADED: RunRecord.Flag
    class Station(_message.Message):
        __slots__ = ("station_id", "position", "camera_ids")
        STATION_ID_FIELD_NUMBER: _ClassVar[int]
        POSITION_FIELD_NUMBER: _ClassVar[int]
        CAMERA_IDS_FIELD_NUMBER: _ClassVar[int]
        station_id: str
        position: _common_pb2.GeodeticPoint
        camera_ids: _containers.RepeatedScalarFieldContainer[str]
        def __init__(self, station_id: _Optional[str] = ..., position: _Optional[_Union[_common_pb2.GeodeticPoint, _Mapping]] = ..., camera_ids: _Optional[_Iterable[str]] = ...) -> None: ...
    class Measurement(_message.Message):
        __slots__ = ("criterion_id", "value", "sigma", "unit")
        CRITERION_ID_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        SIGMA_FIELD_NUMBER: _ClassVar[int]
        UNIT_FIELD_NUMBER: _ClassVar[int]
        criterion_id: str
        value: float
        sigma: float
        unit: str
        def __init__(self, criterion_id: _Optional[str] = ..., value: _Optional[float] = ..., sigma: _Optional[float] = ..., unit: _Optional[str] = ...) -> None: ...
    RUN_ID_FIELD_NUMBER: _ClassVar[int]
    EVENT_ID_FIELD_NUMBER: _ClassVar[int]
    START_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    END_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    RANGE_ORIGIN_FIELD_NUMBER: _ClassVar[int]
    STATIONS_FIELD_NUMBER: _ClassVar[int]
    SOFTWARE_REVISION_FIELD_NUMBER: _ClassVar[int]
    PARAMETER_FILES_FIELD_NUMBER: _ClassVar[int]
    MODELS_FIELD_NUMBER: _ClassVar[int]
    ARTIFACTS_FIELD_NUMBER: _ClassVar[int]
    KILL_ASSESSMENT_FIELD_NUMBER: _ClassVar[int]
    FOOTPRINT_FIELD_NUMBER: _ClassVar[int]
    MEASUREMENTS_FIELD_NUMBER: _ClassVar[int]
    FLAGS_FIELD_NUMBER: _ClassVar[int]
    run_id: str
    event_id: str
    start_utc_ns: int
    end_utc_ns: int
    range_origin: _common_pb2.GeodeticPoint
    stations: _containers.RepeatedCompositeFieldContainer[RunRecord.Station]
    software_revision: str
    parameter_files: _containers.RepeatedCompositeFieldContainer[_common_pb2.FileDigest]
    models: _containers.RepeatedCompositeFieldContainer[_common_pb2.FileDigest]
    artifacts: _containers.RepeatedCompositeFieldContainer[_common_pb2.FileDigest]
    kill_assessment: _kill_assessment_pb2.KillAssessment
    footprint: _footprint_pb2.Footprint
    measurements: _containers.RepeatedCompositeFieldContainer[RunRecord.Measurement]
    flags: _containers.RepeatedScalarFieldContainer[RunRecord.Flag]
    def __init__(self, run_id: _Optional[str] = ..., event_id: _Optional[str] = ..., start_utc_ns: _Optional[int] = ..., end_utc_ns: _Optional[int] = ..., range_origin: _Optional[_Union[_common_pb2.GeodeticPoint, _Mapping]] = ..., stations: _Optional[_Iterable[_Union[RunRecord.Station, _Mapping]]] = ..., software_revision: _Optional[str] = ..., parameter_files: _Optional[_Iterable[_Union[_common_pb2.FileDigest, _Mapping]]] = ..., models: _Optional[_Iterable[_Union[_common_pb2.FileDigest, _Mapping]]] = ..., artifacts: _Optional[_Iterable[_Union[_common_pb2.FileDigest, _Mapping]]] = ..., kill_assessment: _Optional[_Union[_kill_assessment_pb2.KillAssessment, _Mapping]] = ..., footprint: _Optional[_Union[_footprint_pb2.Footprint, _Mapping]] = ..., measurements: _Optional[_Iterable[_Union[RunRecord.Measurement, _Mapping]]] = ..., flags: _Optional[_Iterable[_Union[RunRecord.Flag, str]]] = ...) -> None: ...
