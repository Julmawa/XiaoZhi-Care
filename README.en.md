# XiaoZhi Care

**Personal Memory & Care Assistant**

[Español](README.md) · [Português](README.pt-BR.md)

XiaoZhi Care is an open-source extension for **XiaoZhi ESP32** designed to support older adults through local personal memory, reminders, everyday assistance and a family-friendly configuration interface.

> **Current status:** `v0.3.2-alpha`  
> **Validated base:** XiaoZhi `v2.5.0`  
> **Validated hardware:** ESP32-S3 N16R8  
> **Validated release profile:** `bread-compact-wifi`  
> **License:** MIT

## Main features

- Local personal memory.
- Web configuration panel.
- MCP integration with XiaoZhi.
- Daily reminders.
- Conversational pillbox assistance.
- Visual confirmation flow for unverified past doses.
- Personalized reminder audio.
- Internet radio.
- LED status interface.
- Windows installer with a full backup before any modification.
- Byte-for-byte rollback to the previous state.
- Multilingual installer: Español, English and Português.

## End-user installation

The precompiled release **does not replace the base XiaoZhi installation**.

Before installing XiaoZhi Care:

1. XiaoZhi must already be installed and working.
2. Display, microphone and speaker must work correctly.
3. The board must match the hardware profile supported by the release.
4. The installer verifies chip, Flash, PSRAM, security, version and profile before writing.

The installer first creates a **complete 16 MB backup** and never performs a full-chip erase.

See [docs/INSTALLATION.md](docs/INSTALLATION.md).

## First binary release compatibility

Validated for:

- ESP32-S3 N16R8.
- 16 MB Flash.
- 8 MB PSRAM.
- Working XiaoZhi 2.5.0 installation.
- `bread-compact-wifi` profile.
- Secure Boot disabled.
- Flash Encryption disabled.

A precompiled firmware image contains its hardware configuration and cannot automatically preserve arbitrary custom pin mappings.

## Privacy

XiaoZhi Care follows a **Local First** approach for Care-specific personal memory and stored data.

This does not mean the entire XiaoZhi stack is offline. Speech recognition, models and TTS may still use the backend configured in XiaoZhi. See [docs/PRIVACY.md](docs/PRIVACY.md).

## Medication and safety

XiaoZhi Care may assist with pillbox reminders, but it **is not a medical device** and does not diagnose, prescribe or decide treatment.

See [docs/SAFETY.md](docs/SAFETY.md).

## Upstream project

XiaoZhi Care is derived from [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32), distributed under the MIT License.

The validated base for this release is **XiaoZhi v2.5.0**. Original copyright and license notices are preserved.

See [UPSTREAM.md](UPSTREAM.md).

## License

MIT. See [LICENSE](LICENSE).
