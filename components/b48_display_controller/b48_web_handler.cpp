#include "b48_web_handler.h"
#ifdef USE_B48_WEB_UI

#include "b48_display_controller.h"
#include "b48_database_manager.h"
#include "b48_web_index.h"
#include "esphome/core/log.h"
#include "esphome/core/application.h"

#include <cstring>
#include <mutex>

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.web";

// API base path
static const char *const API_BASE = "/b48/api/v1";

void B48WebHandler::setup() {
  ESP_LOGCONFIG(TAG, "Setting up B48 Web Handler...");
  this->base_->init();
  this->base_->add_handler(this);
  ESP_LOGI(TAG, "B48 Web Handler registered at /b48/");
}

float B48WebHandler::get_setup_priority() const {
  // After WiFi, similar to prometheus handler
  return setup_priority::WIFI - 1.0f;
}

bool B48WebHandler::canHandle(AsyncWebServerRequest *request) const {
  // Handle all requests starting with /b48/
  std::string url = request->url().c_str();
  return url.rfind("/b48/", 0) == 0 || url == "/b48";
}

void B48WebHandler::handleRequest(AsyncWebServerRequest *request) {
  std::string url = request->url().c_str();
  int method = request->method();

  ESP_LOGD(TAG, "Request: method=%d url=%s", method, url.c_str());

  // Admin UI (HTML)
  if ((url == "/b48" || url == "/b48/") && method == HTTP_GET) {
    this->handle_index(request);
    return;
  }

  // API: Status endpoint
  if (url == std::string(API_BASE) + "/status" && method == HTTP_GET) {
    this->handle_api_status(request);
    return;
  }

  // API: Refresh cache endpoint (POST)
  if ((url == std::string(API_BASE) + "/refresh" || url == std::string(API_BASE) + "/refresh/") && method == HTTP_POST) {
    this->handle_api_refresh(request);
    return;
  }

  // API: Restart device endpoint (POST)
  if ((url == std::string(API_BASE) + "/restart" || url == std::string(API_BASE) + "/restart/") && method == HTTP_POST) {
    this->handle_api_restart(request);
    return;
  }

  // API: Messages collection endpoints
  // Note: ESP-IDF web server only supports GET/POST/OPTIONS, so we use POST for all modifications:
  // - POST /messages = create new message
  // - POST /messages/{id} = update message
  // - POST /messages/{id}/delete = delete message
  std::string messages_base = std::string(API_BASE) + "/messages";

  if (url == messages_base || url == messages_base + "/") {
    if (method == HTTP_GET) {
      this->handle_api_messages_list(request);
      return;
    } else if (method == HTTP_POST) {
      this->handle_api_messages_create(request);
      return;
    } else {
      this->send_json_error(request, 405, "Method not allowed");
      return;
    }
  }

  // API: Single message endpoints (/b48/api/v1/messages/{id} or /messages/{id}/delete)
  if (url.rfind(messages_base + "/", 0) == 0 && url.length() > messages_base.length() + 1) {
    // Check for /messages/{id}/delete pattern
    std::string suffix = url.substr(messages_base.length() + 1);
    size_t slash_pos = suffix.find('/');

    if (slash_pos != std::string::npos) {
      // Has another slash - check for /delete suffix
      std::string id_str = suffix.substr(0, slash_pos);
      std::string action = suffix.substr(slash_pos + 1);

      // Remove trailing slash from action if present
      if (!action.empty() && action.back() == '/') {
        action.pop_back();
      }

      int message_id = atoi(id_str.c_str());
      if (message_id <= 0) {
        this->send_json_error(request, 400, "Invalid message ID");
        return;
      }

      if (action == "delete" && method == HTTP_POST) {
        this->handle_api_messages_delete(request, message_id);
        return;
      }

      this->send_json_error(request, 404, "Unknown action");
      return;
    }

    // Simple /messages/{id} pattern
    int message_id = this->extract_message_id_from_url(url);
    if (message_id < 0) {
      this->send_json_error(request, 400, "Invalid message ID");
      return;
    }

    if (method == HTTP_GET) {
      this->handle_api_messages_get(request, message_id);
      return;
    } else if (method == HTTP_POST) {
      // POST to /messages/{id} = update
      this->handle_api_messages_update(request, message_id);
      return;
    } else {
      this->send_json_error(request, 405, "Method not allowed");
      return;
    }
  }

  // 404 for unrecognized paths
  this->send_json_error(request, 404, "Not found");
}

void B48WebHandler::handle_index(AsyncWebServerRequest *request) {
  // Serve gzipped HTML from embedded data
  // Use beginResponse (not beginResponse_P which is Arduino-specific)
  AsyncWebServerResponse *response =
      request->beginResponse(200, "text/html", B48_WEB_INDEX_HTML, B48_WEB_INDEX_HTML_SIZE);
  response->addHeader("Content-Encoding", "gzip");
  response->addHeader("Cache-Control", "max-age=3600");
  request->send(response);
}

void B48WebHandler::handle_api_status(AsyncWebServerRequest *request) {
  if (this->controller_ == nullptr) {
    this->send_json_error(request, 500, "Controller not initialized");
    return;
  }

  std::lock_guard<std::mutex> lock(this->controller_->get_message_mutex());
  auto *db = this->controller_->get_database_manager();
  if (db == nullptr) {
    this->send_json_error(request, 500, "Database not initialized");
    return;
  }

  int count = db->get_message_count();
  uint32_t uptime = millis() / 1000;

  AsyncResponseStream *stream = request->beginResponseStream("application/json");
  stream->print("{\"message_count\":");
  stream->print(count);
  stream->print(",\"uptime_seconds\":");
  stream->print(uptime);
  stream->print(",\"device_name\":\"");
  stream->print(App.get_name().c_str());
  stream->print("\"}");
  request->send(stream);
}

void B48WebHandler::handle_api_messages_list(AsyncWebServerRequest *request) {
  if (this->controller_ == nullptr) {
    this->send_json_error(request, 500, "Controller not initialized");
    return;
  }

  std::lock_guard<std::mutex> lock(this->controller_->get_message_mutex());
  auto *db = this->controller_->get_database_manager();
  if (db == nullptr) {
    this->send_json_error(request, 500, "Database not initialized");
    return;
  }

  auto messages = db->get_active_persistent_messages();

  AsyncResponseStream *stream = request->beginResponseStream("application/json");
  stream->print("[");

  bool first = true;
  for (const auto &msg : messages) {
    if (!first)
      stream->print(",");
    first = false;
    write_message_json(stream, msg);
  }

  stream->print("]");
  request->send(stream);
}

void B48WebHandler::handle_api_messages_get(AsyncWebServerRequest *request, int message_id) {
  if (this->controller_ == nullptr) {
    this->send_json_error(request, 500, "Controller not initialized");
    return;
  }

  std::lock_guard<std::mutex> lock(this->controller_->get_message_mutex());
  auto *db = this->controller_->get_database_manager();
  if (db == nullptr) {
    this->send_json_error(request, 500, "Database not initialized");
    return;
  }

  // Get all messages and find the one with matching ID
  auto messages = db->get_active_persistent_messages();
  for (const auto &msg : messages) {
    if (msg->message_id == message_id) {
      AsyncResponseStream *stream = request->beginResponseStream("application/json");
      write_message_json(stream, msg);
      request->send(stream);
      return;
    }
  }

  this->send_json_error(request, 404, "Message not found");
}

void B48WebHandler::handle_api_messages_create(AsyncWebServerRequest *request) {
  if (this->controller_ == nullptr) {
    this->send_json_error(request, 500, "Controller not initialized");
    return;
  }

  // Parse URL-encoded form data using request->arg()
  int priority = 50;
  int line_number = 0;
  int tarif_zone = 0;
  int duration_seconds = 0;
  std::string static_intro;
  std::string scrolling_message;
  std::string next_message_hint;

  if (request->hasParam("priority")) {
    priority = atoi(request->arg("priority").c_str());
  }
  if (request->hasParam("line_number")) {
    line_number = atoi(request->arg("line_number").c_str());
  }
  if (request->hasParam("tarif_zone")) {
    tarif_zone = atoi(request->arg("tarif_zone").c_str());
  }
  if (request->hasParam("duration_seconds")) {
    duration_seconds = atoi(request->arg("duration_seconds").c_str());
  }
  if (request->hasParam("static_intro")) {
    static_intro = request->arg("static_intro");
  }
  if (request->hasParam("scrolling_message")) {
    scrolling_message = request->arg("scrolling_message");
  }
  if (request->hasParam("next_message_hint")) {
    next_message_hint = request->arg("next_message_hint");
  }

  ESP_LOGD(TAG, "Create message: priority=%d, line=%d, msg='%s'", priority, line_number, scrolling_message.c_str());

  if (scrolling_message.empty()) {
    this->send_json_error(request, 400, "scrolling_message is required");
    return;
  }

  // Schedule message creation in main loop (avoids httpd stack overflow)
  ESP_LOGD(TAG, "Scheduling add_message for main loop...");
  this->controller_->schedule_add_message(priority, line_number, tarif_zone, static_intro, scrolling_message,
                                          next_message_hint, duration_seconds);

  // Return success immediately - actual work happens in main loop
  this->send_json_success(request, "Message scheduled for creation");
}

void B48WebHandler::handle_api_messages_update(AsyncWebServerRequest *request, int message_id) {
  if (this->controller_ == nullptr) {
    this->send_json_error(request, 500, "Controller not initialized");
    return;
  }

  // Parse form data - form always sends all fields
  int priority = 50;
  int line_number = 0;
  int tarif_zone = 0;
  int duration_seconds = 0;
  std::string static_intro;
  std::string scrolling_message;
  std::string next_message_hint;

  if (request->hasParam("priority")) {
    priority = atoi(request->arg("priority").c_str());
  }
  if (request->hasParam("line_number")) {
    line_number = atoi(request->arg("line_number").c_str());
  }
  if (request->hasParam("tarif_zone")) {
    tarif_zone = atoi(request->arg("tarif_zone").c_str());
  }
  if (request->hasParam("duration_seconds")) {
    duration_seconds = atoi(request->arg("duration_seconds").c_str());
  }
  if (request->hasParam("static_intro")) {
    static_intro = request->arg("static_intro");
  }
  if (request->hasParam("scrolling_message")) {
    scrolling_message = request->arg("scrolling_message");
  }
  if (request->hasParam("next_message_hint")) {
    next_message_hint = request->arg("next_message_hint");
  }

  ESP_LOGD(TAG, "Scheduling update for message %d: priority=%d, line=%d", message_id, priority, line_number);

  // Schedule update in main loop (avoids httpd stack overflow)
  this->controller_->schedule_update_message(message_id, priority, line_number, tarif_zone, static_intro,
                                             scrolling_message, next_message_hint, duration_seconds);

  this->send_json_success(request, "Message update scheduled");
}

void B48WebHandler::handle_api_messages_delete(AsyncWebServerRequest *request, int message_id) {
  if (this->controller_ == nullptr) {
    this->send_json_error(request, 500, "Controller not initialized");
    return;
  }

  ESP_LOGD(TAG, "Scheduling delete for message %d", message_id);

  // Schedule delete in main loop (avoids httpd stack overflow)
  this->controller_->schedule_delete_message(message_id);

  this->send_json_success(request, "Message deletion scheduled");
}

void B48WebHandler::handle_api_refresh(AsyncWebServerRequest *request) {
  if (this->controller_ == nullptr) {
    this->send_json_error(request, 500, "Controller not initialized");
    return;
  }

  ESP_LOGI(TAG, "Manual cache refresh triggered via API");
  this->controller_->trigger_cache_refresh();
  this->send_json_success(request, "Cache refresh triggered");
}

void B48WebHandler::handle_api_restart(AsyncWebServerRequest *request) {
  ESP_LOGW(TAG, "Device restart requested via API");
  this->send_json_success(request, "Device restarting...");

  // Schedule restart after response is sent
  App.schedule_dump_config();
  delay(100);
  App.safe_reboot();
}

void B48WebHandler::write_json_escaped(AsyncResponseStream *stream, const std::string &str) {
  for (char c : str) {
    if (c == '"')
      stream->print("\\\"");
    else if (c == '\\')
      stream->print("\\\\");
    else if (c == '\n')
      stream->print("\\n");
    else if (c == '\r')
      stream->print("\\r");
    else
      stream->printf("%c", c);
  }
}

void B48WebHandler::write_message_json(AsyncResponseStream *stream, const std::shared_ptr<MessageEntry> &msg) {
  stream->print("{\"id\":");
  stream->print(msg->message_id);
  stream->print(",\"priority\":");
  stream->print(msg->priority);
  stream->print(",\"is_enabled\":true");
  stream->print(",\"line_number\":");
  stream->print(msg->line_number);
  stream->print(",\"tarif_zone\":");
  stream->print(msg->tarif_zone);
  stream->print(",\"static_intro\":\"");
  write_json_escaped(stream, msg->static_intro);
  stream->print("\",\"scrolling_message\":\"");
  write_json_escaped(stream, msg->scrolling_message);
  stream->print("\",\"next_message_hint\":\"");
  write_json_escaped(stream, msg->next_message_hint);
  stream->print("\"}");
}

void B48WebHandler::send_json_error(AsyncWebServerRequest *request, int code, const char *message) {
  AsyncResponseStream *stream = request->beginResponseStream("application/json");
  stream->print("{\"error\":\"");
  stream->print(message);
  stream->print("\"}");
  request->send(stream);
}

void B48WebHandler::send_json_success(AsyncWebServerRequest *request, const char *message) {
  AsyncResponseStream *stream = request->beginResponseStream("application/json");
  stream->print("{\"success\":true,\"message\":\"");
  stream->print(message);
  stream->print("\"}");
  request->send(stream);
}

int B48WebHandler::extract_message_id_from_url(const std::string &url) {
  // URL format: /b48/api/v1/messages/{id}
  std::string messages_prefix = std::string(API_BASE) + "/messages/";
  if (url.rfind(messages_prefix, 0) != 0) {
    return -1;
  }

  std::string id_str = url.substr(messages_prefix.length());
  // Remove trailing slash if present
  if (!id_str.empty() && id_str.back() == '/') {
    id_str.pop_back();
  }

  if (id_str.empty()) {
    return -1;
  }

  // Validate that it's a number
  for (char c : id_str) {
    if (!isdigit(c)) {
      return -1;
    }
  }

  return atoi(id_str.c_str());
}

}  // namespace b48_display_controller
}  // namespace esphome

#endif  // USE_B48_WEB_UI
