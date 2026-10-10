"""Diagnostico offline V2: NO habilita grabaciones.

La identidad de hardware y la seguridad proceden de una verificacion EXTERNA;
la imagen de Flash no demuestra chip, PSRAM ni configuracion de pines.
El registro de builds relaciona ELF con configuraciones verificadas por el
proceso de compilacion, nunca con una suposicion basada en SKU.
"""
from __future__ import annotations

from dataclasses import dataclass
from typing import Mapping, Optional

from compatibility import Device, Decision, Status, evaluate
from firmware_identity import inspect_flash, AppIdentity
from ota_identity import analyze_flash


@dataclass(frozen=True)
class HardwareEvidence:
    chip: str
    flash_mb: int
    psram_mb: int
    security_verified: bool
    secure_boot_disabled: bool
    flash_encryption_disabled: bool


@dataclass(frozen=True)
class Diagnostic:
    status: Status
    reason: str
    selected_slot: Optional[str] = None
    app_elf_sha256: Optional[str] = None
    package_id: Optional[str] = None
    # Nunca se concede permiso para grabar con este modulo.
    flash_authorized: bool = False


def diagnose(
    flash: bytes,
    hardware: HardwareEvidence,
    build_registry: Mapping[str, Mapping[str, str]],
    package_catalog: Mapping[str, Mapping[str, str]],
) -> Diagnostic:
    """Analiza un respaldo y evidencias aportadas por un flujo confiable.

    Un resultado 'compatible' significa CANDIDATO de paquete, no autorización:
    falta autenticar registros, validar binarios y preservar periféricos.
    """
    if hardware.chip.strip().upper() != 'ESP32-S3' or hardware.flash_mb != 16 or hardware.psram_mb != 8:
        return Diagnostic(Status.INCOMPATIBLE, 'Se requiere ESP32-S3 N16R8.')
    if not hardware.security_verified:
        return Diagnostic(Status.PENDING, 'Seguridad del chip no verificada externamente.')
    if not hardware.secure_boot_disabled or not hardware.flash_encryption_disabled:
        return Diagnostic(Status.INCOMPATIBLE, 'Configuracion de seguridad no admitida por este instalador.')

    # Estos dos lectores no conectan al dispositivo y no escriben la Flash.
    try:
        inventory = inspect_flash(flash)
        ota = analyze_flash(flash)
    except (ValueError, IndexError, OverflowError) as exc:
        return Diagnostic(Status.INCOMPATIBLE, 'Respaldo invalido o layout no admitido: ' + str(exc))

    if ota.status != 'selected' or ota.selected_slot is None:
        return Diagnostic(Status.PENDING, 'Seleccion OTA no concluyente: ' + ota.reason)

    selected: list[AppIdentity] = [a for a in inventory.applications if a.partition == ota.selected_slot]
    if len(selected) != 1:
        return Diagnostic(Status.PENDING, 'No se encontro exactamente una imagen valida en el slot OTA seleccionado.', ota.selected_slot)
    app = selected[0]
    if app.project != 'xiaozhi':
        return Diagnostic(Status.INCOMPATIBLE, 'La imagen seleccionada no corresponde a XiaoZhi.', ota.selected_slot, app.app_elf_sha256)
    if not app.app_elf_sha256:
        return Diagnostic(Status.PENDING, 'La aplicacion carece de huella ELF.', ota.selected_slot)
    build = build_registry.get(app.app_elf_sha256)
    if not build:
        return Diagnostic(Status.PENDING, 'Compilacion desconocida: no se pueden inferir los GPIO desde el ELF.', ota.selected_slot, app.app_elf_sha256)
    if build.get('xiaozhi_version') != app.version or not build.get('board_sku'):
        return Diagnostic(Status.PENDING, 'Registro de compilacion incompleto o incongruente.', ota.selected_slot, app.app_elf_sha256)
    config_digest = build.get('config_sha256')
    device = Device(
        chip=hardware.chip, flash_mb=hardware.flash_mb, psram_mb=hardware.psram_mb,
        xiaozhi_version=app.version, board_sku=build['board_sku'],
        config_sha256=config_digest,
        security_verified=hardware.security_verified,
        secure_boot_disabled=hardware.secure_boot_disabled,
        flash_encryption_disabled=hardware.flash_encryption_disabled,
    )
    decision: Decision = evaluate(device, package_catalog)
    return Diagnostic(decision.status, decision.reason, ota.selected_slot,
                      app.app_elf_sha256, decision.package_id)
