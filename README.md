# CraftOS-ESP
Currently a testing repo for various ESP32 experiments. Will eventually hold a port of CraftOS-PC for ESP32S3.

## Requirements
- System: ESP32-S3 module with 16+ MB flash, 2+ MB PSRAM (32/8 ideal)
- VGA: VGA breakout port/connector and a resistor bridge for DACs (reference: 3x ~710 ohm + 6x ~1400 ohm)
- SD: SD/microSD breakout/port with full SD pins (no SPI only boards)
- Audio: 3.5mm jack, RC filter over PWM output recommended
- USB: USB-OTG adapter (micro USB to USB-A) if using devkit, otherwise USB-A port

## Wiring
![pinout](pinout.png)