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
  ESP_LOGV(TAG, "Sending command: %s", payload.c_str());
  std::string debug_bytes;
  for (char c : payload) {
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X ", static_cast<uint8_t>(c));
    debug_bytes += hex;
  }
  ESP_LOGV(TAG, "Bytes: %s", debug_bytes.c_str());

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

std::string BUSE120SerialProtocol::encode_czech_characters(const std::string &text) {
  // Use the new character mapping manager for encoding
  return CharacterMappingManager::get_instance().encode_for_display(text);
}

std::string BUSE120SerialProtocol::safe_truncate(const std::string &text, size_t max_bytes) {
  if (text.length() <= max_bytes) {
    return text;
  }
  
  // Find safe truncation point that doesn't break \x0e sequences
  for (size_t i = max_bytes; i > 0; --i) {
    // Check if we're about to break a \x0e sequence
    if (i > 0 && static_cast<unsigned char>(text[i - 1]) == 0x0e) {
      // We're at the second byte of \x0e sequence - move back to before \x0e
      return text.substr(0, i - 1);
    }
    // If we're at a safe boundary, truncate here
    unsigned char c = static_cast<unsigned char>(text[i]);
    if (c < 0x80 || c == 0x0e) {  // ASCII or start of display sequence
      return text.substr(0, i);
    }
  }
  
  // Fallback - return empty string if we can't find a safe point
  return "";
}

void BUSE120SerialProtocol::send_invert_command() {
  char payload[2];
  snprintf(payload, sizeof(payload), "i");  // not tested to be working. Also try b for blinking.
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
  // Convert Czech characters to display encoding first
  std::string encoded = encode_czech_characters(text);
  // Safely truncate to 15 bytes without breaking multi-byte sequences
  std::string truncated = safe_truncate(encoded, 15);
  std::string payload = "zI " + truncated;
  send_command(payload);
}

void BUSE120SerialProtocol::send_scrolling_message(const std::string &text) {
  // Convert Czech characters to display encoding first
  std::string encoded = encode_czech_characters(text);
  // Safely truncate to 511 bytes without breaking multi-byte sequences
  std::string truncated = safe_truncate(encoded, 511);
  std::string payload = "zM " + truncated;
  send_command(payload);
}

void BUSE120SerialProtocol::send_next_message_hint(const std::string &text) {
  // Convert Czech characters to display encoding first
  std::string encoded = encode_czech_characters(text);
  // Safely truncate to 15 bytes without breaking multi-byte sequences
  std::string truncated = safe_truncate(encoded, 15);
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

bool BUSE120SerialProtocol::send_raw_payload(const std::string &raw_payload) {
  if (!this->uart_) {
    ESP_LOGE(TAG, "UART not initialized for raw payload");
    return false;
  }

  uint8_t checksum = calculate_checksum(raw_payload);

  ESP_LOGV(TAG, "Sending raw payload: \"%s\"", raw_payload.c_str());
  std::string debug_bytes;
  for (char c : raw_payload) {
    char hex[4];
    snprintf(hex, sizeof(hex), "%02X ", static_cast<uint8_t>(c));
    debug_bytes += hex;
  }
  ESP_LOGV(TAG, "Raw payload bytes: %s", debug_bytes.c_str());

  // Send payload
  this->uart_->write_array(reinterpret_cast<const uint8_t *>(raw_payload.c_str()), raw_payload.length());

  // Send terminator (CR)
  this->uart_->write_byte(CR);

  // Send checksum
  this->uart_->write_byte(checksum);

  return true;
}

}  // namespace b48_display_controller
}  // namespace esphome