"""MAVLink's checksum: CRC-16/MCRF4XX, which MAVLink calls X.25 (ICS-018)."""

INITIAL: int = 0xFFFF


def x25(data: bytes, crc: int = INITIAL) -> int:
    """The checksum of ``data``, continuing from ``crc``."""
    for byte in data:
        mixed = (byte ^ crc) & 0xFF
        mixed = (mixed ^ (mixed << 4)) & 0xFF
        crc = ((crc >> 8) ^ (mixed << 8) ^ (mixed << 3) ^ (mixed >> 4)) & 0xFFFF
    return crc
