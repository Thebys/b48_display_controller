#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include <vector>
#include <string>
#include <ctime>
#include <queue>
#include <map>
#include <memory>
#include <mutex>

#include <sqlite3.h>
#include <Arduino.h>
#include <LittleFS.h>

namespace esphome {
namespace b48_display_controller {

// Message entry structure as per specs
struct MessageEntry {
  bool is_ephemeral = false;
  int message_id = -1;         // -1 for ephemeral
  int priority = 50;           // 0-100
  time_t expiry_time = 0;      // When this message expires
  time_t last_display_time = 0; // Timestamp when message was last displayed
  int remaining_displays = 0;  // For ephemeral messages
  int line_number = 0;        // Line number to display
  int tarif_zone = 0;         // Tariff zone to display
  std::string static_intro;   // Static intro text (zI command)
  std::string scrolling_message; // Main scrolling message (zM command)
  std::string next_message_hint; // Next stop hint (v command)
};

// Display state enum
enum DisplayState {
  TRANSITION_MODE,
  MESSAGE_PREPARATION,
  DISPLAY_MESSAGE
};

class B48DisplayController : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return esphome::setup_priority::LATE; }

  // Configuration setters
  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  void set_database_path(const std::string &path) { this->database_path_ = path; }
  void set_transition_duration(int duration) { this->transition_duration_ = duration; }
  void set_time_sync_interval(int interval) { this->time_sync_interval_ = interval; }
  void set_emergency_priority_threshold(int threshold) { this->emergency_priority_threshold_ = threshold; }
  void set_min_seconds_between_repeats(int seconds) { this->min_seconds_between_repeats_ = seconds; }
  
  // For testing: expose checksum function
  uint8_t test_checksum(const std::string &payload) { return this->calculate_checksum(payload); }

  // Message management
  bool add_persistent_message(int priority, int line_number, int tarif_zone, 
                            const std::string &static_intro, const std::string &scrolling_message,
                            const std::string &next_message_hint, int duration_seconds = 0,
                            const std::string &source_info = "");
  
  bool update_persistent_message(int message_id, int priority, bool is_enabled,
                              int line_number, int tarif_zone, const std::string &static_intro,
                              const std::string &scrolling_message, const std::string &next_message_hint,
                              int duration_seconds = 0, const std::string &source_info = "");
  
  bool delete_persistent_message(int message_id);
  
  bool add_ephemeral_message(int priority, int line_number, int tarif_zone,
                           const std::string &static_intro, const std::string &scrolling_message,
                           const std::string &next_message_hint, int display_count = 1,
                           int ttl_seconds = 300);

 protected:
  // Database methods
  bool init_database();
  bool refresh_message_cache();
  void check_expired_messages();
  
  // Display algorithm methods
  std::shared_ptr<MessageEntry> select_next_message();
  int calculate_display_duration(const std::shared_ptr<MessageEntry> &msg);
  void update_message_display_stats(const std::shared_ptr<MessageEntry> &msg);
  
  // BUSE120 protocol methods
  void send_line_number(int line);
  void send_tarif_zone(int zone);
  void send_static_intro(const std::string &text);
  void send_scrolling_message(const std::string &text);
  void send_next_message_hint(const std::string &text);
  void send_time_update();
  void switch_to_cycle(int cycle);
  void send_commands_for_message(const std::shared_ptr<MessageEntry> &msg);
  void send_serial_command(const std::string &payload);
  uint8_t calculate_checksum(const std::string &payload);
  
  // Display state machine methods
  void run_transition_mode();
  void run_message_preparation();
  void run_display_message();
  void display_fallback_message();
  void check_for_emergency_messages();
  
  // Member variables
  uart::UARTComponent *uart_{nullptr};
  std::string database_path_;
  int transition_duration_{4};
  int time_sync_interval_{60};
  int emergency_priority_threshold_{95};
  int min_seconds_between_repeats_{30};
  
  sqlite3 *db_{nullptr};
  
  // Message cache
  std::vector<std::shared_ptr<MessageEntry>> persistent_messages_;
  std::vector<std::shared_ptr<MessageEntry>> ephemeral_messages_;
  std::shared_ptr<MessageEntry> current_message_{nullptr};
  
  // State tracking
  DisplayState state_{TRANSITION_MODE};
  unsigned long state_change_time_{0};
  unsigned long last_time_sync_{0};
  time_t current_time_{0};
  
  // Threading protection
  std::mutex message_mutex_;
};

}  // namespace b48_display_controller
}  // namespace esphome 