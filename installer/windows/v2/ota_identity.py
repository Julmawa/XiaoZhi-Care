"""Analizador conservador de otadata ESP-IDF para copias Flash (solo lectura).

No detecta por sí solo la app en ejecución y NO autoriza flashear.
Soporta exclusivamente una tabla conocida con dos particiones OTA.
"""
from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

SECTOR = 0x1000
OTA_DATA_SIZE = 0x2000
FLASH_SIZE = 0x1000000
STATES = {0: 'NEW', 1: 'PENDING_VERIFY', 2: 'VALID', 3: 'INVALID', 4: 'ABORTED', 0xFFFFFFFF: 'UNDEFINED'}


@dataclass(frozen=True)
class OtaEntry:
    index: int
    sequence: int
    state: str
    crc_ok: bool
    eligible: bool


@dataclass(frozen=True)
class OtaDecision:
    status: str  # selected, pending, unknown
    selected_slot: str | None
    reason: str
    entries: tuple[OtaEntry, OtaEntry]


def _parse_entry(raw: bytes, index: int) -> OtaEntry:
    pos = index * SECTOR
    sequence, = struct.unpack_from('<I', raw, pos)
    state_code, stored_crc = struct.unpack_from('<II', raw, pos + 24)
    calculated = zlib.crc32(struct.pack('<I', sequence)) & 0xFFFFFFFF
    crc_ok = (stored_crc == calculated and sequence not in (0, 0xFFFFFFFF))
    state = STATES.get(state_code, 'UNKNOWN')
    eligible = crc_ok and state in ('VALID', 'UNDEFINED', 'NEW', 'PENDING_VERIFY')
    return OtaEntry(index, sequence, state, crc_ok, eligible)


def analyze_otadata(raw: bytes, ota_slots: int = 2) -> OtaDecision:
    """Interpreta las dos entradas; señala casos dudosos para revisión.

    selected_slot es solamente la selección persistida inferida del registro.
    No demuestra qué aplicación estaba ejecutándose al capturar la Flash.
    """
    if len(raw) != OTA_DATA_SIZE or ota_slots != 2:
        raise ValueError('Se requieren 8192 bytes otadata y exactamente dos slots OTA.')
    entries = (_parse_entry(raw, 0), _parse_entry(raw, 1))
    eligible = [e for e in entries if e.eligible]
    if not eligible:
        return OtaDecision('unknown', None, 'Sin registros OTA elegibles; podría existir fallback del bootloader.', entries)
    top = max(e.sequence for e in eligible)
    best = [e for e in eligible if e.sequence == top]
    if len(best) != 1:
        return OtaDecision('pending', None, 'Registros OTA con secuencias empatadas: selección ambigua.', entries)
    chosen = best[0]
    slot = 'ota_' + str((chosen.sequence - 1) % ota_slots)
    if chosen.state in ('NEW', 'PENDING_VERIFY'):
        return OtaDecision('pending', slot, 'Imagen pendiente de verificación; posible rollback.', entries)
    if any(e.crc_ok and e.state == 'UNKNOWN' for e in entries):
        return OtaDecision('pending', None, 'Estado OTA desconocido: requiere inspección adicional.', entries)
    return OtaDecision('selected', slot,
                       'Selección OTA persistida; no prueba por sí sola ejecución actual ni integridad de imagen.', entries)


def analyze_flash(flash: bytes, otadata_offset: int = 0xD000) -> OtaDecision:
    if len(flash) != FLASH_SIZE or otadata_offset != 0xD000:
        raise ValueError('Solo se admite backup completo 16 MB con layout conocido (otadata 0xD000).')
    return analyze_otadata(flash[otadata_offset:otadata_offset + OTA_DATA_SIZE])
