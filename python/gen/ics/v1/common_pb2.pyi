from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class EntityRole(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    ENTITY_ROLE_UNSPECIFIED: _ClassVar[EntityRole]
    ENTITY_ROLE_TARGET: _ClassVar[EntityRole]
    ENTITY_ROLE_INTERCEPTOR: _ClassVar[EntityRole]
    ENTITY_ROLE_DEBRIS: _ClassVar[EntityRole]
    ENTITY_ROLE_OTHER: _ClassVar[EntityRole]
ENTITY_ROLE_UNSPECIFIED: EntityRole
ENTITY_ROLE_TARGET: EntityRole
ENTITY_ROLE_INTERCEPTOR: EntityRole
ENTITY_ROLE_DEBRIS: EntityRole
ENTITY_ROLE_OTHER: EntityRole

class GeodeticPoint(_message.Message):
    __slots__ = ("latitude_deg", "longitude_deg", "height_ellipsoid_m")
    LATITUDE_DEG_FIELD_NUMBER: _ClassVar[int]
    LONGITUDE_DEG_FIELD_NUMBER: _ClassVar[int]
    HEIGHT_ELLIPSOID_M_FIELD_NUMBER: _ClassVar[int]
    latitude_deg: float
    longitude_deg: float
    height_ellipsoid_m: float
    def __init__(self, latitude_deg: _Optional[float] = ..., longitude_deg: _Optional[float] = ..., height_ellipsoid_m: _Optional[float] = ...) -> None: ...

class EnuVector(_message.Message):
    __slots__ = ("east", "north", "up")
    EAST_FIELD_NUMBER: _ClassVar[int]
    NORTH_FIELD_NUMBER: _ClassVar[int]
    UP_FIELD_NUMBER: _ClassVar[int]
    east: float
    north: float
    up: float
    def __init__(self, east: _Optional[float] = ..., north: _Optional[float] = ..., up: _Optional[float] = ...) -> None: ...

class EnuCovariance(_message.Message):
    __slots__ = ("ee", "en", "eu", "nn", "nu", "uu")
    EE_FIELD_NUMBER: _ClassVar[int]
    EN_FIELD_NUMBER: _ClassVar[int]
    EU_FIELD_NUMBER: _ClassVar[int]
    NN_FIELD_NUMBER: _ClassVar[int]
    NU_FIELD_NUMBER: _ClassVar[int]
    UU_FIELD_NUMBER: _ClassVar[int]
    ee: float
    en: float
    eu: float
    nn: float
    nu: float
    uu: float
    def __init__(self, ee: _Optional[float] = ..., en: _Optional[float] = ..., eu: _Optional[float] = ..., nn: _Optional[float] = ..., nu: _Optional[float] = ..., uu: _Optional[float] = ...) -> None: ...

class FileDigest(_message.Message):
    __slots__ = ("path", "sha256_hex", "size_bytes", "media_type")
    PATH_FIELD_NUMBER: _ClassVar[int]
    SHA256_HEX_FIELD_NUMBER: _ClassVar[int]
    SIZE_BYTES_FIELD_NUMBER: _ClassVar[int]
    MEDIA_TYPE_FIELD_NUMBER: _ClassVar[int]
    path: str
    sha256_hex: str
    size_bytes: int
    media_type: str
    def __init__(self, path: _Optional[str] = ..., sha256_hex: _Optional[str] = ..., size_bytes: _Optional[int] = ..., media_type: _Optional[str] = ...) -> None: ...
