---
description: Information about hardware side of things - boards, pins, etc...
globs: 
alwaysApply: false
---
For ESP32, recomended Serial TX is GPIO17 (D17 (sic!) on dev kit) with UART2. Generally out of scope.

## LittleFS Initialization Notes

- Initial `LittleFS.begin()` call might fail on some boards.
- A retry with format enabled might be necessary: `LittleFS.begin(true, "/littlefs", 4, "spiffs")`.
- **Important:** The parameters `format_if_failed=true`, `base_path="/littlefs"`, `max_files=4`, and especially the partition label `"spiffs"` are likely specific to the board's partition table configuration. The label `"spiffs"` is common default, but may vary. Adjust to your needs or reformat partitions.
- **Path Usage:** When LittleFS is mounted with a specific `base_path` (e.g., `/littlefs`), file operations (especially SQLite using `sqlite3_open`) must use the full path, including the base path (e.g., `/littlefs/your_file.db`). Accessing `/your_file.db` directly will likely fail. Tested.


## ESP32 4 MB Flash Partition table with 512 KB msg memory:
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
otadata,  data, ota,     0xe000,  0x2000,
app0,     app,  ota_0,   0x10000, 0x170000,
app1,     app,  ota_1,   0x180000,0x170000,
spiffs,   data, spiffs,  0x350000,0x080000,
coredump, data, coredump,0x3F0000,0x10000,
