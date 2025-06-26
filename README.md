# Base48 Tram Display Controller

💡 **Transform your old IBIS display into a smart home device for ~300 CZK!**

An ESPHome component that breathes new life into vintage BUSE120 LED displays by converting them into modern, Home Assistant-integrated smart displays. Originally developed for the BUSE120 display found in Base48 hackerspace, Brno, this solution offers a cost-effective alternative to expensive legacy hardware. ⚠️ This is pre-alpha version.

## 🚀 Why This Project?

**💰 Cost-Effective**: Replace expensive legacy computers with a simple ESP32 (~300 CZK) + basic level shifters (3.3V ↔ 5V ↔ 24V)

**🏠 Smart Home Ready**: Direct Home Assistant integration - control your display from anywhere

**⚡ More Flexible**: Modern software stack vs. outdated proprietary systems

**🔧 Easy to Maintain**: Open-source ESPHome ecosystem instead of obsolete hardware

## 🎯 Features

- **📨 Message Management**: Prioritized message queueing (ephemeral and persistent)
- **🔄 Smart Transitions**: Smooth message transitions and display effects  
- **🏠 Home Assistant Integration**: Native ESPHome component with full HA support
- **💾 Persistent Storage**: SQLite database on ESP32 filesystem (LittleFS)
- **⚙️ Configurable Display**: Priority, duration, line positioning, and more
- **📡 Remote Control**: Web interface(work in progress) and HA/ESPHome API(working) for message management
- **🔧 IBIS Protocol**: Native support for original display communication (work in progress)

## 🛠️ Hardware Requirements

- **ESP32** with at least 4 MB FLASH (~300 CZK)
- **Level Shifters**: Simple circuits for 3.3V ↔ 5V ↔ 24V conversion
- **LittleFS Partition**: ~512 KB for database storage (see `specs/hardware-info.md`)

*Total cost: Much cheaper than maintaining old computers or buying proprietary controllers!*

## 📋 Usage

See `b48dc.example.yaml` for example ESPHome device configuration.

**Getting Started**: This project requires some electronics knowledge for the level shifter setup. Feel free to reach out for help - integrating legacy displays can be tricky! Check `specs/` directory for documentation (note: under active development).

## ⚠️ Compatibility Note

This component is specifically designed for IBIS serial communication protocol. Adapting it for other display types will require significant modifications to the protocol implementation. The current implementation uses like 7 basic commands and Brno style cycle 0 for message display, cycle 6 @ 5 seconds (next stop) for transition.

## 🙏 Credits

Huge thanks to [Base48 hackerspace](https://base48.cz/), Brno for their support and providing the testing environment!

<br><br>
<p align="center">
<a href="https://base48.cz/" target="_blank">
  <img width="460px" src="https://raw.githubusercontent.com/hackerspace/logo/refs/heads/master/logo.png"></a>
</p>