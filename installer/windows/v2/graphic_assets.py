"""DP-045 offline assets inspection and composition. Never accesses a serial port.

The voice/model payload and every non-graphic resource come from the selected
firmware build. A graphic-only download is never distributed as a complete pack.
"""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path
import struct

LIMIT = 0x600000
TABLE = struct.Struct('<32sIIHH')
THEME_KEY = 'care_graphic_theme_v1'
EMOTIONS = frozenset(('neutral happy laughing funny sad angry crying loving '
                     'embarrassed surprised shocked thinking winking cool relaxed '
                     'delicious kissy confident sleepy silly confused').split())


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_pack(data: bytes) -> dict[str, bytes]:
    if not 12 <= len(data) <= LIMIT:
        raise ValueError('Assets fuera del limite de particion de 6 MiB')
    count, checksum, length = struct.unpack_from('<III', data)
    if not 1 <= count <= 1024 or length != len(data) - 12 or count * TABLE.size > length:
        raise ValueError('Cabecera assets invalida o archivo truncado')
    if sum(data[12:]) & 0xffff != checksum:
        raise ValueError('Checksum assets incorrecto')
    result = {}
    intervals = []
    base = 12 + count * TABLE.size
    for i in range(count):
        raw, size, offset, _, _ = TABLE.unpack_from(data, 12 + i * TABLE.size)
        name = raw.split(b'\0', 1)[0].decode('ascii')
        if not name or name in result or any(c in name for c in '/\\:') or name in ('.', '..'):
            raise ValueError('Nombre de recurso invalido o duplicado')
        start, end = base + offset, base + offset + 2 + size
        if size == 0 or end > len(data) or data[start:start + 2] != b'ZZ':
            raise ValueError('Recurso fuera de limites o sin prefijo: ' + name)
        intervals.append((start, end))
        result[name] = data[start + 2:end]
    intervals.sort()
    cursor = base
    for start, end in intervals:
        if start != cursor:
            raise ValueError('Recursos superpuestos o datos sin referenciar')
        cursor = end
    if cursor != len(data):
        raise ValueError('Datos finales sin referenciar')
    return result


def write_pack(entries: dict[str, bytes]) -> bytes:
    table, payload = bytearray(), bytearray()
    for name, data in sorted(entries.items()):
        encoded = name.encode('ascii')
        if not 0 < len(encoded) < 32 or not data:
            raise ValueError('Recurso vacio o nombre demasiado largo')
        table += TABLE.pack(encoded, len(data), len(payload), 0, 0)
        payload += b'ZZ' + data
    body = table + payload
    result = struct.pack('<III', len(entries), sum(body) & 0xffff, len(body)) + body
    read_pack(result)
    return result


def index_of(entries: dict[str, bytes]) -> dict:
    index = json.loads(entries['index.json'])
    if not isinstance(index, dict) or index.get('version') != 1:
        raise ValueError('Version index.json no admitida')
    return index


def voice_of(entries: dict[str, bytes]) -> bytes:
    name = index_of(entries).get('srmodels')
    if not isinstance(name, str) or name not in entries or len(entries[name]) < 4:
        raise ValueError('Faltan modelos de reconocimiento de voz')
    voice = entries[name]
    count = struct.unpack_from('<I', voice)[0]
    if not 0 < count < 128 or len(voice) < 4 + count * 36:
        raise ValueError('Cabecera de modelos de voz invalida')
    return voice


def image_size(data: bytes) -> tuple[int, int]:
    if data[:6] in (b'GIF87a', b'GIF89a') and len(data) >= 13:
        return struct.unpack_from('<HH', data, 6)
    if data[:8] == b'\x89PNG\r\n\x1a\n' and len(data) >= 24 and data[12:16] == b'IHDR':
        return struct.unpack_from('>II', data, 16)
    raise ValueError('Formato de emoji no admitido')


def validate_graphics(entries: dict[str, bytes], size: int) -> None:
    index = index_of(entries)
    font = index.get('text_font')
    if not isinstance(font, str) or font not in entries or len(entries[font]) < 64:
        raise ValueError('Fuente ausente o truncada')
    emojis = index.get('emoji_collection')
    if not isinstance(emojis, list) or len(emojis) != len(EMOTIONS):
        raise ValueError('Coleccion de emojis incompleta')
    seen = set()
    for emoji in emojis:
        name, filename = emoji.get('name'), emoji.get('file')
        if name not in EMOTIONS or name in seen or filename not in entries or 'eaf' in emoji:
            raise ValueError('Emoji ausente, duplicado o incompatible')
        seen.add(name)
        if image_size(entries[filename]) != (size, size):
            raise ValueError('Resolucion de emoji incorrecta: ' + name)


def compose(base: bytes, graphics: bytes, theme: str) -> bytes:
    if theme not in ('oscuro', 'claro'):
        raise ValueError('Tema no admitido')
    original, source = read_pack(base), read_pack(graphics)
    original_index, graphic_index = index_of(original), index_of(source)
    original_voice = voice_of(original)
    validate_graphics(source, 64)
    result = dict(original)
    index = copy.deepcopy(original_index)
    # Only remove resources owned exclusively by the replaced graphic fields.
    old_graphics = {index.get('text_font')}
    old_graphics.update(e.get('file') for e in index.get('emoji_collection', []))
    for filename in old_graphics:
        if filename and filename != index.get('srmodels'):
            result.pop(filename, None)
    new_files = {graphic_index['text_font']}
    new_files.update(e['file'] for e in graphic_index['emoji_collection'])
    for filename in new_files:
        if filename in result and result[filename] != source[filename]:
            raise ValueError('El grafico sobrescribiria un recurso no grafico: ' + filename)
        result[filename] = source[filename]
    index['text_font'] = graphic_index['text_font']
    # Never advertise Noto glyph-push metadata for a PuHui font.
    index.pop('text_font_meta', None)
    index['emoji_collection'] = graphic_index['emoji_collection']
    if 'skin' in graphic_index:
        index['skin'] = graphic_index['skin']
    index[THEME_KEY] = 'dark' if theme == 'oscuro' else 'light'
    result['index.json'] = (json.dumps(index, ensure_ascii=False, separators=(',', ':')) + '\n').encode()
    data = write_pack(result)
    if voice_of(read_pack(data)) != original_voice:
        raise ValueError('Los modelos de voz cambiaron')
    validate_graphics(read_pack(data), 64)
    return data


def build_catalog(base_path: Path, source_path: Path, app_path: Path,
                  output: Path, pins: dict) -> dict:
    base, source, app = base_path.read_bytes(), source_path.read_bytes(), app_path.read_bytes()
    if sha(source) != pins['source_sha256']:
        raise ValueError('El recurso grafico no coincide con el origen revisado')
    if THEME_KEY.encode() + b'\0' not in app:
        raise ValueError('Recompilar firmware: falta soporte DP-045 para el tema seleccionado')
    source_entries = read_pack(source)
    font = source_entries[index_of(source_entries)['text_font']]
    if sha(font) != pins['font_sha256']:
        raise ValueError('La fuente no coincide con PuHuiTi 30 revisada')
    voice_hash = sha(voice_of(read_pack(base)))
    if output.exists():
        raise ValueError('La salida ya existe; usar otra carpeta para conservar el respaldo')
    output.mkdir(parents=True)
    profiles = []
    for theme in ('oscuro', 'claro'):
        identifier = f'fuente-30-emoji-64-{theme}'
        data = compose(base, source, theme)
        filename = identifier + '.bin'
        (output / filename).write_bytes(data)
        profiles.append({'profile_id': identifier, 'font_family': 'Alibaba PuHuiTi',
                         'font_px': 30, 'emoji_family': 'Twemoji', 'emoji_px': 64,
                         'theme': theme, 'file': filename, 'size': len(data),
                         'sha256': sha(data), 'srmodels_sha256': voice_hash})
    catalog = {'schema': 'xiaozhi-care-graphics-v2', 'schema_version': 1,
               'chip': 'ESP32-S3', 'board_sku': 'bread-compact-wifi-lcd',
               'lcd_width': 240, 'lcd_height': 240, 'partition_bytes': LIMIT,
               'app_sha256': sha(app), 'base_assets_sha256': sha(base),
               'srmodels_sha256': voice_hash, 'source': pins['source_url'],
               'source_sha256': sha(source), 'hardware_tested': False,
               'default_profile': 'fuente-30-emoji-64-oscuro', 'profiles': profiles,
               'unavailable': {'font_px': [22, 26], 'emoji_px': [32]}}
    (output / 'graphics.json').write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    return catalog


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', required=True, type=Path)
    parser.add_argument('--graphics', required=True, type=Path)
    parser.add_argument('--app', required=True, type=Path)
    parser.add_argument('--pins', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    try:
        catalog = build_catalog(args.base, args.graphics, args.app, args.output,
                                json.loads(args.pins.read_text(encoding='utf-8-sig')))
    except (ValueError, KeyError, OSError) as exc:
        parser.exit(2, str(exc) + '\n')
    print(json.dumps({'profiles': len(catalog['profiles']), 'srmodels_sha256': catalog['srmodels_sha256'],
                      'hardware_tested': False}))


if __name__ == '__main__':
    main()
