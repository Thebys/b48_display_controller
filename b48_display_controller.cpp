#include "b48_display_controller.h"
#include "b48_database_manager.h"
#include "b48_ha_integration.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include <sstream>
#include <iomanip>
#include <cstring>
#include <algorithm>
#include <memory>
#include <mutex>

#include <sqlite3.h>
#include <Arduino.h>
#include <LittleFS.h>

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.main";
static const char CR = 0x0D;  // Carriage Return for BUSE120 protocol

// Destructor
B48DisplayController::~B48DisplayController() {
  if (this->db_manager_) {
    // Optionally perform cleanup related to db_manager_ if needed
    // sqlite3_close is handled by db_manager_ destructor
  }
}

// --- HA Entity Setters ---
void B48DisplayController::set_message_queue_size_sensor(sensor::Sensor *sensor) {
  if (this->ha_integration_) {
    this->ha_integration_->set_message_queue_size_sensor(sensor);
  }
}

void B48DisplayController::setup() {
  ESP_LOGCONFIG(TAG, "Setting up B48 Display Controller");

  // Configure and enable the display enable pin if specified
  // This is only needed for test boards - production hardware should
  // have the display enable handled externally
  // Specifically once proper connector is used, this will be removed
  if (this->display_enable_pin_ >= 0) {
    ESP_LOGCONFIG(TAG, "  Configuring display enable pin: %d", this->display_enable_pin_);
    pinMode(this->display_enable_pin_, OUTPUT);
    digitalWrite(this->display_enable_pin_, HIGH);
    ESP_LOGCONFIG(TAG, "  Display enable pin pulled HIGH");
  }

  // Initialize LittleFS
  if (!LittleFS.begin(false /*don't format*/, "/littlefs", 10 /*max files*/)) {
    ESP_LOGW(TAG, "Initial LittleFS mount failed. Trying format=true...");
    if (!LittleFS.begin(true /*format*/, "/littlefs", 10)) {
      ESP_LOGE(TAG, "Failed to mount LittleFS even after formatting.");
      this->mark_failed();
      return;
    }
    ESP_LOGI(TAG, "LittleFS mounted successfully after formatting.");
  } else {
      ESP_LOGI(TAG, "LittleFS mounted successfully.");
  }

  // Initialize the database manager
  db_manager_.reset(new B48DatabaseManager(this->database_path_));
  if (!this->db_manager_->initialize()) {
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
  // Display loading message
  auto loading_msg = std::make_shared<MessageEntry>();
  loading_msg->message_id = -1;
  loading_msg->line_number = 48;
  loading_msg->tarif_zone = 101;
  loading_msg->static_intro = "Loading";
  loading_msg->scrolling_message = "System initialization in progress...";
  loading_msg->next_message_hint = "Please wait";
  loading_msg->priority = 75;
  send_commands_for_message(loading_msg);

  // Load messages from the database
  if (!refresh_message_cache()) {
    ESP_LOGE(TAG, "Failed to refresh message cache initially");
    // Don't mark failed, maybe DB is just empty?
  }

  // Setup the HA integration component *after* DB is ready
  if (this->ha_integration_) {
      App.register_component(this->ha_integration_.get()); // Register HA integration component
      // ha_integration_->setup(); // Setup is called by ESPHome scheduler
  } else {
      ESP_LOGW(TAG, "HA integration component not initialized!");
  }

  // Initial update for HA sensors
  update_ha_queue_size();

  // Start with transition mode
  this->state_ = TRANSITION_MODE;
  this->state_change_time_ = millis();
  ESP_LOGI(TAG, "B48 Display Controller setup complete");
}

void B48DisplayController::loop() {
  // Update current time using standard C time
  this->current_time_ = time(nullptr);
  // Log if time seems invalid (before 2021)
  if (this->current_time_ < 1609459200 && this->current_time_ != 0) { 
      ESP_LOGV(TAG, "System time might not be synchronized yet (%ld)", (long)this->current_time_);
  }

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
  // --- Add periodic time sync --- (Removed sync service, handle internally)
  if (this->time_sync_interval_ > 0 && this->current_time_ > 0) {
      if (millis() - this->last_time_sync_ > (unsigned long)this->time_sync_interval_ * 1000) {
          send_time_update(); // Send current internal time to display
          this->last_time_sync_ = millis();
      }
  }
}

void B48DisplayController::dump_config() {
  ESP_LOGCONFIG(TAG, "B48 Display Controller:");
  ESP_LOGCONFIG(TAG, "  Database Path: %s", this->database_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Transition Duration: %d seconds", this->transition_duration_);
  ESP_LOGCONFIG(TAG, "  Time Sync Interval: %d seconds", this->time_sync_interval_);
  ESP_LOGCONFIG(TAG, "  Emergency Priority Threshold: %d", this->emergency_priority_threshold_);
  ESP_LOGCONFIG(TAG, "  Min Seconds Between Repeats: %d", this->min_seconds_between_repeats_);
  ESP_LOGCONFIG(TAG, "  Run Tests on Startup: %s", YESNO(this->run_tests_on_startup_));
  ESP_LOGCONFIG(TAG, "  Wipe Database on Boot: %s", YESNO(this->wipe_database_on_boot_));
  if (this->display_enable_pin_ >= 0) {
      ESP_LOGCONFIG(TAG, "  Display Enable Pin: GPIO%d", this->display_enable_pin_);
  }

  // Log cache info
  std::lock_guard<std::mutex> lock(this->message_mutex_);
  ESP_LOGCONFIG(TAG, "  Persistent Messages (in cache): %d", this->persistent_messages_.size());
  ESP_LOGCONFIG(TAG, "  Ephemeral Messages (in RAM): %d", this->ephemeral_messages_.size());

  // Dump HA Integration config
  if (this->ha_integration_) {
      this->ha_integration_->dump_config();
  }
}

// --- Public Methods Called by HA Integration ---

bool B48DisplayController::add_persistent_message(int priority, int line_number, int tarif_zone,
                                                  const std::string &static_intro, const std::string &scrolling_message,
                                                  const std::string &next_message_hint, int duration_seconds,
                                                  const std::string &source_info) {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized for add_persistent_message");
    return false;
  }

  // Call the database manager to add the message
  bool success = this->db_manager_->add_persistent_message(
    priority, line_number, tarif_zone, static_intro, scrolling_message,
    next_message_hint, duration_seconds, source_info
  );

  if (success) {
    // Refresh cache and update HA sensor
    if (refresh_message_cache()) {
        update_ha_queue_size();
    } else {
        ESP_LOGE(TAG, "Failed to refresh message cache after adding persistent message.");
        // Message added to DB, but cache might be stale. Continue anyway.
    }
  }

  return success;
}

bool B48DisplayController::delete_persistent_message(int message_id) {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized for delete_persistent_message");
    return false;
  }
  // Call the database manager to mark the message as deleted
  bool success = this->db_manager_->delete_persistent_message(message_id);

  if (success) {
    // Refresh cache and update HA sensor
    if (refresh_message_cache()) {
        update_ha_queue_size();
    } else {
        ESP_LOGE(TAG, "Failed to refresh message cache after deleting persistent message.");
    }
  }
  return success;
}

bool B48DisplayController::clear_all_persistent_messages() {
    if (!this->db_manager_) {
        ESP_LOGE(TAG, "Database manager is not initialized for clear_all_persistent_messages");
        return false;
    }
    // Call DB manager to clear messages (assuming it has a method, or implement it)
    // For now, let's assume B48DatabaseManager needs a clear_all_messages method
    // bool success = this->db_manager_->clear_all_messages();

    // --- Temporary implementation using wipe + reinit --- 
    // WARNING: This is inefficient and will remove schema if not careful!
    // Proper implementation requires a specific clear method in db_manager_
    ESP_LOGW(TAG, "clear_all_persistent_messages: Using WIPE + REINIT (inefficient)");
    bool success = this->db_manager_->wipe_database();
    if (success) {
        success = this->db_manager_->initialize(); // Recreate schema
    }
    // --- End Temporary --- 

    if (success) {
        ESP_LOGI(TAG, "Cleared all persistent messages.");
        // Refresh cache and update HA sensor
        if (refresh_message_cache()) {
            update_ha_queue_size();
        } else {
            ESP_LOGE(TAG, "Failed to refresh message cache after clearing all messages.");
        }
    } else {
        ESP_LOGE(TAG, "Failed to clear all persistent messages.");
    }
    return success;
}

bool B48DisplayController::add_ephemeral_message(int priority, int line_number, int tarif_zone,
                                                 const std::string &static_intro, const std::string &scrolling_message,
                                                 const std::string &next_message_hint, int display_count,
                                                 int ttl_seconds) {
  std::lock_guard<std::mutex> lock(this->message_mutex_); // Lock access to ephemeral_messages_

  auto entry = std::make_shared<MessageEntry>();
  entry->is_ephemeral = true;
  entry->message_id = -1; // Ephemeral messages don't have a DB ID
  entry->priority = priority;
  entry->line_number = line_number;
  entry->tarif_zone = tarif_zone;
  entry->static_intro = static_intro;
  entry->scrolling_message = scrolling_message;
  entry->next_message_hint = next_message_hint;
  entry->remaining_displays = (display_count <= 0) ? -1 : display_count; // -1 for infinite displays until TTL
  entry->last_display_time = 0;

  if (ttl_seconds > 0) {
    entry->expiry_time = time(nullptr) + ttl_seconds;
  } else {
    entry->expiry_time = 0; // No time-based expiry
  }

  // Add to the ephemeral queue
  this->ephemeral_messages_.push_back(entry);
  ESP_LOGI(TAG, "Added ephemeral message (Priority: %d, TTL: %ds, Count: %d): %s",
            priority, ttl_seconds, display_count, scrolling_message.substr(0, 30).c_str());

  // Sort ephemeral messages by priority (higher first)
  std::sort(this->ephemeral_messages_.begin(), this->ephemeral_messages_.end(),
            [](const std::shared_ptr<MessageEntry>& a, const std::shared_ptr<MessageEntry>& b) {
              return a->priority > b->priority;
            });

  // No need to update HA queue size sensor for ephemeral messages
  return true;
}

// --- Internal HA State Update ---
void B48DisplayController::update_ha_queue_size() {
    if (this->ha_integration_ && this->db_manager_) {
        // Fetch current persistent message count from DB manager
        // Requires db_manager_ to have a get_persistent_message_count() method
        int count = this->db_manager_->get_message_count(); // Assuming this counts active persistent messages
        if (count >= 0) { // Check if count retrieval was successful
            this->ha_integration_->publish_queue_size(count);
        } else {
            ESP_LOGW(TAG, "Failed to get message count from DB manager for HA update.");
        }
    } else if (this->ha_integration_) {
        ESP_LOGW(TAG, "Cannot update HA queue size: DB manager not initialized.");
    }
}

// --- Database Methods (Modified) ---

bool B48DisplayController::refresh_message_cache() {
  std::lock_guard<std::mutex> lock(this->message_mutex_);

  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized in refresh_message_cache");
    return false;
  }

  // Clear the existing persistent cache
  this->persistent_messages_.clear();

  // Get messages from database manager
  this->persistent_messages_ = this->db_manager_->get_active_persistent_messages();

  ESP_LOGD(TAG, "Refreshed persistent message cache, loaded %d messages", this->persistent_messages_.size());

  // Sort persistent messages by priority (DESC) and then ID (ASC) - DB query already does this
  // Optional: Verify sort order if needed
  /*
  std::sort(this->persistent_messages_.begin(), this->persistent_messages_.end(),
            [](const std::shared_ptr<MessageEntry>& a, const std::shared_ptr<MessageEntry>& b) {
              if (a->priority != b->priority) {
                return a->priority > b->priority; // Higher priority first
              }
              return a->message_id < b->message_id; // Lower ID (older) first within same priority
            });
  */

  // HA queue size is updated separately after DB operations trigger a refresh
  return true;
}

void B48DisplayController::check_expired_messages() {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized for expiry check");
    return;
  }

  // --- Check and expire persistent messages in DB ---
  int persistent_expired = this->db_manager_->expire_old_messages();
  if (persistent_expired > 0) {
    ESP_LOGI(TAG, "Expired %d persistent messages in database", persistent_expired);
    // Refresh cache and update HA sensor if changes occurred
    if (refresh_message_cache()) {
        update_ha_queue_size();
    } else {
        ESP_LOGE(TAG, "Failed to refresh cache after expiring persistent messages.");
    }
  } else if (persistent_expired < 0) {
      ESP_LOGE(TAG, "Error checking persistent message expiry.");
  }

  // --- Check and expire ephemeral messages in RAM ---
  std::lock_guard<std::mutex> lock(this->message_mutex_);
  time_t now = time(nullptr);
  int ephemeral_expired = 0;

  // Remove expired ephemeral messages based on TTL or display count
  this->ephemeral_messages_.erase(
      std::remove_if(this->ephemeral_messages_.begin(), this->ephemeral_messages_.end(),
                     [&](const std::shared_ptr<MessageEntry>& msg) {
                       bool expired = false;
                       // Check TTL
                       if (msg->expiry_time > 0 && msg->expiry_time <= now) {
                         expired = true;
                       }
                       // Check display count (if not TTL expired)
                       if (!expired && msg->remaining_displays == 0) { // 0 means used up, -1 means infinite
                         expired = true;
                       }
                       if (expired) {
                         ephemeral_expired++;
                       }
                       return expired;
                     }),
      this->ephemeral_messages_.end());

  if (ephemeral_expired > 0) {
    ESP_LOGI(TAG, "Expired %d ephemeral messages from RAM", ephemeral_expired);
    // No HA sensor update needed for ephemeral messages
  }
}

// --- Display Algorithm Methods (potentially need adjustments for ephemeral messages) ---

std::shared_ptr<MessageEntry> B48DisplayController::select_next_message() {
  std::lock_guard<std::mutex> lock(this->message_mutex_);
  time_t now = time(nullptr);
  std::shared_ptr<MessageEntry> selected_message = nullptr;

  // 1. Check Ephemeral Messages (highest priority first due to pre-sorting)
  for (const auto& msg : this->ephemeral_messages_) {
      // Basic checks (should already be handled by expiry check, but good for safety)
      if (msg->expiry_time > 0 && msg->expiry_time <= now) continue;
      if (msg->remaining_displays == 0) continue;

      // Check minimum time between repeats (if applicable, use main setting for now)
      if (now - msg->last_display_time < this->min_seconds_between_repeats_) {
          continue;
      }

      selected_message = msg;
      ESP_LOGD(TAG, "Selected ephemeral message (Prio: %d)", selected_message->priority);
      break; // Select the highest priority available ephemeral message
  }

  // 2. If no suitable ephemeral message, check Persistent Messages
  if (!selected_message) {
      // Iterate through persistent messages (already sorted by priority DESC, id ASC)
      // Find the next message to display, considering rotation and min repeat interval
      // This part needs careful implementation to cycle through messages fairly.

      // Simple round-robin for now within priorities (needs improvement)
      static size_t last_persistent_index = 0; 
      size_t current_index = last_persistent_index;
      for (size_t i = 0; i < this->persistent_messages_.size(); ++i) {
          current_index = (last_persistent_index + i) % this->persistent_messages_.size();
          const auto& msg = this->persistent_messages_[current_index];
          
          // Check minimum time between repeats
          auto it = last_display_times_.find(msg->message_id);
          time_t last_shown = (it != last_display_times_.end()) ? it->second : 0;

          if (now - last_shown >= this->min_seconds_between_repeats_) {
              selected_message = msg;
              last_persistent_index = (current_index + 1) % this->persistent_messages_.size(); // Move to next for next time
              ESP_LOGD(TAG, "Selected persistent message ID: %d (Prio: %d)", selected_message->message_id, selected_message->priority);
              break;
          }
      }
  }

  // 3. If still no message, return nullptr (will trigger fallback)
  if (!selected_message) {
      ESP_LOGV(TAG, "No suitable message found for display.");
  }

  return selected_message;
}


// --- Other methods (calculate_display_duration, update_message_display_stats, etc.) ---
// Need to be updated to handle ephemeral messages correctly (e.g., decrement remaining_displays)

int B48DisplayController::calculate_display_duration(const std::shared_ptr<MessageEntry> &msg) {
  // Simple duration calculation (example)
  // Could be based on message length, priority, etc.
  if (!msg) return 5; // Default duration if msg is null

  int base_duration = 5; // Base seconds
  int chars_per_second = 5; // Estimated scroll speed
  int length_duration = msg->scrolling_message.length() / chars_per_second;

  // Use a simple heuristic: longer messages display for longer, up to a max
  return std::max(base_duration, std::min(length_duration, 20)); // E.g., 5-20 seconds
}

void B48DisplayController::update_message_display_stats(const std::shared_ptr<MessageEntry> &msg) {
  if (!msg) return;

  time_t now = time(nullptr);

  if (msg->is_ephemeral) {
      std::lock_guard<std::mutex> lock(this->message_mutex_); // Lock as we modify ephemeral list potentially
      msg->last_display_time = now;
      if (msg->remaining_displays > 0) { // Only decrement if it's a finite count
          msg->remaining_displays--;
          ESP_LOGD(TAG, "Decremented display count for ephemeral message (Remaining: %d)", msg->remaining_displays);
      }
      // Check if expired now due to count
      if (msg->remaining_displays == 0) {
          ESP_LOGI(TAG, "Ephemeral message reached display count limit, will be removed.");
          // Mark for removal in the next check_expired_messages cycle
      }
  } else {
      // Update last display time for persistent messages (used for repeat delay)
      last_display_times_[msg->message_id] = now;
      // DB update for display count/time is not part of this schema
      ESP_LOGV(TAG, "Updated last display time for persistent message ID %d", msg->message_id);
  }
}

// ... (send_commands_for_message, BUSE120 methods, state machine methods remain largely the same) ...
// Make sure send_commands_for_message uses the fields from the MessageEntry struct correctly.

// --- BUSE120 Protocol Methods ---

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
  // Ensure current_time_ is updated
  if (this->current_time_ == 0) {
      // If we don't have a valid time, maybe try to get it now? 
      // Or simply skip? For now, skipping.
      // ESP_LOGW(TAG, "Cannot send time update, current time unknown.");
      // return; 
      // Let's get the current time if not set
      time(&this->current_time_); 
  }

  // Convert time_t to struct tm to extract hour and minute
  struct tm *timeinfo;
  timeinfo = localtime(&this->current_time_);

  if (timeinfo) {
    ESP_LOGD(TAG, "Sending time update: %02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    this->serial_protocol_.send_time_update(timeinfo->tm_hour, timeinfo->tm_min);
  } else {
    ESP_LOGW(TAG, "Failed to convert time_t to tm struct for time update.");
  }
  // Update last sync time
  this->last_time_sync_ = millis();
}

void B48DisplayController::send_invert_command() {
    this->serial_protocol_.send_invert_command();
}

void B48DisplayController::switch_to_cycle(int cycle) {
    this->serial_protocol_.switch_to_cycle(cycle);
}

void B48DisplayController::send_commands_for_message(const std::shared_ptr<MessageEntry> &msg) {
    if (!msg) {
        ESP_LOGW(TAG, "send_commands_for_message called with null message");
        return;
    }
    ESP_LOGD(TAG, "Sending commands for message (Prio: %d, ID: %d, Ephem: %d)",
             msg->priority, msg->message_id, msg->is_ephemeral);

    // Example sequence based on MessageEntry fields
    send_line_number(msg->line_number);
    send_tarif_zone(msg->tarif_zone);
    send_static_intro(msg->static_intro);         // Send zI command
    send_scrolling_message(msg->scrolling_message); // Send zM command
    send_next_message_hint(msg->next_message_hint); // Send v command
    // Add delay if needed between commands based on hardware requirements
}

// --- State Machine Methods ---

void B48DisplayController::run_transition_mode() {
    // Simple transition: wait, then prepare next message
    unsigned long time_in_state = millis() - this->state_change_time_;
    if (time_in_state >= (unsigned long)this->transition_duration_ * 1000) {
        ESP_LOGV(TAG, "Transition complete, moving to MESSAGE_PREPARATION");
        this->state_ = MESSAGE_PREPARATION;
        this->state_change_time_ = millis();
        // Optionally send a blanking command or next_message_hint here
    }
}

void B48DisplayController::run_message_preparation() {
    ESP_LOGV(TAG, "Preparing next message...");
    this->current_message_ = select_next_message();

    if (this->current_message_) {
        send_commands_for_message(this->current_message_);
        this->state_ = DISPLAY_MESSAGE;
        ESP_LOGD(TAG, "Message prepared, moving to DISPLAY_MESSAGE");
    } else {
        display_fallback_message(); // Display fallback if no message available
        this->state_ = DISPLAY_MESSAGE; // Still move to display state for fallback
        ESP_LOGD(TAG, "No message selected, displaying fallback, moving to DISPLAY_MESSAGE");
    }
    this->state_change_time_ = millis();
}

void B48DisplayController::run_display_message() {
    int display_duration_ms = 5000; // Default if current_message_ is null
    if (this->current_message_) {
        display_duration_ms = calculate_display_duration(this->current_message_) * 1000;
    }

    unsigned long time_in_state = millis() - this->state_change_time_;
    if (time_in_state >= (unsigned long)display_duration_ms) {
        ESP_LOGV(TAG, "Display duration ended, updating stats and moving to TRANSITION_MODE");
        update_message_display_stats(this->current_message_); // Update stats before transitioning
        this->current_message_ = nullptr; // Clear current message
        this->state_ = TRANSITION_MODE;
        this->state_change_time_ = millis();
    }
    // Display logic runs implicitly via the sent commands
}

void B48DisplayController::display_fallback_message() {
    // Define a simple fallback message
    auto fallback_msg = std::make_shared<MessageEntry>();
    fallback_msg->is_ephemeral = true; // Treat fallback as ephemeral
    fallback_msg->message_id = -1;
    fallback_msg->line_number = 48;
    fallback_msg->tarif_zone = 0;
    fallback_msg->static_intro = "Base48";
    fallback_msg->scrolling_message = "--.-"; // Placeholder/Idle message
    fallback_msg->next_message_hint = "Idle";
    fallback_msg->priority = 0; // Low priority

    ESP_LOGD(TAG, "Displaying fallback message.");
    send_commands_for_message(fallback_msg);
    // Do not update stats for fallback message
}

void B48DisplayController::check_for_emergency_messages() {
    // Placeholder for emergency logic (e.g., checking a specific ephemeral message flag)
    // If an emergency message needs to be displayed immediately, you might:
    // 1. Force this->current_message_ to the emergency message.
    // 2. Send commands immediately.
    // 3. Reset the state machine to DISPLAY_MESSAGE with a longer duration.
}
}  // namespace b48_display_controller
}  // namespace esphome