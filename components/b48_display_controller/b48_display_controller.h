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
#include <atomic>

#include <sqlite3.h>
#include <Arduino.h>
#include <LittleFS.h>

#include "b48_database_manager.h"
#include "buse120_serial_protocol.h"
#include "b48_ha_integration.h"

namespace esphome {
namespace b48_display_controller {

// Define the threshold (in seconds) below which messages are treated as ephemeral (not saved to DB)
// 3600 seconds = 1 hour
const int EPHEMERAL_DURATION_THRESHOLD_SECONDS = 3600;

// Forward declare HA integration class
class B48HAIntegration;

// Message entry structure is now defined in b48_database_manager.h
// Don't redefine it here

// Display state enum
enum DisplayState {
  TRANSITION_MODE,
  MESSAGE_PREPARATION,
  DISPLAY_MESSAGE,
  TIME_TEST_MODE,  // New state for time test mode
  CHARACTER_REVERSE_TEST_MODE  // New state for character reverse test mode
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
  void set_uart(uart::UARTComponent *uart) {
    this->uart_ = uart;
    this->serial_protocol_.set_uart(uart);
  }
  void set_database_path(const std::string &path) { this->database_path_ = path; }
  void set_transition_duration(int duration) { this->transition_duration_ = duration; }
  void set_time_sync_interval(int interval) { this->time_sync_interval_ = interval; }
  void set_emergency_priority_threshold(int threshold) { this->emergency_priority_threshold_ = threshold; }
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

  // Configuration for database maintenance
  void set_purge_interval_hours(int hours) { this->purge_interval_hours_ = hours; }

  // Message management
  /**
   * @brief Adds a message to be displayed. Handles both persistent and ephemeral messages based on duration.
   *
   * If duration_seconds is > 0 and < EPHEMERAL_DURATION_THRESHOLD_SECONDS, the message is treated as ephemeral (not
   * stored). If duration_seconds is 0 or negative, the message is persistent and never expires. If duration_seconds is
   * >= EPHEMERAL_DURATION_THRESHOLD_SECONDS, the message is persistent with an expiration time.
   *
   * @param priority Message priority (0-100).
   * @param line_number Target display line (1-based).
   * @param tarif_zone Tarif zone for display.
   * @param static_intro Static text before scrolling message.
   * @param scrolling_message Main scrolling text.
   * @param next_message_hint Hint text for the next message.
   * @param duration_seconds Duration in seconds. Controls persistence and expiration.
   * @param source_info Information about the message source (e.g., "HA Service").
   * @param check_duplicates If true, prevents adding identical messages already in the DB.
   * @return true if the message was added successfully, false otherwise.
   */
  bool add_message(int priority, int line_number, int tarif_zone, const std::string &static_intro,
                   const std::string &scrolling_message, const std::string &next_message_hint, int duration_seconds,
                   const std::string &source_info = "", bool check_duplicates = true);

  bool update_message(int message_id, int priority, bool is_enabled, int line_number, int tarif_zone,
                      const std::string &static_intro, const std::string &scrolling_message,
                      const std::string &next_message_hint, int duration_seconds = 0,
                      const std::string &source_info = "");

  bool delete_persistent_message(int message_id);

  // --- Public methods called by HA Integration Layer ---
  B48DatabaseManager *get_database_manager() { return db_manager_.get(); }
  bool wipe_and_reinitialize_database();
  void dump_database_for_diagnostics();

  // --- Internal helper to update HA state ---
  void update_ha_queue_size();

  // Time test mode public methods
  void start_time_test_mode();
  void run_time_test_mode();
  void stop_time_test_mode();
  bool is_time_test_mode_active() const { return time_test_mode_active_; }
  
  // Character reverse test mode methods
  void start_character_reverse_test_mode();
  void run_character_reverse_test_mode();
  void stop_character_reverse_test_mode();
  bool is_character_reverse_test_mode_active() const { return character_reverse_test_mode_active_; }

  // Database maintenance methods
  bool purge_disabled_messages();
  int get_purge_interval_hours() const { return this->purge_interval_hours_; }

  // Filesystem stats method for HA
  void display_filesystem_stats() { log_filesystem_stats(); }

  // --- Raw BUSE Command and State Machine Control ---
  /**
   * @brief Sends a raw command string directly to the BUSE120 display.
   * This bypasses usual message formatting and directly uses the serial protocol's
   * raw send capability. The protocol handler will add CR and checksum.
   * @param raw_payload The raw command string.
   */
  void send_raw_buse_command(const std::string &raw_payload);

  /**
   * @brief Pauses the main display state machine.
   * Display updates will halt, but background tasks may continue.
   */
  void pause_state_machine();

  /**
   * @brief Resumes the main display state machine.
   */
  void resume_state_machine();

  /**
   * @brief Checks if the state machine is currently paused.
   * @return true if paused, false otherwise.
   */
  bool is_state_machine_paused() const;

 protected:
  // Database methods
  bool init_database();
  bool refresh_message_cache();
  void check_expired_messages();
  void check_expired_ephemeral_messages();
  void check_purge_interval();  // Periodic check for message purging

  // Setup helper methods
  bool initialize_filesystem();
  bool check_database_prerequisites();
  bool initialize_database();
  bool handle_database_wipe();
  void display_startup_message(bool db_initialized);

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
  bool executeTest(bool (B48DisplayController::*testMethod)(), const char* testName);

  // Add new test declarations above here

 private:  // Helper for test execution
  // Member variables
  uart::UARTComponent *uart_{nullptr};
  std::string database_path_;
  int transition_duration_{4};
  int time_sync_interval_{10};
  int emergency_priority_threshold_{95};
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
  bool should_interrupt_{false};
  unsigned long state_change_time_{0};
  unsigned long last_time_sync_{0};
  unsigned long last_ephemeral_check_time_{0};
  time_t current_time_{0};
  unsigned long current_display_duration_ms_{5000};  // Store calculated display duration

  // Threading protection
  std::mutex message_mutex_;

  // HA Integration Layer instance
  std::unique_ptr<B48HAIntegration> ha_integration_{nullptr};

  // Stored sensor for HA integration
  sensor::Sensor *message_queue_size_sensor_{nullptr};

  // Tracking last display times for persistent messages
  std::map<int, time_t> last_display_times_;

  // Time test mode variables
  bool time_test_mode_active_{false};
  int current_time_test_value_{0}; // Will count from 0 to 2459
  unsigned long last_time_test_update_{0};
  static constexpr unsigned long TIME_TEST_INTERVAL_MS = 500; // 500ms between updates
  
  // Character reverse test mode variables
  bool character_reverse_test_mode_active_{false};
  int current_character_test_value_{0}; // Character code to test
  unsigned long last_character_test_update_{0};
  static constexpr unsigned long CHARACTER_TEST_INTERVAL_MS = 30000; // 1 minute between updates

  // Database maintenance variables
  time_t last_purge_time_{0};
  int purge_interval_hours_{24};  // Default to daily purge

  // Helper to schedule refresh of message cache on loopTask
  std::atomic<bool> pending_message_cache_refresh_{false};

  bool first_cycle_in_state_{true};

  // State machine pause flag
  std::atomic<bool> state_machine_paused_{false};

  void log_filesystem_stats();
};

}  // namespace b48_display_controller
}  // namespace esphome