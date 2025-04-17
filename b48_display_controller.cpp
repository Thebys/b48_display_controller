#include "b48_display_controller.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>

#include <sqlite3.h>
#include <Arduino.h>
#include <LittleFS.h>

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48_display_controller";
static const char CR = 0x0D;  // Carriage Return for BUSE120 protocol

void B48DisplayController::setup() {
  ESP_LOGCONFIG(TAG, "Setting up B48 Display Controller");

  // Initialize the database
  if (!init_database()) {
    ESP_LOGE(TAG, "Failed to initialize the database");
    //this->mark_failed();
    //return;
  }

  // Load messages from the database
  if (!refresh_message_cache()) {
    ESP_LOGE(TAG, "Failed to load messages from the database");
    //this->mark_failed();
    //return;
  }

  // Initialize time tracking
  this->current_time_ = time(nullptr);
  this->last_time_sync_ = 0;
  this->state_change_time_ = millis();

  ESP_LOGI(TAG, "B48 Display Controller initialized successfully");

  // Run self-tests if enabled
  if (this->run_tests_on_startup_) {
      this->runSelfTests();
  } else {
      ESP_LOGI(TAG, "Self-tests are disabled.");
  }
}

void B48DisplayController::loop() {
  // Update current time
  this->current_time_ = time(nullptr);

  // Check for emergency messages first
  check_for_emergency_messages();

  // Run the state machine
  switch (this->state_) {
    case TRANSITION_MODE:
      run_transition_mode();
      break;
    case MESSAGE_PREPARATION:
      run_message_preparation();
      break;
    case DISPLAY_MESSAGE:
      run_display_message();
      break;
  }

  // Periodically check for expired messages (every 60 seconds)
  static unsigned long last_expiry_check = 0;
  if (millis() - last_expiry_check > 60000) {
    check_expired_messages();
    last_expiry_check = millis();
  }
}

void B48DisplayController::dump_config() {
  ESP_LOGCONFIG(TAG, "B48 Display Controller:");
  ESP_LOGCONFIG(TAG, "  Database Path: %s", this->database_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Transition Duration: %d seconds", this->transition_duration_);
  ESP_LOGCONFIG(TAG, "  Time Sync Interval: %d seconds", this->time_sync_interval_);
  ESP_LOGCONFIG(TAG, "  Emergency Priority Threshold: %d", this->emergency_priority_threshold_);
  ESP_LOGCONFIG(TAG, "  Min Seconds Between Repeats: %d", this->min_seconds_between_repeats_);

  // Log cache info
  std::lock_guard<std::mutex> lock(this->message_mutex_);
  ESP_LOGCONFIG(TAG, "  Persistent Messages: %d", this->persistent_messages_.size());
  ESP_LOGCONFIG(TAG, "  Ephemeral Messages: %d", this->ephemeral_messages_.size());
}

// Database Methods

bool B48DisplayController::init_database() {
  int rc = sqlite3_open(this->database_path_.c_str(), &this->db_);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Cannot open database: %s", sqlite3_errmsg(this->db_));
    sqlite3_close(this->db_);
    return false;
  }

  // Get the database version
  int user_version = 0;
  char *err_msg = nullptr;
  const char *query = "PRAGMA user_version;";

  auto callback = [](void *data, int argc, char **argv, char **azColName) -> int {
    int *version = static_cast<int *>(data);
    if (argc > 0 && argv[0]) {
      *version = std::stoi(argv[0]);
    }
    return 0;
  };

  rc = sqlite3_exec(this->db_, query, callback, &user_version, &err_msg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL error: %s", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(this->db_);
    return false;
  }

  ESP_LOGI(TAG, "Database schema version: %d", user_version);

  // Create tables if they don't exist or update schema
  if (user_version < 1) {
    // Initial schema creation
    const char *create_tables = R"SQL(
      CREATE TABLE IF NOT EXISTS messages (
        message_id INTEGER PRIMARY KEY AUTOINCREMENT,
        priority INTEGER NOT NULL DEFAULT 50,
        is_enabled INTEGER NOT NULL DEFAULT 1,
        tarif_zone INTEGER NOT NULL DEFAULT 0,
        line_number INTEGER NOT NULL DEFAULT 0,
        static_intro TEXT NOT NULL DEFAULT '',
        scrolling_message TEXT NOT NULL,
        next_message_hint TEXT NOT NULL DEFAULT '',
        datetime_added INTEGER NOT NULL,
        duration_seconds INTEGER DEFAULT NULL,
        source_info TEXT DEFAULT NULL
      );
      
      CREATE INDEX IF NOT EXISTS idx_messages_priority ON messages (is_enabled, priority, message_id);
      CREATE INDEX IF NOT EXISTS idx_messages_expiry ON messages (is_enabled, duration_seconds, datetime_added);
      
      PRAGMA user_version = 1;
    )SQL";

    rc = sqlite3_exec(this->db_, create_tables, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
      ESP_LOGE(TAG, "SQL error: %s", err_msg);
      sqlite3_free(err_msg);
      sqlite3_close(this->db_);
      return false;
    }

    ESP_LOGI(TAG, "Database schema created successfully");
  }

  // Implement schema upgrades as needed for future versions

  return true;
}

bool B48DisplayController::refresh_message_cache() {
  std::lock_guard<std::mutex> lock(this->message_mutex_);

  // Clear the existing cache
  this->persistent_messages_.clear();

  // Prepare the query to get all active messages
  const char *query = R"SQL(
    SELECT message_id, priority, line_number, tarif_zone, static_intro, 
           scrolling_message, next_message_hint, datetime_added, duration_seconds
    FROM messages
    WHERE
      is_enabled = 1
      AND (
        duration_seconds IS NULL
        OR (datetime_added + duration_seconds) > strftime('%s', 'now')
      )
    ORDER BY
      priority DESC,
      message_id ASC;
  )SQL";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  // Fetch and process each row
  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    auto entry = std::make_shared<MessageEntry>();

    entry->is_ephemeral = false;
    entry->message_id = sqlite3_column_int(stmt, 0);
    entry->priority = sqlite3_column_int(stmt, 1);
    entry->line_number = sqlite3_column_int(stmt, 2);
    entry->tarif_zone = sqlite3_column_int(stmt, 3);

    const char *static_intro = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    if (static_intro)
      entry->static_intro = static_intro;

    const char *scrolling_message = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    if (scrolling_message)
      entry->scrolling_message = scrolling_message;

    const char *next_hint = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    if (next_hint)
      entry->next_message_hint = next_hint;

    time_t added_time = static_cast<time_t>(sqlite3_column_int64(stmt, 7));
    int duration_seconds = sqlite3_column_type(stmt, 8) == SQLITE_NULL ? 0 : sqlite3_column_int(stmt, 8);

    entry->last_display_time = 0;  // Never displayed yet

    // Calculate expiry time if duration is set
    if (duration_seconds > 0) {
      entry->expiry_time = added_time + duration_seconds;
    }

    this->persistent_messages_.push_back(entry);
    count++;
  }

  sqlite3_finalize(stmt);
  ESP_LOGI(TAG, "Loaded %d messages into cache", count);

  return true;
}

void B48DisplayController::check_expired_messages() {
  const char *query = R"SQL(
    UPDATE messages
    SET is_enabled = 0
    WHERE
      is_enabled = 1
      AND duration_seconds IS NOT NULL
      AND (datetime_added + duration_seconds) <= strftime('%s', 'now');
  )SQL";

  char *err_msg = nullptr;
  int rc = sqlite3_exec(this->db_, query, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL error during expiry check: %s", err_msg);
    sqlite3_free(err_msg);
    return;
  }

  int changes = sqlite3_changes(this->db_);
  if (changes > 0) {
    ESP_LOGI(TAG, "Expired %d messages", changes);
    refresh_message_cache();
  }

  // Also check ephemeral messages in RAM
  std::lock_guard<std::mutex> lock(this->message_mutex_);
  time_t now = time(nullptr);
  auto it = this->ephemeral_messages_.begin();
  int expired = 0;

  while (it != this->ephemeral_messages_.end()) {
    if ((*it)->expiry_time > 0 && (*it)->expiry_time <= now) {
      it = this->ephemeral_messages_.erase(it);
      expired++;
    } else {
      ++it;
    }
  }

  if (expired > 0) {
    ESP_LOGI(TAG, "Expired %d ephemeral messages", expired);
  }
}

// Message Management Methods

bool B48DisplayController::add_persistent_message(int priority, int line_number, int tarif_zone,
                                                  const std::string &static_intro, const std::string &scrolling_message,
                                                  const std::string &next_message_hint, int duration_seconds,
                                                  const std::string &source_info) {
  const char *query = R"SQL(
    INSERT INTO messages (
      priority, line_number, tarif_zone, static_intro, scrolling_message, 
      next_message_hint, datetime_added, duration_seconds, source_info
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
  )SQL";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  // Bind parameters
  sqlite3_bind_int(stmt, 1, priority);
  sqlite3_bind_int(stmt, 2, line_number);
  sqlite3_bind_int(stmt, 3, tarif_zone);
  sqlite3_bind_text(stmt, 4, static_intro.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, scrolling_message.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, next_message_hint.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 7, time(nullptr));

  if (duration_seconds > 0) {
    sqlite3_bind_int(stmt, 8, duration_seconds);
  } else {
    sqlite3_bind_null(stmt, 8);
  }

  if (!source_info.empty()) {
    sqlite3_bind_text(stmt, 9, source_info.c_str(), -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 9);
  }

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    ESP_LOGE(TAG, "Failed to add message: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  // Refresh cache after adding a new message
  refresh_message_cache();
  return true;
}

bool B48DisplayController::update_persistent_message(int message_id, int priority, bool is_enabled, int line_number,
                                                     int tarif_zone, const std::string &static_intro,
                                                     const std::string &scrolling_message,
                                                     const std::string &next_message_hint, int duration_seconds,
                                                     const std::string &source_info) {
  const char *query = R"SQL(
    UPDATE messages
    SET 
      priority = ?,
      is_enabled = ?,
      line_number = ?,
      tarif_zone = ?,
      static_intro = ?,
      scrolling_message = ?,
      next_message_hint = ?,
      duration_seconds = ?,
      source_info = ?
    WHERE message_id = ?;
  )SQL";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare update statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  // Bind parameters
  sqlite3_bind_int(stmt, 1, priority);
  sqlite3_bind_int(stmt, 2, is_enabled ? 1 : 0);
  sqlite3_bind_int(stmt, 3, line_number);
  sqlite3_bind_int(stmt, 4, tarif_zone);
  sqlite3_bind_text(stmt, 5, static_intro.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, scrolling_message.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, next_message_hint.c_str(), -1, SQLITE_STATIC);

  if (duration_seconds > 0) {
    sqlite3_bind_int(stmt, 8, duration_seconds);
  } else {
    sqlite3_bind_null(stmt, 8);
  }

  if (!source_info.empty()) {
    sqlite3_bind_text(stmt, 9, source_info.c_str(), -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 9);
  }

  sqlite3_bind_int(stmt, 10, message_id);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    ESP_LOGE(TAG, "Failed to update message: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  // Refresh cache after updating a message
  refresh_message_cache();
  return true;
}

bool B48DisplayController::delete_persistent_message(int message_id) {
  // Using logical deletion to reduce flash wear
  const char *query = "UPDATE messages SET is_enabled = 0 WHERE message_id = ?;";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare delete statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  sqlite3_bind_int(stmt, 1, message_id);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    ESP_LOGE(TAG, "Failed to delete message: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  // Refresh cache after deleting a message
  refresh_message_cache();
  return true;
}

bool B48DisplayController::add_ephemeral_message(int priority, int line_number, int tarif_zone,
                                                 const std::string &static_intro, const std::string &scrolling_message,
                                                 const std::string &next_message_hint, int display_count,
                                                 int ttl_seconds) {
  std::lock_guard<std::mutex> lock(this->message_mutex_);

  auto entry = std::make_shared<MessageEntry>();
  entry->is_ephemeral = true;
  entry->message_id = -1;  // Indicates ephemeral
  entry->priority = priority;
  entry->line_number = line_number;
  entry->tarif_zone = tarif_zone;
  entry->static_intro = static_intro;
  entry->scrolling_message = scrolling_message;
  entry->next_message_hint = next_message_hint;
  entry->remaining_displays = display_count;
  entry->last_display_time = 0;  // Never displayed yet

  // Set expiry time if TTL is specified
  if (ttl_seconds > 0) {
    entry->expiry_time = time(nullptr) + ttl_seconds;
  }

  this->ephemeral_messages_.push_back(entry);
  ESP_LOGI(TAG, "Added ephemeral message with priority %d", priority);

  return true;
}

// Display algorithm methods

std::shared_ptr<MessageEntry> B48DisplayController::select_next_message() {
  std::lock_guard<std::mutex> lock(this->message_mutex_);

  if (this->persistent_messages_.empty() && this->ephemeral_messages_.empty()) {
    return nullptr;  // No messages available
  }

  time_t current_time = time(nullptr);
  std::shared_ptr<MessageEntry> selected_msg = nullptr;
  float highest_score = -99999.0f;

  // First, check for any new emergency messages
  for (auto &msg : this->ephemeral_messages_) {
    if (msg->priority >= this->emergency_priority_threshold_ && msg->last_display_time == 0) {
      // Return immediately if a new emergency message is found
      ESP_LOGI(TAG, "Found new emergency message with priority %d", msg->priority);
      return msg;
    }
  }

  // Combine all valid messages for evaluation
  std::vector<std::shared_ptr<MessageEntry>> all_messages;

  // Add valid persistent messages
  for (auto &msg : this->persistent_messages_) {
    if (msg->expiry_time == 0 || msg->expiry_time > current_time) {
      all_messages.push_back(msg);
    }
  }

  // Add valid ephemeral messages
  for (auto &msg : this->ephemeral_messages_) {
    if ((msg->expiry_time == 0 || msg->expiry_time > current_time) && msg->remaining_displays > 0) {
      all_messages.push_back(msg);
    }
  }

  if (all_messages.empty()) {
    return nullptr;  // No valid messages
  }

  // Evaluate all messages for selection
  for (auto &msg : all_messages) {
    // Skip messages shown too recently
    if (msg->last_display_time > 0 && (current_time - msg->last_display_time) < this->min_seconds_between_repeats_) {
      continue;
    }

    // Calculate score based on priority and time since last display
    float time_factor =
        (msg->last_display_time == 0) ? 99999.0f : static_cast<float>(current_time - msg->last_display_time);

    // Score calculation with higher weight on priority but still factoring in time
    float score = (msg->priority * 10.0f) + (time_factor / 10.0f);

    // Add a small random factor to prevent predictable sequencing
    score += (rand() % 100) / 100.0f;

    if (score > highest_score) {
      highest_score = score;
      selected_msg = msg;
    }
  }

  // If no suitable message (all shown recently), pick highest priority fallback
  if (selected_msg == nullptr && !all_messages.empty()) {
    // Sort by priority as fallback
    std::sort(all_messages.begin(), all_messages.end(),
              [](const std::shared_ptr<MessageEntry> &a, const std::shared_ptr<MessageEntry> &b) {
                return a->priority > b->priority;
              });
    selected_msg = all_messages[0];
    ESP_LOGD(TAG, "Using fallback selection - highest priority message");
  }

  if (selected_msg != nullptr) {
    ESP_LOGD(TAG, "Selected message ID %d with priority %d and score %.2f", selected_msg->message_id,
             selected_msg->priority, highest_score);
  }

  return selected_msg;
}

int B48DisplayController::calculate_display_duration(const std::shared_ptr<MessageEntry> &msg) {
  if (!msg)
    return 20;  // Default duration if no message

  // Formula: 20.0 + (0.3 * length(scrolling_message)) in seconds
  int duration = 20 + static_cast<int>(0.3f * msg->scrolling_message.length());

  // Cap at maximum duration of 180 seconds
  return std::min(duration, 180);
}

void B48DisplayController::update_message_display_stats(const std::shared_ptr<MessageEntry> &msg) {
  if (!msg)
    return;

  std::lock_guard<std::mutex> lock(this->message_mutex_);

  // Update last display time
  msg->last_display_time = time(nullptr);

  // For ephemeral messages, decrement remaining displays
  if (msg->is_ephemeral) {
    msg->remaining_displays--;

    // Remove if display count reached zero
    if (msg->remaining_displays <= 0) {
      auto it = std::find_if(this->ephemeral_messages_.begin(), this->ephemeral_messages_.end(),
                             [&msg](const std::shared_ptr<MessageEntry> &entry) { return entry.get() == msg.get(); });

      if (it != this->ephemeral_messages_.end()) {
        ESP_LOGD(TAG, "Removing ephemeral message after display count reached zero");
        this->ephemeral_messages_.erase(it);
      }
    }
  }
}

// BUSE120 protocol methods

void B48DisplayController::send_line_number(int line) {
  char payload[5];
  snprintf(payload, sizeof(payload), "l%03d", line);
  send_serial_command(payload);
}

void B48DisplayController::send_tarif_zone(int zone) {
  char payload[9];
  snprintf(payload, sizeof(payload), "e%06ld", static_cast<long>(zone));
  send_serial_command(payload);
}

void B48DisplayController::send_static_intro(const std::string &text) {
  // Limit to 15 characters as per spec
  std::string truncated = text.substr(0, 15);
  std::string payload = "zI " + truncated;
  send_serial_command(payload);
}

void B48DisplayController::send_scrolling_message(const std::string &text) {
  // Limit to 511 characters as per spec
  std::string truncated = text.substr(0, 511);
  std::string payload = "zM " + truncated;
  send_serial_command(payload);
}

void B48DisplayController::send_next_message_hint(const std::string &text) {
  // Limit to 15 characters as per spec
  std::string truncated = text.substr(0, 15);
  std::string payload = "v " + truncated;
  send_serial_command(payload);
}

void B48DisplayController::send_time_update() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);

  char payload[6];
  snprintf(payload, sizeof(payload), "u%02d%02d", timeinfo->tm_hour, timeinfo->tm_min);
  send_serial_command(payload);

  this->last_time_sync_ = millis();
}

void B48DisplayController::switch_to_cycle(int cycle) {
  char payload[4];
  snprintf(payload, sizeof(payload), "xC%d", cycle);
  send_serial_command(payload);
}

void B48DisplayController::send_commands_for_message(const std::shared_ptr<MessageEntry> &msg) {
  if (!msg)
    return;

  send_line_number(msg->line_number);
  send_tarif_zone(msg->tarif_zone);
  send_static_intro(msg->static_intro);
  send_scrolling_message(msg->scrolling_message);
}

void B48DisplayController::send_serial_command(const std::string &payload) {
  if (!this->uart_) {
    ESP_LOGE(TAG, "UART not initialized");
    return;
  }

  uint8_t checksum = calculate_checksum(payload);

  // Log the command (without checksum for readability)
  ESP_LOGD(TAG, "Sending command: %s", payload.c_str());

  // Send payload
  this->uart_->write_array(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());

  // Send terminator (CR)
  this->uart_->write_byte(CR);

  // Send checksum
  this->uart_->write_byte(checksum);
}

uint8_t B48DisplayController::calculate_checksum(const std::string &payload) {
  uint8_t checksum = 0x7F;

  // XOR with each byte in the payload
  for (char c : payload) {
    checksum ^= static_cast<uint8_t>(c);
  }

  // XOR with the terminator (CR)
  checksum ^= CR;

  return checksum;
}

// Display state machine methods

void B48DisplayController::check_for_emergency_messages() {
  std::lock_guard<std::mutex> lock(this->message_mutex_);

  // Check for emergency messages (priority >= threshold) that haven't been displayed yet
  for (auto &msg : this->ephemeral_messages_) {
    if (msg->priority >= this->emergency_priority_threshold_ && msg->last_display_time == 0) {
      ESP_LOGI(TAG, "Emergency message detected, interrupting normal cycle");

      // Send commands for the emergency message
      send_commands_for_message(msg);

      // Switch to Cycle 0 immediately
      switch_to_cycle(0);

      // Mark the message as displayed
      msg->last_display_time = time(nullptr);

      // Calculate display duration and set state to show this message
      this->current_message_ = msg;
      this->state_ = DISPLAY_MESSAGE;
      this->state_change_time_ = millis();

      // Decrement remaining displays for ephemeral messages
      if (msg->is_ephemeral) {
        msg->remaining_displays--;
      }

      break;  // Process only one emergency message at a time
    }
  }
}

void B48DisplayController::run_transition_mode() {
  unsigned long current_millis = millis();

  // If just entered this state, set up the transition display
  if (this->state_change_time_ == current_millis) {
    ESP_LOGD(TAG, "Entering TRANSITION_MODE state");

    // Select the next message to prepare for
    std::shared_ptr<MessageEntry> next_msg = select_next_message();
    if (!next_msg) {
      ESP_LOGD(TAG, "No message available, using fallback");
      display_fallback_message();
      return;
    }

    // Store as current message
    this->current_message_ = next_msg;

    // Display next stop hint
    send_next_message_hint(next_msg->next_message_hint);

    // Periodically update the time display
    if (millis() - this->last_time_sync_ >= (this->time_sync_interval_ * 1000)) {
      send_time_update();
    }

    // Switch to Cycle 6 (transition mode)
    switch_to_cycle(6);
  }

  // Check if transition period has elapsed
  if (current_millis - this->state_change_time_ >= (this->transition_duration_ * 1000)) {
    // Move to message preparation state
    this->state_ = MESSAGE_PREPARATION;
    this->state_change_time_ = current_millis;
  }
}

void B48DisplayController::run_message_preparation() {
  // This state is instantaneous - just prepare the message and advance to display
  ESP_LOGD(TAG, "Preparing message display");

  if (!this->current_message_) {
    ESP_LOGW(TAG, "No message to prepare, returning to transition mode");
    this->state_ = TRANSITION_MODE;
    this->state_change_time_ = millis();
    return;
  }

  // Send all the commands for the current message
  send_commands_for_message(this->current_message_);

  // Switch to Cycle 0 for main display
  switch_to_cycle(0);

  // Update message stats (last display time, etc.)
  update_message_display_stats(this->current_message_);

  // Immediately advance to display state
  this->state_ = DISPLAY_MESSAGE;
  this->state_change_time_ = millis();
}

void B48DisplayController::run_display_message() {
  unsigned long current_millis = millis();

  // If no message, return to transition mode
  if (!this->current_message_) {
    ESP_LOGW(TAG, "No current message to display");
    this->state_ = TRANSITION_MODE;
    this->state_change_time_ = current_millis;
    return;
  }

  // Calculate display duration based on message content
  int display_duration = calculate_display_duration(this->current_message_);

  // Check if display period has elapsed
  if (current_millis - this->state_change_time_ >= (display_duration * 1000)) {
    // Move back to transition mode for next message
    this->state_ = TRANSITION_MODE;
    this->state_change_time_ = current_millis;
  }
}

void B48DisplayController::display_fallback_message() {
  // Display a fallback message when no regular messages are available
  ESP_LOGD(TAG, "Displaying fallback message");

  // Get current time for the fallback display
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  char date_str[30];
  strftime(date_str, sizeof(date_str), "%Y-%m-%d", timeinfo);

  // Create a simple fallback message
  auto fallback = std::make_shared<MessageEntry>();
  fallback->line_number = 0;
  fallback->tarif_zone = 0;
  fallback->static_intro = date_str;
  fallback->scrolling_message = "No messages available";
  fallback->next_message_hint = "Waiting...";

  // Send commands for the fallback
  send_commands_for_message(fallback);

  // Update time if needed
  if (millis() - this->last_time_sync_ >= (this->time_sync_interval_ * 1000)) {
    send_time_update();
  }

  // Switch to Cycle 0
  switch_to_cycle(0);

  // Store as current message
  this->current_message_ = fallback;

  // Set state to display this message
  this->state_ = DISPLAY_MESSAGE;
  this->state_change_time_ = millis();
}

// Self-Test Methods

void B48DisplayController::runSelfTests() {
    ESP_LOGI(TAG, "--- Running Self-Tests ---");
    int pass_count = 0;
    int fail_count = 0;

    if (this->testAlwaysPasses()) {
        pass_count++;
    } else {
        fail_count++;
    }

    if (this->testAlwaysFails()) {
        pass_count++;
    } else {
        fail_count++;
    }

    // Add more test calls here...

    ESP_LOGI(TAG, "--- Self-Test Summary --- Passed: %d, Failed: %d ---", pass_count, fail_count);

    if (fail_count > 0) {
        ESP_LOGW(TAG, "One or more self-tests failed. Check logs above.");
        // Optionally: Add logic to handle failures, e.g., mark component as failed
        // this->mark_failed(); 
    }
}

bool B48DisplayController::testAlwaysPasses() {
    ESP_LOGI(TAG, "[TEST] testAlwaysPasses: PASSED");
    return true;
}

bool B48DisplayController::testAlwaysFails() {
    ESP_LOGE(TAG, "[TEST] testAlwaysFails: FAILED (Intentionally)");
    return false;
}
}  // namespace b48_display_controller
}  // namespace esphome