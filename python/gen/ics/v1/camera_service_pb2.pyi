from google.protobuf.internal import containers as _containers
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Iterable as _Iterable, Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class ArmAndOffloadRequest(_message.Message):
    __slots__ = ("run_id",)
    RUN_ID_FIELD_NUMBER: _ClassVar[int]
    run_id: str
    def __init__(self, run_id: _Optional[str] = ...) -> None: ...

class CameraOffload(_message.Message):
    __slots__ = ("camera_id", "verified", "segments", "frames", "files", "error")
    CAMERA_ID_FIELD_NUMBER: _ClassVar[int]
    VERIFIED_FIELD_NUMBER: _ClassVar[int]
    SEGMENTS_FIELD_NUMBER: _ClassVar[int]
    FRAMES_FIELD_NUMBER: _ClassVar[int]
    FILES_FIELD_NUMBER: _ClassVar[int]
    ERROR_FIELD_NUMBER: _ClassVar[int]
    camera_id: str
    verified: bool
    segments: int
    frames: int
    files: _containers.RepeatedScalarFieldContainer[str]
    error: str
    def __init__(self, camera_id: _Optional[str] = ..., verified: _Optional[bool] = ..., segments: _Optional[int] = ..., frames: _Optional[int] = ..., files: _Optional[_Iterable[str]] = ..., error: _Optional[str] = ...) -> None: ...

class ArmAndOffloadResponse(_message.Message):
    __slots__ = ("folder", "cameras")
    FOLDER_FIELD_NUMBER: _ClassVar[int]
    CAMERAS_FIELD_NUMBER: _ClassVar[int]
    folder: str
    cameras: _containers.RepeatedCompositeFieldContainer[CameraOffload]
    def __init__(self, folder: _Optional[str] = ..., cameras: _Optional[_Iterable[_Union[CameraOffload, _Mapping]]] = ...) -> None: ...
