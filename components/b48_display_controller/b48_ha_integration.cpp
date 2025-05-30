#include "b48_ha_integration.h"
#include "b48_display_controller.h" // Include the main controller header
#include "esphome/core/log.h"
#include "esphome/core/application.h"
#include "esphome/components/api/api_server.h" // Required for register_service
#include "esphome/components/api/custom_api_device.h" // For API service registration

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.ha"; // Short tag for HA integration

// ====================================
// B48HAIntegration Implementation
// ====================================

void B48HAIntegration::setup() {
  ESP_LOGCONFIG(TAG, "Setting up B48 Home Assistant Integration...");
  this->register_services_();
  // Initial state publishing (e.g., queue size) should be triggered by the parent controller
  // after it has loaded its state.
}

void B48HAIntegration::register_services_() {
  ESP_LOGD(TAG, "Registering HA services...");

  // Register service for deleting persistent messages
  register_service(&B48HAIntegration::handle_delete_message_service_, "delete_persistent_message",
                 {"message_id"});

  // Register service for wiping the database
  register_service(&B48HAIntegration::handle_wipe_database_service_, "wipe_database");

  // Register service for dumping database diagnostics
  register_service(&B48HAIntegration::handle_dump_database_service_, "dump_messages_for_diagnostics");
  
  // Register services for time test mode
  register_service(&B48HAIntegration::handle_start_time_test_service_, "start_time_test");
  register_service(&B48HAIntegration::handle_stop_time_test_service_, "stop_time_test");
  
  // Register services for character reverse test mode
  register_service(&B48HAIntegration::handle_start_character_reverse_test_service_, "start_character_reverse_test");
  register_service(&B48HAIntegration::handle_stop_character_reverse_test_service_, "stop_character_reverse_test");
  
  // Add shorter aliases
  register_service(&B48HAIntegration::handle_start_character_reverse_test_service_, "start_char_test");
  register_service(&B48HAIntegration::handle_stop_character_reverse_test_service_, "stop_char_test");
  
  // Register service for database maintenance
  register_service(&B48HAIntegration::handle_purge_disabled_messages_service_, "purge_disabled_messages");

  // Register service for filesystem stats
  register_service(&B48HAIntegration::handle_display_filesystem_stats_service_, "display_filesystem_stats");

  // Register services for raw command and state machine control
  register_service(&B48HAIntegration::handle_send_raw_buse_command_service_, "send_raw_buse_command", {"payload"});
  register_service(&B48HAIntegration::handle_pause_state_machine_service_, "pause_display_state_machine");
  register_service(&B48HAIntegration::handle_resume_state_machine_service_, "resume_display_state_machine");

  ESP_LOGD(TAG, "Service registration complete.");
}

// --- Service Handlers ---

void B48HAIntegration::handle_delete_message_service_(int message_id) {
  ESP_LOGD(TAG, "Service b48_delete_message called: message_id=%d", message_id);
  if (message_id <= 0) {
    ESP_LOGW(TAG, "Delete message failed: Invalid message_id (%d).", message_id);
    return;
  }

  // Call parent controller's method
  bool success = parent_->delete_persistent_message(message_id);

  if (success) {
    ESP_LOGI(TAG, "Persistent message %d deleted (marked inactive) successfully via HA service.", message_id);
    // Parent's delete_persistent_message should handle updating the queue size sensor
  } else {
    ESP_LOGE(TAG, "Failed to delete persistent message %d via HA service.", message_id);
  }
}

void B48HAIntegration::handle_wipe_database_service_() {
  ESP_LOGW(TAG, "Service wipe_database called. Wiping and reinitializing database...");

  // Call parent controller's new method
  bool success = parent_->wipe_and_reinitialize_database();

  if (success) {
    ESP_LOGW(TAG, "Database wipe and reinitialization successful via HA service.");
    // Parent's wipe method should handle updating the queue size sensor
  } else {
    ESP_LOGE(TAG, "Failed to wipe and reinitialize database via HA service.");
  }
}

void B48HAIntegration::handle_dump_database_service_() {
  ESP_LOGI(TAG, "Service dump_messages_for_diagnostics called. Dumping all database messages.");
  
  if (parent_) {
    parent_->dump_database_for_diagnostics();
  } else {
    ESP_LOGE(TAG, "Cannot dump database - parent controller not available.");
  }
}

// --- New Time Test Service Handlers ---

void B48HAIntegration::handle_start_time_test_service_() {
  ESP_LOGI(TAG, "Service start_time_test called. Starting time test mode...");
  
  if (parent_) {
    if (parent_->is_time_test_mode_active()) {
      ESP_LOGW(TAG, "Time test mode is already active");
      return;
    }
    parent_->start_time_test_mode();
    ESP_LOGI(TAG, "Time test mode started via HA service");
  } else {
    ESP_LOGE(TAG, "Cannot start time test mode - parent controller not available");
  }
}

void B48HAIntegration::handle_stop_time_test_service_() {
  ESP_LOGI(TAG, "Service stop_time_test called. Stopping time test mode...");
  
  if (parent_) {
    if (!parent_->is_time_test_mode_active()) {
      ESP_LOGW(TAG, "Time test mode is not active");
      return;
    }
    parent_->stop_time_test_mode();
    ESP_LOGI(TAG, "Time test mode stopped via HA service");
  } else {
    ESP_LOGE(TAG, "Cannot stop time test mode - parent controller not available");
  }
}

// --- Character Reverse Test Service Handlers ---

void B48HAIntegration::handle_start_character_reverse_test_service_() {
  ESP_LOGI(TAG, "Service start_character_reverse_test called. Starting character reverse test mode...");
  
  if (parent_) {
    if (parent_->is_character_reverse_test_mode_active()) {
      ESP_LOGW(TAG, "Character reverse test mode is already active");
      return;
    }
    parent_->start_character_reverse_test_mode();
    ESP_LOGI(TAG, "Character reverse test mode started via HA service");
  } else {
    ESP_LOGE(TAG, "Cannot start character reverse test mode - parent controller not available");
  }
}

void B48HAIntegration::handle_stop_character_reverse_test_service_() {
  ESP_LOGI(TAG, "Service stop_character_reverse_test called. Stopping character reverse test mode...");
  
  if (parent_) {
    if (!parent_->is_character_reverse_test_mode_active()) {
      ESP_LOGW(TAG, "Character reverse test mode is not active");
      return;
    }
    parent_->stop_character_reverse_test_mode();
    ESP_LOGI(TAG, "Character reverse test mode stopped via HA service");
  } else {
    ESP_LOGE(TAG, "Cannot stop character reverse test mode - parent controller not available");
  }
}

// --- Database Maintenance Service Handlers ---

void B48HAIntegration::handle_purge_disabled_messages_service_() {
  ESP_LOGI(TAG, "Service purge_disabled_messages called. Purging disabled messages from database...");
  
  if (parent_) {
    bool success = parent_->purge_disabled_messages();
    if (success) {
      ESP_LOGI(TAG, "Successfully purged disabled messages via HA service");
    } else {
      ESP_LOGE(TAG, "Failed to purge disabled messages via HA service");
    }
  } else {
    ESP_LOGE(TAG, "Cannot purge disabled messages - parent controller not available");
  }
}

// Add implementation of the filesystem stats service handler (after the purge handler):
void B48HAIntegration::handle_display_filesystem_stats_service_() {
  ESP_LOGI(TAG, "Service display_filesystem_stats called. Displaying filesystem stats...");
  
  if (parent_) {
    parent_->display_filesystem_stats();
    ESP_LOGI(TAG, "Filesystem stats displayed via HA service");
  } else {
    ESP_LOGE(TAG, "Cannot display filesystem stats - parent controller not available");
  }
}

// --- Raw Command and State Machine Service Handler Implementations ---
void B48HAIntegration::handle_send_raw_buse_command_service_(std::string payload) {
  ESP_LOGI(TAG, "Service send_raw_buse_command called with payload: %s", payload.c_str());
  if (parent_) {
    // The controller itself will check if it's safe to send raw commands (e.g., if paused)
    parent_->send_raw_buse_command(payload);
  } else {
    ESP_LOGE(TAG, "Cannot send raw BUSE command - parent controller not available.");
  }
}

void B48HAIntegration::handle_pause_state_machine_service_() {
  ESP_LOGI(TAG, "Service pause_display_state_machine called.");
  if (parent_) {
    parent_->pause_state_machine();
    ESP_LOGI(TAG, "Display state machine paused via HA service.");
  } else {
    ESP_LOGE(TAG, "Cannot pause state machine - parent controller not available.");
  }
}

void B48HAIntegration::handle_resume_state_machine_service_() {
  ESP_LOGI(TAG, "Service resume_display_state_machine called.");
  if (parent_) {
    parent_->resume_state_machine();
    ESP_LOGI(TAG, "Display state machine resumed via HA service.");
  } else {
    ESP_LOGE(TAG, "Cannot resume state machine - parent controller not available.");
  }
}

// --- Sensor Update Method ---

void B48HAIntegration::publish_queue_size(int size) {
  if (this->message_queue_size_sensor_ != nullptr) {
      // Check if the state actually changed before publishing
      if (!this->message_queue_size_sensor_->has_state() || this->message_queue_size_sensor_->get_state() != (float)size) {
          this->message_queue_size_sensor_->publish_state(size);
          ESP_LOGD(TAG, "Published message_queue_size: %d", size);
      } else {
          ESP_LOGV(TAG, "Skipping publish_queue_size: state %d hasn't changed.", size);
      }
  } else {
      ESP_LOGW(TAG, "Cannot publish queue size, sensor not configured.");
  }
}

} // namespace b48_display_controller
} // namespace esphome 