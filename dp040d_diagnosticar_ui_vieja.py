from pathlib import Path
from datetime import datetime
import urllib.request

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
SRC = ROOT / "components" / "care-web" / "care_web_page.cc"
BIN = ROOT / "build" / "default" / "xiaozhi.bin"
URL = "http://192.168.0.233:8080"

MARKERS = [
    "DP040D_FASE1_NAV_VISUAL",
    "careNavGroup",
    "Perfil principal",
    "Presión arterial",
]

OLD_MARKERS = [
    '>Perfil</button>',
    '>Cuidados</button>',
    '>Presión</button>',
    '>Mantenimiento</button>',
]

print("DP-040D - DIAGNOSTICO DE UI")
print("=" * 60)
print("Fecha:", datetime.now().isoformat(timespec="seconds"))
print()

def inspect_text_file(path):
    print("ARCHIVO FUENTE")
    print("-" * 60)
    if not path.exists():
        print("NO EXISTE:", path)
        return None
    text = path.read_text(encoding="utf-8", errors="replace")
    print("Ruta:", path)
    print("Tamaño:", path.stat().st_size, "bytes")
    print("Modificado:", datetime.fromtimestamp(path.stat().st_mtime))
    for m in MARKERS:
        print(f"{m:32} ->", m in text)
    print("Marcadores antiguos:")
    for m in OLD_MARKERS:
        print(f"{m:32} ->", m in text)
    print()
    return text

def inspect_bin(path):
    print("BINARIO COMPILADO")
    print("-" * 60)
    if not path.exists():
        print("NO EXISTE:", path)
        return
    data = path.read_bytes()
    print("Ruta:", path)
    print("Tamaño:", path.stat().st_size, "bytes")
    print("Modificado:", datetime.fromtimestamp(path.stat().st_mtime))
    for m in MARKERS:
        print(f"{m:32} ->", m.encode("utf-8") in data)
    print()

def inspect_device(url):
    print("PÁGINA SERVIDA POR LA ESP32")
    print("-" * 60)
    print("URL:", url)
    try:
        req = urllib.request.Request(
            url,
            headers={
                "User-Agent": "Mozilla/5.0",
                "Cache-Control": "no-cache",
                "Pragma": "no-cache",
            },
        )
        with urllib.request.urlopen(req, timeout=5) as r:
            raw = r.read()
            html = raw.decode("utf-8", errors="replace")
            print("HTTP:", r.status)
            print("Bytes:", len(raw))
            print("Headers:")
            for k, v in r.headers.items():
                if k.lower() in {"cache-control", "etag", "last-modified", "content-type"}:
                    print(f"  {k}: {v}")
            for m in MARKERS:
                print(f"{m:32} ->", m in html)
            print("Marcadores antiguos:")
            for m in OLD_MARKERS:
                print(f"{m:32} ->", m in html)
            print()
            return html
    except Exception as e:
        print("ERROR consultando ESP32:", repr(e))
        print()
        return None

src = inspect_text_file(SRC)
inspect_bin(BIN)
device = inspect_device(URL)

print("CONCLUSIÓN AUTOMÁTICA")
print("-" * 60)

src_new = src is not None and "DP040D_FASE1_NAV_VISUAL" in src
dev_new = device is not None and "DP040D_FASE1_NAV_VISUAL" in device
bin_new = BIN.exists() and b"DP040D_FASE1_NAV_VISUAL" in BIN.read_bytes()

if not src_new:
    print("La modificación NO está en care_web_page.cc.")
    print("Hay que volver a ejecutar el parche DP-040D Fase 1.")
elif src_new and not bin_new:
    print("La fuente está modificada, pero el binario NO contiene la nueva UI.")
    print("Conviene ejecutar: idf.py fullclean ; idf.py build ; idf.py flash")
elif src_new and bin_new and device is not None and not dev_new:
    print("La fuente y el binario tienen la nueva UI, pero la ESP32 sirve la vieja.")
    print("Probablemente se flasheó otro binario/partición o el flash no se actualizó.")
elif src_new and bin_new and dev_new:
    print("La ESP32 YA sirve la nueva UI.")
    print("Si el navegador muestra la vieja, es caché del navegador.")
    print("Usar Ctrl+Shift+R o Ctrl+F5, o abrir una ventana de incógnito.")
else:
    print("No pude determinarlo automáticamente. Revisar las tres secciones anteriores.")
