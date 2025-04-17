#include "b48_display_controller.h"
#include "b48_database_manager.h"
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

static const char *const TAG = "b48c";
static const char CR = 0x0D;  // Carriage Return for BUSE120 protocol

// Add destructor implementation - this needs to be after including b48_database_manager.h
B48DisplayController::~B48DisplayController() = default;

void B48DisplayController::setup() {
  ESP_LOGCONFIG(TAG, "Setting up B48 Display Controller");

  // Configure and enable the display enable pin if specified
  // This is only needed for test boards - production hardware should
  // have the display enable handled externally
  if (this->display_enable_pin_ >= 0) {
    ESP_LOGCONFIG(TAG, "  Configuring display enable pin: %d", this->display_enable_pin_);
    pinMode(this->display_enable_pin_, OUTPUT);
    digitalWrite(this->display_enable_pin_, HIGH);
    ESP_LOGCONFIG(TAG, "  Display enable pin pulled HIGH");
  }

  // Initialize LittleFS
  if (!LittleFS.begin()) {
    ESP_LOGW(TAG, "Initial mount of LittleFS failed, trying again...");
    delay(400);
    // Try to initialize with different parameters
    if (!LittleFS.begin(true)) {
      ESP_LOGE(TAG, "Failed to mount LittleFS, format attempted.");
      this->mark_failed();
      return;
    }
  }

  // Initialize the database manager
  db_manager_.reset(new B48DatabaseManager(this->database_path_));
  if (!db_manager_->initialize()) {
    ESP_LOGE(TAG, "Failed to initialize the database manager");
    this->mark_failed();
    return;
  }

  // Wipe the database if configured to do so
  if (this->wipe_database_on_boot_) {
    ESP_LOGW(TAG, "Configuration has wipe_database_on_boot enabled. Wiping database...");
    if (!db_manager_->wipe_database()) {
      ESP_LOGE(TAG, "Failed to wipe database");
      // Not a fatal error, continue
    } else {
      // Need to recreate schema and bootstrap after wiping
      if (!db_manager_->initialize()) {
        ESP_LOGE(TAG, "Failed to reinitialize database after wiping");
        this->mark_failed();
        return;
      }
    }
  }

  // Run self-tests if configured to do so
  if (this->run_tests_on_startup_) {
    this->runSelfTests();
  }

  // Load messages from the database
  if (!refresh_message_cache()) {
    ESP_LOGE(TAG, "Failed to refresh message cache");
    this->mark_failed();
    return;
  }

  // Start with transition mode
  this->state_ = TRANSITION_MODE;
  this->state_change_time_ = millis();
  ESP_LOGI(TAG, "B48 Display Controller setup complete");
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

// Database Methods - Keep only what is needed in Controller

// All database schema, bootstrapping, and direct DB operations are now handled by B48DatabaseManager

bool B48DisplayController::refresh_message_cache() {
  std::lock_guard<std::mutex> lock(this->message_mutex_);

  // Clear the existing cache
  this->persistent_messages_.clear();

  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized");
    return false;
  }

  // Get messages from database manager
  this->persistent_messages_ = this->db_manager_->get_active_persistent_messages();
  
  ESP_LOGI(TAG, "Loaded %d messages into cache", this->persistent_messages_.size());
  return true;
}

void B48DisplayController::check_expired_messages() {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized");
    return;
  }

  // Check and expire persistent messages
  int changes = this->db_manager_->expire_old_messages();
  
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
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized");
    return false;
  }

  bool success = this->db_manager_->add_persistent_message(
    priority, line_number, tarif_zone, static_intro, scrolling_message,
    next_message_hint, duration_seconds, source_info
  );

  if (success) {
    // Refresh cache after adding a new message
    refresh_message_cache();
  }

  return success;
}

bool B48DisplayController::update_persistent_message(int message_id, int priority, bool is_enabled, int line_number,
                                                     int tarif_zone, const std::string &static_intro,
                                                     const std::string &scrolling_message,
                                                     const std::string &next_message_hint, int duration_seconds,
                                                     const std::string &source_info) {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized");
    return false;
  }

  bool success = this->db_manager_->update_persistent_message(
    message_id, priority, is_enabled, line_number, tarif_zone,
    static_intro, scrolling_message, next_message_hint,
    duration_seconds, source_info
  );

  if (success) {
    // Refresh cache after updating a message
    refresh_message_cache();
  }

  return success;
}

bool B48DisplayController::delete_persistent_message(int message_id) {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized");
    return false;
  }

  bool success = this->db_manager_->delete_persistent_message(message_id);

  if (success) {
    // Refresh cache after deleting a message
    refresh_message_cache();
  }

  return success;
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
  this->serial_protocol_.send_line_number(line);
}

void B48DisplayController::send_tarif_zone(int zone) {
  this->serial_protocol_.send_tarif_zone(zone);
}

void B48DisplayController::send_static_intro(const std::string &text) {
  this->serial_protocol_.send_static_intro(text);
}

void B48DisplayController::send_scrolling_message(const std::string &text) {
  this->serial_protocol_.send_scrolling_message(text);
}

void B48DisplayController::send_next_message_hint(const std::string &text) {
  this->serial_protocol_.send_next_message_hint(text);
}

void B48DisplayController::send_time_update() {
  time_t now = time(nullptr);
  struct tm *timeinfo = localtime(&now);
  
  this->serial_protocol_.send_time_update(timeinfo->tm_hour, timeinfo->tm_min);
  this->last_time_sync_ = millis();
}

void B48DisplayController::switch_to_cycle(int cycle) {
  this->serial_protocol_.switch_to_cycle(cycle);
}

void B48DisplayController::send_commands_for_message(const std::shared_ptr<MessageEntry> &msg) {
  if (!msg)
    return;

  send_line_number(msg->line_number);
  send_tarif_zone(msg->tarif_zone);
  send_static_intro(msg->static_intro);
  send_scrolling_message(msg->scrolling_message);
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

}  // namespace b48_display_controller
}  // namespace esphome