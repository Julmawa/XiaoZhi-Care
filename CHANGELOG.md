# Changelog

All notable XiaoZhi Care changes are documented here.

## [0.3.2-alpha] - 2026-09

### Added

- Local Care storage and schema initialization.
- Web management panel for personal Care data.
- MCP integration for Care queries.
- Daily reminders and separation between medication-related and non-medication queries.
- Pillbox assistance with time-aware behavior and visual confirmation of past unverified doses.
- Personalized reminder audio support.
- Internet radio support.
- Installer with automatic board checks and complete 16 MB backup.
- Full rollback workflow with byte-for-byte SHA-256 verification.
- Multilingual installer interface: Spanish, English and Portuguese.
- Windows UI language auto-detection with manual override.
- Visible progress handling based on PowerShell `Write-Progress`.

### Validated

- XiaoZhi 2.5.0 baseline.
- ESP32-S3 N16R8.
- 16 MB Flash + 8 MB PSRAM.
- `bread-compact-wifi` release profile.
- Full cycle: XiaoZhi -> Care -> XiaoZhi rollback -> Care.
- Exact rollback image verification by SHA-256.

### Alpha limitations

- First binary release is hardware-profile-specific.
- Arbitrary custom pin mappings are not preserved by a precompiled firmware image.
- Final end-to-end smoke test must be repeated with the exact release ZIP before publishing each GitHub Release.
