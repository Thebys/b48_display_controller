# Character Mapping System

The B48 Display Controller now includes a comprehensive character mapping system that handles Czech characters, emojis, and special symbols for the BUSE120 display.

## Overview

The character mapping system converts UTF-8 input text to the display's internal encoding format. This allows you to use:
- Czech characters (á, č, ř, š, etc.)
- Emojis (🚌, 🏥, ✈️, etc.)
- Special symbols (arrows, ellipsis, etc.)

## Features

### 🇨🇿 Czech Character Support
All Czech characters are properly mapped to their display equivalents:
- **Lowercase**: á, č, ď, é, ě, í, ň, ó, ř, š, ť, ú, ů, ý, ž
- **Uppercase**: Á, Č, Ď, É, Ě, Í, Ň, Ó, Ř, Š, Ť, Ú, Ů, Ý, Ž

### 🚌 Transport Emojis
Perfect for public transport displays:
- 🚌 Bus → Display bus symbol
- 🚊 🚋 Tram → Display tram symbol  
- 🚎 Trolleybus → Display trolleybus symbol
- 🚆 🚇 Train/Metro → Display train symbol
- 🚂 Steam locomotive → Display locomotive symbol
- ✈️ 🛩️ Airplane → Display airplane symbol

### 🏥 Medical/Emergency Emojis
- 🏥 Hospital → Display cross/hospital symbol
- 🚑 Ambulance → Display cross/hospital symbol
- ⚕️ Medical symbol → Display cross/hospital symbol
- ❤️ Heart → Display cross/hospital symbol
- 💊 Pills → Display cross/hospital symbol
- 🩺 Stethoscope → Display cross/hospital symbol

### 🎭 Entertainment Emojis
- 🎭 Theater masks → Display theater symbol
- 🎪 Circus → Display theater symbol
- 🎨 Art → Display theater symbol
- 🎬 Movie → Display theater symbol
- 🎵 🎶 Music → Display theater symbol

### ♿ Accessibility Emojis
- ♿ Wheelchair → Display wheelchair symbol
- 🦽 Manual wheelchair → Display wheelchair symbol
- 🦼 Motorized wheelchair → Display wheelchair symbol

### ➡️ Navigation Emojis
- ➡️ → Right arrow → Display right arrow
- ⬅️ ← Left arrow → Display left arrow
- ⬆️ ↑ Up arrow → Display up arrow
- ⬇️ ↓ Down arrow → Display down arrow

### 🛑 Stop/Terminal Emojis
- 🛑 Stop sign → Display final stop symbol
- 🚏 Bus stop → Display final stop symbol
- 🚥 Traffic light → Display final stop symbol
- 🔚 End symbol → Display final stop symbol

### ⚓ Marine/Nautical Emojis
- ⚓ Anchor → Display anchor symbol
- 🛳️ 🚢 Ship → Display anchor symbol
- ⛵ Sailboat → Display anchor symbol

## Usage Examples

### Basic Czech Text
```cpp
std::string czech_text = "Příští zastávka: Česká";
std::string encoded = mapper.encode_for_display(czech_text);
// Result: "Příští zastávka: Česká" with proper character encoding
```

### Transport Messages
```cpp
std::string bus_message = "🚌 Autobus č. 12 → Centrum";
std::string encoded = mapper.encode_for_display(bus_message);
// Bus emoji converts to bus symbol, arrow converts to arrow symbol
```

### Medical Emergency
```cpp
std::string emergency = "Emergency: 🚑 → 🏥 rychle!";
std::string encoded = mapper.encode_for_display(emergency);
// Both emojis convert to hospital/cross symbol
```

### Mixed Content
```cpp
std::string mixed = "Linka 48: Směr 🏥 nemocnice";
std::string encoded = mapper.encode_for_display(mixed);
// Czech characters and hospital emoji properly converted
```

## Technical Details

### Display Limitations
**Important**: The BUSE120 display predates Unicode and uses a custom multibyte solution. It cannot handle Unicode characters directly.

### Character Encoding Strategy
The system uses a two-tier approach:

#### 1. Unicode → ASCII Conversion
- **Unicode ellipsis "…"** → **ASCII dots "..."** 
- **Unicode quotes " "** → **ASCII quotes " "**
- **Unicode dashes "–" "—"** → **ASCII hyphen "-"**

#### 2. Special Character Encoding
- **Czech characters** → **Custom display encoding with `\x0e` prefix**
- **Emojis** → **Custom display symbols with `\x0e` prefix**
- **ASCII characters (0-127)** → **Pass through unchanged**

### Data Flow Examples
```
Database Storage (minimal sanitization):
Input: "Loading…"           → Stored: "Loading..."
Input: "Text "quoted""      → Stored: "Text \"quoted\""
Input: "Příště 🚌"          → Stored: "Příště 🚌" (Czech chars & emojis preserved)

Display Output (full encoding):
Stored: "Příště 🚌"        → Display: "Pří\x0e\x28t\x0e\x88 \x0e\x72"
Stored: "Hospital: 🏥"     → Display: "Hospital: \x0e\x7a"
```

### Mapping Algorithm
1. **Longest Match**: Find the longest matching UTF-8 sequence
2. **Multi-byte Support**: Handles 1-4 byte UTF-8 sequences (including emojis)
3. **Conversion Priority**: Unicode → ASCII → Display encoding
4. **ASCII Preservation**: Standard ASCII characters pass through unchanged
5. **Fallback**: Unknown characters are replaced with spaces

## API Reference

### CharacterMappingManager Class

```cpp
// Get singleton instance
auto& mapper = CharacterMappingManager::get_instance();

// Convert text to display encoding
std::string encoded = mapper.encode_for_display(input_text);

// Add custom mapping
mapper.add_mapping("💡", "\x0e\x77", "Light bulb -> airplane symbol");

// Get mapping count
size_t count = mapper.get_mapping_count();
```

### Integration with Protocol

The system is automatically used by:
- `BUSE120SerialProtocol::encode_czech_characters()`
- `B48DatabaseManager::sanitize_for_czech_display()`

## Adding Custom Mappings

You can add custom character mappings at runtime:

```cpp
auto& mapper = CharacterMappingManager::get_instance();

// Map a custom emoji to an existing display symbol
mapper.add_mapping("🎯", "\x0e\x71", "Target -> final stop symbol");

// Map a custom symbol
mapper.add_mapping("§", "\x0e\x2c", "Section sign -> theater symbol");
```

## Performance Considerations

- **Initialization**: Mappings are loaded once at startup
- **Memory**: Uses hash map for O(1) lookup performance
- **Processing**: Efficient longest-match algorithm
- **Caching**: Singleton pattern ensures single instance

## Troubleshooting

### Characters Not Displaying
1. Check if character is in the mapping table
2. Verify UTF-8 encoding of input text
3. Add custom mapping if needed
4. Check display firmware compatibility

### Unexpected Symbols
1. Verify the mapping is correct for your display version
2. Check `mb_char_map.md` for display-specific symbols
3. Test with simpler characters first

### Performance Issues
1. Avoid very long strings (>500 chars)
2. Use appropriate log levels to reduce debug output
3. Consider pre-encoding frequently used strings

## Future Enhancements

- **Dynamic Loading**: Load mappings from configuration files
- **Locale Support**: Different mapping sets for different languages
- **Display Detection**: Automatic mapping based on display type
- **Validation**: Runtime validation of mapping correctness

## References

- `mb_char_map.md`: Display character mapping reference
- `character_mapping_example.cpp`: Usage examples and tests
- `BUSE120SerialProtocol`: Protocol implementation
- `B48DatabaseManager`: Database integration 