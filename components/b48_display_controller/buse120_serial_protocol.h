#pragma once

#include "esphome/components/uart/uart.h"
#include "character_mappings.h"
#include <string>
#include <cstdint>
#include <memory>

namespace esphome {
namespace b48_display_controller {

/**
 * @brief BUSE120 Serial Protocol implementation
 * 
 * This class handles the serial communication protocol for the BUSE120 display.
 * It implements the message format and checksum calculation as per the specification.
 */
class BUSE120SerialProtocol {
 public:
  BUSE120SerialProtocol() = default;
  
  /**
   * @brief Initialize the protocol with a UART component
   * @param uart The UART component to use for communication
   */
  void set_uart(uart::UARTComponent *uart) { this->uart_ = uart; }
  
  /**
   * @brief Send a command to the display
   * @param payload The command payload without terminator or checksum
   * @return true if successful, false otherwise
   */
  bool send_command(const std::string &payload);
  
  /**
   * @brief Send line number command
   * @param line The line number to set (0-999)
   */
  void send_line_number(int line);
  
  /**
   * @brief Send tarif zone command
   * @param zone The tarif zone to set
   */
  void send_tarif_zone(int zone);
  
  /**
   * @brief Send static intro text command
   * @param text The static intro text (will be truncated to 15 chars if longer)
   */
  void send_static_intro(const std::string &text);
  
  /**
   * @brief Send scrolling message command
   * @param text The scrolling message (will be truncated to 511 chars if longer)
   */
  void send_scrolling_message(const std::string &text);
  
  /**
   * @brief Send next message hint command
   * @param text The next message hint (will be truncated to 15 chars if longer)
   */
  void send_next_message_hint(const std::string &text);
  
  /**
   * @brief Send time update command (clock time format)
   * @param hour The hour (0-23)
   * @param minute The minute (0-59)
   */
  void send_time_update(int hour, int minute);
  
  /**
   * @brief Send cycle switch command
   * @param cycle The cycle number to switch to (typically 0-9)
   */
  void switch_to_cycle(int cycle);
  
  /**
   * @brief Send invert command to toggle display inversion
   */
  void send_invert_command();

  /**
   * @brief Send a raw payload string directly to the display.
   * The method will append the CR terminator and calculate/append the checksum.
   * @param raw_payload The raw command string to send.
   * @return true if successful, false otherwise.
   */
  bool send_raw_payload(const std::string &raw_payload);

  /**
   * @brief Convert Czech UTF-8 characters to display encoding (\x0e prefix format)
   * @param text The text to encode
   * @return The encoded text with Czech characters converted to display format
   */
  static std::string encode_czech_characters(const std::string &text);

  /**
   * @brief Safely truncate string without breaking multi-byte sequences
   * @param text The text to truncate
   * @param max_bytes Maximum number of bytes to keep
   * @return Safely truncated string
   */
  static std::string safe_truncate(const std::string &text, size_t max_bytes);
  
 private:
  /**
   * @brief Calculate the checksum for a payload
   * @param payload The payload to calculate checksum for
   * @return The calculated checksum byte
   */
  uint8_t calculate_checksum(const std::string &payload);
  
  // Member variables
  uart::UARTComponent *uart_{nullptr};
  static constexpr char CR = 0x0D;  // Carriage Return for BUSE120 protocol
};

}  // namespace b48_display_controller
}  // namespace esphome 