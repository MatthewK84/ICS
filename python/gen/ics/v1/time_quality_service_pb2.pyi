from ics.v1 import time_quality_pb2 as _time_quality_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from collections.abc import Mapping as _Mapping
from typing import ClassVar as _ClassVar, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class WatchTimeQualityRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class WatchTimeQualityResponse(_message.Message):
    __slots__ = ("report",)
    REPORT_FIELD_NUMBER: _ClassVar[int]
    report: _time_quality_pb2.TimeQuality
    def __init__(self, report: _Optional[_Union[_time_quality_pb2.TimeQuality, _Mapping]] = ...) -> None: ...
