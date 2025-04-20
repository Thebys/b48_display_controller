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
#include <Arduino.h>       // For delay() and yield()
#include <esp_task_wdt.h>  // For esp_task_wdt_reset()
#include <LittleFS.h>
#include <esp_partition.h>  // For esp_partition_find and esp_partition_get

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.main";
static const char CR = 0x0D;  // Carriage Return for BUSE120 protocol

// Destructor
B48DisplayController::~B48DisplayController() {}

// --- HA Entity Setters ---
void B48DisplayController::set_message_queue_size_sensor(sensor::Sensor *sensor) {
  // Store the sensor regardless of whether HA integration exists yet
  this->message_queue_size_sensor_ = sensor;

  // If HA integration exists, pass the sensor to it
  if (this->ha_integration_) {
    this->ha_integration_->set_message_queue_size_sensor(sensor);
  }
}

void B48DisplayController::setup() {
  ESP_LOGCONFIG(TAG, "Setting up B48 Display Controller");
  ESP_LOGI(TAG, "Database path: '%s'", this->database_path_.empty() ? "(EMPTY)" : this->database_path_.c_str());

  // Initialize HA integration if it hasn't been already
  if (!this->ha_integration_) {
    ESP_LOGD(TAG, "Creating HA integration instance");
    this->ha_integration_.reset(new B48HAIntegration());
    this->ha_integration_->set_parent(this);

    // Pass the stored sensor to the HA integration
    if (this->message_queue_size_sensor_) {
      ESP_LOGD(TAG, "Passing message queue size sensor to HA integration");
      this->ha_integration_->set_message_queue_size_sensor(this->message_queue_size_sensor_);
    }
  }

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

  // --- Database Initialization Phase ---
  bool db_initialized = false;

  // Setup the HA integration component right away - it doesn't need DB
  if (this->ha_integration_) {
    ESP_LOGI(TAG, "Registering HA integration component...");
    App.register_component(this->ha_integration_.get());  // Register HA integration component
  } else {
    ESP_LOGW(TAG, "HA integration component not initialized!");
  }

  // Initialize filesystem first (required for database)
  bool filesystem_ok = initialize_filesystem();

  // Only continue with database initialization if filesystem is ready
  if (filesystem_ok) {
    // Check if we have enough space and a valid path before proceeding
    if (check_database_prerequisites()) {
      // We can try to initialize the database
      db_initialized = initialize_database();

      // If the database initialized successfully, check if we need to wipe it
      if (db_initialized && this->wipe_database_on_boot_) {
        db_initialized = handle_database_wipe();
      }

      // Run self-tests if configured to do so and database is available
      if (db_initialized && this->run_tests_on_startup_) {
        this->runSelfTests();
      }

      // Load messages from the database if available
      if (db_initialized) {
        this->pending_message_cache_refresh_.store(true);
      }
    }
  }

  // Prepare appropriate loading message based on database status
  display_startup_message(db_initialized);

  // Initial update for HA sensors
  update_ha_queue_size();

  // Start with transition mode
  this->state_ = TRANSITION_MODE;
  this->state_change_time_ = millis();

  // Note: we don't mark the component as failed even without a database
  // Since it can still work in ephemeral-only mode
  ESP_LOGI(TAG, "B48 Display Controller setup complete - %s", db_initialized ? "with database" : "in no-database mode");
}
void B48DisplayController::loop() {
  // Update current time using standard C time
  this->current_time_ = time(nullptr);

  // Check for pending refresh from callbacks
  if (this->pending_message_cache_refresh_.exchange(false)) {
    this->refresh_message_cache();
    update_ha_queue_size();
  }

  // Check for time test mode
  if (this->time_test_mode_active_) {
    // Run the time test mode state machine
    run_time_test_mode();
    return;
  }

  // Check for emergency messages first
  check_for_emergency_messages();

  // State machine switch
  switch (this->state_) {
    case TRANSITION_MODE:
      run_transition_mode();
      break;
    case DISPLAY_MESSAGE:
      run_display_message();
      break;
    case TIME_TEST_MODE:
      run_time_test_mode();
      break;
  }

  // Check ephemeral messages frequently (every 6 seconds)
  static unsigned long last_ephemeral_check = 0;
  if (millis() - last_ephemeral_check > 6 * 1000) {
    check_expired_ephemeral_messages();
    last_ephemeral_check = millis();
  }

  // Periodically check for expired messages (every hour)
  static unsigned long last_expiry_check = 0;
  if (millis() - last_expiry_check > 3600 * 1000) {
    check_expired_messages();
    last_expiry_check = millis();
  }

  // Check if we should purge disabled messages (every 24 hours)
  check_purge_interval();

  // Handle time synchronization with the display
  if (this->time_sync_interval_ > 0 && this->current_time_ > 0) {
    unsigned long current_millis = millis();
    unsigned long time_since_last_sync = current_millis - this->last_time_sync_;
    unsigned long sync_interval_millis = (unsigned long) this->time_sync_interval_ * 1000;

    if (time_since_last_sync >= sync_interval_millis) {
      ESP_LOGD(TAG, "Performing time sync. Elapsed: %lu ms, Interval: %lu ms", time_since_last_sync,
               sync_interval_millis);
      send_time_update();
      this->last_time_sync_ = current_millis;
    }
  }

  // Feed watchdog at end of loop to prevent timeout
  yield();
  esp_task_wdt_reset();
}

void B48DisplayController::dump_config() {
  ESP_LOGCONFIG(TAG, "B48 Display Controller:");
  ESP_LOGCONFIG(TAG, "  Database Path: %s", this->database_path_.c_str());
  ESP_LOGCONFIG(TAG, "  Transition Duration: %d seconds", this->transition_duration_);
  ESP_LOGCONFIG(TAG, "  Time Sync Interval: %d seconds", this->time_sync_interval_);
  ESP_LOGCONFIG(TAG, "  Emergency Priority Threshold: %d", this->emergency_priority_threshold_);
  ESP_LOGCONFIG(TAG, "  Run Tests on Startup: %s", YESNO(this->run_tests_on_startup_));
  ESP_LOGCONFIG(TAG, "  Wipe Database on Boot: %s", YESNO(this->wipe_database_on_boot_));
  if (this->display_enable_pin_ >= 0) {
    ESP_LOGCONFIG(TAG, "  Display Enable Pin: GPIO%d", this->display_enable_pin_);
  }
  ESP_LOGCONFIG(TAG, "  Time Test Mode: Available via HA service");
  ESP_LOGCONFIG(TAG, "  Time Test Status: %s", this->time_test_mode_active_ ? "Active" : "Inactive");

  // Log cache info
  std::lock_guard<std::mutex> lock(this->message_mutex_);
  ESP_LOGCONFIG(TAG, "  Persistent Messages (in cache): %d", this->persistent_messages_.size());
  ESP_LOGCONFIG(TAG, "  Ephemeral Messages (in RAM): %d", this->ephemeral_messages_.size());
}

// --- Public Methods Called by HA Integration ---

bool B48DisplayController::add_message(int priority, int line_number, int tarif_zone, const std::string &static_intro,
                                       const std::string &scrolling_message, const std::string &next_message_hint,
                                       int duration_seconds, const std::string &source_info, bool check_duplicates) {
  bool success = false;
  // Determine if the message is ephemeral or persistent based on duration
  if (duration_seconds > 0 && duration_seconds < EPHEMERAL_DURATION_THRESHOLD_SECONDS) {
    // --- Handle Ephemeral Message (Not saved to DB) ---
    ESP_LOGD(TAG, "Adding ephemeral message (duration %ds < %ds): %s%s (len=%zu)", duration_seconds,
             EPHEMERAL_DURATION_THRESHOLD_SECONDS, scrolling_message.substr(0, 30).c_str(),
             scrolling_message.length() > 30 ? "..." : "", scrolling_message.length());

    auto msg = std::make_shared<MessageEntry>();
    msg->message_id = -1;  // Ephemeral messages don't have a DB ID
    msg->priority = priority;
    msg->line_number = line_number;
    msg->tarif_zone = tarif_zone;
    msg->static_intro = static_intro;
    msg->scrolling_message = scrolling_message;
    msg->next_message_hint = next_message_hint;
    msg->expiry_time = time(nullptr) + duration_seconds;  // Set TTL based on current time
    msg->last_display_time = 0;                           // Use correct member name
    msg->is_ephemeral = true;                             // Mark as ephemeral

    {
      std::lock_guard<std::mutex> lock(this->message_mutex_);
      this->ephemeral_messages_.push_back(msg);
      ESP_LOGD(TAG, "Ephemeral message added to RAM queue. Current ephemeral count: %d",
               this->ephemeral_messages_.size());
    }

    // After adding a new ephemeral message
    std::sort(this->ephemeral_messages_.begin(), this->ephemeral_messages_.end(),
              [](const std::shared_ptr<MessageEntry> &a, const std::shared_ptr<MessageEntry> &b) {
                return a->priority > b->priority;  // Sort by descending priority
              });

    // If the message is above emergency threshold, force transition to display message
    if (priority >= this->emergency_priority_threshold_) {
      this->should_interrupt_ = true;
    }

    success = true;
  } else {
    // --- Handle Persistent Message (Saved to DB) ---
    ESP_LOGD(TAG, "Adding persistent message (duration %ds >= %ds or <= 0): %s%s (len=%zu)", duration_seconds,
             EPHEMERAL_DURATION_THRESHOLD_SECONDS, scrolling_message.substr(0, 30).c_str(),
             scrolling_message.length() > 30 ? "..." : "", scrolling_message.length());

    if (!this->db_manager_) {
      ESP_LOGW(TAG, "Database manager is not initialized - converting to ephemeral message");

      // If database is not available, convert to ephemeral message with requested duration
      // or a default duration if persistent (0 or negative)
      int ephemeral_duration = (duration_seconds > 0)
                                   ? duration_seconds
                                   : EPHEMERAL_DURATION_THRESHOLD_SECONDS;  // Default 10 min for persistent

      return add_message(priority, line_number, tarif_zone, static_intro, scrolling_message, next_message_hint,
                         ephemeral_duration, source_info, false);
    }

    // Ensure duration is valid (set to 0 for permanent if > 1 year)
    int actual_duration = duration_seconds;
    if (actual_duration > 31536000) {  // 1 year in seconds
      ESP_LOGW(TAG, "Duration %d exceeds maximum (1 year), setting message to permanent (duration 0)",
               duration_seconds);
      actual_duration = 0;  // Set to 0 for permanent instead of capping
    }

    // Call the database manager to add the message
    bool success = this->db_manager_->add_persistent_message(
        priority, line_number, tarif_zone, static_intro, scrolling_message, next_message_hint,
        actual_duration,  // Use potentially capped duration
        source_info.empty() ? "Persistent" : source_info, check_duplicates);

    if (success) {
      ESP_LOGI(TAG, "Successfully added message to database. Triggering cache refresh.");
      this->pending_message_cache_refresh_.store(true);
    } else {
      ESP_LOGE(TAG, "Failed to add message to database.");
    }
  }
  return success;
}

bool B48DisplayController::update_message(int message_id, int priority, bool is_enabled, int line_number,
                                          int tarif_zone, const std::string &static_intro,
                                          const std::string &scrolling_message, const std::string &next_message_hint,
                                          int duration_seconds, const std::string &source_info) {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized for update_persistent_message");
    return false;
  }

  ESP_LOGD(TAG, "Updating persistent message with ID %d: %s%s (len=%zu)", message_id,
           scrolling_message.substr(0, 30).c_str(), scrolling_message.length() > 30 ? "..." : "",
           scrolling_message.length());

  // Call the database manager to update the message
  bool success = this->db_manager_->update_persistent_message(message_id, priority, is_enabled, line_number, tarif_zone,
                                                              static_intro, scrolling_message, next_message_hint,
                                                              duration_seconds, source_info);

  if (success) {
    this->pending_message_cache_refresh_.store(true);
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
    this->pending_message_cache_refresh_.store(true);
  }

  return success;
}

// New method to wipe and reinitialize the database and clear RAM cache
bool B48DisplayController::wipe_and_reinitialize_database() {
  ESP_LOGW(TAG, "Wiping and reinitializing database...");
  // Only lock when modifying ephemeral cache, avoid holding lock over DB operations

  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized for wipe_and_reinitialize_database");
    return false;
  }

  // 1. Wipe the database (drops and recreates table)
  if (!this->db_manager_->wipe_database()) {
    ESP_LOGE(TAG, "Failed to wipe database.");
    return false;  // Don't proceed if wipe fails
  }

  // 2. Reinitialize the database (recreates schema)
  if (!this->db_manager_->initialize()) {
    ESP_LOGE(TAG, "Failed to re-initialize database after wipe.");
    this->mark_failed();
    return false;
  }

  // 3. Clear ephemeral message cache in RAM under lock
  {
    std::lock_guard<std::mutex> lock(this->message_mutex_);
    this->ephemeral_messages_.clear();
    ESP_LOGD(TAG, "Cleared ephemeral message cache.");
  }
  // Trigger refresh of message cache
  this->pending_message_cache_refresh_.store(true);

  ESP_LOGW(TAG, "Database wipe and reinitialization complete.");
  return true;
}

// --- Internal HA State Update ---
void B48DisplayController::update_ha_queue_size() {
  // Combine counts from both persistent cache and ephemeral queue
  int total_messages = 0;
  {
    std::lock_guard<std::mutex> lock(this->message_mutex_);
    total_messages = this->persistent_messages_.size() + this->ephemeral_messages_.size();
  }

  if (this->ha_integration_) {
    this->ha_integration_->publish_queue_size(total_messages);
  }
}

bool B48DisplayController::refresh_message_cache() {
  // Ensure database manager is initialized
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager is not initialized in refresh_message_cache");
    return false;
  }

  // Perform database query outside of mutex lock to avoid blocking other tasks
  ESP_LOGD(TAG, "Querying persistent messages from database outside lock...");
  auto new_persistent = this->db_manager_->get_active_persistent_messages();
  ESP_LOGD(TAG, "Database returned %d persistent messages", new_persistent.size());

  // Now update the cache under lock
  {
    std::lock_guard<std::mutex> lock(this->message_mutex_);
    this->persistent_messages_ = std::move(new_persistent);
  }

  // Update HA sensor with new queue size
  update_ha_queue_size();
  return true;
}

void B48DisplayController::check_expired_ephemeral_messages() {
  // Only check ephemeral messages in RAM (no database interaction)
  time_t now = time(nullptr);
  int ephemeral_expired = 0;
  std::lock_guard<std::mutex> lock(this->message_mutex_);

  // Remove expired ephemeral messages based on TTL only
  this->ephemeral_messages_.erase(std::remove_if(this->ephemeral_messages_.begin(), this->ephemeral_messages_.end(),
                                                 [&](const std::shared_ptr<MessageEntry> &msg) {
                                                   bool expired = false;
                                                   // Check TTL
                                                   if (msg->expiry_time > 0 && msg->expiry_time <= now) {
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
    // No need to update HA sensor for ephemeral messages
  }
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
    this->pending_message_cache_refresh_.store(true);
  } else if (persistent_expired < 0) {
    ESP_LOGE(TAG, "Error checking persistent message expiry.");
  }

  // No need to check ephemeral messages here since we have a separate method for that
}

std::shared_ptr<MessageEntry> B48DisplayController::select_next_message() {
  time_t now = time(nullptr);
  std::shared_ptr<MessageEntry> selected_message = nullptr;
  bool has_database = (this->db_manager_ != nullptr);

  // Priority threshold for emergency messages
  const int emergency_threshold = this->emergency_priority_threshold_;

  // Copy necessary data under lock to minimize lock duration
  std::vector<std::shared_ptr<MessageEntry>> ephemeral_copy;
  std::vector<std::shared_ptr<MessageEntry>> persistent_copy;

  // Take a brief lock to copy the message vectors
  {
    std::lock_guard<std::mutex> lock(this->message_mutex_);
    ephemeral_copy = this->ephemeral_messages_;
    if (has_database) {
      persistent_copy = this->persistent_messages_;
    }
  }

  // 1. First pass: Check for emergency messages (above threshold) in ephemeral messages
  // Now we can work with our copies without holding the lock
  for (const auto &msg : ephemeral_copy) {
    if (msg->expiry_time > 0 && msg->expiry_time <= now)
      continue;

    if (msg->priority >= emergency_threshold) {
      selected_message = msg;
      ESP_LOGI(TAG, "Selected emergency ephemeral message (Prio: %d)", selected_message->priority);
      break;
    }
  }

  // 2. If no emergency message found, consider all messages based on a weighted approach
  if (!selected_message) {
    // For non-emergency selection, we'll mix ephemeral and persistent messages

    // Temporary vector to hold candidate messages with their selection weights
    std::vector<std::pair<std::shared_ptr<MessageEntry>, float>> candidates;

    // Add valid ephemeral messages to candidates
    for (const auto &msg : ephemeral_copy) {
      if (msg->expiry_time > 0 && msg->expiry_time <= now)
        continue;

      // Calculate a weight based on priority - higher priority = higher weight
      float weight = 0.5f + (msg->priority / 100.0f);
      candidates.push_back({msg, weight});
    }

    // Add persistent messages if database is available
    if (has_database && !persistent_copy.empty()) {
      static size_t last_persistent_index = 0;

      // Add ALL persistent messages to candidates, not just a limited number
      // This ensures we consider the entire message pool
      for (size_t i = 0; i < persistent_copy.size(); i++) {
        auto msg = persistent_copy[i];

        // Slightly improved weight calculation that better scales with priority
        // Base weight is 0.3, max priority contribution would be ~0.5 for priority 60
        float weight = 0.3f + (msg->priority / 100.0f);

        candidates.push_back({msg, weight});
      }
    }

    // If we have candidates and time available, build candidates list
    if (!candidates.empty()) {
      ESP_LOGD(TAG, "Considering %d total candidates for new message.", candidates.size());
      // Sort candidates by weight in descending order
      std::sort(candidates.begin(), candidates.end(),
                [](const std::pair<std::shared_ptr<MessageEntry>, float> &a,
                   const std::pair<std::shared_ptr<MessageEntry>, float> &b) { return a.second > b.second; });

      // Create a vector to store penalty information for the final table
      std::vector<std::tuple<std::shared_ptr<MessageEntry>, float, float, time_t>> penalty_info;
      
      // Penalize or remove candidates that have been displayed recently
      time_t now = time(nullptr);
      const int MIN_REPEAT_SECONDS = 180;  // Minimum seconds before showing same message again

      for (auto &candidate : candidates) {
        auto &msg = candidate.first;
        time_t last_display = 0;
        float original_weight = candidate.second;

        // Get appropriate last display time based on message type
        if (msg->is_ephemeral) {
          last_display = msg->last_display_time;
        } else if (msg->message_id > 0) {
          // Check if we have last display time for this persistent message
          auto it = last_display_times_.find(msg->message_id);
          if (it != last_display_times_.end()) {
            last_display = it->second;
          }
        }

        // Calculate time since last display
        time_t time_since_display = (last_display > 0) ? now - last_display : -1;
        float penalty_factor = 1.0f;

        // Apply penalties based on how recently the message was displayed
        if (last_display > 0) {
          if (time_since_display < MIN_REPEAT_SECONDS) {
            // High penalty for very recent messages - 80% weight reduction
            penalty_factor = 0.2f;
          } else if (time_since_display < MIN_REPEAT_SECONDS * 3) {
            // Medium penalty - 50% weight reduction
            penalty_factor = 0.5f;
          } else if (time_since_display < MIN_REPEAT_SECONDS * 18) {
            // Light penalty - 20% weight reduction
            penalty_factor = 0.8f;
          }
          
          // Apply the penalty
          candidate.second *= penalty_factor;
        }
        
        // Store all the info for the table
        penalty_info.push_back(std::make_tuple(msg, original_weight, penalty_factor, time_since_display));
      }

      // Resort after applying penalties
      std::sort(candidates.begin(), candidates.end(),
                [](const std::pair<std::shared_ptr<MessageEntry>, float> &a,
                   const std::pair<std::shared_ptr<MessageEntry>, float> &b) { return a.second > b.second; });

      // Sort penalty_info to match the new candidate order
      std::sort(penalty_info.begin(), penalty_info.end(), 
                [&candidates](const std::tuple<std::shared_ptr<MessageEntry>, float, float, time_t> &a,
                             const std::tuple<std::shared_ptr<MessageEntry>, float, float, time_t> &b) {
                    // Find weights in the sorted candidates list
                    float weight_a = 0.0f, weight_b = 0.0f;
                    for (const std::pair<std::shared_ptr<MessageEntry>, float> &c : candidates) {
                        if (c.first == std::get<0>(a)) weight_a = c.second;
                        if (c.first == std::get<0>(b)) weight_b = c.second;
                    }
                    return weight_a > weight_b;
                });

      // Log the consolidated table
      ESP_LOGI(TAG, "Message selection table (%d candidates):", candidates.size());
      ESP_LOGI(TAG, "  # | ID  | Type       | Prio | Initial | Penalty | Final  | Last Seen");
      ESP_LOGI(TAG, "----|-----|------------|------|---------|---------|--------|----------");
      
      const int candidates_to_log = std::min(20, static_cast<int>(penalty_info.size()));
      for (int i = 0; i < candidates_to_log; i++) {
        const auto &info = penalty_info[i];
        auto msg = std::get<0>(info);
        float original_weight = std::get<1>(info);
        float penalty_factor = std::get<2>(info);
        time_t time_since_display = std::get<3>(info);
        
        // Find the final weight in candidates
        float final_weight = 0.0f;
        for (const auto &c : candidates) {
            if (c.first == msg) {
                final_weight = c.second;
                break;
            }
        }
        
        const char* selected_marker = (i == 0) ? "→ " : "  ";
        
        // Format the time since display
        std::string time_display;
        if (time_since_display < 0) {
            time_display = "never";
        } else {
            char time_buffer[16];
            snprintf(time_buffer, sizeof(time_buffer), "%ds ago", (int)time_since_display);
            time_display = time_buffer;
        }
        
        ESP_LOGI(TAG, "%s%2d | %-3d | %-10s | %4d | %7.3f | %7.3f | %6.3f | %s",
                 selected_marker, i+1, msg->message_id, 
                 msg->is_ephemeral ? "ephemeral" : "persistent",
                 msg->priority, original_weight, penalty_factor, final_weight,
                 time_display.c_str());
      }

      // Select the highest weighted candidate
      selected_message = candidates[0].first;
      float selected_weight = candidates[0].second;

      // Update the round-robin index for persistent messages
      if (!selected_message->is_ephemeral && has_database) {
        static size_t last_persistent_index = 0;
        // Find the index of this message in persistent_copy
        for (size_t i = 0; i < persistent_copy.size(); i++) {
          if (persistent_copy[i]->message_id == selected_message->message_id) {
            last_persistent_index = (i + 1) % persistent_copy.size();
            break;
          }
        }
      }

      ESP_LOGI(TAG, "Selected %s message ID: %d (Prio: %d, Weight: %.2f) - Title: %s",
               selected_message->is_ephemeral ? "ephemeral" : "persistent", selected_message->message_id,
               selected_message->priority, selected_weight, selected_message->static_intro.c_str());
    }

    // Fall back to a simple selection if we have no candidates with positive weights
    else if (has_database && !persistent_copy.empty()) {
      static size_t last_persistent_index = 0;

      ESP_LOGW(TAG, "Weighted selection algorithm found no suitable candidates, falling back to round-robin");

      // Only use the fallback if there are actually persistent messages available
      size_t current_index = last_persistent_index % persistent_copy.size();
      selected_message = persistent_copy[current_index];
      last_persistent_index = (last_persistent_index + 1) % persistent_copy.size();

      ESP_LOGD(TAG, "Selected fallback persistent message ID: %d (Prio: %d)", selected_message->message_id,
               selected_message->priority);
    }
  }

  // 3. If still no message, return nullptr (will trigger fallback)
  if (!selected_message) {
    ESP_LOGW(TAG, "No suitable message found for display.");
  } else {
    ESP_LOGD(TAG, "Selected message: %d", selected_message->message_id);
    this->current_display_duration_ms_ = calculate_display_duration(selected_message) * 1000;
  }
  return selected_message;
}

// --- Other methods (calculate_display_duration, update_message_display_stats, etc.) ---
// Updated to handle ephemeral messages using TTL-based expiration only

int B48DisplayController::calculate_display_duration(const std::shared_ptr<MessageEntry> &msg) {
  // Could be based on message length, priority, etc.
  if (!msg)
    return 4;  // Default duration if msg is null

  int base_duration = 5;     // Base seconds
  int chars_per_second = 3;  // Estimated scroll speed
  int length_duration = base_duration;
  if (msg->is_ephemeral) {
    length_duration = msg->expiry_time - time(nullptr);
  } else {
    length_duration = (base_duration + msg->scrolling_message.length()) / chars_per_second;
  }

  // Use a simple heuristic: longer messages display for longer, up to a max
  int calculated_duration = std::min(length_duration, 60);
  ESP_LOGI(
      TAG,
      "Calculated display duration for message ID %d: base_duration=%d, length_duration=%d, calculated_duration=%d",
      msg->message_id, base_duration, length_duration, calculated_duration);
  return calculated_duration;
}

void B48DisplayController::update_message_display_stats(const std::shared_ptr<MessageEntry> &msg) {
  if (!msg)
    return;

  time_t now = time(nullptr);

  if (msg->is_ephemeral) {
    std::lock_guard<std::mutex> lock(this->message_mutex_);
    msg->last_display_time = now;
  } else {
    // Update last display time for persistent messages (used for repeat delay)
    last_display_times_[msg->message_id] = now;
    ESP_LOGV(TAG, "Updated last display time for persistent message ID %d", msg->message_id);
  }
}

// --- BUSE120 Protocol Methods ---

void B48DisplayController::send_line_number(int line) { this->serial_protocol_.send_line_number(line); }

void B48DisplayController::send_tarif_zone(int zone) { this->serial_protocol_.send_tarif_zone(zone); }

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

void B48DisplayController::send_invert_command() { this->serial_protocol_.send_invert_command(); }

void B48DisplayController::switch_to_cycle(int cycle) { this->serial_protocol_.switch_to_cycle(cycle); }

void B48DisplayController::send_commands_for_message(const std::shared_ptr<MessageEntry> &msg) {
  if (!msg) {
    ESP_LOGW(TAG, "send_commands_for_message called with null message");
    return;
  }
  ESP_LOGD(TAG, "Sending commands for message (Prio: %d, ID: %d, Ephem: %d): %s%s (len=%zu)", msg->priority,
           msg->message_id, msg->is_ephemeral, msg->scrolling_message.substr(0, 30).c_str(),
           msg->scrolling_message.length() > 30 ? "..." : "", msg->scrolling_message.length());

  send_line_number(msg->line_number);
  send_tarif_zone(msg->tarif_zone);
  send_static_intro(msg->static_intro);
  send_scrolling_message(msg->scrolling_message);
  send_next_message_hint(msg->next_message_hint);
}

// --- State Machine Methods ---

void B48DisplayController::run_transition_mode() {
  // The transition mode is used to prepare the next message.
  // First set cycle 6, showing "next message" hint of CURRENT message.
  unsigned long time_in_state = millis() - this->state_change_time_;
  unsigned long transition_duration_ms = this->transition_duration_ * 1000;

  // If this is our first entry to this state, log and setup
  if (time_in_state < 10 || this->should_interrupt_) {
    ESP_LOGD(TAG, "========= WELCOME TO TRANSITION MODE =========");
    if (this->should_interrupt_) {
      transition_duration_ms = 0;
      this->should_interrupt_ = false;
    }
    ESP_LOGD(TAG, "Current message: %d", this->current_message_ ? this->current_message_->message_id : 0);
    this->serial_protocol_.switch_to_cycle(6);
    this->current_message_ = select_next_message();

    ESP_LOGD(TAG, "Selected message: %d", this->current_message_ ? this->current_message_->message_id : 0);

    if (this->current_message_) {
      send_commands_for_message(this->current_message_);
      ESP_LOGD(TAG, "Message prepared, waiting in cycle 6 for %d seconds", this->transition_duration_);
    } else {
      display_fallback_message();
      ESP_LOGD(TAG, "No message selected, displaying fallback, waiting in cycle 6 for %d seconds",
               this->transition_duration_);
    }
  }

  // Check if transition duration has elapsed
  if (time_in_state < transition_duration_ms) {
    // Not enough time has passed, stay in transition mode
    return;
  }

  // Transition duration has elapsed, switch to cycle 0 and move to display mode
  ESP_LOGD(TAG, "Transition duration elapsed, spending %d ms", time_in_state);
  this->serial_protocol_.switch_to_cycle(0);

  this->state_ = DISPLAY_MESSAGE;
  this->state_change_time_ = millis();
}

void B48DisplayController::run_display_message() {
  // Use the pre-calculated display duration instead of recalculating it
  unsigned long time_in_state = millis() - this->state_change_time_;

  // Check if we've reached the end of the display duration
  if (time_in_state >= this->current_display_duration_ms_ || this->should_interrupt_) {
    ESP_LOGV(TAG, "Display state ending, updating stats and moving to TRANSITION_MODE");
    update_message_display_stats(this->current_message_);  // Update stats before transitioning
    this->current_message_ = nullptr;                      // Clear current message
    this->state_ = TRANSITION_MODE;
    this->state_change_time_ = millis();
  }
}

void B48DisplayController::display_fallback_message() {
  // Define a simple fallback message
  auto fallback_msg = std::make_shared<MessageEntry>();
  fallback_msg->is_ephemeral = true;  // Treat fallback as ephemeral
  fallback_msg->message_id = -1;
  fallback_msg->line_number = 48;
  fallback_msg->tarif_zone = 101;
  fallback_msg->static_intro = "Base48";
  fallback_msg->scrolling_message = "This is fallback message. Something is wrong.";  // Placeholder/Idle message
  fallback_msg->next_message_hint = "0xDEADBEEF__";
  fallback_msg->priority = 0;  // Low priority

  ESP_LOGD(TAG, "Displaying fallback message.");
  send_commands_for_message(fallback_msg);
  // Do not update stats for fallback message
}

void B48DisplayController::check_for_emergency_messages() {
  // Check we have sensible time to check
  if (time(nullptr) - this->last_ephemeral_check_time_ < 1000) {
    return;
  }
  this->last_ephemeral_check_time_ = time(nullptr);
  // Variables for decision making - populated under different locks
  std::shared_ptr<MessageEntry> highest_priority_message = nullptr;
  bool has_messages = false;
  unsigned long time_in_state = 0;

  // First, safely check if we have any messages and grab the highest priority one
  {
    std::lock_guard<std::mutex> lock(this->message_mutex_);

    if (this->ephemeral_messages_.empty()) {
      return;  // No messages to process
    }
    // Copy the highest priority message to use outside the lock
    highest_priority_message = this->ephemeral_messages_[0];
  }
  // Check if the message is expired based on expiry_time
  if (highest_priority_message->expiry_time > 0 && highest_priority_message->expiry_time <= time(nullptr)) {
    ESP_LOGD(TAG, "Highest priority message is expired, removing it from the queue");
    std::lock_guard<std::mutex> lock(this->message_mutex_);
    {
      this->ephemeral_messages_.erase(this->ephemeral_messages_.begin());
    }
    highest_priority_message = nullptr;
  }
  has_messages = highest_priority_message != nullptr;
  if (!has_messages) {
    return;
  }

  // Outside the lock, do business...
  // Check for emergency priority threshold

  // Process the interruption if needed
  if (this->should_interrupt_) {
    update_message_display_stats(this->current_message_);  // Update stats before transitioning
    this->state_ = TRANSITION_MODE;                        // Force transition to pick up new message
    this->state_change_time_ = millis();
  }
}

void B48DisplayController::dump_database_for_diagnostics() {
  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Cannot dump database - database manager is not initialized");
    return;
  }

  ESP_LOGI(TAG, "Dumping database for diagnostics");
  this->db_manager_->dump_all_messages();

  // Also dump the message cache state
  {
    std::lock_guard<std::mutex> lock(this->message_mutex_);
    ESP_LOGI(TAG, "Current message cache state: %d persistent messages in cache", this->persistent_messages_.size());
    for (size_t i = 0; i < this->persistent_messages_.size(); i++) {
      const auto &msg = this->persistent_messages_[i];
      ESP_LOGI(TAG, "Cache[%d]: ID=%d, Priority=%d, Line=%d, Zone=%d, Text='%s%s' (len=%zu)", i, msg->message_id,
               msg->priority, msg->line_number, msg->tarif_zone, msg->scrolling_message.substr(0, 30).c_str(),
               msg->scrolling_message.length() > 30 ? "..." : "", msg->scrolling_message.length());
    }
  }
}

// Helper methods for setup to improve readability and avoid gotos

void B48DisplayController::log_filesystem_stats() {
  // check if LittleFS is available
  if (!LittleFS.begin(false)) {
    ESP_LOGE(TAG, "LittleFS is not available");
    return;
  }
  ESP_LOGI(TAG, "========= FILESYSTEM STATS =========");

  // Log ESP32 heap information first
  ESP_LOGI(TAG, "ESP32 Memory - Free heap: %u bytes, Minimum free heap: %u bytes", ESP.getFreeHeap(),
           ESP.getMinFreeHeap());

  // Get and log detailed storage information
  size_t total_bytes = LittleFS.totalBytes();
  size_t used_bytes = LittleFS.usedBytes();
  size_t free_bytes = total_bytes - used_bytes;
  float used_percent = (used_bytes * 100.0f) / total_bytes;

  ESP_LOGI(TAG, "LittleFS storage:");
  ESP_LOGI(TAG, "  Total space: %zu bytes (%.1f KB)", total_bytes, total_bytes / 1024.0f);
  ESP_LOGI(TAG, "  Used space:  %zu bytes (%.1f KB)", used_bytes, used_bytes / 1024.0f);
  ESP_LOGI(TAG, "  Free space:  %zu bytes (%.1f KB)", free_bytes, free_bytes / 1024.0f);
  ESP_LOGI(TAG, "  Usage:       %.1f%%", used_percent);

  // List files in the root directory to see what's taking up space
  ESP_LOGI(TAG, "Files in LittleFS root:");
  File root = LittleFS.open("/");
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    int file_count = 0;
    size_t total_listed_size = 0;
    while (file) {  // Remove the limit to show all files
      size_t file_size = file.size();
      total_listed_size += file_size;
      ESP_LOGI(TAG, "  %s: %zu bytes (%.1f KB)", file.name(), file_size, file_size / 1024.0f);
      file = root.openNextFile();
      file_count++;
    }
    ESP_LOGI(TAG, "Total: %d files using %zu bytes (%.1f KB)", file_count, total_listed_size,
             total_listed_size / 1024.0f);
  }
  root.close();
}

bool B48DisplayController::initialize_filesystem() {
  ESP_LOGI(TAG, "Initializing LittleFS...");

  // Find the partition labeled "spiffs" or similar in the partition table
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
  if (it == NULL) {
    ESP_LOGE(TAG, "Failed to find SPIFFS partition!");
    return false;
  }

  const esp_partition_t *spiffs_partition = esp_partition_get(it);
  esp_partition_iterator_release(it);

  ESP_LOGI(TAG, "Found SPIFFS partition: label='%s', size=%u bytes (%.1f KB)", spiffs_partition->label,
           spiffs_partition->size, spiffs_partition->size / 1024.0f);

  // Try to mount without formatting first
  if (!LittleFS.begin(false)) {
    ESP_LOGW(TAG, "Initial LittleFS mount failed. Trying format=true...");
    if (!LittleFS.begin(true)) {
      ESP_LOGE(TAG, "Failed to mount LittleFS even after formatting. Running without database.");
      return false;
    }
    ESP_LOGI(TAG, "LittleFS mounted successfully after formatting.");
  } else {
    ESP_LOGI(TAG, "LittleFS mounted successfully without formatting.");
  }

  // Get basic info needed for checks
  size_t total_bytes = LittleFS.totalBytes();
  size_t free_bytes = total_bytes - LittleFS.usedBytes();

  ESP_LOGI(TAG, "  Partition:   %u bytes (%.1f KB)", spiffs_partition->size, spiffs_partition->size / 1024.0f);

  // If total_bytes is significantly smaller than partition size, something is wrong
  if (total_bytes < spiffs_partition->size * 0.8) {
    ESP_LOGW(TAG, "LittleFS is only seeing %zu bytes when partition is %u bytes!", total_bytes, spiffs_partition->size);
    ESP_LOGW(TAG, "This may indicate a configuration issue. Will continue with available space.");
  }

  // If this is a dedicated partition for SQLite, inform about expectations
  ESP_LOGI(TAG, "SQLite typically needs 30-50KB of free contiguous space");

  // Only check if we have minimum required space - lower threshold to work with available space
  if (free_bytes < 16384) {  // Changed from 32768 (32KB) to 16384 (16KB)
    ESP_LOGE(TAG, "Not enough free space for database (need at least 16KB). Consider increasing partition size.");
    return false;
  }

  // Log filesystem stats again after mounting to show what's really available
  log_filesystem_stats();

  return true;
}

bool B48DisplayController::check_database_prerequisites() {
  // Check database path
  if (this->database_path_.empty()) {
    ESP_LOGE(TAG, "Database path is empty! Running without database.");
    return false;
  }

  return true;
}

bool B48DisplayController::initialize_database() {
  ESP_LOGI(TAG, "Creating database manager with path: '%s'", this->database_path_.c_str());

  // Check if the database file already exists and log its size
  if (LittleFS.exists(this->database_path_.c_str())) {
    File db_file = LittleFS.open(this->database_path_.c_str(), "r");
    if (db_file) {
      size_t file_size = db_file.size();
      db_file.close();
      ESP_LOGI(TAG, "Existing database file size: %zu bytes (%.1f KB)", file_size, file_size / 1024.0f);

      // Only delete if the file is suspiciously small (likely corrupt)
      if (file_size < 512) {
        ESP_LOGW(TAG, "Database file exists but is very small, might be corrupt. Removing...");
        if (LittleFS.remove(this->database_path_.c_str())) {
          ESP_LOGI(TAG, "Removed potentially corrupt database file");
        } else {
          ESP_LOGE(TAG, "Failed to remove potentially corrupt database file");
        }
      }
    }
  } else {
    ESP_LOGI(TAG, "No existing database file found, will create new");
  }

  // Create the database manager
  db_manager_.reset(new B48DatabaseManager(this->database_path_));

  // Try to initialize the database with retries
  for (int retry = 0; retry < 3; retry++) {
    if (retry > 0) {
      ESP_LOGW(TAG, "Retrying database initialization (attempt %d of 3)...", retry + 1);

      // Log memory status before retry
      size_t total_bytes = LittleFS.totalBytes();
      size_t used_bytes = LittleFS.usedBytes();
      size_t free_bytes = total_bytes - used_bytes;
      ESP_LOGI(TAG, "Before retry: %.1f KB free in LittleFS, %u bytes free in heap", free_bytes / 1024.0f,
               ESP.getFreeHeap());

      // If second retry fails, try to delete the file before final attempt
      if (retry == 2) {
        ESP_LOGW(TAG, "Final retry attempt - trying to remove database file first...");
        if (LittleFS.exists(this->database_path_.c_str())) {
          if (LittleFS.remove(this->database_path_.c_str())) {
            ESP_LOGI(TAG, "Successfully removed existing database file for fresh start");
          } else {
            ESP_LOGE(TAG, "Failed to remove database file");
          }
        }
      }

      delay(1000);  // Wait a second before retrying
    }

    // Attempt initialization
    bool success = this->db_manager_->initialize();

    if (success) {
      // Log final database file size on success
      if (LittleFS.exists(this->database_path_.c_str())) {
        File db_file = LittleFS.open(this->database_path_.c_str(), "r");
        if (db_file) {
          size_t file_size = db_file.size();
          db_file.close();
          ESP_LOGI(TAG, "Successfully created database file: %zu bytes (%.1f KB)", file_size, file_size / 1024.0f);
        }
      }

      ESP_LOGI(TAG, "Database initialized successfully!");
      return true;
    } else {
      ESP_LOGE(TAG, "Failed to initialize the database manager (attempt %d)", retry + 1);
    }
  }

  // If we get here, all attempts failed
  ESP_LOGE(TAG, "All database initialization attempts failed! Running without database.");
  db_manager_.reset(nullptr);  // Clean up failed DB manager
  return false;
}

bool B48DisplayController::handle_database_wipe() {
  ESP_LOGW(TAG, "Configuration has wipe_database_on_boot enabled. Wiping database...");
  if (!db_manager_->wipe_database()) {
    ESP_LOGE(TAG, "Failed to wipe database");
    return true;  // Not a fatal error, continue with database (even if wipe failed)
  }

  // Need to recreate schema and bootstrap after wiping
  if (!db_manager_->initialize()) {
    ESP_LOGE(TAG, "Failed to reinitialize database after wiping. Running without database.");
    db_manager_.reset(nullptr);  // Clean up failed DB manager
    return false;
  }

  return true;
}

void B48DisplayController::display_startup_message(bool db_initialized) {
  auto loading_msg = std::make_shared<MessageEntry>();
  loading_msg->message_id = -1;
  loading_msg->line_number = 48;
  loading_msg->tarif_zone = 101;
  loading_msg->static_intro = "Loading";
  loading_msg->is_ephemeral = true;

  if (db_initialized) {
    loading_msg->scrolling_message = "System ready with database.";
    loading_msg->next_message_hint = "DB Ready";
    ESP_LOGI(TAG, "Running with database support");
  } else {
    loading_msg->scrolling_message = "System running in no-database mode.";
    loading_msg->next_message_hint = "No DB";
    ESP_LOGW(TAG, "Running in no-database mode");
  }

  loading_msg->priority = 75;
  send_commands_for_message(loading_msg);
}

// --- Database maintenance methods ---

bool B48DisplayController::purge_disabled_messages() {
  ESP_LOGI(TAG, "Purging disabled messages from database");

  if (!this->db_manager_) {
    ESP_LOGE(TAG, "Database manager not available, cannot purge messages");
    return false;
  }

  int purged_count = this->db_manager_->purge_disabled_messages();

  if (purged_count < 0) {
    ESP_LOGE(TAG, "Error occurred during disabled message purge");
    return false;
  }

  ESP_LOGI(TAG, "Successfully purged %d disabled messages", purged_count);

  // Log filesystem stats after purge
  if (purged_count > 0) {
    ESP_LOGI(TAG, "Filesystem stats after purge:");
    log_filesystem_stats();
  }

  // Update the last purge time regardless of whether messages were found
  this->last_purge_time_ = time(nullptr);

  return true;
}

void B48DisplayController::check_purge_interval() {
  // Skip if no database manager
  if (!this->db_manager_) {
    return;
  }

  // Get current time
  time_t now = time(nullptr);

  // Check if this is the first run (last_purge_time_ is 0)
  if (this->last_purge_time_ == 0) {
    this->last_purge_time_ = now;
    ESP_LOGD(TAG, "Initialized last purge time to current time");
    return;
  }

  // Calculate elapsed time in hours
  double hours_elapsed = difftime(now, this->last_purge_time_) / 3600.0;

  // Check if purge interval has elapsed
  if (hours_elapsed >= this->purge_interval_hours_) {
    ESP_LOGI(TAG, "Purge interval of %d hours elapsed (%.2f hours since last purge), starting automatic purge",
             this->purge_interval_hours_, hours_elapsed);

    this->purge_disabled_messages();
  }
}
}  // namespace b48_display_controller
}  // namespace esphome