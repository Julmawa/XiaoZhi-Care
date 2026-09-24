# XiaoZhi Care v0.3.2-alpha

First public alpha release of XiaoZhi Care.

## Validated platform

- XiaoZhi 2.5.0
- ESP32-S3 N16R8
- 16 MB Flash
- 8 MB PSRAM
- `bread-compact-wifi`

## Highlights

- Local personal Care memory.
- Family configuration web panel.
- MCP integration.
- Daily reminders.
- Pillbox-oriented conversational assistance.
- Personalized audio reminders.
- Internet radio.
- Full pre-install Flash backup.
- Exact rollback workflow.
- Spanish / English / Portuguese installer UI.

## Installer safety

The installer verifies the supported platform before writing and creates a complete 16 MB backup first.

The rollback workflow has been validated by reading the restored Flash back and matching its SHA-256 byte for byte against the original backup.

## Alpha limitations

The precompiled firmware is hardware-profile-specific.

Do not install this release on arbitrary ESP32-S3 hardware or custom GPIO mappings merely because the MCU and memory size match.

## Upgrade note

This alpha is intended for controlled testing. Keep the installer-generated backup until the new installation has been fully validated.
