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
  std::string result;
  result.reserve(text.length() * 2);  // Reserve extra space for potential \x0e expansions

  for (size_t i = 0; i < text.length(); ++i) {
    unsigned char c1 = static_cast<unsigned char>(text[i]);

    if (c1 <= 0x7F) {
      // Standard ASCII character - keep as is
      result += c1;
    } else if (c1 >= 0xC2 && c1 <= 0xDF && i + 1 < text.length()) {
      // 2-byte UTF-8 sequence
      unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
      if ((c2 & 0xC0) == 0x80) {  // Valid continuation byte
        uint16_t utf8_val = (c1 << 8) | c2;
        bool converted = false;

        // Convert Czech characters to display encoding
        switch (utf8_val) {
          case 0xC3A1:  // á
            result += "\x0e\x20";
            converted = true;
            break;
          case 0xC3AD:  // í
            result += "\x0e\x21";
            converted = true;
            break;
          case 0xC3B3:  // ó
            result += "\x0e\x22";
            converted = true;
            break;
          case 0xC3BA:  // ú
            result += "\x0e\x23";
            converted = true;
            break;
          case 0xC588:  // ň
            result += "\x0e\x24";
            converted = true;
            break;
          case 0xC583:  // Ń (Polish, but in the mapping)
            result += "\x0e\x25";
            converted = true;
            break;
          case 0xC5AE:  // Ů
            result += "\x0e\x26";
            converted = true;
            break;
          case 0xC5AF:  // ů
            result += "\x0e\x27";
            converted = true;
            break;
          case 0xC5A1:  // š
            result += "\x0e\x28";
            converted = true;
            break;
          case 0xC599:  // ř
            result += "\x0e\x29";
            converted = true;
            break;
          case 0xC381:  // Á
            result += "\x0e\x80";
            converted = true;
            break;
          case 0xC3A9:  // é
            result += "\x0e\x82";
            converted = true;
            break;
          case 0xC48F:  // ď
            result += "\x0e\x83";
            converted = true;
            break;
          case 0xC48E:  // Ď
            result += "\x0e\x85";
            converted = true;
            break;
          case 0xC5A4:  // Ť
            result += "\x0e\x86";
            converted = true;
            break;
          case 0xC48D:  // č
            result += "\x0e\x87";
            converted = true;
            break;
          case 0xC49B:  // ě
            result += "\x0e\x88";
            converted = true;
            break;
          case 0xC49A:  // Ě
            result += "\x0e\x89";
            converted = true;
            break;
          case 0xC389:  // É
            result += "\x0e\x90";
            converted = true;
            break;
          case 0xC5BE:  // ž
            result += "\x0e\x91";
            converted = true;
            break;
          case 0xC5BD:  // Ž
            result += "\x0e\x92";
            converted = true;
            break;
          case 0xC393:  // Ó
            result += "\x0e\x95";
            converted = true;
            break;
          case 0xC39A:  // Ú
            result += "\x0e\x97";
            converted = true;
            break;
          case 0xC3BD:  // ý
            result += "\x0e\x98";
            converted = true;
            break;
          case 0xC48C:  // Č
            result += "\x0e\x80";
            converted = true;
            break;
          case 0xC38D:  // Í
            result += "\x0e\x21";
            converted = true;
            break;
          case 0xC39D:  // Ý
            result += "\x0e\x98";
            converted = true;
            break;
          case 0xC598:  // Ř
            result += "\x0e\x29";
            converted = true;
            break;
          case 0xC5A0:  // Š
            result += "\x0e\x28";
            converted = true;
            break;
          case 0xC5A5:  // ť
            result += "\x0e\x86";
            converted = true;
            break;
          case 0xC587:  // Ň
            result += "\x0e\x24";
            converted = true;
            break;
          default:
            // Not a Czech character, keep original UTF-8 bytes
            result += c1;
            result += c2;
            converted = true;
            break;
        }

        if (converted) {
          i++;  // Skip the next byte since we processed it
        }
      } else {
        // Invalid UTF-8, keep as is
        result += c1;
      }
    } else {
      // Other non-ASCII characters (3-byte, 4-byte UTF-8 or invalid), keep as is
      result += c1;
    }
  }

  return result;
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
  // Limit to 15 characters after encoding (note: Czech chars become 2 bytes each)
  std::string truncated = encoded.substr(0, 15);
  std::string payload = "zI " + truncated;
  send_command(payload);
}

void BUSE120SerialProtocol::send_scrolling_message(const std::string &text) {
  // Convert Czech characters to display encoding first
  std::string encoded = encode_czech_characters(text);
  // Limit to 511 characters after encoding (note: Czech chars become 2 bytes each)
  std::string truncated = encoded.substr(0, 511);
  std::string payload = "zM " + truncated;
  send_command(payload);
}

void BUSE120SerialProtocol::send_next_message_hint(const std::string &text) {
  // Convert Czech characters to display encoding first
  std::string encoded = encode_czech_characters(text);
  // Limit to 15 characters after encoding (note: Czech chars become 2 bytes each)
  std::string truncated = encoded.substr(0, 15);
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