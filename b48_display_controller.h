#pragma once

#include "esphome/core/component.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
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
// Include the full header instead of forward declaration
#include "b48_database_manager.h"
#include "buse120_serial_protocol.h"
#include "b48_ha_integration.h"

namespace esphome {
namespace b48_display_controller {

// Forward declare HA integration class
class B48HAIntegration;

// Message entry structure is now defined in b48_database_manager.h
// Don't redefine it here

// Display state enum
enum DisplayState {
  TRANSITION_MODE,
  MESSAGE_PREPARATION,
  DISPLAY_MESSAGE
};

class B48DisplayController : public Component {
 public:
  B48DisplayController() = default;
  ~B48DisplayController();

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return esphome::setup_priority::LATE; }

  // Configuration setters
  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; this->serial_protocol_.set_uart(uart); }
  void set_database_path(const std::string &path) { this->database_path_ = path; }
  void set_transition_duration(int duration) { this->transition_duration_ = duration; }
  void set_time_sync_interval(int interval) { this->time_sync_interval_ = interval; }
  void set_emergency_priority_threshold(int threshold) { this->emergency_priority_threshold_ = threshold; }
  void set_min_seconds_between_repeats(int seconds) { this->min_seconds_between_repeats_ = seconds; }
  void set_run_tests_on_startup(bool run_tests) { this->run_tests_on_startup_ = run_tests; }
  void set_wipe_database_on_boot(bool wipe) { this->wipe_database_on_boot_ = wipe; }
  
  /**
   * @brief Set a pin to be pulled high during setup to enable the display (testing only)
   * 
   * This is only needed for testing setups where an additional enable pin needs 
   * to be pulled high to activate the display. In production hardware, this should
   * be handled externally.
   * 
   * @param pin GPIO pin number to pull high
   */
  void set_display_enable_pin(int pin) { this->display_enable_pin_ = pin; }
  
  // HA Entity Setters (called from __init__.py)
  void set_message_queue_size_sensor(sensor::Sensor *sensor);

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

  // --- Public methods called by HA Integration Layer ---
  B48DatabaseManager* get_database_manager() { return db_manager_.get(); }
  bool clear_all_persistent_messages();

  // --- Internal helper to update HA state ---
  void update_ha_queue_size();

 protected:
  // Database methods
  bool init_database();
  bool refresh_message_cache();
  void check_expired_messages();
  
  // Display algorithm methods
  std::shared_ptr<MessageEntry> select_next_message();
  int calculate_display_duration(const std::shared_ptr<MessageEntry> &msg);
  void update_message_display_stats(const std::shared_ptr<MessageEntry> &msg);
  
  // BUSE120 protocol methods - now delegated to the serial_protocol_ object
  void send_line_number(int line);
  void send_tarif_zone(int zone);
  void send_static_intro(const std::string &text);
  void send_scrolling_message(const std::string &text);
  void send_next_message_hint(const std::string &text);
  void send_time_update();
  void send_invert_command();
  void switch_to_cycle(int cycle);
  void send_commands_for_message(const std::shared_ptr<MessageEntry> &msg);
  
  // Display state machine methods
  void run_transition_mode();
  void run_message_preparation();
  void run_display_message();
  void display_fallback_message();
  void check_for_emergency_messages();
  
  // Self-test methods
  void runSelfTests();
  bool testLittleFSMount();
  bool testSqliteBasicOperations();
  bool testSerialProtocol();
  
  // Add new test declarations above here

 private: // Helper for test execution
  bool executeTest(bool (B48DisplayController::*testMethod)(), const char* testName);

  // Member variables
  uart::UARTComponent *uart_{nullptr};
  std::string database_path_;
  int transition_duration_{4};
  int time_sync_interval_{60};
  int emergency_priority_threshold_{95};
  int min_seconds_between_repeats_{30};
  bool run_tests_on_startup_{false};
  bool wipe_database_on_boot_{false};
  
  // Pin configuration for testing
  int display_enable_pin_{-1};  // Default to -1 (disabled)
  
  // Serial protocol handler
  BUSE120SerialProtocol serial_protocol_{};
  
  // Database manager
  std::unique_ptr<B48DatabaseManager> db_manager_{nullptr};
  
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

  // HA Integration Layer instance
  std::unique_ptr<B48HAIntegration> ha_integration_{nullptr};

  // Stored sensor for HA integration
  sensor::Sensor *message_queue_size_sensor_{nullptr};

  // Tracking last display times for messages
  std::map<int, time_t> last_display_times_;
};

}  // namespace b48_display_controller
}  // namespace esphome 