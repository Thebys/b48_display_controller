# B48 Display Controller - Home Assistant Integration Specification

This document outlines the integration points between the `b48_display_controller` component and Home Assistant via the ESPHome API. The goal is to provide robust control and monitoring capabilities with minimal need for custom lambda functions in the user's YAML configuration.

## Exposed Services

The following services will be exposed to Home Assistant, allowing for control over the display controller:

1.  **`b48_add_message`**
    *   **Description:** Adds a new **persistent** message to the display queue database (stored in FLASH).
    *   **Fields:**
        *   `priority` (integer, required): Scheduling priority (e.g., 0-100). Higher values have higher priority.
        *   `line_number` (integer, required): Target line number (0-999).
        *   `tarif_zone` (integer, required): Tariff zone (0-999).
        *   `scrolling_message` (string, required): The main scrolling text content.
        *   `static_intro` (string, optional): Static text displayed before scrolling message (max ~15 chars). Default: "".
        *   `next_message_hint` (string, optional): Short text displayed briefly during transitions. Default: "".
        *   `duration_seconds` (integer, optional): How long the message remains valid after creation, in seconds. Default: 0 (lives forever).
        *   `source_info` (string, optional): Optional text describing the source (e.g., "HA Automation"). Default: "HomeAssistant".
    *   **Action:** Inserts the message into the SQLite database (`messages` table).

2.  **`b48_delete_message`**
    *   **Description:** Deletes a specific message from the database using its unique ID.
    *   **Fields:**
        *   `message_id` (integer, required): The ID of the message to delete.
    *   **Action:** Removes the specified message from the database.

3.  **`b48_clear_all_messages`**
    *   **Description:** Removes all messages from the display queue database.
    *   **Fields:** None
    *   **Action:** Deletes all entries from the message table in the database.

4.  **`b48_display_ephemeral_message`**
    *   **Description:** Immediately displays a new message **without saving to FLASH** (stored in RAM only). Ideal for temporary notifications. Lost on reboot.
    *   **Fields:**
        *   `priority` (integer, required): Scheduling priority (e.g., 0-100). Higher values have higher priority.
        *   `line_number` (integer, required): Target line number (0-999).
        *   `tarif_zone` (integer, required): Tariff zone (0-999).
        *   `scrolling_message` (string, required): The main scrolling text content.
        *   `static_intro` (string, optional): Static text displayed before scrolling message (max ~15 chars). Default: "".
        *   `next_message_hint` (string, optional): Short text displayed briefly during transitions. Default: "".
        *   `display_count` (integer, optional): Number of times the message should be displayed before being removed. Default: 1. Use 0 for indefinite repeats until TTL expires.
        *   `ttl_seconds` (integer, optional): Time-to-live in seconds. How long the message stays in RAM before being automatically removed. Default: 300 (5 minutes). Use 0 for no time limit (relies on `display_count`).
    *   **Action:** Adds the message to the controller's in-memory ephemeral queue.

## Exposed Entities

The following entities will be created in Home Assistant to provide status information and control:


1.  **Sensor: `message_queue_size`**
    *   **Description:** Reports the total number of messages currently stored in the database.
    *   **State:** Integer.
    *   **Updates:** After messages are added or deleted.
 

## Implementation Notes

*   A new pair of files, `b48_ha_integration.h` and `b48_ha_integration.cpp`, will be created to encapsulate this integration logic.
*   The main `B48DisplayController` class will hold an instance of the integration class.
*   ESPHome's `custom_component.h` features (like `register_service` and `Component::make_sensor`) will be used.
*   Error handling and validation will be implemented for service calls.
*   Time parsing is **not** needed for adding messages via services, as `duration_seconds` is relative to the time of addition (handled by `datetime_added` in the DB or TTL logic in RAM). Time synchronization for the device clock itself is handled separately by the controller component.