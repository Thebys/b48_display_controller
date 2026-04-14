#pragma once

#include "esphome/core/defines.h"
#ifdef USE_B48_WEB_UI

#include "esphome/core/component.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <memory>

namespace esphome {
namespace b48_display_controller {

// Forward declarations
class B48DisplayController;

/**
 * @brief Web handler for B48 Display Controller REST API and admin UI.
 *
 * Provides:
 * - REST API for CRUD operations on messages at /b48/api/v1/
 * - HTML admin UI at /b48/
 * - Thread-safe database access via controller mutex
 *
 * Integrates with ESPHome's web_server_base for shared port and authentication.
 */
class B48WebHandler : public AsyncWebHandler, public Component {
 public:
  B48WebHandler(web_server_base::WebServerBase *base, B48DisplayController *controller)
      : base_(base), controller_(controller) {}

  // Component interface
  void setup() override;
  float get_setup_priority() const override;

  // AsyncWebHandler interface
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  // API Handlers
  void handle_api_messages_list(AsyncWebServerRequest *request);
  void handle_api_messages_get(AsyncWebServerRequest *request, int message_id);
  void handle_api_messages_create(AsyncWebServerRequest *request);
  void handle_api_messages_update(AsyncWebServerRequest *request, int message_id);
  void handle_api_messages_delete(AsyncWebServerRequest *request, int message_id);
  void handle_api_status(AsyncWebServerRequest *request);
  void handle_api_refresh(AsyncWebServerRequest *request);
  void handle_api_restart(AsyncWebServerRequest *request);

  // UI Handler
  void handle_index(AsyncWebServerRequest *request);

  // Helper methods
  void send_json_error(AsyncWebServerRequest *request, int code, const char *message);
  void send_json_success(AsyncWebServerRequest *request, const char *message);
  static void write_json_escaped(AsyncResponseStream *stream, const std::string &str);
  void write_message_json(AsyncResponseStream *stream, const std::shared_ptr<struct MessageEntry> &msg);
  int extract_message_id_from_url(const std::string &url);

  web_server_base::WebServerBase *base_;
  B48DisplayController *controller_;
};

}  // namespace b48_display_controller
}  // namespace esphome

#endif  // USE_B48_WEB_UI
