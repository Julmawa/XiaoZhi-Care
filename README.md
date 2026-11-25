# XiaoZhi Care

Asistente de voz para una persona mayor que vive sola, basado en el firmware de
[XiaoZhi AI](https://github.com/78/xiaozhi-esp32) (ESP32‑S3). Funciona como un
**radio despertador moderno** para la mesita de luz: le dice qué día y qué hora es,
le recuerda lo que tiene que hacer, la conecta con su familia por Telegram y le avisa
a la familia si algo no anda bien.

Todo se maneja con **un solo botón táctil** y la voz. No hay nada que aprender.

## Qué hace

| Función | Descripción |
|---|---|
| 🗓️ **Orientación en el tiempo** | "¿Qué día es hoy?": fecha y hora argentinas en palabras simples, llamándola por su nombre. |
| ⏰ **Recordatorios con voz** | A la hora indicada XiaoZhi habla solo ("Tengo un aviso"). Se confirman tocando el botón o diciendo "listo"; si no, se repiten cada 10 minutos (hasta 3 veces) y la familia recibe un aviso por Telegram. |
| 💊 **Pastillero** | Qué remedio va en cada casillero (Mañana / Tarde / Noche × 7 días), dosis y cómo es cada pastilla. Recordatorio a la hora de cada franja. |
| 👨‍👩‍👧 **Familia** | Nombres, apodos, teléfonos, direcciones y cumpleaños: "¿Cuál es el teléfono de Mati?". |
| 🌅 **Saludo al despertar** | Con el radar detecta cuándo se levanta después de dormir y la saluda con frases elegidas por la familia. |
| 💚 **Control de bienestar** | Si pasan muchas horas sin señales (radar, botón o voz), avisa a la familia por Telegram. |
| 📰 **Noticias** | Las 3 noticias más importantes de Argentina (Infobae / La Nación), evitando policiales y tragedias. |
| 💬 **Telegram** | Mensajes de texto y de voz en los dos sentidos con cada familiar (bot de BotFather), alta de familiares con `/vincular`. |
| ⚙️ **Página de configuración** | Dentro del propio equipo (`http://IP-del-equipo`), con 4 pestañas: Familia, Recordatorios, Pastillero y Ajustes. Copia de seguridad para exportar e importar. |

Para saber la dirección de la página: preguntarle a XiaoZhi **"¿dónde me conecto?"**
(la dice y la muestra en la pantalla), o mirar el mensaje que manda por Telegram al arrancar.

## Hardware

Dos equipos, cada uno con su configuración (CMake preset):

| | Equipo de pruebas (`default`) | Equipo de mamá (`madre`) |
|---|---|---|
| Placa | ESP32‑S3 N16R8 | ESP32‑S3 N16R8 |
| Pantalla | ST7789 240×240 (SPI) | OLED SSD1306 128×64 (I2C: SDA 41, SCL 42) |
| Micrófono INMP441 | WS 4, SCK 5, SD 6 | WS 4, SCK 5, SD 6 |
| Amplificador MAX98357A | DIN 7, BCLK 15, LRC 16 | DIN 7, BCLK 15, LRC 16 |
| Botón táctil TTP223 | GPIO 38 (activo en alto) | GPIO 38 (activo en alto) |
| Tira WS2812 (7 LEDs) | GPIO 48 | GPIO 48 |
| Radar LD2410C | – | TX del radar → GPIO 17, RX del radar → GPIO 18 (**necesita fuente de 5 V**) |

Colores de la tira: azul = escuchando, verde = hablando, naranja = conectando, rojo = error.

## Compilar

Requiere **ESP‑IDF v6.0.3** y Visual Studio Code con la extensión de ESP‑IDF.

1. Elegir la configuración en la barra de abajo: **`default`** (equipo de pruebas) o
   **`Madre (equipo de mamá, OLED)`**.
2. Configurar con *menuconfig* → **Telegram Bot**: token del bot, chat IDs autorizados.
   ⚠️ El token queda en `build/<config>/sdkconfig`, que **no** se sube a GitHub.
3. "Build, Flash and Monitor".

La configuración `madre` usa la tabla de particiones `partitions/v2/16m_madre.csv`,
que agrega una partición NVS propia de 64 KB (`madre`) para los datos de la familia,
los recordatorios, el pastillero y los ajustes.

## Estructura del código propio

| Carpeta / archivo | Contenido |
|---|---|
| `main/telegram/` | Bot de Telegram (mensajes, voz, familia, `/vincular`). |
| `main/madre/` | Equipo de mamá: página web, datos, reloj, recordatorios, saludo, bienestar, noticias y herramientas para XiaoZhi. |
| `main/boards/bread-compact-wifi/radar_ld2410.h` | Lector y configuración del radar LD2410C. |
| `main/led/single_led.cc` | Colores de la tira de LEDs. |
| `main/audio/codecs/no_audio_codec.cc` | Filtro del micrófono (pasa‑altos 120 Hz). |

## Pendiente

- Conexión con el pastillero ESP32‑C3 (lectura de `/api/state` por WiFi) para saber si
  ya tomó los remedios.

## Créditos

Basado en [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) (licencia MIT).
El README original está en [`docs/README_xiaozhi_original.md`](docs/README_xiaozhi_original.md).
