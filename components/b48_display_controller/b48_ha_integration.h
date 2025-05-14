#pragma once

#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/api/custom_api_device.h"
#include <string> // Include string header
#include <vector> // Include vector header

// Forward declaration
namespace esphome {
namespace b48_display_controller {

class B48DisplayController;

class B48HAIntegration : public Component, public api::CustomAPIDevice {
 public:
  // Default constructor
  B48HAIntegration() = default;

  // Constructor with parent
  explicit B48HAIntegration(B48DisplayController *parent) : parent_(parent) {}

  // Setter for parent
  void set_parent(B48DisplayController *parent) { this->parent_ = parent; }

  void setup() override;
  float get_setup_priority() const override { return esphome::setup_priority::AFTER_CONNECTION; } // Needs API connection

  // --- Setters for Entities (called from parent) ---
  void set_message_queue_size_sensor(sensor::Sensor *sensor) { this->message_queue_size_sensor_ = sensor; }

  // --- Update methods for Entities (called from parent) ---
  void publish_queue_size(int size);

 protected:
  // --- Service Registration Method ---
  void register_services_();

  // --- Service Handler Methods ---
  void handle_delete_message_service_(int message_id);

  void handle_wipe_database_service_();

  void handle_dump_database_service_();
  
  // New time test mode service handlers
  void handle_start_time_test_service_();
  
  void handle_stop_time_test_service_();
  
  // Character reverse test mode service handlers
  void handle_start_character_reverse_test_service_();
  
  void handle_stop_character_reverse_test_service_();
  
  // Database maintenance service handlers
  void handle_purge_disabled_messages_service_();
  
  // Filesystem stats service handler
  void handle_display_filesystem_stats_service_();

  // --- Raw Command and State Machine Service Handlers ---
  void handle_send_raw_buse_command_service_(std::string payload);
  void handle_pause_state_machine_service_();
  void handle_resume_state_machine_service_();

  // --- Member Variables ---
  B48DisplayController *parent_; // Pointer to the main controller component

  // Exposed Entities
  sensor::Sensor *message_queue_size_sensor_{nullptr};
};

} // namespace b48_display_controller
} // namespace esphome 