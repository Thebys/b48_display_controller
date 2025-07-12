# Czech Character Support Implementation

## Overview

This document describes the implementation of Czech character support in the B48 display controller. The solution consists of two layers:

1. **Database Layer**: Preserves Czech characters in UTF-8 format
2. **Serial Protocol Layer**: Converts Czech characters to display encoding when sending to hardware

## Implementation Details

### Database Layer (Storage)

**File**: `b48_database_manager.cpp`

- **Function**: `sanitize_for_czech_display()`
- **Purpose**: Preserves Czech characters while converting problematic non-Czech characters
- **Behavior**: 
  - Keeps Czech characters in UTF-8 format (á, č, ď, é, ě, í, ň, ó, ř, š, ť, ú, ů, ý, ž)
  - Converts other characters (German, French, Polish) to ASCII equivalents
  - Stores messages in database with Czech characters intact

### Serial Protocol Layer (Display Output)

**File**: `buse120_serial_protocol.cpp`

- **Function**: `encode_czech_characters()`
- **Purpose**: Converts Czech UTF-8 characters to display's native encoding
- **Behavior**:
  - Converts Czech characters to `\x0e` prefix format (e.g., `á` → `\x0e\x20`)
  - Applied automatically in all text sending functions
  - Transparent to calling code

## Character Mapping

Based on `mb_char_map.md`.

## Code Flow

1. **Message Input**: User sends message with Czech characters
   ```
   "Příští zastávka: Náměstí Míru"
   ```

2. **Database Storage**: Message sanitized and stored
   ```cpp
   // Preserves Czech characters, converts problematic others
   sanitize_for_czech_display(message)
   ```

3. **Display Output**: When sending to display
   ```cpp
   // Converts to display encoding automatically
   encode_czech_characters(message)
   // Result: "P\x0e\x29\x0e\x21\x0e\x28\x0e\x86\x0e\x21..."
   ```

## Functions Modified

### Text Sending Functions (buse120_serial_protocol.cpp)
- `send_static_intro()` - Now encodes Czech characters
- `send_scrolling_message()` - Now encodes Czech characters  
- `send_next_message_hint()` - Now encodes Czech characters

### Character Processing (b48_database_manager.cpp)
- `add_persistent_message()` - Uses Czech-preserving sanitization
- Database storage operations - Preserve Czech characters

## Testing

Two test functions verify the implementation:

1. **`test_czech_character_preservation()`**
   - Verifies database layer preserves Czech characters
   - Confirms non-Czech characters are still converted

2. **`test_czech_character_encoding()`**
   - Verifies serial protocol layer converts Czech to display encoding
   - Tests individual character mappings
   - Confirms ASCII characters remain unchanged

## Benefits

1. **No Data Loss**: Czech characters preserved in database
2. **Seamless Integration**: No changes required to calling code
3. **Display Compatibility**: Proper encoding for hardware display
4. **Extensible**: Easy to add more character mappings if needed

## Usage

The implementation is transparent to users. Simply send messages with Czech text:

```cpp
controller.add_message(50, 1, 123, "Doprava", 
    "Příští zastávka: Náměstí Míru", "Další", 0);
```

The system will:
- Store the message with Czech characters intact
- Automatically convert to display encoding when showing on screen
- Display proper Czech characters on the hardware 