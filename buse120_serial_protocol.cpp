#include "buse120_serial_protocol.h"
#include "esphome/core/log.h"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "buse120";

bool BUSE120SerialProtocol::send_command(const std::string &payload) {
  if (!this->uart_) {
    ESP_LOGE(TAG, "UART not initialized");
    return false;
  }

  uint8_t checksum = calculate_checksum(payload);

  // Log the command and bytes for debugging
  ESP_LOGD(TAG, "Sending command: %s", payload.c_str());
  std::string debug_bytes;
  for (char c : payload) {
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X ", static_cast<uint8_t>(c));
    debug_bytes += hex;
  }
  //ESP_LOGD(TAG, "Command bytes: %s", debug_bytes.c_str());
  //ESP_LOGD(TAG, "Terminator: 0D, Checksum: %02X", checksum);

  // Send payload
  this->uart_->write_array(reinterpret_cast<const uint8_t *>(payload.c_str()), payload.length());

  // Send terminator (CR)
  this->uart_->write_byte(CR);

  // Send checksum
  this->uart_->write_byte(checksum);
  
  return true;
}

uint8_t BUSE120SerialProtocol::calculate_checksum(const std::string &payload) {
  uint8_t checksum = 0x7F;

  // XOR with each byte in the payload
  for (char c : payload) {
    checksum ^= static_cast<uint8_t>(c);
  }

  // XOR with the terminator (CR)
  checksum ^= CR;

  return checksum;
}
void BUSE120SerialProtocol::send_invert_command() {
  char payload[2];
  snprintf(payload, sizeof(payload), "i"); //not tested to be working. Also try b for blinking.
  send_command(payload);
}
void BUSE120SerialProtocol::send_line_number(int line) {
  char payload[5];
  snprintf(payload, sizeof(payload), "l%03d", line);
  send_command(payload);
}

void BUSE120SerialProtocol::send_tarif_zone(int zone) {
  char payload[9];
  snprintf(payload, sizeof(payload), "e%03d000", zone);
  send_command(payload);
}

void BUSE120SerialProtocol::send_static_intro(const std::string &text) {
  // Limit to 15 characters as per spec
  std::string truncated = text.substr(0, 15);
  std::string payload = "zI " + truncated;
  send_command(payload);
}

void BUSE120SerialProtocol::send_scrolling_message(const std::string &text) {
  // Limit to 511 characters as per spec
  std::string truncated = text.substr(0, 511);
  std::string payload = "zM " + truncated;
  send_command(payload);
}

void BUSE120SerialProtocol::send_next_message_hint(const std::string &text) {
  // Limit to 15 characters as per spec
  std::string truncated = text.substr(0, 15);
  std::string payload = "v " + truncated;
  send_command(payload);
}

void BUSE120SerialProtocol::send_time_update(int hour, int minute) {
  char payload[6];
  snprintf(payload, sizeof(payload), "u%02d%02d", hour, minute);
  send_command(payload);
}

void BUSE120SerialProtocol::switch_to_cycle(int cycle) {
  char payload[4];
  snprintf(payload, sizeof(payload), "xC%d", cycle);
  send_command(payload);
}

}  // namespace b48_display_controller
}  // namespace esphome 