# ESP32-S3 Based Six-Layer Smart Display PCB

This repository contains the design files and documentation for a **custom six-layer PCB** powered by the **ESP32-S3 microcontroller**. The board is designed for advanced **audio-visual applications** with HUB75 LED matrix displays, audio playback, and modern USB-C integration—all packed into a compact, highly optimized board.

---

## 🔧 Key Features

- **ESP32-S3 module** (16MB Flash, 8MB PSRAM *recommended*)
- **Six-layer bare PCB** for enhanced signal stability and thermal performance
- **Onboard microSD card storage** for audio, image, and data assets
- **MAX98357AETE+T I2S Digital Audio Amplifier**
  - Supports up to **8Ω speakers**
  - High-fidelity sound via I2S interface
- **HUB75 LED Matrix Interface**
  - Onboard **HUB75 connector** (16 GPIOs)
  - Optimized for **64x32 and similar RGB matrix panels**
- **USB Type-C (USB 2.1, 16-pin)**
  - First-in-industry integration for **power + UART + expansion**
  - Supports fast prototyping and modern devices
- **UART Bridge using CP2102N-A02-GQFN28R**
  - Compliant with Silicon Labs specs
  - **Errata handled** and resolved
- **Signal Shifting using Nexperia 74AHCT245PW**
  - Shift-register-based level shifter
  - Converts ESP32’s 3.3V signals safely to 5V for HUB75
  - Ensures display stability and reduces ghosting
- **PSRAM Pin Optimization**
  - GPIOs mapped with care
  - ⚠️ *If you need internal PSRAM, remove or reconfigure conflicting components as marked in the schematic*

---

## 🧠 Design Philosophy & Notes

- **Shift registers are used**: Specifically, the **Nexperia 74AHCT245PW** has been chosen for its high-speed performance and compatibility.
- Design is based on **thorough research and technical literature**.
- **No additional multiplexing or daisy-chaining**—this is a **pure GPIO performance** build.
- Crafted for **minimal EMI**, **low signal latency**, and **mechanical robustness**.

---

## 📁 Design Files

- ✅ **EasyEDA and Altium Designer** files included
  - ⚠️ *Altium files are converted* and currently in **beta** — some traces or nets may require manual validation
- ✅ **Bill of Materials (BOM)** file included

---

## 🔌 Power & Connectivity

- **Power input:** USB-C (Recommended: 5V / 5A)
- **UART connection** supported over USB-C via CP2102N
- **HUB75 display power** routed through onboard connector
- **Audio amplifier** powered via dedicated **5V LDO rail**

---

## 🚀 Usage Instructions

1. Flash ESP32-S3 firmware via USB or UART.
2. Connect your HUB75 matrix panel and speaker.
3. Insert your microSD card with the appropriate content.
4. Power up and enjoy visual + audio playback.
5. ⚠️ *If using PSRAM*, check the schematic and remove any components that conflict with PSRAM-designated GPIOs.

---

## 📦 What’s Included

- ✅ Schematic and layout files (EasyEDA + Altium)
- ✅ Bill of Materials (BOM)
- ✅ Documentation of known GPIO conflicts (PSRAM / HUB75 overlap)
- ✅ Sample firmware (optional)

---

## 🔮 Future Plans

- Add capacitive touch buttons
- OTA firmware update support
- Automatic speaker detection
- Battery support and power-saving modes

---

## 📜 License

This project is licensed under the **MIT License**.  
Feel free to **fork**, **modify**, and **contribute** to make it even better!

---

