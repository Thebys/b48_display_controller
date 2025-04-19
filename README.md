
# Base48 Tram Display Controller

An ESPHome component for controlling BUSE120 LED display found in Base48, Brno. Has message handling capabilities. This system enables prioritized message queueing (ephemeral and persistent), transitions between messages, and Home Assistant integration. Messages are stored in a SQLite database on the ESP32 filesystem or maintained in memory with configurable display parameters (priority, duration, line number, etc.). The controller follows the BUSE120 serial communication protocol found in the display itself, so this is limited and will need significant changes for any other purpose.

# Stack
- ESP32 with at least 4 MB FLASH (fits tight)
- Partition (~ 512 KB) for LittleFS, see specs/hardware-info.md for more info
- SQLite database residing on the LittleFS

# Usage
Probably get in touch, it will be difficult to get this running. See specs/ for some documentation.

# Credits
Great support by [Base48 hackerspace](https://base48.cz/), Brno. THX!
<br><br>
<p align="center">
<a href="https://base48.cz/" target="_blank">
  <img width="460px" src="https://raw.githubusercontent.com/hackerspace/logo/refs/heads/master/logo.png"></a>
</p>