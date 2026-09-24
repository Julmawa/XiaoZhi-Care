# Validation status

## Core installer / rollback validation

Validated on the development prototype:

- ESP32-S3 detected correctly.
- 8 MB PSRAM detected.
- 16 MB Flash detected.
- Secure Boot disabled detected.
- Flash Encryption disabled detected.
- Full 16 MB pre-install backup completed.
- Care installation completed.
- Written firmware data verified.
- Original XiaoZhi state restored.
- Complete restored Flash re-read.
- Restored SHA-256 matched the original backup exactly.
- Care was installed again successfully after rollback.

This validates the cycle:

`XiaoZhi -> XiaoZhi Care -> original XiaoZhi -> XiaoZhi Care`

## Multilingual layer

Validated without touching Flash:

- Windows UI culture detection.
- Spanish selection for `es-*`.
- English support.
- Portuguese support.
- Manual language override.
- No-BOM batch launcher requirement.
- Translation table duplicate-key checks.
- PowerShell progress rendering test.

## Still required before public release

Perform one full end-to-end install using the **final V1.8 release ZIP**, then verify:

- boot;
- Wi-Fi;
- display;
- microphone;
- speaker;
- Care web panel;
- LEDs;
- reminders;
- radio.

This final smoke test is intentionally left as a release gate because V1.8 adds the internationalization layer after the already validated installer core.
