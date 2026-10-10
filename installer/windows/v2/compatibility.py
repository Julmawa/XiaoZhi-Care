"""XiaoZhi Care Installer V2 - offline compatibility decision engine.

Does not access hardware, Flash, serial ports, or the network.
Only an exact, verified build match is eligible; user claims cannot attest
that a firmware is compatible with a custom board configuration.
"""
from __future__ import annotations

from dataclasses import dataclass
from enum import Enum
from typing import Mapping, Optional


class Status(str, Enum):
    COMPATIBLE = "compatible"
    PENDING = "pending"
    INCOMPATIBLE = "incompatible"


@dataclass(frozen=True)
class Device:
    chip: str
    flash_mb: int
    psram_mb: int
    xiaozhi_version: str
    board_sku: str
    # Fingerprint of the *complete*, reproducible build configuration.
    # Must be obtained from a trusted build inventory, not guessed from SKU.
    config_sha256: Optional[str] = None
    security_verified: bool = False
    secure_boot_disabled: bool = False
    flash_encryption_disabled: bool = False


@dataclass(frozen=True)
class Decision:
    status: Status
    reason: str
    package_id: Optional[str] = None


def _sha256(s: Optional[str]) -> bool:
    return isinstance(s, str) and len(s) == 64 and all(c in "0123456789abcdef" for c in s)


def evaluate(device: Device, catalog: Mapping[str, Mapping[str, str]]) -> Decision:
    """Assess compatibility using externally verified device/build evidence.

    `catalog` is trusted metadata from an independently verified package
    repository. This function does not verify signatures or binary hashes.
    It must not be used alone to authorize flashing.
    """
    if device.chip.strip().upper() != "ESP32-S3":
        return Decision(Status.INCOMPATIBLE, "Se requiere ESP32-S3.")
    if device.flash_mb != 16 or device.psram_mb != 8:
        return Decision(Status.INCOMPATIBLE, "Se requieren 16 MB Flash y 8 MB PSRAM.")
    if not device.security_verified:
        return Decision(Status.PENDING, "No se verificaron los indicadores de seguridad de la placa.")
    if not device.secure_boot_disabled or not device.flash_encryption_disabled:
        return Decision(Status.INCOMPATIBLE, "La instalacion actual requiere Secure Boot y Flash Encryption desactivados.")
    if not device.xiaozhi_version or not device.board_sku:
        return Decision(Status.PENDING, "No se pudo identificar la version o el perfil de XiaoZhi.")
    if not _sha256(device.config_sha256):
        return Decision(Status.PENDING, "Falta huella verificable de la configuracion completa de hardware.")

    # Match by exact configuration digest, never by SKU or LCD type alone.
    entry = catalog.get(device.config_sha256)
    if not entry:
        return Decision(Status.PENDING, "No existe un firmware Care verificado para esta configuracion.")
    if (entry.get("chip") != device.chip or
            entry.get("xiaozhi_version") != device.xiaozhi_version or
            entry.get("board_sku") != device.board_sku or
            not entry.get("package_id")):
        return Decision(Status.PENDING, "El paquete disponible no coincide exactamente con la version o perfil.")
    return Decision(Status.COMPATIBLE, "Existe un paquete coincidente; falta validar integridad y realizar respaldo.", entry["package_id"])
