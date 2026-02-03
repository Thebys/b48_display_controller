# Known Bugs and Fixes

## BUG-001: SQLite Database Files Not Persisting on LittleFS (ESP-IDF)

**Status:** FIXED
**Date Found:** 2026-01-30
**Date Fixed:** 2026-02-03
**Time to Fix:** 4 days
**Severity:** Critical - Complete data loss on every reboot

### Symptoms

- SQLite database files created successfully, data written and verified
- After `close()`, files appear to vanish - `stat()` returns -1 (file not found)
- On reboot, database is empty, all data lost
- Boot counter always shows 1
- Database schema version always 0
- "Bootstrapping default messages" every boot instead of loading existing data

### Diagnostic Output (Before Fix)

```
esp32_Close: /littlefs/messages.db size=8192 sync=0 close=0 exists=0 size_after=-1
```

- `size=8192` - fstat() shows file has 8KB before close
- `sync=0` - fsync() returns success
- `close=0` - close() returns success
- `exists=0` - stat() says file doesn't exist after close!
- `size_after=-1` - can't get size because file "doesn't exist"

### Root Cause

**Two bugs combined:**

1. **ESP-IDF LittleFS `stat()` bug:** The `stat()` function incorrectly returns -1 for files that actually exist on LittleFS. This is a known quirk of the ESP-IDF VFS layer with LittleFS.

2. **SQLite VFS using `O_TRUNC` unnecessarily:** The SQLite VFS implementation (`esp32.c`) used `O_CREAT | O_TRUNC` when opening files for read-write that "don't exist" (according to buggy `stat()`).

**The deadly sequence:**
1. SQLite creates database file, writes data, closes file
2. Next open: `esp32_Access()` calls `stat()` → returns -1 (LittleFS bug)
3. VFS thinks file doesn't exist → opens with `O_CREAT | O_TRUNC`
4. `O_TRUNC` **truncates the existing file to 0 bytes** - DATA DESTROYED!
5. SQLite sees empty file, reinitializes database

### The Fix

In `esp32-idf-sqlite3/esp32.c`, function `esp32_Open()`:

```c
// BEFORE (BROKEN):
if (result == 1) {
    oflags = O_RDWR;
} else {
    oflags = O_RDWR | O_CREAT | O_TRUNC;  // O_TRUNC destroys existing data!
}

// AFTER (FIXED):
if (result == 1) {
    oflags = O_RDWR;
} else {
    // NOTE: Don't use O_TRUNC - it can interfere with LittleFS file persistence
    oflags = O_RDWR | O_CREAT;  // O_CREAT alone is safe
}
```

**Why this works:** When a file already exists, `O_CREAT` alone does nothing - it just opens the file. Only `O_TRUNC` actively destroys data.

### Verification (After Fix)

```
[W][b48c.main:1195]:   BOOT COUNTER: 10
[I][b48c.db:314]: Database schema version: 1
[D][b48c.db:920]: Database already contains 8 active messages, skipping bootstrap.
```

### Files Changed

- `components/b48_display_controller/esp32-idf-sqlite3/esp32.c`
  - Removed `O_TRUNC` from file creation flags
  - Added diagnostic output for debugging

### Lessons Learned

1. **Never trust `stat()` on embedded filesystems** - actual file operations may work even when stat fails
2. **`O_TRUNC` is dangerous** - only use when you explicitly want to delete existing data
3. **Add extensive diagnostics early** - the fstat/stat comparison was key to finding this
4. **Test persistence across reboots** - unit tests that don't reboot miss this class of bugs

### Related Issues

- ESP-IDF LittleFS stat() returning incorrect results for valid files
- Similar issues reported in: https://github.com/espressif/esp-idf/issues/

### Test Impact

After this fix, some tests now "fail" with "table already exists" errors - these are actually **false negatives** proving that persistence works! Tests should be updated to use `CREATE TABLE IF NOT EXISTS`.

---

*Documented by Claude Code after 4 days of debugging*
