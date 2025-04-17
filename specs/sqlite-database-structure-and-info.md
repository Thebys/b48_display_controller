---
description: 
globs: 
alwaysApply: true
---
# SQLite Database Schema Specification
**File:** `database.schema.md`
**Date:** April 13, 2025
**Version:** 1.4
## Overview
This document specifies the SQLite database schema for storing **persistent** messages used by the `message_display_controller` external component. The database (`messages.db`) resides on the LittleFS partition. **Ephemeral (temporary, non-persistent) messages bypass this database entirely and are handled in RAM.** The schema supports a priority-based display scheduling algorithm implemented in the C++ component, which operates primarily on a RAM cache populated from this database.
## Database File
*   **Name:** `messages.db`
*   **Location:** `/littlefs/messages.db` (Assuming LittleFS is mounted at `/littlefs`).
## Core Principles
*   **Persistence:** Store messages intended to survive reboots.
*   **Priority-Driven:** Support a scheduling algorithm based on message priority.
*   **Flash Wear Mitigation:** Minimize writes by handling ephemeral messages in RAM and using logical deletion for persistent messages. Updates to priority should be infrequent.
*   **RAM Cache Centric:** The primary display logic operates on a cache of persistent messages held in RAM to maximize responsiveness and minimize DB reads.
## Design Choices Rationale
*   **No `order_index`:** Display sequence is determined by `priority` and the C++ scheduler. Within the same priority, `message_id` provides stable insertion order.
*   **Ephemeral Messages:** Handled entirely in RAM to avoid flash wear for temporary notifications.
## Tables
### `messages` Table (Stores Persistent Messages Only)
| Column Name         | Data Type                 | Constraints                     | Description                                                                                                                               | Flash Write Impact        |
| :------------------ | :------------------------ | :------------------------------ | :---------------------------------------------------------------------------------------------------------------------------------------- | :------------------------ |
| `message_id`        | `INTEGER`                 | `PRIMARY KEY AUTOINCREMENT`     | **Internal unique ID.** Managed by SQLite. Essential for `UPDATE`/`DELETE`. Provides fallback order within priority.                      | Written once on `INSERT`. |
| `priority`          | `INTEGER`                 | `NOT NULL DEFAULT 50`           | **Scheduling Priority (e.g., 0-100).** Higher values displayed more urgently/frequently based on C++ logic. Default is medium priority.   | Written on `INSERT`. **Writes on `UPDATE` (Changing priority) - Minimize this!** |
| `is_enabled`        | `INTEGER`                 | `NOT NULL DEFAULT 1`            | **Logical deletion flag.** (1 = Active, 0 = Hidden). Use for expiration/disabling to minimize `DELETE` operations and flash wear.       | Written on `INSERT`. Writes on `UPDATE` (Enable/Disable/Expire). Preferred over `DELETE`. |
| `tarif_zone`        | `INTEGER`                 | `NOT NULL DEFAULT 0`            | **Tariff zone (0-999).** Specifies the tariff zone associated with the message.                                                          | Written on `INSERT`/`UPDATE`. |
| `line_number`       | `INTEGER`                 | `NOT NULL DEFAULT 0`            | **Line number (0-999).** Specifies which line the message should be displayed on.                                                         | Written on `INSERT`/`UPDATE`. |
| `static_intro`      | `TEXT`                    | `NOT NULL DEFAULT ''`           | Static text (max ~15 chars). For `zI` command.                                                                                            | Written on `INSERT`/`UPDATE`. |
| `scrolling_message` | `TEXT`                    | `NOT NULL`                      | Main scrolling text content. For `zM` command.                                                                                            | Written on `INSERT`/`UPDATE`. |
| `next_message_hint` | `TEXT`                    | `NOT NULL DEFAULT ''`           | Short text for brief display during transitions. For `v` command.                                                                         | Written on `INSERT`/`UPDATE`. |
| `datetime_added`    | `INTEGER`                 | `NOT NULL`                      | Unix timestamp (seconds) of creation/addition. Base for expiration & fallback ordering.                                                   | Written once on `INSERT`. |
| `duration_seconds`  | `INTEGER`                 | `DEFAULT NULL`                  | **Persistence duration.** Validity in seconds from `datetime_added`. `NULL` means no duration-based expiry. Used by C++ expiration logic. | Written on `INSERT`/`UPDATE`. |
| `source_info`       | `TEXT`                    | `DEFAULT NULL`                  | Optional metadata about the message origin (e.g., HA user, automation ID).                                                                | Written on `INSERT`/`UPDATE`. |
## Indices
1.  **`idx_messages_priority`**: On `(is_enabled, priority, message_id)`
*   **Purpose:** Efficiently query active persistent messages, ordered primarily by priority, then by insertion order (`SELECT ... WHERE is_enabled = 1 AND (duration_seconds IS NULL OR (datetime_added + duration_seconds) > strftime('%s', 'now')) ORDER BY priority DESC, message_id ASC`). Used for populating the RAM cache.
2.  **`idx_messages_expiry`**: On `(is_enabled, duration_seconds, datetime_added)`
*   **Purpose:** Efficiently find potentially expired persistent messages for the background cleanup task (`UPDATE ... SET is_enabled = 0 WHERE ...`).
*(Note: The PRIMARY KEY (`message_id`) is automatically indexed.)*
## Usage Notes & System Implications
1.  **Schema Initialization:** C++ component ensures table/indices exist on startup.
2.  **Populating the RAM Cache:**
*   The C++ component periodically reads **all** relevant persistent messages from the database into a RAM cache (e.g., a `std::vector` or `std::list`).
*   **Query Used:**
```sql
SELECT message_id, priority, static_intro, scrolling_message, next_message_hint -- Select necessary fields
FROM messages
WHERE
is_enabled = 1
AND (
duration_seconds IS NULL -- Message has no expiry duration OR
OR (datetime_added + duration_seconds) > strftime('%s', 'now') -- Message has not yet expired
)
ORDER BY
priority DESC, -- Highest priority first
message_id ASC; -- Within same priority, oldest first (FIFO)
```
*   **No `LIMIT`:** Do not use `LIMIT` in this query unless absolutely necessary due to severe RAM constraints. The goal is to cache *all* potentially displayable persistent messages to allow the scheduler to cycle through them fairly.
*   **Cache Refresh:** This full query and cache rebuild should only occur when necessary:
*   On component startup.
*   After receiving a command from HA that modifies persistent messages (add, remove, update priority).
*   After the background expiration task marks messages as disabled.
3.  **C++ Scheduler Operation:**
*   The core display loop operates **primarily on the RAM cache** and the separate RAM storage for ephemeral messages.
*   It uses the `priority` field and internal timers/logic to select the *next* message (from RAM cache or ephemeral storage) to send to the display hardware.
*   It does **not** query the database for every message display cycle.
4.  **Background Expiration Task:**
*   A separate, less frequent C++ task should periodically execute an `UPDATE` query to mark expired persistent messages as disabled in the database:
```sql
UPDATE messages
SET is_enabled = 0
WHERE
is_enabled = 1
AND duration_seconds IS NOT NULL
AND (datetime_added + duration_seconds) <= strftime('%s', 'now');
```
*   If this update modifies any rows, it should trigger a refresh of the main RAM cache.
5.  **Ephemeral Messages (RAM Only):**
*   Handled entirely in RAM via separate HA service calls and C++ logic. **No database interaction.** Lost on reboot. Prioritized by the scheduler.
6.  **FLASH WEAR - Priority Updates:** Changing `priority` requires an `UPDATE`. Minimize this operation.
7.  **FLASH WEAR - Logical Deletion (`is_enabled`):** Use `UPDATE ... SET is_enabled = 0` as the **primary method** for removing persistent messages from display (expiration, manual deletion). Physical `DELETE` operations should be exceptional and infrequent (optional low-frequency cleanup task).
8.  **Data Sanitization:** Crucial for all text inputs from HA before DB insertion or sending to the display. Enforce length limits.
## Schema Version Control
The database schema is versioned to support future migrations:

1. Version is stored in `pragma_user_version` SQLite pragma
2. Component checks version on startup and performs migration if needed
3. Migration logic preserves existing messages when possible
4. Version history:
  - 1.0: Initial schema
  - 1.4: Current schema (added source_info field)

## Performance Considerations
- **Message Count**: Optimal performance with <1000 messages
- **Query Optimization**: Primary queries use indices for O(log n) performance
- **Cache Refresh**: Full cache rebuilds takes unknown ammount of time, should be logged.
