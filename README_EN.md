# ESP32-S3 8-Channel Wireless EMG Acquisition System

This repository documents a prototype built from an EMG analog front end, an ADS131M08 eight-channel ADC, an ESP32-S3(R8) controller, and a separate ESP8266EX Wi-Fi board. The repository includes the EDA Pro design archives supplied by the author.

## Verified hardware records

- Analog front-end schematic instances include INA333, two OPA333 devices, and three electrode symbols.
- The ADC design includes ADS131M08 and an 8.192 MHz clock component, with repeated four-pin input connectors.
- The 2026-10-29 controller design uses ESP32-S3(R8) and XT25F16 external flash. An earlier 2026-10-25 revision uses ESP32-C3FH4 and is a different design iteration.
- The Wi-Fi design includes ESP8266EX and W25Q80 external flash. UART connection and UDP transmission are described by the project context; exact protocol settings remain unverified.

The repository includes an Arduino single-channel ESP32-S3 prototype, an ADS131M08 eight-channel acquisition prototype, and an ESP8266EX UART-to-UDP forwarding prototype. The eight-channel and Wi-Fi code have not been compiled or verified on the hardware; pin mapping and ADC configuration require validation against the latest board. A UDP receiver application has not been supplied. See [hardware](docs/hardware.md), [communication](docs/communication.md), and [software](docs/software.md) for details and open items.

Hardware design files are licensed under CERN-OHL-W-2.0; software source is licensed under MIT. See `LICENSE`, `LICENSE.hardware`, and `LICENSE.software`. Other documentation, photos, and third-party materials are not automatically covered. Confirm redistribution rights for embedded third-party symbols, footprints, and models before public release.
