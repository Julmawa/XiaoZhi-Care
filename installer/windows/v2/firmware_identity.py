"""Inspect an offline ESP32 Flash backup without modifying it.

This reader deliberately does not infer the running OTA slot or GPIO layout.
It never authorizes flashing or assumes the ELF digest is a config digest.
"""
from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import hashlib
import struct
from typing import Optional

FLASH_SIZE = 16 * 1024 * 1024
PARTITION_OFFSET = 0x8000
PARTITION_LENGTH = 0xC00
APP_DESC_MAGIC = 0xABCD5432


@dataclass(frozen=True)
class AppIdentity:
    partition: str
    offset: int
    version: str
    project: str
    idf_version: str
    app_elf_sha256: Optional[str]
    image_length: int


@dataclass(frozen=True)
class FlashInventory:
    backup_sha256: str
    applications: tuple[AppIdentity, ...]
    running_partition: Optional[str] = None  # deliberately unknown offline


def _ascii_field(data: bytes) -> str:
    return data.split(b'\0', 1)[0].decode('ascii', errors='replace')


def _image_length(data: bytes, offset: int, max_len: int) -> Optional[int]:
    if offset < 0 or offset + 24 > len(data) or data[offset] != 0xE9:
        return None
    segments = data[offset + 1]
    if not 1 <= segments <= 16:
        return None
    pos = offset + 24
    limit = min(len(data), offset + max_len)
    for _ in range(segments):
        if pos + 8 > limit:
            return None
        length = struct.unpack_from('<I', data, pos + 4)[0]
        pos += 8
        if length > max_len or pos + length > limit:
            return None
        pos += length
    return pos - offset


def read_app(data: bytes, label: str, offset: int, size: int) -> Optional[AppIdentity]:
    # esp_app_desc_t: starts at image offset + 24-byte header + 8-byte segment header
    desc = offset + 32
    if size < 32 + 176 or desc + 176 > len(data):
        return None
    if struct.unpack_from('<I', data, desc)[0] != APP_DESC_MAGIC:
        return None
    length = _image_length(data, offset, size)
    if length is None or length < 32 + 176:
        return None
    version = _ascii_field(data[desc + 16:desc + 48])
    project = _ascii_field(data[desc + 48:desc + 80])
    idf_version = _ascii_field(data[desc + 112:desc + 144])
    digest = data[desc + 144:desc + 176]
    elf = digest.hex() if any(digest) and digest != b'\xff' * 32 else None
    return AppIdentity(label, offset, version, project, idf_version, elf, length)


def inspect_flash(data: bytes) -> FlashInventory:
    if len(data) != FLASH_SIZE:
        raise ValueError('El respaldo debe contener exactamente 16 MB de Flash.')
    apps: list[AppIdentity] = []
    for pos in range(PARTITION_OFFSET, PARTITION_OFFSET + PARTITION_LENGTH, 32):
        entry = data[pos:pos + 32]
        if entry[:2] == b'\xff\xff':
            break
        if entry[:2] != b'\xaa\x50':
            continue
        ptype, subtype = entry[2], entry[3]
        offset, size = struct.unpack_from('<II', entry, 4)
        label = _ascii_field(entry[12:28])
        if ptype != 0 or not (subtype == 0 or 0x10 <= subtype <= 0x1f):
            continue
        if offset < 0x10000 or size == 0 or offset + size > len(data):
            continue
        app = read_app(data, label, offset, size)
        if app is not None:
            apps.append(app)
    return FlashInventory(hashlib.sha256(data).hexdigest(), tuple(apps))


def inspect_file(path: str | Path) -> FlashInventory:
    path = Path(path)
    if path.stat().st_size != FLASH_SIZE:
        raise ValueError('El respaldo debe contener exactamente 16 MB de Flash.')
    return inspect_flash(path.read_bytes())
