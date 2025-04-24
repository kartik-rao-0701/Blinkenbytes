#ESP32-S3 Based Six-Layer Smart Display PCB
This repository contains the design files and documentation for a custom six-layer PCB powered by the ESP32-S3 microcontroller. The board is designed for advanced audio-visual applications with HUB75 LED matrix displays, audio playback, and modern USB-C integration—all packed into a compact, highly optimized board.

##Key Features
#ESP32-S3 module (16MB Flash, 8MB PSRAM) *recommended
#Six-layer bare PCB for signal stability and thermal performance
#Onboard microSD card storage for audio, image, and data assets
#MAX98357AETE+T I2S Digital Audio Amplifier
#Supports up to 8Ω speakers
#High-fidelity sound from ESP32 using I2S
#HUB75 Interface
 :Direct onboard HUB75 connector (16 GPIO)
 :Optimized for 64x32 and similar LED matrix panels

#USB Type-C (USB 2.1, 16-pin)
First-in-industry integration for power + UART + expansion
Supports fast prototyping and modern device compatibility
UART Bridge using CP2102N-A02-GQFN28R
Fully compliant with Silicon Labs specs
Known errata handled and resolved in design

Signal Shifting using Nexperia 74AHCT245PW

Shift register-based level shifter

Ensures safe 5V logic interfacing with HUB75 displays

Improves display stability and reduces ghosting

PSRAM pin optimization

GPIO assignments carefully mapped

##If you need internal PSRAM, remove conflicting components as noted in schematic

##Design Philosophy & Notes
Shift registers are used: Nexperia’s 74AHCT245PW has been chosen for its speed and reliability to shift ESP32's 3.3V signals safely to 5V for HUB75 matrix communication.

Extensive research and testing was done before choosing this setup; the decision to use shift registers was backed by technical documentation and real-world validation.

#No additional multiplexing or daisy-chaining required—pure performance via dedicated GPIOs.

#Designed for minimal EMI, low signal latency, and robust mechanical routing.

##Design Files
EasyEDA and Altium Designer files available

Note: Altium files are converted and may contain minor issues

Beta release – some traces or nets might need manual verification

#BOM file included for all components

##Power & Connectivity
Power via USB-C (5V/5A recommended)

UART connection supported over USB-C via CP2102N

HUB75 display power routed through onboard connectors

Audio amplifier supplied via dedicated 5V LDO rail

##Usage Notes
ESP32-S3 firmware must be flashed using USB or UART

Connect a HUB75 panel and speaker

Insert microSD card with content

Run firmware to control visuals and audio

If using PSRAM, refer to schematic and remove/reconfigure components occupying PSRAM-designated GPIOs

What’s Included
Schematic and layout files (EasyEDA + Altium)

BOM file

Documentation of known hardware conflicts (PSRAM/display overlap)

Sample firmware

##Future Plans
Add capacitive touch buttons

OTA firmware update support

Automatic speaker detection

Battery support and power-saving modes

##License
This project is licensed under the MIT License. Feel free to fork, modify, and contribute.
