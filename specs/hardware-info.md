---
description: Information about hardware side of things - boards, pins, etc...
globs: 
alwaysApply: false
---
For ESP32, recomended Serial TX is GPIO17 (D17 on dev kit) with UART2. Generally out of scope.

## LittleFS Initialization Notes

- Initial `LittleFS.begin()` call might fail on some boards.
- A retry with format enabled might be necessary: `LittleFS.begin(true, "/littlefs", 4, "spiffs")`.
- **Important:** The parameters `format_if_failed=true`, `base_path="/littlefs"`, `max_files=4`, and especially the partition label `"spiffs"` are likely specific to the board's partition table configuration. The label `"spiffs"` is common default, but may vary. Adjust to your needs or reformat partitions.