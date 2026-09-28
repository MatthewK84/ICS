from ics.v1 import common_pb2 as _common_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Fragment(_message.Message):
    __slots__ = ("fragment_id", "track_id", "parent_role", "material_class", "ballistic_coefficient_kg_per_m2", "ballistic_coefficient_sigma_kg_per_m2", "projected_area_m2", "projected_area_sigma_m2", "drag_coefficient", "mass_kg", "mass_sigma_kg", "first_seen_utc_ns")
    FRAGMENT_ID_FIELD_NUMBER: _ClassVar[int]
    TRACK_ID_FIELD_NUMBER: _ClassVar[int]
    PARENT_ROLE_FIELD_NUMBER: _ClassVar[int]
    MATERIAL_CLASS_FIELD_NUMBER: _ClassVar[int]
    BALLISTIC_COEFFICIENT_KG_PER_M2_FIELD_NUMBER: _ClassVar[int]
    BALLISTIC_COEFFICIENT_SIGMA_KG_PER_M2_FIELD_NUMBER: _ClassVar[int]
    PROJECTED_AREA_M2_FIELD_NUMBER: _ClassVar[int]
    PROJECTED_AREA_SIGMA_M2_FIELD_NUMBER: _ClassVar[int]
    DRAG_COEFFICIENT_FIELD_NUMBER: _ClassVar[int]
    MASS_KG_FIELD_NUMBER: _ClassVar[int]
    MASS_SIGMA_KG_FIELD_NUMBER: _ClassVar[int]
    FIRST_SEEN_UTC_NS_FIELD_NUMBER: _ClassVar[int]
    fragment_id: str
    track_id: str
    parent_role: _common_pb2.EntityRole
    material_class: str
    ballistic_coefficient_kg_per_m2: float
    ballistic_coefficient_sigma_kg_per_m2: float
    projected_area_m2: float
    projected_area_sigma_m2: float
    drag_coefficient: float
    mass_kg: float
    mass_sigma_kg: float
    first_seen_utc_ns: int
    def __init__(self, fragment_id: _Optional[str] = ..., track_id: _Optional[str] = ..., parent_role: _Optional[_Union[_common_pb2.EntityRole, str]] = ..., material_class: _Optional[str] = ..., ballistic_coefficient_kg_per_m2: _Optional[float] = ..., ballistic_coefficient_sigma_kg_per_m2: _Optional[float] = ..., projected_area_m2: _Optional[float] = ..., projected_area_sigma_m2: _Optional[float] = ..., drag_coefficient: _Optional[float] = ..., mass_kg: _Optional[float] = ..., mass_sigma_kg: _Optional[float] = ..., first_seen_utc_ns: _Optional[int] = ...) -> None: ...
