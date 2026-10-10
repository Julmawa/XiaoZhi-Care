"""Catalogo offline de compilaciones XiaoZhi/Care (V2).

Los registros son datos NO confiables hasta que un distribuidor compruebe
su procedencia/autenticidad. Esta biblioteca solo valida estructura y
coherencia, nunca autoriza grabar firmware.
"""
from __future__ import annotations

import json
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

_HEX64 = re.compile(r"^[0-9a-f]{64}$")


class CatalogError(ValueError):
    """El catalogo no cumple los requisitos estructurales."""


@dataclass(frozen=True)
class Catalog:
    build_registry: dict[str, dict[str, str]]
    package_catalog: dict[str, dict[str, str]]
    authenticated: bool = False  # Esta API NO autentica archivos.


def _hex(value: Any, field: str) -> str:
    if not isinstance(value, str) or not _HEX64.fullmatch(value):
        raise CatalogError(f"{field}: se requieren 64 caracteres hexadecimales minusculos")
    return value


def _string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.strip() or len(value) > 160:
        raise CatalogError(f"{field}: texto no valido")
    return value


def parse_catalog(data: Mapping[str, Any]) -> Catalog:
    """Devuelve los dos mapas esperados por installation_diagnostic.diagnose.

    No confundir integridad sintactica con autenticidad. En particular, el
    digest ELF identifica una compilacion, pero NO describe sus perifericos.
    """
    if not isinstance(data, Mapping) or data.get("schema") != "xiaozhi-care-build-catalog-v2" or data.get("schema_version") != 1:
        raise CatalogError("Esquema/version de catalogo no admitidos")
    builds = data.get("builds")
    packages = data.get("packages")
    if not isinstance(builds, list) or not isinstance(packages, list):
        raise CatalogError("builds y packages deben ser listas")

    registry: dict[str, dict[str, str]] = {}
    for i, item in enumerate(builds):
        if not isinstance(item, Mapping):
            raise CatalogError(f"builds[{i}] no es un objeto")
        elf = _hex(item.get("app_elf_sha256"), "app_elf_sha256")
        digest = _hex(item.get("config_sha256"), "config_sha256")
        version = _string(item.get("xiaozhi_version"), "xiaozhi_version")
        sku = _string(item.get("board_sku"), "board_sku")
        if elf in registry:
            raise CatalogError("Huella ELF duplicada")
        registry[elf] = {"config_sha256": digest, "xiaozhi_version": version, "board_sku": sku}

    catalog: dict[str, dict[str, str]] = {}
    for i, item in enumerate(packages):
        if not isinstance(item, Mapping):
            raise CatalogError(f"packages[{i}] no es un objeto")
        digest = _hex(item.get("config_sha256"), "config_sha256")
        version = _string(item.get("xiaozhi_version"), "xiaozhi_version")
        sku = _string(item.get("board_sku"), "board_sku")
        package_id = _string(item.get("package_id"), "package_id")
        chip = _string(item.get("chip"), "chip")
        if chip != "ESP32-S3":
            raise CatalogError("Solo se admite ESP32-S3")
        if digest in catalog:
            raise CatalogError("Configuracion duplicada (paquete ambiguo)")
        catalog[digest] = {"chip": chip, "xiaozhi_version": version,
                           "board_sku": sku, "package_id": package_id}

    # No publicar asociaciones que discrepan entre los dos mapas.
    for entry in registry.values():
        package = catalog.get(entry["config_sha256"])
        if package and (package["xiaozhi_version"] != entry["xiaozhi_version"] or
                        package["board_sku"] != entry["board_sku"]):
            raise CatalogError("Build y paquete discrepan sobre version o perfil")
    return Catalog(registry, catalog, authenticated=False)


def load_catalog(path: str | Path) -> Catalog:
    """Solo carga JSON local: NO realiza descargas ni verifica firmas."""
    raw = Path(path).read_text(encoding="utf-8")
    try:
        data = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise CatalogError("JSON invalido") from exc
    return parse_catalog(data)
