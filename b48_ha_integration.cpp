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

void B48HAIntegration::dump_config() {
  ESP_LOGCONFIG(TAG, "B48 Home Assistant Integration:");
  LOG_SENSOR("  ", "Message Queue Size Sensor", this->message_queue_size_sensor_);
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