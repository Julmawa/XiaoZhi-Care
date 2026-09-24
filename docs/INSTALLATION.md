# Installation

## Important concept

XiaoZhi Care is installed **on top of an already functioning XiaoZhi installation**.

The first public binary release is not a universal blank-board image.

## Validated release requirements

- ESP32-S3 N16R8.
- 16 MB Flash.
- 8 MB PSRAM.
- XiaoZhi 2.5.0 already installed and working.
- Release profile `bread-compact-wifi`.
- Secure Boot disabled.
- Flash Encryption disabled.
- Windows computer with USB access to the board.

## Before installing

Verify manually that the current XiaoZhi installation has:

- working display;
- working microphone;
- working speaker;
- working Wi-Fi.

Close serial monitors and any application that is using the COM port.

## Installer flow

The Windows installer:

1. Verifies release file hashes.
2. Obtains and verifies the supported Espressif esptool build when needed.
3. Detects the serial port.
4. Verifies ESP32-S3.
5. Verifies 8 MB PSRAM.
6. Verifies 16 MB Flash.
7. Verifies Secure Boot and Flash Encryption are disabled.
8. Creates a complete 16 MB Flash backup.
9. Reads the current partition table and app descriptors.
10. Verifies the supported XiaoZhi version and board profile.
11. Requests explicit confirmation.
12. Migrates the partition layout when required.
13. Flashes the Care application and assets.
14. Verifies written data.
15. Resets the board.

## Data preservation

The installer does not run a full-chip erase and preserves XiaoZhi NVS.

For the first stock-XiaoZhi to Care migration, the final 2 MB of the previous assets area are repurposed for:

- `care_data`: 1 MB
- `voice`: 1 MB

The installer first creates a full Flash backup so the previous state can be restored exactly.

## Multilingual UI

The installer detects Windows UI culture and supports:

- Spanish (`es-*`)
- English (`en-*`)
- Portuguese (`pt-*`)

A manual language selector is available at startup. Unsupported UI languages fall back to English.

## Rollback

The rollback tool:

1. Validates the original 16 MB backup.
2. Creates another backup of the current Care state.
3. Requires an explicit restore confirmation.
4. Writes the original 16 MB image.
5. Reads the complete Flash again.
6. Compares SHA-256 against the original backup.

A matching SHA-256 demonstrates byte-for-byte restoration.

## Release rule

Never publish a binary installer only because it was successfully generated. Perform one final end-to-end installation test using the **exact ZIP that will be attached to the GitHub Release**.
