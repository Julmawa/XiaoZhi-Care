# XiaoZhi Care

**Personal Memory & Care Assistant**

[English](README.en.md) · [Português](README.pt-BR.md)

XiaoZhi Care es una extensión open source para **XiaoZhi ESP32** orientada a acompañar a personas mayores mediante memoria personal local, recordatorios, asistencia cotidiana y una interfaz simple para la familia.

> **Estado actual:** `v0.3.2-alpha`  
> **Base validada:** XiaoZhi `v2.5.0`  
> **Hardware validado:** ESP32-S3 N16R8  
> **Perfil de release validado:** `bread-compact-wifi`  
> **Licencia:** MIT

## Objetivo

El proyecto busca mantener la autonomía de la persona usuaria sin reemplazar a la familia, cuidadores ni profesionales. La información personal de XiaoZhi Care se administra localmente en la ESP32 y puede configurarse desde un panel web.

## Funciones principales

- Memoria personal local.
- Panel web de configuración.
- Integración MCP con XiaoZhi.
- Recordatorios cotidianos.
- Asistencia conversacional para el pastillero.
- Confirmación visual de tomas pendientes.
- Audios personalizados asociados a recordatorios.
- Radio por Internet.
- Estados visuales mediante LEDs.
- Instalador de Windows con backup completo antes de modificar la placa.
- Restauración byte por byte del estado anterior.
- Instalador multidioma: Español, English y Português.

## Instalación para usuarios finales

La release precompilada **no reemplaza la instalación base de XiaoZhi**.

Antes de instalar XiaoZhi Care:

1. XiaoZhi debe estar instalado y funcionando.
2. Pantalla, micrófono y parlante deben funcionar correctamente.
3. La placa debe coincidir con el hardware y perfil soportados por la release.
4. El instalador verificará chip, Flash, PSRAM, seguridad, versión y perfil antes de escribir.

El instalador crea primero un **backup completo de 16 MB** y no ejecuta un borrado total de la memoria.

Consulta [docs/INSTALLATION.md](docs/INSTALLATION.md).

## Compatibilidad de la primera release

La primera release binaria está validada únicamente para:

- ESP32-S3 N16R8.
- 16 MB Flash.
- 8 MB PSRAM.
- XiaoZhi 2.5.0 previamente funcional.
- Perfil `bread-compact-wifi`.
- Secure Boot desactivado.
- Flash Encryption desactivado.

> Un firmware precompilado contiene su configuración de hardware. No puede conservar automáticamente pinouts personalizados arbitrarios.

## Privacidad

XiaoZhi Care sigue un enfoque **Local First** para su memoria personal y datos Care.

Esto no significa que todo XiaoZhi funcione sin servicios externos: el reconocimiento de voz, modelos y TTS pueden depender del backend configurado en XiaoZhi. Consulta [docs/PRIVACY.md](docs/PRIVACY.md).

## Medicación y seguridad

XiaoZhi Care puede ayudar con recordatorios y orientación sobre un pastillero, pero **no es un dispositivo médico** y no diagnostica, prescribe ni decide tratamientos.

Consulta [docs/SAFETY.md](docs/SAFETY.md).

## Arquitectura

Los módulos Care están diseñados para mantenerse separados del núcleo de XiaoZhi:

- `care-core`
- `care-storage`
- `care-web`
- `care-mcp`
- `care-ui`
- `care-daily`
- `care-radio`

Más información: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Desarrollo

La rama pública se genera desde una copia limpia del árbol de desarrollo. No deben publicarse:

- `build/`
- dumps de Flash
- backups
- logs privados
- credenciales Wi-Fi
- datos personales
- binarios temporales
- archivos locales del entorno de desarrollo

Antes de publicar una release se debe ejecutar el escaneo de prepublicación y completar [docs/RELEASE_CHECKLIST.md](docs/RELEASE_CHECKLIST.md).

## Proyecto base

XiaoZhi Care deriva de [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32), distribuido bajo licencia MIT.

La base validada de esta versión es **XiaoZhi v2.5.0**. Los avisos de copyright y licencia del proyecto original se conservan.

Consulta [UPSTREAM.md](UPSTREAM.md).

## Licencia

XiaoZhi Care se distribuye bajo la licencia MIT. Consulta [LICENSE](LICENSE).

---

> “La tecnología no reemplaza el cariño de la familia; la tecnología ayuda a que ese cariño llegue más lejos.”
