from ics.v1 import common_pb2 as _common_pb2
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class PliSource(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    PLI_SOURCE_UNSPECIFIED: _ClassVar[PliSource]
    PLI_SOURCE_MAVLINK: _ClassVar[PliSource]
    PLI_SOURCE_COT: _ClassVar[PliSource]
    PLI_SOURCE_LATTICE: _ClassVar[PliSource]
    PLI_SOURCE_SAPIENT: _ClassVar[PliSource]
    PLI_SOURCE_ULOG: _ClassVar[PliSource]
    PLI_SOURCE_DATAFLASH: _ClassVar[PliSource]

class PliTimeBasis(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    PLI_TIME_BASIS_UNSPECIFIED: _ClassVar[PliTimeBasis]
    PLI_TIME_BASIS_VEHICLE_GNSS: _ClassVar[PliTimeBasis]
    PLI_TIME_BASIS_VEHICLE_ALIGNED: _ClassVar[PliTimeBasis]
    PLI_TIME_BASIS_RECEIPT: _ClassVar[PliTimeBasis]
PLI_SOURCE_UNSPECIFIED: PliSource
PLI_SOURCE_MAVLINK: PliSource
PLI_SOURCE_COT: PliSource
PLI_SOURCE_LATTICE: PliSource
PLI_SOURCE_SAPIENT: PliSource
PLI_SOURCE_ULOG: PliSource
PLI_SOURCE_DATAFLASH: PliSource
PLI_TIME_BASIS_UNSPECIFIED: PliTimeBasis
PLI_TIME_BASIS_VEHICLE_GNSS: PliTimeBasis
PLI_TIME_BASIS_VEHICLE_ALIGNED: PliTimeBasis
PLI_TIME_BASIS_RECEIPT: PliTimeBasis

class PliRecord(_message.Message):
    __slots__ = ("entity_id", "role", "source", "valid_utc_ns", "received_utc_ns", "time_basis", "position", "velocity_enu_mps", "horizontal_sigma_m", "vertical_sigma_m", "fix_type", "attitude")
    class FixType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        FIX_TYPE_UNSPECIFIED: _ClassVar[PliRecord.FixType]
        FIX_TYPE_NONE: _ClassVar[PliRecord.FixType]
        FIX_TYPE_TWO_DIMENSIONAL: _ClassVar[PliRecord.FixType]
        FIX_TYPE_THREE_DIMENSIONAL: _ClassVar[PliRecord.FixType]
        FIX_TYPE_DGNSS: _ClassVar[PliRecord.FixType]
        FIX_TYPE_RTK_FLOAT: _ClassVar[PliRecord.FixType]
        FIX_TYPE_RTK_FIXED: _ClassVar[PliRecord.FixType]
        FIX_TYPE_OTHER: _ClassVar[PliRecord.FixType]
    FIX_TYPE_UNSPECIFIED: PliRecord.FixType
    FIX_TYPE_NONE: PliRecord.FixType
    FIX_TYPE_TWO_DIMENSIONAL: PliRecord.FixType
    FIX_TYPE_THREE_DIMENSIONAL: PliRecord.FixType
    FIX_TYPE_DGNSS: PliRecord.FixType
    FIX_TYPE_RTK_FLOAT: PliRecord.FixType
    FIX_TYPE_RTK_FIXED: PliRecord.FixType
    FIX_TYPE_OTHER: PliRecord.FixType
    class Attitude(_message.Message):
        __slots__ = ("w", "x", "y", "z")
        W_FIELD_NUMBER: _ClassVar[int]
        X_FIELD_NUMBER: _ClassVar[int]
        Y_FIELD_NUMBER: _ClassVar[int]
        Z_FIELD_NUMBER: _ClassVar[int]
        w: float
        x: float
        y: float
        z: float
        def __init__(self, w: _Optional[float] = ..., x: _Optional[float] = ..., y: _Optional[float] = ..., z: _Optional[float] = ...) -> None: ...
    ENTITY_ID_FIELD_NUMBER: _ClassVar[int]
    ROLE_FIELD_NUMBER: _ClassVar[int]
    SOURCE_FIELD_NUMBER: _ClassVar[int]
    VALID_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    RECEIVED_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    TIME_BASIS_FIELD_NUMBER: _ClassVar[int]
    POSITION_FIELD_NUMBER: _ClassVar[int]
    VELOCITY_ENU_MPS_FIELD_NUMBER: _ClassVar[int]
    HORIZONTAL_SIGMA_M_FIELD_NUMBER: _ClassVar[int]
    VERTICAL_SIGMA_M_FIELD_NUMBER: _ClassVar[int]
    FIX_TYPE_FIELD_NUMBER: _ClassVar[int]
    ATTITUDE_FIELD_NUMBER: _ClassVar[int]
    entity_id: str
    role: _common_pb2.EntityRole
    source: PliSource
    valid_utc_ns: int
    received_utc_ns: int
    time_basis: PliTimeBasis
    position: _common_pb2.GeodeticPoint
    velocity_enu_mps: _common_pb2.EnuVector
    horizontal_sigma_m: float
    vertical_sigma_m: float
    fix_type: PliRecord.FixType
    attitude: PliRecord.Attitude
    def __init__(self, entity_id: _Optional[str] = ..., role: _Optional[_Union[_common_pb2.EntityRole, str]] = ..., source: _Optional[_Union[PliSource, str]] = ..., valid_utc_ns: _Optional[int] = ..., received_utc_ns: _Optional[int] = ..., time_basis: _Optional[_Union[PliTimeBasis, str]] = ..., position: _Optional[_Union[_common_pb2.GeodeticPoint, _Mapping]] = ..., velocity_enu_mps: _Optional[_Union[_common_pb2.EnuVector, _Mapping]] = ..., horizontal_sigma_m: _Optional[float] = ..., vertical_sigma_m: _Optional[float] = ..., fix_type: _Optional[_Union[PliRecord.FixType, str]] = ..., attitude: _Optional[_Union[PliRecord.Attitude, _Mapping]] = ...) -> None: ...

class PliEvent(_message.Message):
    __slots__ = ("entity_id", "source", "time_utc_ns", "time_basis", "kind", "command", "command_result", "detail")
    class Kind(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        KIND_UNSPECIFIED: _ClassVar[PliEvent.Kind]
        KIND_ARMED: _ClassVar[PliEvent.Kind]
        KIND_DISARMED: _ClassVar[PliEvent.Kind]
        KIND_MODE_CHANGED: _ClassVar[PliEvent.Kind]
        KIND_COMMAND: _ClassVar[PliEvent.Kind]
        KIND_COMMAND_ACK: _ClassVar[PliEvent.Kind]
        KIND_STATUS_TEXT: _ClassVar[PliEvent.Kind]
        KIND_LINK_LOST: _ClassVar[PliEvent.Kind]
        KIND_LINK_RESTORED: _ClassVar[PliEvent.Kind]
    KIND_UNSPECIFIED: PliEvent.Kind
    KIND_ARMED: PliEvent.Kind
    KIND_DISARMED: PliEvent.Kind
    KIND_MODE_CHANGED: PliEvent.Kind
    KIND_COMMAND: PliEvent.Kind
    KIND_COMMAND_ACK: PliEvent.Kind
    KIND_STATUS_TEXT: PliEvent.Kind
    KIND_LINK_LOST: PliEvent.Kind
    KIND_LINK_RESTORED: PliEvent.Kind
    ENTITY_ID_FIELD_NUMBER: _ClassVar[int]
    SOURCE_FIELD_NUMBER: _ClassVar[int]
    TIME_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    TIME_BASIS_FIELD_NUMBER: _ClassVar[int]
    KIND_FIELD_NUMBER: _ClassVar[int]
    COMMAND_FIELD_NUMBER: _ClassVar[int]
    COMMAND_RESULT_FIELD_NUMBER: _ClassVar[int]
    DETAIL_FIELD_NUMBER: _ClassVar[int]
    entity_id: str
    source: PliSource
    time_utc_ns: int
    time_basis: PliTimeBasis
    kind: PliEvent.Kind
    command: int
    command_result: int
    detail: str
    def __init__(self, entity_id: _Optional[str] = ..., source: _Optional[_Union[PliSource, str]] = ..., time_utc_ns: _Optional[int] = ..., time_basis: _Optional[_Union[PliTimeBasis, str]] = ..., kind: _Optional[_Union[PliEvent.Kind, str]] = ..., command: _Optional[int] = ..., command_result: _Optional[int] = ..., detail: _Optional[str] = ...) -> None: ...
