# Telegram para xiaozhi-esp32 (ESP32-S3 N16R8)

Con esto tu XiaoZhi puede:

- **Recibir** mensajes de Telegram: los muestra en pantalla con un sonido de aviso, y cuando el equipo está libre el asistente los lee en voz alta.
- **Enviar** mensajes a Telegram: le decís, por ejemplo, *"mandá por Telegram que ya tomé el remedio"* y el asistente lo manda.
- **Responder** *"¿tengo mensajes de Telegram?"*: el asistente te lee los últimos que llegaron.
- **Notas de voz que llegan**: se reproducen por el parlante (máximo 60 segundos, configurable).
- **Mandar notas de voz**: le decís *"mandá un audio por Telegram"*. El asistente te avisa, la conversación se cierra, suena un pitido, se graba (10 s por defecto, podés pedir entre 3 y 30: *"mandá un audio de 20 segundos"*), suena otro pitido y se envía.

Sólo aceptan mensajes los chats que vos autorices. Los mensajes de cualquier otra persona se ignoran.

---

## 1. Crear el bot con BotFather (desde el celular)

1. En Telegram buscá **@BotFather** y escribile `/newbot`.
2. Elegí un nombre (ej.: `XiaoZhi de casa`) y un usuario que termine en `bot` (ej.: `xiaozhi_casa_bot`).
3. BotFather te da un **token**, con esta forma: `123456789:AAH...`. Guardalo y no lo compartas con nadie.
4. Abrí el chat con tu bot nuevo y tocá **Iniciar**.

## 2. Instalar el módulo en el proyecto

1. Descomprimí este zip en cualquier carpeta, por ejemplo `C:\xiaozhi-telegram`.
2. En VS Code abrí `C:\xiaozhi-esp32-main` y abrí la terminal de ESP-IDF (*ESP-IDF: Open ESP-IDF Terminal*).
3. Ejecutá:

   ```
   python C:\xiaozhi-telegram\instalar_telegram.py C:\xiaozhi-esp32-main
   ```

   El script copia `main\telegram\` y modifica 3 archivos: `main\Kconfig.projbuild`, `main\CMakeLists.txt` y `main\main.cc`. De cada uno guarda antes una copia `.bak`.

> **Para hacerlo a mano:** copiá la carpeta `main\telegram` al proyecto y después hacé estos tres cambios:
> - En `main\CMakeLists.txt`, antes de `idf_component_register(`, agregá
>   `list(APPEND SOURCES "telegram/telegram_bot.cc")` y `list(APPEND INCLUDE_DIRS "telegram")`.
> - En `main\Kconfig.projbuild`, pegá el contenido de `Kconfig.telegram` antes del último `endmenu`.
> - En `main\main.cc`, agregá `#include "telegram_bot.h"` y, justo antes de `app.Run();`, la línea `TelegramBot::GetInstance().Start();`.

## 3. Configurar (menuconfig)

1. Elegí tu placa como siempre (target `esp32s3` y el *Board Type* que ya usás).
2. Abrí menuconfig: en VS Code es el ícono del engranaje (*SDK Configuration Editor*), o desde la terminal `idf.py menuconfig`.
3. Andá a **Xiaozhi Assistant → Telegram Bot** y configurá:
   - **Enable Telegram bot**: activado.
   - **Bot token**: pegá el token de BotFather.
   - **Allowed chat IDs**: por ahora dejalo vacío. Lo completás en el paso 4.
4. Revisá que en **Xiaozhi Assistant** esté activado **Send Wake Word Data**. Lo necesita la opción "Assistant reads incoming messages aloud".

## 4. Averiguar tu chat_id

1. Compilá y flasheá (`idf.py build flash monitor`, o los botones de VS Code).
2. Cuando el XiaoZhi esté conectado, escribile **/id** a tu bot. Te contesta con tu **chat_id** (un número).
3. Volvé a menuconfig y poné ese número en **Allowed chat IDs**.
   - Para autorizar a varias personas, separá los números con comas: `123456789, 987654321`. El primero de la lista es quien recibe los mensajes que manda el XiaoZhi.
   - Para que los mensajes vayan a un grupo familiar, agregá el bot al grupo, escribí `/id` en el grupo y usá ese número (es negativo).
4. Compilá y flasheá de nuevo. Al arrancar, el bot te escribe "XiaoZhi está en línea."

## 5. Uso

| Querés… | Hacés… |
|---|---|
| Que el XiaoZhi diga algo | Escribís al bot desde Telegram |
| Mandar un mensaje desde el XiaoZhi | Decís "mandá por Telegram que…" |
| Escuchar los últimos mensajes | Preguntás "¿tengo mensajes de Telegram?" |
| Ver tu chat_id | Escribís `/id` al bot |
| Que el XiaoZhi reproduzca un audio | Le mandás una nota de voz al bot |
| Mandar un audio desde el XiaoZhi | Decís "mandá un audio por Telegram" y hablás después del pitido |

Si llega un mensaje mientras estás hablando con el asistente, no se corta la conversación: el mensaje queda en espera y se lee cuando el equipo vuelve a quedar libre.

## Problemas comunes

- **En el monitor sale `HTTP 401`**: el token está mal copiado.
- **En el monitor sale `chat_id ... no autorizado`**: falta tu número en *Allowed chat IDs*.
- **El mensaje aparece en pantalla pero el asistente no lo lee**: revisá que estén activados *Send Wake Word Data* y *Assistant reads incoming messages aloud*. Esta función le pasa el texto al servidor como si fuera la palabra de activación, y algunos servidores no lo procesan como pregunta. Si te pasa, avisame y lo resolvemos de otra manera.
- **El asistente no manda mensajes**: la herramienta aparece en el servidor después de reiniciar. Probá decirlo más explícito: "usá Telegram para mandar…".
- **Para volver atrás**: restaurá los archivos `.bak` y borrá `main\telegram`.

**Seguridad:** el token queda guardado en el archivo `sdkconfig`. Si subís el proyecto a GitHub, no subas ese archivo.
