"""DP-045 selection prototype. Generates metadata, never flashes."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent / 'v2'))
from graphic_profiles import choose, prepare_selection

print('XiaoZhi Care V2 - DP-045 (preparacion, NO instala firmware)')
print('Letras: 22 / 26 / 30 px  [30 recomendado]')
print('Emojis: 32 / 64 px      [64 recomendado]')
print('Tema: oscuro / claro    [oscuro recomendado]')
try:
    font = int(input('Tamaño de letras [30]: ').strip() or '30')
    emoji = int(input('Tamaño de emojis [64]: ').strip() or '64')
    theme = input('Tema [oscuro]: ').strip().lower() or 'oscuro'
    profile = choose(font, emoji, theme)
    output = Path(__file__).parent / 'seleccion_grafica_dp045.json'
    prepare_selection(output, profile)
except (ValueError, EOFError) as error:
    print(f'Configuracion rechazada: {error}')
    raise SystemExit(1)
print(f'Seleccion guardada en: {output}')
print('PENDIENTE: recursos completos y autenticados, validacion de particion, y conexion al instalador.')
print('No se ha grabado ni modificado ningun dispositivo.')
