---
description: Documents available messages and their low level serial protocol implementation.
globs: 
alwaysApply: false
---
# Base48 BUSE120 Display Serial Protocol Specification
**File:** `buse120.serialspec`
**Date:** April 13, 2025
**Version:** 1.0
## Overview
This document specifies the serial communication protocol for controlling BUSE120 tram/bus display. It is based on common implementations and reverse-engineered logic. This protocol is used to send commands from a controller (e.g., ESP32) to the display unit.
## 1. Communication Parameters
*   **Interface:** Serial UART
*   **Baud Rate:** 1200 bps *(Standard IBIS rate)*
*   **Data Bits:** 7
*   **Parity:** Even
*   **Stop Bits:** 2
*   **Flow Control:** None
*   **Encoding:** ASCII for payload content.
*(Note: These parameters must be correctly configured in the sending device's UART settings, for example, within ESPHome's `uart:` component configuration.)*
## 2. Message Structure
All commands sent to the display follow a consistent byte sequence:
1.  **Payload:** A sequence of ASCII characters defining the command and its parameters. Specific formats are detailed under "Supported Commands".
2.  **Terminator:** A single Carriage Return (CR) byte (`0x0D`). This byte immediately follows the last byte of the **Payload**.
3.  **Checksum:** A single byte calculated over the **Payload** *and* the **Terminator**.
**Transmission Order:**
The bytes are sent sequentially over the serial line in this exact order:
`[Payload Bytes][Terminator Byte][Checksum Byte]`
**Checksum Calculation:**
The checksum is an 8-bit XOR sum, calculated as follows:
1.  Initialize an 8-bit unsigned integer checksum variable to `0x7F`.
2.  Iterate through each byte of the ASCII **Payload**:
*   `checksum = checksum XOR current_payload_byte`
3.  XOR the checksum with the **Terminator** byte (CR):
*   `checksum = checksum XOR 0x0D`
4.  The final value of the `checksum` variable is the Checksum byte transmitted.
## 3. Supported Commands
The following commands are supported. Payloads are represented using `printf`-style formatting where applicable. `<space>` indicates a literal ASCII space character (`0x20`).
### Set Line Number
*   **Payload Format:** `l%03d`
*   **Description:** Sets the primary line number displayed (e.g., route number). The number is zero-padded to 3 digits.
*   **Example Payloads:**
```
l005 // Set line to 5
l123 // Set line to 123
```
### Set Tariff Zone
*   **Payload Format:** `e%06ld`
*   **Description:** Sets tariff zone information. The number is zero-padded to 6 digits, but our specific display shows only last three digits. Usage depends on the specific display configuration set and progremmed via ancient gBUSE utility.
*   **Example Payload:**
```
e000123 // Set tariff zone to 123
```
### Set Destination Text (Message title / topic / static intro)
*   **Payload Format:** `zI<space><text>`
*   **Description:** Sets the text for the destination stop, bold with left inward arrow. Max 15 characters. Static without scroll. Visible in cycle 0, after about 5 seconds it scrolls left and gets replaced by scrolling message.
*   **Example Payload:**
```
zI Airport via Downtown
```
### Set Scrolling Message (`set_scroll_message`)
*   **Payload Format:** `zM<space><text>`
*   **Description:** Sets a scrolling message string. Often displayed in Cycle 0. Sensible limit to be something like 255 or 511 chars.
*   **Example Payload:**
```
zM Welcome to Base48 Hackerspace!
zM Barbecue every Friday - hackers and friends welcome! Bring food drink and good mood.
zM Please move to the rear
```
### Set Next Stop (message outro / next topic message)
*   **Payload Format:** `v<space><next_text>`
*   **Description:** Sets text displayed during transition phase (Cycle 6). Typically shows a hint about the next message topic (if known / available) or something goofy. Limited to approximately 15 characters.
*   **Example Payload:**
```
v Base48
v Novinky
v Friday BBQ
v stranka 2
v nacitam
v core dump
v kernel panic
```
### Set (update) time
*   **Payload Format:** `u%02d%02d`
*   **Description:** Updates time displayed on the unit Hour and minute are each zero-padded to 2 digits (24-hour format).
*   **Example Payloads:**
```
u0905 // 09:05
u1234 // 12:34
```
### Set Display Cycle (mode)
*   **Payload Format:** `xC<cycle_number>`
*   **Description:** Switches the display mode to a specific pre-defined "cycle" (typically a single digit 0-9). Common cycles:
*   `xC0`: Shows primary info (Line / Tarif zone / Static intro / Scroll message / time).
*   `xC6`: Shows next stop information(next stop text / time).
*   **Example Payloads:**
```
xC0 // Switch to Cycle 0 content
xC6 // Switch to Cycle 6 content
```
## 4. Typical Operational Sequence (Controlled by External Controller)
1.  **Load Data:** The external controller (e.g., ESP32) sends commands (Set Line, Set Intro, Set Scroll Message, etc.) as needed to populate the display's internal state for the desired message content.
2.  **Controlled Display Cycle:** The external controller manages the display timing and sequence. A typical flow managed by the controller is:
    *   Send `xC6` to switch to the transition/next stop display (Cycle 6).
    *   Send `v <next_text>` to update the next stop/message hint.
    *   Optionally, send `u%02d%02d` to update the time display (recommended **at least once per minute**, ideally during Cycle 6, synchronized from Home Assistant or another reliable time source).
    *   Wait for a short duration (e.g., 3-4 seconds).
    *   Send the commands for the main message content (`l`, `e`, `zI`, `zM`).
    *   Send `xC0` to switch to the main display (Cycle 0).
    *   Wait for the calculated message display duration.
    *   Repeat the cycle with the next message selected by the controller's logic.
3.  **Time Updates:** The display does not keep time independently. The external controller (e.g., ESP32, driven by Home Assistant) **must** periodically send the `u%02d%02d` command (**at least once per minute**) to keep the displayed time accurate. This synchronization is crucial for the display's timekeeping.
4.  **No Internal Itinerary:** The display relies entirely on the external controller to send commands and switch cycles (`xC0`, `xC6`). It does not automatically advance through messages or keep track of time without commands.
## 5. Message Length Limitations
* **Line Number:** 3 digits (limited by protocol format)
* **Tariff Zone:** 6 digits (shown as last 3 digits)
* **Static Intro:** 15 characters maximum (truncated if longer)
* **Scrolling Message:** 511 characters maximum
* **Next Stop Text:** 15 characters maximum
## 6. Error Handling
The BUSE120 display has no built-in acknowledgment mechanism. If a command fails due to invalid format or checksum errors, the display typically ignores it without notification.