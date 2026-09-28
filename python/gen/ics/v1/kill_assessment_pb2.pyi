from ics.v1 import common_pb2 as _common_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class KillAssessment(_message.Message):
    __slots__ = ("run_id", "target_track_id", "interceptor_track_id", "computed_class", "closest_approach_utc_ns", "contact_utc_ns", "miss_distance_m", "miss_distance_sigma_m", "intercept_point_enu_m", "breakup_piece_count", "ballistic_fit_residual", "mission_denied", "thresholds", "thresholds_file", "overrides")
    class KillClass(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        KILL_CLASS_UNSPECIFIED: _ClassVar[KillAssessment.KillClass]
        KILL_CLASS_NO_KILL: _ClassVar[KillAssessment.KillClass]
        KILL_CLASS_MISSION_KILL: _ClassVar[KillAssessment.KillClass]
        KILL_CLASS_HARD_KILL: _ClassVar[KillAssessment.KillClass]
        KILL_CLASS_CATASTROPHIC_KILL: _ClassVar[KillAssessment.KillClass]
        KILL_CLASS_NO_TEST: _ClassVar[KillAssessment.KillClass]
        KILL_CLASS_UNDETERMINED: _ClassVar[KillAssessment.KillClass]
    KILL_CLASS_UNSPECIFIED: KillAssessment.KillClass
    KILL_CLASS_NO_KILL: KillAssessment.KillClass
    KILL_CLASS_MISSION_KILL: KillAssessment.KillClass
    KILL_CLASS_HARD_KILL: KillAssessment.KillClass
    KILL_CLASS_CATASTROPHIC_KILL: KillAssessment.KillClass
    KILL_CLASS_NO_TEST: KillAssessment.KillClass
    KILL_CLASS_UNDETERMINED: KillAssessment.KillClass
    class Threshold(_message.Message):
        __slots__ = ("name", "value", "unit")
        NAME_FIELD_NUMBER: _ClassVar[int]
        VALUE_FIELD_NUMBER: _ClassVar[int]
        UNIT_FIELD_NUMBER: _ClassVar[int]
        name: str
        value: float
        unit: str
        def __init__(self, name: _Optional[str] = ..., value: _Optional[float] = ..., unit: _Optional[str] = ...) -> None: ...
    class Override(_message.Message):
        __slots__ = ("kill_class", "user", "time_utc_ns", "reason")
        KILL_CLASS_FIELD_NUMBER: _ClassVar[int]
        USER_FIELD_NUMBER: _ClassVar[int]
        TIME_UTC_NS_FIELD_NUMBER: _ClassVar[int]
        REASON_FIELD_NUMBER: _ClassVar[int]
        kill_class: KillAssessment.KillClass
        user: str
        time_utc_ns: int
        reason: str
        def __init__(self, kill_class: _Optional[_Union[KillAssessment.KillClass, str]] = ..., user: _Optional[str] = ..., time_utc_ns: _Optional[int] = ..., reason: _Optional[str] = ...) -> None: ...
    RUN_ID_FIELD_NUMBER: _ClassVar[int]
    TARGET_TRACK_ID_FIELD_NUMBER: _ClassVar[int]
    INTERCEPTOR_TRACK_ID_FIELD_NUMBER: _ClassVar[int]
    COMPUTED_CLASS_FIELD_NUMBER: _ClassVar[int]
    CLOSEST_APPROACH_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    CONTACT_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    MISS_DISTANCE_M_FIELD_NUMBER: _ClassVar[int]
    MISS_DISTANCE_SIGMA_M_FIELD_NUMBER: _ClassVar[int]
    INTERCEPT_POINT_ENU_M_FIELD_NUMBER: _ClassVar[int]
    BREAKUP_PIECE_COUNT_FIELD_NUMBER: _ClassVar[int]
    BALLISTIC_FIT_RESIDUAL_FIELD_NUMBER: _ClassVar[int]
    MISSION_DENIED_FIELD_NUMBER: _ClassVar[int]
    THRESHOLDS_FIELD_NUMBER: _ClassVar[int]
    THRESHOLDS_FILE_FIELD_NUMBER: _ClassVar[int]
    OVERRIDES_FIELD_NUMBER: _ClassVar[int]
    run_id: str
    target_track_id: str
    interceptor_track_id: str
    computed_class: KillAssessment.KillClass
    closest_approach_utc_ns: int
    contact_utc_ns: int
    miss_distance_m: float
    miss_distance_sigma_m: float
    intercept_point_enu_m: _common_pb2.EnuVector
    breakup_piece_count: int
    ballistic_fit_residual: float
    mission_denied: bool
    thresholds: _containers.RepeatedCompositeFieldContainer[KillAssessment.Threshold]
    thresholds_file: _common_pb2.FileDigest
    overrides: _containers.RepeatedCompositeFieldContainer[KillAssessment.Override]
    def __init__(self, run_id: _Optional[str] = ..., target_track_id: _Optional[str] = ..., interceptor_track_id: _Optional[str] = ..., computed_class: _Optional[_Union[KillAssessment.KillClass, str]] = ..., closest_approach_utc_ns: _Optional[int] = ..., contact_utc_ns: _Optional[int] = ..., miss_distance_m: _Optional[float] = ..., miss_distance_sigma_m: _Optional[float] = ..., intercept_point_enu_m: _Optional[_Union[_common_pb2.EnuVector, _Mapping]] = ..., breakup_piece_count: _Optional[int] = ..., ballistic_fit_residual: _Optional[float] = ..., mission_denied: _Optional[bool] = ..., thresholds: _Optional[_Iterable[_Union[KillAssessment.Threshold, _Mapping]]] = ..., thresholds_file: _Optional[_Union[_common_pb2.FileDigest, _Mapping]] = ..., overrides: _Optional[_Iterable[_Union[KillAssessment.Override, _Mapping]]] = ...) -> None: ...
