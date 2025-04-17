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

  // Register service for adding persistent messages
  register_service(&B48HAIntegration::handle_add_message_service_, "add_persistent_message",
                 {"priority", "line_number", "tarif_zone", "static_intro", "scrolling_message", 
                  "next_message_hint", "duration_seconds", "source_info"});

  // Register service for deleting persistent messages
  register_service(&B48HAIntegration::handle_delete_message_service_, "delete_persistent_message",
                 {"message_id"});

  // Register service for clearing all persistent messages
  register_service(&B48HAIntegration::handle_clear_all_messages_service_, "clear_all_persistent_messages");

  // Register service for adding ephemeral messages
  register_service(&B48HAIntegration::handle_display_ephemeral_message_service_, "b48_display_ephemeral_message",
                   {"priority", "line_number", "tarif_zone", "scrolling_message",
                    "static_intro", "next_message_hint", "display_count", "ttl_seconds"});

  ESP_LOGD(TAG, "Service registration complete.");
}

// --- Service Handlers ---

void B48HAIntegration::handle_add_message_service_(int priority, int line_number, int tarif_zone,
                                                   std::string scrolling_message, std::string static_intro,
                                                   std::string next_message_hint, int duration_seconds, std::string source_info) {
  ESP_LOGD(TAG, "Service b48_add_message called: priority=%d, line=%d, zone=%d, msg='%s'",
           priority, line_number, tarif_zone, scrolling_message.substr(0, 20).c_str()); // Log truncated message

  // Basic validation (can add more specific checks)
  if (scrolling_message.empty()) {
    ESP_LOGW(TAG, "Add message failed: scrolling_message cannot be empty.");
    return;
  }
  if (priority < 0 || priority > 100) {
    ESP_LOGW(TAG, "Add message failed: Priority (%d) must be between 0 and 100.", priority);
    return;
  }
  // Add validation for line_number, tarif_zone if needed based on display constraints

  // Use default source_info if empty
  if (source_info.empty()) {
      source_info = "HomeAssistant";
  }

  // Call parent controller's method to handle DB interaction
  bool success = parent_->add_persistent_message(priority, line_number, tarif_zone,
                                                  static_intro, scrolling_message, next_message_hint,
                                                  duration_seconds, source_info);

  if (success) {
    ESP_LOGI(TAG, "Persistent message added successfully via HA service.");
    // Parent's add_persistent_message should handle updating the queue size sensor
  } else {
    ESP_LOGE(TAG, "Failed to add persistent message via HA service.");
  }
}

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

void B48HAIntegration::handle_clear_all_messages_service_() {
  ESP_LOGD(TAG, "Service b48_clear_all_messages called.");

  // Call parent controller's method (assuming it exists)
  bool success = parent_->clear_all_persistent_messages();

  if (success) {
    ESP_LOGI(TAG, "All persistent messages cleared successfully via HA service.");
    // Parent's clear_all_persistent_messages should handle updating the queue size sensor
  } else {
    ESP_LOGE(TAG, "Failed to clear all persistent messages via HA service.");
  }
}

void B48HAIntegration::handle_display_ephemeral_message_service_(int priority, int line_number, int tarif_zone,
                                                                 std::string scrolling_message, std::string static_intro,
                                                                 std::string next_message_hint, int display_count, int ttl_seconds) {
  ESP_LOGD(TAG, "Service b48_display_ephemeral_message called: priority=%d, line=%d, zone=%d, msg='%s'",
           priority, line_number, tarif_zone, scrolling_message.substr(0, 20).c_str()); // Log truncated message

  // Basic validation
  if (scrolling_message.empty()) {
    ESP_LOGW(TAG, "Display ephemeral message failed: scrolling_message cannot be empty.");
    return;
  }
   if (priority < 0 || priority > 100) {
    ESP_LOGW(TAG, "Display ephemeral message failed: Priority (%d) must be between 0 and 100.", priority);
    return;
  }
  if (display_count < 0) {
     ESP_LOGW(TAG, "Display ephemeral message failed: display_count cannot be negative.");
     return;
  }
   if (ttl_seconds < 0) {
     ESP_LOGW(TAG, "Display ephemeral message failed: ttl_seconds cannot be negative.");
     return;
  }

  // Call parent controller's method
  bool success = parent_->add_ephemeral_message(priority, line_number, tarif_zone,
                                                  static_intro, scrolling_message, next_message_hint,
                                                  display_count, ttl_seconds);

  if (success) {
    ESP_LOGI(TAG, "Ephemeral message added successfully via HA service.");
  } else {
    ESP_LOGE(TAG, "Failed to add ephemeral message via HA service.");
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