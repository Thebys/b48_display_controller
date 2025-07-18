#pragma once

#include <string>
#include <vector>
#include <memory>
#include <ctime>  // For time_t in MessageEntry
#include <sqlite3.h>
#include "character_mappings.h"
// Remove the circular dependency
// #include "b48_display_controller.h" // For MessageEntry struct

namespace esphome {
namespace b48_display_controller {

// Copy the MessageEntry definition here
struct MessageEntry {
  bool is_ephemeral = false;
  int message_id = -1;         // -1 for ephemeral
  int priority = 50;           // 0-100
  time_t expiry_time = 0;      // When this message expires
  time_t last_display_time = 0; // Timestamp when message was last displayed
  int line_number = 0;        // Line number to display
  int tarif_zone = 0;         // Tariff zone to display
  std::string static_intro;   // Static intro text (zI command)
  std::string scrolling_message; // Main scrolling message (zM command)
  std::string next_message_hint; // Next stop hint (v command)
  
  // Default constructor needed for std::make_shared<MessageEntry>()
  MessageEntry() = default;
  
  // Constructor for creating simple messages (used for loading/fallback messages)
  MessageEntry(int message_id, int line_number, int tarif_zone, 
               const std::string &static_intro, const std::string &scrolling_message,
               const std::string &next_message_hint, int priority = 50, bool is_ephemeral = false)
      : is_ephemeral(is_ephemeral), message_id(message_id), priority(priority),
        line_number(line_number), tarif_zone(tarif_zone), static_intro(static_intro),
        scrolling_message(scrolling_message), next_message_hint(next_message_hint) {}
};

class B48DatabaseManager {
 public:
  explicit B48DatabaseManager(const std::string &db_path);
  ~B48DatabaseManager();

  // Initialization and Schema Management
  bool initialize();
  bool wipe_database();  // Add method to wipe the database

  // Message Operations
  bool add_persistent_message(int priority, int line_number, int tarif_zone,
                              const std::string &static_intro, const std::string &scrolling_message,
                              const std::string &next_message_hint, int duration_seconds,
                              const std::string &source_info, bool check_duplicates = true);

  bool update_persistent_message(int message_id, int priority, bool is_enabled,
                               int line_number, int tarif_zone, const std::string &static_intro,
                               const std::string &scrolling_message, const std::string &next_message_hint,
                               int duration_seconds, const std::string &source_info);

  bool delete_persistent_message(int message_id);

  std::vector<std::shared_ptr<MessageEntry>> get_active_persistent_messages();

  // Maintenance
  int expire_old_messages(); // Returns number of messages expired

  int purge_disabled_messages(); // Returns number of disabled messages physically deleted from the database

  int get_message_count(); // <-- ADDED: Get count of active persistent messages

  bool clear_all_messages(); // <-- ADDED: Method to clear all persistent messages

  // Dump all messages (including disabled) for debugging
  void dump_all_messages();

  // Bootstrapping
  bool bootstrap_default_messages();

  // Convert non-ASCII characters to their ASCII equivalents (use only when ASCII is required)
  static std::string convert_to_ascii(const std::string &str);

  // Preserve Czech characters, convert only problematic non-Czech characters (preferred for message display)
  static std::string sanitize_for_czech_display(const std::string &str);

  // Minimal sanitization for database storage - only converts problematic Unicode to ASCII
  static std::string sanitize_for_database_storage(const std::string &str);

 private:
  // Helper for schema creation/migration
  bool check_and_create_schema(); 

  std::string database_path_;
  sqlite3 *db_{nullptr};

  // Disable copy and assign
  B48DatabaseManager(const B48DatabaseManager&) = delete;
  B48DatabaseManager& operator=(const B48DatabaseManager&) = delete;
};

} // namespace b48_display_controller
} // namespace esphome 