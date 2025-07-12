#include "character_mappings.h"
#include "esphome/core/log.h"
#include <algorithm>

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "char_map";

CharacterMappingManager::CharacterMappingManager() {
  initialize_default_mappings();
  ESP_LOGI(TAG, "Initialized character mapping manager with %zu mappings", mappings_.size());
}

void CharacterMappingManager::initialize_default_mappings() {
  // Clear any existing mappings
  mappings_.clear();
  
  // Add all mapping categories
  add_czech_mappings();
  add_emoji_mappings();
  add_special_symbol_mappings();
}

void CharacterMappingManager::add_czech_mappings() {
  // Czech lowercase letters (confirmed from mb_char_map.md)
  mappings_["á"] = "\x0e\x20";  // \x0e\x20 = á
  mappings_["í"] = "\x0e\x21";  // \x0e\x21 = í
  mappings_["ó"] = "\x0e\x22";  // \x0e\x22 = ó
  mappings_["ú"] = "\x0e\x23";  // \x0e\x23 = ú
  mappings_["ň"] = "\x0e\x24";  // \x0e\x24 = ň
  mappings_["š"] = "\x0e\x28";  // \x0e\x28 = š
  mappings_["ř"] = "\x0e\x29";  // \x0e\x29 = ř
  mappings_["é"] = "\x0e\x82";  // \x0e\x82 = é
  mappings_["ď"] = "\x0e\x83";  // \x0e\x83 = ď
  mappings_["č"] = "\x0e\x87";  // \x0e\x87 = č
  mappings_["ě"] = "\x0e\x88";  // \x0e\x88 = ě
  mappings_["ž"] = "\x0e\x91";  // \x0e\x91 = ž
  mappings_["ů"] = "\x0e\x96";  // \x0e\x96 = ů
  mappings_["ý"] = "\x0e\x98";  // \x0e\x98 = ý
  mappings_["ť"] = "\x0e\x9f";  // \x0e\x9f = ť
  
  // Czech uppercase letters (only ones explicitly defined in mb_char_map.md)
  mappings_["Ů"] = "\x96";      // Single byte! \x96 = Ů
  mappings_["Č"] = "\x0e\x80";  // \x0e\x80 = Č
  mappings_["Ď"] = "\x0e\x85";  // \x0e\x85 = Ď 
  mappings_["Ť"] = "\x0e\x86";  // \x0e\x86 = Ť
  mappings_["Ě"] = "\x0e\x89";  // \x0e\x89 = Ě
  mappings_["Á"] = "\x0e\x8f";  // \x0e\x8f = Á
  mappings_["É"] = "\x0e\x90";  // \x0e\x90 = É
  mappings_["Í"] = "\x7f";      // Single byte! \x7f = Í
  mappings_["Ň"] = "\x0e\xa5";  // \x0e\xa5 = Ň
  mappings_["Ž"] = "\x0e\x92";  // \x0e\x92 = Ž
  mappings_["Ó"] = "\x0e\x95";  // \x0e\x95 = Ó
  mappings_["Ú"] = "\x0e\x97";  // \x0e\x97 = Ú
  mappings_["Ý"] = "\x0e\x9d";  // \x0e\x9d = Ý
  mappings_["Š"] = "\x0e\x9b";  // \x0e\x9b = Š
  mappings_["Ř"] = "\x0e\x9e";  // \x0e\x9e = Ř
  
  
  ESP_LOGD(TAG, "Added Czech character mappings (verified against mb_char_map.md)");
}

void CharacterMappingManager::add_emoji_mappings() {
  // Transport emojis
  mappings_["🚌"] = "\x0e\x72";  // Bus (autobus - harmonika)
  mappings_["🚊"] = "\x0e\x73";  // Tram (trolejbus nebo šalina)
  mappings_["🚋"] = "\x0e\x73";  // Tram (alternative)
  mappings_["🚎"] = "\x0e\xf4";  // Trolleybus
  mappings_["🚂"] = "\x0e\x76";  // Steam locomotive (parohy)
  mappings_["🚆"] = "\x0e\x74";  // Train (trolejbus nebo vlak)
  mappings_["🚇"] = "\x0e\x74";  // Metro/subway
  mappings_["✈️"] = "\x0e\xf7";  // Airplane (letadlo)
  mappings_["🛩️"] = "\x0e\xf7";  // Small airplane
  
  // Medical/emergency emojis
  mappings_["🏥"] = "\x0e\x7a";  // Hospital (křížek/nemocnice)
  mappings_["⚕️"] = "\x0e\x7a";  // Medical symbol
  mappings_["🚑"] = "\x0e\x7a";  // Ambulance (maps to hospital symbol)
  mappings_["❤️"] = "\x0e\x7a";  // Heart (health-related)
  mappings_["💊"] = "\x0e\x7a";  // Pills (medical)
  mappings_["🩺"] = "\x0e\x7a";  // Stethoscope
  
  // Entertainment emojis
  mappings_["🎭"] = "\x0e\x2c";  // Theater masks (divadlo)
  mappings_["🎪"] = "\x0e\x2c";  // Circus tent
  mappings_["🎨"] = "\x0e\x2c";  // Art/culture
  mappings_["🎬"] = "\x0e\x2c";  // Movie clapper
  mappings_["🎵"] = "\x0e\x2c";  // Music note
  mappings_["🎶"] = "\x0e\x2c";  // Musical notes
  
  // Accessibility emojis
  mappings_["♿"] = "\x0e\x2f";  // Wheelchair (invalidní vozík)
  mappings_["🦽"] = "\x0e\x2f";  // Manual wheelchair
  mappings_["🦼"] = "\x0e\x2f";  // Motorized wheelchair
  
  // Navigation emojis
  mappings_["➡️"] = "\x0e\x2a";  // Right arrow (šipka doprava)
  mappings_["→"] = "\x0e\x2a";   // Right arrow (alternative)
  mappings_["↔️"] = "\x0e\xf0";  // Right arrow double (šipka doprava - tlustá - konečná stanice)
  mappings_["↔"] = "\x0e\xf0";   
  mappings_["⏩"] = "\x0e\xf0";   
  mappings_["⬅️"] = "\x0e\x7c";  // Left arrow (šipka doleva)
  mappings_["←"] = "\x0e\x7c";   // Left arrow (alternative)
  mappings_["⬆️"] = "\x0e\x7d";  // Up arrow (šipka nahoru)
  mappings_["↑"] = "\x0e\x7d";   // Up arrow (alternative)
  
  // Terminal/final stop emojis
  mappings_["🛑"] = "\x0e\x71";  // Stop sign (konečná zastávka)
  mappings_["🚏"] = "\x0e\x71";  // Bus stop
  mappings_["🚥"] = "\x0e\x71";  // Traffic light
  mappings_["🔚"] = "\x0e\x71";  // End symbol
  
  // Marine/nautical emojis
  mappings_["⚓"] = "\x0e\x75";  // Anchor (kotva)
  mappings_["🛳️"] = "\x0e\x75";  // Ship
  mappings_["⛵"] = "\x0e\x75";  // Sailboat
  mappings_["🚢"] = "\x0e\x75";  // Ship (alternative)

  // Misc emojis
  mappings_["🛡️"] = "\x0e\xff";  // Brno / Shield
  mappings_["🦌"] = "\x0e\xf8";  // Deer / Santa
  ESP_LOGD(TAG, "Added emoji mappings");
}

void CharacterMappingManager::add_special_symbol_mappings() {
  // Convert Unicode symbols to ASCII equivalents that the display can handle
  // The display predates Unicode and uses a custom multibyte solution
  
  // Convert Unicode ellipsis to ASCII dots (display can handle ASCII)
  mappings_["…"] = "...";  // Unicode ellipsis → three ASCII dots
  
  // Convert other Unicode punctuation to ASCII equivalents
  mappings_["'"] = "'";    // Left single quotation mark → ASCII apostrophe
  mappings_["'"] = "'";    // Right single quotation mark → ASCII apostrophe
  mappings_["–"] = "-";    // En dash → ASCII hyphen
  mappings_["—"] = "-";    // Em dash → ASCII hyphen
  
  // Note: ASCII characters (0-127) pass through unchanged
  // Only Unicode characters need to be converted to ASCII equivalents
  
  ESP_LOGD(TAG, "Added special symbol mappings (Unicode → ASCII conversion)");
}

void CharacterMappingManager::add_mapping(const std::string &utf8_sequence, const std::string &display_encoding, const char* description) {
  if (utf8_sequence.empty() || display_encoding.empty()) {
    ESP_LOGW(TAG, "Ignoring empty mapping");
    return;
  }
  
  auto existing = mappings_.find(utf8_sequence);
  if (existing != mappings_.end()) {
    ESP_LOGW(TAG, "Overwriting existing mapping for '%s': '%s' -> '%s'", 
             utf8_sequence.c_str(), existing->second.c_str(), display_encoding.c_str());
  }
  
  mappings_[utf8_sequence] = display_encoding;
  ESP_LOGD(TAG, "Added mapping: '%s' -> display encoding (%s)", 
           utf8_sequence.c_str(), description ? description : "custom");
}

std::string CharacterMappingManager::encode_for_display(const std::string &text) {
  std::string result;
  result.reserve(text.length() * 2);  // Reserve extra space for potential expansions
  
  for (size_t i = 0; i < text.length(); ) {
    // Try to find the longest matching UTF-8 sequence
    std::string longest_match;
    std::string longest_encoding;
    
    // Check for multi-byte UTF-8 sequences (up to 4 bytes)
    for (size_t len = std::min(static_cast<size_t>(4), text.length() - i); len > 0; --len) {
      std::string candidate = text.substr(i, len);
      auto mapping = mappings_.find(candidate);
      
      if (mapping != mappings_.end()) {
        if (candidate.length() > longest_match.length()) {
          longest_match = candidate;
          longest_encoding = mapping->second;
        }
      }
    }
    
    if (!longest_match.empty()) {
      // Found a mapping, use it
      result += longest_encoding;
      i += longest_match.length();
    } else {
      // No mapping found, check if it's standard ASCII
      unsigned char c = static_cast<unsigned char>(text[i]);
      if (c <= 0x7F) {
        // Standard ASCII - keep as is
        result += c;
        i++;
      } else {
        // Non-ASCII character without mapping - try to handle gracefully
        // Skip this byte and continue (could also replace with '?' or space)
        ESP_LOGV(TAG, "No mapping found for character at position %zu (0x%02X)", i, c);
        
        // Try to determine UTF-8 sequence length and skip it properly
        if ((c & 0xE0) == 0xC0) {
          // 2-byte sequence
          i += 2;
        } else if ((c & 0xF0) == 0xE0) {
          // 3-byte sequence
          i += 3;
        } else if ((c & 0xF8) == 0xF0) {
          // 4-byte sequence
          i += 4;
        } else {
          // Invalid UTF-8 or single byte
          i++;
        }
        
        // Add a space as fallback
        result += ' ';
      }
    }
  }
  
  return result;
}

} // namespace b48_display_controller
} // namespace esphome 