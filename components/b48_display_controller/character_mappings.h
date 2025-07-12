#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace esphome {
namespace b48_display_controller {

/**
 * @brief Character mapping entry for display encoding
 */
struct CharacterMapping {
  std::string utf8_sequence;      // UTF-8 input sequence
  std::string display_encoding;   // Display encoding (e.g., "\x0e\x20")
  const char* description;        // Human-readable description
};

/**
 * @brief Character mapping manager for BUSE120 display
 * 
 * This class manages the mapping between UTF-8 characters (including emojis)
 * and the display's internal encoding format.
 */
class CharacterMappingManager {
 public:
  /**
   * @brief Get the singleton instance
   */
  static CharacterMappingManager& get_instance() {
    static CharacterMappingManager instance;
    return instance;
  }

  /**
   * @brief Convert UTF-8 text to display encoding
   * @param text Input UTF-8 text
   * @return Text converted to display encoding
   */
  std::string encode_for_display(const std::string &text);

  /**
   * @brief Add a custom mapping
   * @param utf8_sequence UTF-8 character sequence
   * @param display_encoding Display encoding sequence
   * @param description Human-readable description
   */
  void add_mapping(const std::string &utf8_sequence, const std::string &display_encoding, const char* description = nullptr);

  /**
   * @brief Get mapping statistics
   * @return Number of mappings currently loaded
   */
  size_t get_mapping_count() const { return mappings_.size(); }

 private:
  CharacterMappingManager();
  
  void initialize_default_mappings();
  void add_czech_mappings();
  void add_emoji_mappings();
  void add_special_symbol_mappings();
  
  std::unordered_map<std::string, std::string> mappings_;
  
  // Disable copy constructor and assignment
  CharacterMappingManager(const CharacterMappingManager&) = delete;
  CharacterMappingManager& operator=(const CharacterMappingManager&) = delete;
};

} // namespace b48_display_controller
} // namespace esphome 