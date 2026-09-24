# Hardware

## First validated XiaoZhi Care release

- MCU: ESP32-S3 N16R8
- Flash: 16 MB
- PSRAM: 8 MB
- XiaoZhi baseline: 2.5.0
- Release profile: `bread-compact-wifi`

The development prototype also uses:

- INMP441 I2S microphone
- MAX98357A I2S amplifier
- small diagnostic display
- WS2812B LEDs as the primary everyday visual language
- speaker
- USB-C power/programming connection

## Important

The public binary release is compiled for one validated hardware profile.

A board that also contains an ESP32-S3 N16R8 is not automatically compatible if its:

- display controller;
- audio wiring;
- GPIO mapping;
- LED configuration;
- peripherals

differ from the release profile.

Additional hardware variants should be published as separate, explicitly named firmware releases.
