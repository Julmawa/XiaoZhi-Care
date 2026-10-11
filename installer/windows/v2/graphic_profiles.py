"""DP-045: declarative display preferences; never grants permission to flash.

Selected preferences are NOT proof that a matching verified assets package exists.
"""
from __future__ import annotations
from dataclasses import asdict, dataclass
import json
from pathlib import Path

FONT_SIZES = (22, 26, 30)
EMOJI_SIZES = (32, 64)
THEMES = ('oscuro', 'claro')

@dataclass(frozen=True)
class GraphicProfile:
    profile_id: str = 'accesible-30-64-oscuro'
    chip: str = 'ESP32-S3'
    board_sku: str = 'bread-compact-wifi-lcd'
    lcd_width: int = 240
    lcd_height: int = 240
    font_family: str = 'Alibaba PuHuiTi'
    font_px: int = 30
    emoji_family: str = 'Twemoji'
    emoji_px: int = 64
    theme: str = 'oscuro'
    resource_status: str = 'pendiente_verificacion'


def choose(font_px: int = 30, emoji_px: int = 64, theme: str = 'oscuro') -> GraphicProfile:
    if type(font_px) is not int or font_px not in FONT_SIZES:
        raise ValueError('Tamaño de letras no admitido')
    if type(emoji_px) is not int or emoji_px not in EMOJI_SIZES:
        raise ValueError('Tamaño de emojis no admitido')
    if theme not in THEMES:
        raise ValueError('Tema no admitido')
    return GraphicProfile(profile_id=f'fuente-{font_px}-emoji-{emoji_px}-{theme}',
                          font_px=font_px, emoji_px=emoji_px, theme=theme)


def prepare_selection(path: str | Path, profile: GraphicProfile) -> None:
    """Write preference only, never alter firmware, assets or hardware."""
    data = {'schema': 'xiaozhi-care-graphic-selection', 'schema_version': 1,
            'flash_authorized': False, 'selection': asdict(profile),
            'warning': 'Perfil solicitado. Se requiere paquete completo autenticado y compatible.'}
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def may_install(selection: GraphicProfile, *, resources_authenticated: bool,
                complete_resources: bool, available_partition_bytes: int,
                package_bytes: int, target_chip: str, target_board: str,
                target_resolution: tuple[int, int]) -> bool:
    """One conservative precondition; not sufficient alone to authorize flashing."""
    return (resources_authenticated and complete_resources and
            selection.resource_status == 'verificado' and
            0 < package_bytes <= available_partition_bytes and
            target_chip == selection.chip and target_board == selection.board_sku and
            target_resolution == (selection.lcd_width, selection.lcd_height))
