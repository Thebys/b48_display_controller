#include "b48_database_manager.h"
#include "character_mappings.h"
#include "esphome/core/log.h"
#include <sqlite3.h>
#include <utility>         // For std::move
#include <ctime>           // Required for time(nullptr) in add_persistent_message call if used there.
#include <Arduino.h>       // For delay() and yield()
#include <esp_task_wdt.h>  // For esp_task_wdt_reset()

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.db";  // Tag for database manager

B48DatabaseManager::B48DatabaseManager(const std::string &db_path) : database_path_(db_path) {}

B48DatabaseManager::~B48DatabaseManager() {
  if (this->db_) {
    ESP_LOGD(TAG, "Closing database connection.");
    sqlite3_close(this->db_);
    this->db_ = nullptr;
  }

  // Note: We don't shutdown SQLite here since other instances
  // might still be using it. Global cleanup should happen
  // at application termination if needed.
}

bool B48DatabaseManager::initialize() {
  ESP_LOGD(TAG, "Initializing database manager for path: %s", this->database_path_.c_str());

  // Initialize SQLite library globally if needed
  ESP_LOGD(TAG, "Ensuring SQLite library is initialized");
  int init_rc = sqlite3_initialize();
  if (init_rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to initialize SQLite library: %d", init_rc);
    return false;
  }

  // Reset watchdog immediately after initialization
  yield();
  esp_task_wdt_reset();

  ESP_LOGD(TAG, "Opening database connection");
  int rc = sqlite3_open(this->database_path_.c_str(), &this->db_);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to open database at '%s': %s", this->database_path_.c_str(), sqlite3_errmsg(this->db_));
    sqlite3_close(this->db_);  // Close even if open failed partially
    this->db_ = nullptr;
    return false;
  }
  ESP_LOGI(TAG, "Successfully opened database connection at '%s'", this->database_path_.c_str());

  // Reset watchdog after opening database
  yield();
  esp_task_wdt_reset();

  // --- Set Page Size ---
  char *err_msg = nullptr;
  const char *pragma_page_size = "PRAGMA page_size=512;";
  rc = sqlite3_exec(this->db_, pragma_page_size, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    // This might fail harmlessly if the DB already exists with a different page size.
    ESP_LOGW(TAG, "Failed to set page_size=512: %s. This is expected if DB already exists.", err_msg);
    sqlite3_free(err_msg);  // Free error message even on warning
  } else {
    ESP_LOGD(TAG, "Successfully executed PRAGMA page_size=512.");
  }
  // --- End Set Page Size ---

  // Reset watchdog after setting page size
  yield();
  esp_task_wdt_reset();

  if (!check_and_create_schema()) {
    ESP_LOGE(TAG, "Failed to verify or create database schema.");
    sqlite3_close(this->db_);
    this->db_ = nullptr;
    return false;
  }

  // Reset watchdog after schema creation
  yield();
  esp_task_wdt_reset();

  // Bootstrap default messages if needed
  if (!bootstrap_default_messages()) {
    ESP_LOGW(TAG, "Failed to bootstrap default messages. Some functionality may be limited.");
    // We continue even if bootstrap fails - this is not a fatal error
  }

  ESP_LOGI(TAG, "Database manager initialized successfully.");
  return true;
}

bool B48DatabaseManager::wipe_database() {
  ESP_LOGW(TAG, "Wiping database as requested...");

  if (!this->db_) {
    ESP_LOGE(TAG, "Database connection is not open. Cannot wipe.");
    return false;
  }

  yield();               // Yield to the OS before potentially long operation
  esp_task_wdt_reset();  // Reset watchdog timer

  const char *drop_tables = "DROP TABLE IF EXISTS messages;";
  char *err_msg = nullptr;

  int rc = sqlite3_exec(this->db_, drop_tables, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL error during wipe: %s", err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  yield();               // Yield again after the operation
  esp_task_wdt_reset();  // Reset watchdog timer

  ESP_LOGI(TAG, "Database tables successfully dropped");
  return true;
}

// --- Stub Implementations ---

bool B48DatabaseManager::check_and_create_schema() {
  ESP_LOGD(TAG, "Checking and creating database schema if needed.");
  esp_task_wdt_reset();  // Reset watchdog timer at start

  // Get the database version
  int user_version = 0;
  char *err_msg = nullptr;
  const char *query = "PRAGMA user_version;";

  auto callback = [](void *data, int argc, char **argv, char **azColName) -> int {
    int *version = static_cast<int *>(data);
    if (argc > 0 && argv[0]) {
      *version = std::stoi(argv[0]);
    }
    return 0;
  };

  int rc = sqlite3_exec(this->db_, query, callback, &user_version, &err_msg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL error: %s", err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  ESP_LOGI(TAG, "Database schema version: %d", user_version);
  esp_task_wdt_reset();  // Reset watchdog timer after version check

  // Create tables if they don't exist or update schema
  if (user_version < 2) {
    yield();               // Yield before potentially long operation
    esp_task_wdt_reset();  // Reset watchdog timer

    // Initial schema creation
    const char *create_tables = R"SQL(
      CREATE TABLE IF NOT EXISTS messages (
        message_id INTEGER PRIMARY KEY AUTOINCREMENT,
        priority INTEGER NOT NULL DEFAULT 50,
        is_enabled INTEGER NOT NULL DEFAULT 1,
        tarif_zone INTEGER NOT NULL DEFAULT 0,
        line_number INTEGER NOT NULL DEFAULT 0,
        static_intro TEXT NOT NULL DEFAULT '',
        scrolling_message TEXT NOT NULL,
        next_message_hint TEXT NOT NULL DEFAULT '',
        datetime_added INTEGER NOT NULL,
        duration_seconds INTEGER DEFAULT NULL,
        source_info TEXT DEFAULT NULL
      );
      
      CREATE INDEX IF NOT EXISTS idx_messages_priority ON messages (is_enabled, priority, message_id);
      CREATE INDEX IF NOT EXISTS idx_messages_expiry ON messages (is_enabled, duration_seconds, datetime_added);
      
      PRAGMA user_version = 1;
    )SQL";

    // Ensure db handle is valid before executing further commands
    if (!this->db_) {
      ESP_LOGE(TAG, "Database handle is null before creating tables.");
      return false;  // Should not happen if open check is correct, but safer
    }

    // Execute schema creation with yields
    ESP_LOGD(TAG, "Creating database schema...");
    yield();
    esp_task_wdt_reset();  // Reset watchdog timer before SQL exec

    rc = sqlite3_exec(this->db_, create_tables, nullptr, nullptr, &err_msg);
    yield();
    esp_task_wdt_reset();  // Reset watchdog timer after SQL exec

    if (rc != SQLITE_OK) {
      ESP_LOGE(TAG, "SQL error: %s", err_msg);
      sqlite3_free(err_msg);
      return false;
    }

    ESP_LOGI(TAG, "Database schema created successfully");
  }

  // Implement schema upgrades as needed for future versions
  yield();               // Final yield
  esp_task_wdt_reset();  // Final watchdog reset

  return true;
}

bool B48DatabaseManager::add_persistent_message(int priority, int line_number, int tarif_zone,
                                                const std::string &static_intro, const std::string &scrolling_message,
                                                const std::string &next_message_hint, int duration_seconds,
                                                const std::string &source_info, bool check_duplicates) {
  yield();               // Allow watchdog to reset before operation starts
  esp_task_wdt_reset();  // Reset watchdog timer

  // Store RAW text in database - encoding will happen at display time
  // Only do basic Unicode → ASCII conversion for problematic characters
  std::string safe_static_intro = sanitize_for_database_storage(static_intro);
  std::string safe_scrolling_message = sanitize_for_database_storage(scrolling_message);
  std::string safe_next_message_hint = sanitize_for_database_storage(next_message_hint);
  std::string safe_source_info = sanitize_for_database_storage(source_info);

  // Log original vs sanitized if there were changes
  if (safe_scrolling_message != scrolling_message) {
    ESP_LOGW(TAG, "Original message contained non-Czech characters, sanitized: '%s%s' -> '%s%s'",
             scrolling_message.substr(0, 30).c_str(), scrolling_message.length() > 30 ? "..." : "",
             safe_scrolling_message.substr(0, 30).c_str(), safe_scrolling_message.length() > 30 ? "..." : "");
    ESP_LOGW(TAG, "Message lengths: original=%zu, sanitized=%zu", scrolling_message.length(),
             safe_scrolling_message.length());
  }

  // Validate scrolling_message is not empty
  if (safe_scrolling_message.empty()) {
    ESP_LOGE(TAG, "Cannot add message with empty scrolling text");
    return false;
  }

  ESP_LOGI(TAG, "Adding message: Priority=%d, Line=%d, Zone=%d, Text='%s%s' (len=%zu), CheckDup=%s", priority,
           line_number, tarif_zone, safe_scrolling_message.substr(0, 30).c_str(),
           safe_scrolling_message.length() > 30 ? "..." : "", safe_scrolling_message.length(),
           check_duplicates ? "true" : "false");

  // Check for duplicates only if flag is set
  if (check_duplicates) {
    // Check for exact scrolling message text match among ALL active messages
    const char *check_query = R"SQL(
      SELECT COUNT(*) FROM messages
      WHERE 
        is_enabled = 1 AND
        scrolling_message = ?
    )SQL";

    sqlite3_stmt *check_stmt;
    int rc = sqlite3_prepare_v2(this->db_, check_query, -1, &check_stmt, nullptr);
    if (rc == SQLITE_OK) {
      sqlite3_bind_text(check_stmt, 1, safe_scrolling_message.c_str(), -1, SQLITE_STATIC);

      if (sqlite3_step(check_stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(check_stmt, 0);
        ESP_LOGD(TAG, "Duplicate check: found %d similar messages", count);
        if (count > 0) {
          ESP_LOGW(TAG, "Similar message already exists in database, skipping duplicate. Use check_duplicates=false to "
                        "override.");
          sqlite3_finalize(check_stmt);
          return false;
        }
      }
      sqlite3_finalize(check_stmt);
    }
  }

  // Continue using the safe (ASCII) versions in the query
  const char *query = R"SQL(
    INSERT INTO messages (
      is_enabled, priority, line_number, tarif_zone, static_intro, scrolling_message, 
      next_message_hint, datetime_added, duration_seconds, source_info
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
  )SQL";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  yield();               // Allow watchdog to reset after prepare
  esp_task_wdt_reset();  // Reset watchdog timer

  // Bind parameters with the safe ASCII versions
  sqlite3_bind_int(stmt, 1, 1);
  sqlite3_bind_int(stmt, 2, priority);
  sqlite3_bind_int(stmt, 3, line_number);
  sqlite3_bind_int(stmt, 4, tarif_zone);
  sqlite3_bind_text(stmt, 5, safe_static_intro.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, safe_scrolling_message.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, safe_next_message_hint.c_str(), -1, SQLITE_STATIC);

  // Get current time (timestamp) and log it for debugging
  time_t now = time(nullptr);
  ESP_LOGI(TAG, "Current timestamp: %lld", (long long) now);
  sqlite3_bind_int64(stmt, 8, now);

  if (duration_seconds > 0) {
    ESP_LOGI(TAG, "Message will expire at timestamp: %lld", (long long) (now + duration_seconds));
    sqlite3_bind_int(stmt, 9, duration_seconds);
  } else {
    sqlite3_bind_null(stmt, 9);
  }

  if (!safe_source_info.empty()) {
    sqlite3_bind_text(stmt, 10, safe_source_info.c_str(), -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 10);
  }

  yield();               // Allow watchdog to reset after binding params
  esp_task_wdt_reset();  // Reset watchdog timer

  rc = sqlite3_step(stmt);

  yield();               // Allow watchdog to reset after step
  esp_task_wdt_reset();  // Reset watchdog timer

  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    ESP_LOGE(TAG, "Failed to add message: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  ESP_LOGI(TAG, "Successfully added persistent message with priority %d", priority);
  return true;
}

bool B48DatabaseManager::update_persistent_message(int message_id, int priority, bool is_enabled, int line_number,
                                                   int tarif_zone, const std::string &static_intro,
                                                   const std::string &scrolling_message,
                                                   const std::string &next_message_hint, int duration_seconds,
                                                   const std::string &source_info) {
  const char *query = R"SQL(
    UPDATE messages
    SET 
      priority = ?,
      is_enabled = ?,
      line_number = ?,
      tarif_zone = ?,
      static_intro = ?,
      scrolling_message = ?,
      next_message_hint = ?,
      duration_seconds = ?,
      source_info = ?
    WHERE message_id = ?;
  )SQL";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare update statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  // Bind parameters
  sqlite3_bind_int(stmt, 1, priority);
  sqlite3_bind_int(stmt, 2, is_enabled ? 1 : 0);
  sqlite3_bind_int(stmt, 3, line_number);
  sqlite3_bind_int(stmt, 4, tarif_zone);
  sqlite3_bind_text(stmt, 5, static_intro.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, scrolling_message.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 7, next_message_hint.c_str(), -1, SQLITE_STATIC);

  if (duration_seconds > 0) {
    sqlite3_bind_int(stmt, 8, duration_seconds);
  } else {
    sqlite3_bind_null(stmt, 8);
  }

  if (!source_info.empty()) {
    sqlite3_bind_text(stmt, 9, source_info.c_str(), -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 9);
  }

  sqlite3_bind_int(stmt, 10, message_id);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    ESP_LOGE(TAG, "Failed to update message: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  ESP_LOGI(TAG, "Successfully updated message ID %d", message_id);
  return true;
}

bool B48DatabaseManager::delete_persistent_message(int message_id) {
  // Using logical deletion to reduce flash wear
  const char *query = "UPDATE messages SET is_enabled = 0 WHERE message_id = ?;";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare delete statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  sqlite3_bind_int(stmt, 1, message_id);

  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  if (rc != SQLITE_DONE) {
    ESP_LOGE(TAG, "Failed to delete message: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  ESP_LOGI(TAG, "Successfully marked message ID %d as deleted", message_id);
  return true;
}

std::vector<std::shared_ptr<MessageEntry>> B48DatabaseManager::get_active_persistent_messages() {
  std::vector<std::shared_ptr<MessageEntry>> messages;
  // Filter active (enabled and not expired) messages using SQL
  time_t now_ts = time(nullptr);
  ESP_LOGD(TAG, "Filtering active messages with timestamp: %lld", (long long) now_ts);
  const char *query = R"SQL(
    SELECT message_id, priority, line_number, tarif_zone, static_intro,
           scrolling_message, next_message_hint, datetime_added, duration_seconds
    FROM messages
    WHERE is_enabled = 1
      AND (
        duration_seconds IS NULL
        OR duration_seconds = 0
        OR (datetime_added + duration_seconds) > ?
      )
    ORDER BY priority DESC, message_id ASC
  )SQL";

  esp_task_wdt_reset();
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare get_active_persistent_messages statement: %s", sqlite3_errmsg(this->db_));
    return messages;
  }
  ESP_LOGI(TAG, "Now_ts (epoch): %lld", (long long) now_ts);
  sqlite3_bind_int64(stmt, 1, now_ts);
  ESP_LOGI(TAG, "Expanded SQL: %s", sqlite3_expanded_sql(stmt));

  int count = 0;
  ESP_LOGD(TAG, "Starting to fetch messages from database");

  int step_result;
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    esp_task_wdt_reset();
    yield();
    auto entry = std::make_shared<MessageEntry>();
    entry->is_ephemeral = false;
    entry->message_id = sqlite3_column_int(stmt, 0);
    entry->priority = sqlite3_column_int(stmt, 1);
    entry->line_number = sqlite3_column_int(stmt, 2);
    entry->tarif_zone = sqlite3_column_int(stmt, 3);
    const char *static_intro = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    if (static_intro)
      entry->static_intro = static_intro;
    const char *scrolling_message = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    if (scrolling_message)
      entry->scrolling_message = scrolling_message;
    const char *next_hint = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    if (next_hint)
      entry->next_message_hint = next_hint;
    time_t added_time = static_cast<time_t>(sqlite3_column_int64(stmt, 7));
    int duration_seconds = sqlite3_column_type(stmt, 8) == SQLITE_NULL ? 0 : sqlite3_column_int(stmt, 8);
    if (duration_seconds > 0)
      entry->expiry_time = added_time + duration_seconds;
    ESP_LOGD(TAG, "Loaded message ID=%d, Priority=%d, Duration=%d", entry->message_id, entry->priority,
             duration_seconds);
    messages.push_back(entry);
    count++;
  }
  if (step_result != SQLITE_DONE) {
    ESP_LOGE(TAG, "SQLite error in get_active_persistent_messages: %s", sqlite3_errmsg(this->db_));
  }
  sqlite3_finalize(stmt);
  esp_task_wdt_reset();
  ESP_LOGI(TAG, "Loaded %d messages from database", count);
  return messages;
}

// Expire messages whose duration has elapsed and log detailed info
int B48DatabaseManager::expire_old_messages() {
  ESP_LOGI(TAG, "Expiring old messages");

  // Ensure database connection is valid
  if (!this->db_) {
    ESP_LOGE(TAG, "Database connection is not open. Cannot expire messages.");
    return -1;
  }

  // Get current time for expiry check
  time_t now_ts = time(nullptr);
  ESP_LOGD(TAG, "Current timestamp for expiry check: %lld", (long long) now_ts);

  // --- PART 1: Find messages that will expire and update them one by one ---
  int changes = 0;
  std::vector<int> message_ids_to_expire;

  {
    const char *select_sql = R"SQL(
      SELECT message_id, datetime_added, duration_seconds
      FROM messages
      WHERE is_enabled = 1
        AND duration_seconds IS NOT NULL
        AND duration_seconds > 0
        AND (datetime_added + duration_seconds) <= ?
    )SQL";

    sqlite3_stmt *sel_stmt = nullptr;
    int rc = sqlite3_prepare_v2(this->db_, select_sql, -1, &sel_stmt, nullptr);

    if (rc != SQLITE_OK) {
      ESP_LOGE(TAG, "Failed to prepare select expire list: %s", sqlite3_errmsg(this->db_));
      return -1;
    }

    // Bind timestamp safely
    rc = sqlite3_bind_int64(sel_stmt, 1, now_ts);
    if (rc != SQLITE_OK) {
      ESP_LOGE(TAG, "Failed to bind timestamp to select: %s", sqlite3_errmsg(this->db_));
      sqlite3_finalize(sel_stmt);
      return -1;
    }

    // Loop through results and collect message IDs
    while ((rc = sqlite3_step(sel_stmt)) == SQLITE_ROW) {
      int msg_id = sqlite3_column_int(sel_stmt, 0);
      long long added = sqlite3_column_int64(sel_stmt, 1);
      int dur = sqlite3_column_int(sel_stmt, 2);
      long long expiry_ts = added + dur;

      ESP_LOGW(TAG, "Message ID %d will expire: added_ts=%lld, duration=%d, expiry_ts=%lld", msg_id, added, dur,
               expiry_ts);

      message_ids_to_expire.push_back(msg_id);
    }

    if (rc != SQLITE_DONE) {
      ESP_LOGE(TAG, "Error during select step: %s", sqlite3_errmsg(this->db_));
      sqlite3_finalize(sel_stmt);
      return -1;
    }

    // Finalize statement
    sqlite3_finalize(sel_stmt);

    // Yield to prevent watchdog timeouts
    yield();
    esp_task_wdt_reset();
  }

  // --- PART 2: Update each message individually ---
  if (message_ids_to_expire.empty()) {
    ESP_LOGD(TAG, "No messages to expire");
    return 0;
  }

  // Prepare a simple update statement for a single message
  const char *update_single_sql = "UPDATE messages SET is_enabled = 0 WHERE message_id = ?";
  sqlite3_stmt *update_stmt = nullptr;
  int rc = sqlite3_prepare_v2(this->db_, update_single_sql, -1, &update_stmt, nullptr);

  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare single message update: %s", sqlite3_errmsg(this->db_));
    return -1;
  }

  // Update each message individually
  for (int msg_id : message_ids_to_expire) {
    // Clean bind and reset statement for reuse
    sqlite3_clear_bindings(update_stmt);
    sqlite3_reset(update_stmt);

    // Bind the message ID
    rc = sqlite3_bind_int(update_stmt, 1, msg_id);
    if (rc != SQLITE_OK) {
      ESP_LOGE(TAG, "Failed to bind message ID %d: %s", msg_id, sqlite3_errmsg(this->db_));
      continue;  // Try next message
    }

    // Execute the update
    rc = sqlite3_step(update_stmt);
    if (rc != SQLITE_DONE) {
      ESP_LOGE(TAG, "Failed to expire message ID %d: %s", msg_id, sqlite3_errmsg(this->db_));
    } else {
      ESP_LOGI(TAG, "Successfully expired message ID %d", msg_id);
      changes++;
    }

    // Yield every few messages
    if (changes % 3 == 0) {
      yield();
      esp_task_wdt_reset();
    }
  }

  // Finalize the statement
  sqlite3_finalize(update_stmt);

  // Report results
  if (changes > 0) {
    ESP_LOGI(TAG, "Expired %d out of %zu messages", changes, message_ids_to_expire.size());

    if (changes != static_cast<int>(message_ids_to_expire.size())) {
      ESP_LOGW(TAG, "Some messages could not be expired");
    }
  } else {
    ESP_LOGD(TAG, "No messages were expired");
  }

  return changes;
}

int B48DatabaseManager::purge_disabled_messages() {
  ESP_LOGW(TAG, "Physically purging disabled messages from database...");

  // Ensure database connection is valid
  if (!this->db_) {
    ESP_LOGE(TAG, "Database connection is not open. Cannot purge disabled messages.");
    return -1;
  }

  yield();               // Yield to the OS before potentially long operation
  esp_task_wdt_reset();  // Reset watchdog timer

  // First count how many messages will be purged
  const char *count_sql = "SELECT COUNT(*) FROM messages WHERE is_enabled = 0;";
  sqlite3_stmt *count_stmt = nullptr;
  int rc = sqlite3_prepare_v2(this->db_, count_sql, -1, &count_stmt, nullptr);

  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare count statement: %s", sqlite3_errmsg(this->db_));
    return -1;
  }

  int disabled_count = 0;
  if (sqlite3_step(count_stmt) == SQLITE_ROW) {
    disabled_count = sqlite3_column_int(count_stmt, 0);
    ESP_LOGI(TAG, "Found %d disabled messages to purge", disabled_count);
  } else {
    ESP_LOGE(TAG, "Failed to count disabled messages: %s", sqlite3_errmsg(this->db_));
    sqlite3_finalize(count_stmt);
    return -1;
  }
  sqlite3_finalize(count_stmt);

  if (disabled_count == 0) {
    ESP_LOGI(TAG, "No disabled messages to purge");
    return 0;  // Nothing to do
  }

  // Execute the deletion operation
  yield();               // Yield again before deletion
  esp_task_wdt_reset();  // Reset watchdog timer

  const char *delete_sql = "DELETE FROM messages WHERE is_enabled = 0;";
  char *err_msg = nullptr;

  rc = sqlite3_exec(this->db_, delete_sql, nullptr, nullptr, &err_msg);

  yield();               // Yield again after operation
  esp_task_wdt_reset();  // Reset watchdog timer

  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL error during purge of disabled messages: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;
  }

  int actually_deleted = sqlite3_changes(this->db_);
  ESP_LOGI(TAG, "Successfully purged %d disabled messages from database", actually_deleted);

  // Vacuum the database to reclaim space (optional but recommended for infrequent cleanup)
  ESP_LOGI(TAG, "Performing VACUUM operation to reclaim space...");

  yield();               // Yield before vacuum
  esp_task_wdt_reset();  // Reset watchdog timer

  rc = sqlite3_exec(this->db_, "VACUUM;", nullptr, nullptr, &err_msg);

  yield();               // Yield after vacuum
  esp_task_wdt_reset();  // Reset watchdog timer

  if (rc != SQLITE_OK) {
    ESP_LOGW(TAG, "VACUUM operation failed: %s", err_msg);
    sqlite3_free(err_msg);
    // Continue anyway, this is not critical
  } else {
    ESP_LOGI(TAG, "VACUUM operation completed successfully");
  }

  return actually_deleted;
}

// --- Bootstrapping ---

bool B48DatabaseManager::bootstrap_default_messages() {
  ESP_LOGI(TAG, "Checking if default messages need bootstrapping...");

  if (!this->db_) {
    ESP_LOGE(TAG, "Database connection is not open. Cannot bootstrap.");
    return false;
  }

  esp_task_wdt_reset();  // Reset watchdog timer

  sqlite3_stmt *stmt;
  const char *count_query = "SELECT COUNT(*) FROM messages WHERE is_enabled = 1;";  // Only count active messages
  int rc = sqlite3_prepare_v2(this->db_, count_query, -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare count statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  esp_task_wdt_reset();  // Reset watchdog timer

  int count = 0;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  } else {
    ESP_LOGE(TAG, "Failed to execute count statement: %s", sqlite3_errmsg(this->db_));
    sqlite3_finalize(stmt);
    return false;
  }
  sqlite3_finalize(stmt);  // Finalize the statement

  if (count > 0) {
    ESP_LOGD(TAG, "Database already contains %d active messages, skipping bootstrap.", count);
    return true;  // Not an error, just no need to bootstrap
  }

  ESP_LOGI(TAG, "Bootstrapping default persistent messages as the table is empty.");
  esp_task_wdt_reset();  // Reset watchdog timer before inserting messages

  // Define bootstrap messages in a data structure
  struct BootstrapMessage {
    int priority;
    int line_number;
    int tarif_zone;
    const char *static_intro;
    const char *scrolling_message;
    const char *next_message_hint;
    int duration_seconds;
    const char *source_info;
  };

  // Array of bootstrap messages
  const BootstrapMessage bootstrap_messages[] = {
      // Priority, Line number, Tarif zone, Destination, Scroll message, Next message hint, Duration, Source info
      {40, 48, 101, "Base48", "Podporuj svuj mistni hackerspace! Podporuj Base48.", "Loading", 0, "SQLiteBootstrap"},
      {40, 48, 101, "Base48", "Support your local hackerspace! Support Base48.", "Loading", 0, "SQLiteBootstrap"},
      {36, 48, 101, "Grilovacka", "Grilovacka v Base48 kazdy patek. . . Hackeri a pratele vitani !", "Loading", 0,
       "SQLiteBootstrap"},
      {36, 48, 101, "Barbecue", "Barbecue at Base48 every Friday. Food, hackers, friends, music, chill.", "Loading", 0,
       "SQLiteBootstrap"},
      {38, 48, 101, "Uklid", "Udrzujte poradek a cistotu, uklizejte na stolech.", "Cleaning", 0, "SQLiteBootstrap"},
      {38, 48, 101, "Cleanup", "Maintain order and cleanliness, clean the tables.", "Cleaning", 0, "SQLiteBootstrap"},
      {34, 48, 101, "Tech Stack",
       "Running ESPHome on o. g. ESP32. Messages saved in SQLite on LittleFS. Filesystem Partition 512 KB. Exposes "
       "interface to Home Assistant. ASCII messages and DPMB 2005 Firmware.",
       "UART2_TX_OVERF", 0, "SQLiteBootstrap"},
      {34, 48, 101, "Credits",
       "Panel and research - Filip. Serial IBIS protocol research by pavlik.space. Initial HW assistance by Vega "
       "(vega76.cz). ESP - ESPHome - HA software is C++ vibecoded by Thebys. ",
       "GOTO 0xBEEF", 0, "SQLiteBootstrap"},
      // Additional messages can be added here in the future
  };

  const int message_count = sizeof(bootstrap_messages) / sizeof(BootstrapMessage);
  bool success = true;

  // Add messages one by one with yields between operations to prevent watchdog timeout
  for (int i = 0; i < message_count; i++) {
    const auto &msg = bootstrap_messages[i];
    ESP_LOGD(TAG, "Adding bootstrap message %d/%d...", i + 1, message_count);

    success &=
        add_persistent_message(msg.priority, msg.line_number, msg.tarif_zone, msg.static_intro, msg.scrolling_message,
                               msg.next_message_hint, msg.duration_seconds, msg.source_info, true);

    esp_task_wdt_reset();  // Reset watchdog timer
  }

  if (!success) {
    ESP_LOGE(TAG, "Failed to add one or more default messages during bootstrap.");
    return false;
  }

  ESP_LOGI(TAG, "Successfully bootstrapped default messages.");
  return true;
}

// --- New Implementations ---

int B48DatabaseManager::get_message_count() {
  if (!this->db_) {
    ESP_LOGE(TAG, "Database connection is not open. Cannot get message count.");
    return -1;
  }
  // Get current timestamp for expiry comparison
  time_t now_ts = time(nullptr);
  ESP_LOGD(TAG, "get_message_count now_ts: %lld", (long long) now_ts);

  // Count only enabled and not-yet-expired messages
  const char *query = R"SQL(
    SELECT COUNT(*) FROM messages
    WHERE is_enabled = 1
      AND (
        duration_seconds IS NULL
        OR duration_seconds = 0
        OR (datetime_added + duration_seconds) > ?
      )
  )SQL";
  sqlite3_stmt *stmt = nullptr;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare count statement: %s", sqlite3_errmsg(this->db_));
    return -1;
  }
  sqlite3_bind_int64(stmt, 1, now_ts);

  int count = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  } else {
    ESP_LOGE(TAG, "Failed to execute count statement: %s", sqlite3_errmsg(this->db_));
  }
  sqlite3_finalize(stmt);
  ESP_LOGD(TAG, "Active message count: %d", count);
  return count;
}

bool B48DatabaseManager::clear_all_messages() {
  ESP_LOGW(TAG, "Clearing all messages from the database...");

  if (!this->db_) {
    ESP_LOGE(TAG, "Database connection is not open. Cannot clear messages.");
    return false;
  }

  yield();               // Yield to the OS before potentially long operation
  esp_task_wdt_reset();  // Reset watchdog timer

  const char *delete_query = "DELETE FROM messages;";
  char *err_msg = nullptr;

  int rc = sqlite3_exec(this->db_, delete_query, nullptr, nullptr, &err_msg);

  yield();               // Yield again after the operation
  esp_task_wdt_reset();  // Reset watchdog timer

  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL error during clear all messages: %s", err_msg);
    sqlite3_free(err_msg);
    return false;
  }

  int changes = sqlite3_changes(this->db_);
  ESP_LOGI(TAG, "Successfully cleared %d messages from the database.", changes);

  // Optionally, reset the autoincrement counter if desired (use with caution)
  // const char *reset_seq = "DELETE FROM sqlite_sequence WHERE name='messages';";
  // rc = sqlite3_exec(this->db_, reset_seq, nullptr, nullptr, &err_msg);
  // if (rc != SQLITE_OK) {
  //   ESP_LOGW(TAG, "Failed to reset sequence for messages table: %s", err_msg);
  //   sqlite3_free(err_msg);
  //   // Continue anyway, not critical
  // } else {
  //   ESP_LOGD(TAG, "Reset autoincrement sequence for messages table.");
  // }

  return true;
}

void B48DatabaseManager::dump_all_messages() {
  if (!this->db_) {
    ESP_LOGE(TAG, "Database connection is not open. Cannot dump messages.");
    return;
  }

  // Query that gets all messages, including disabled ones
  const char *query = R"SQL(
    SELECT message_id, priority, is_enabled, line_number, tarif_zone, 
           static_intro, scrolling_message, next_message_hint, 
           datetime_added, duration_seconds, source_info
    FROM messages
    ORDER BY message_id ASC;
  )SQL";

  ESP_LOGI(TAG, "============= DUMPING ALL DATABASE MESSAGES =============");
  esp_task_wdt_reset();  // Reset watchdog timer before query

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare statement for dump: %s", sqlite3_errmsg(this->db_));
    return;
  }

  int count = 0;
  esp_task_wdt_reset();  // Reset watchdog timer before fetching rows

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    int message_id = sqlite3_column_int(stmt, 0);
    int priority = sqlite3_column_int(stmt, 1);
    bool is_enabled = sqlite3_column_int(stmt, 2) != 0;
    int line_number = sqlite3_column_int(stmt, 3);
    int tarif_zone = sqlite3_column_int(stmt, 4);

    const char *static_intro = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    const char *scrolling_message = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));
    const char *next_hint = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 7));

    time_t added_time = static_cast<time_t>(sqlite3_column_int64(stmt, 8));
    int duration_seconds = sqlite3_column_type(stmt, 9) == SQLITE_NULL ? 0 : sqlite3_column_int(stmt, 9);
    const char *source_info = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 10));

    // Format time for display
    char time_str[64] = "(unknown)";
    struct tm timeinfo;
    if (localtime_r(&added_time, &timeinfo)) {
      strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }

    // Calculate expiry time string if duration exists
    char expiry_str[64] = "never";
    if (duration_seconds > 0) {
      time_t expiry_time = added_time + duration_seconds;
      struct tm expiry_info;
      if (localtime_r(&expiry_time, &expiry_info)) {
        strftime(expiry_str, sizeof(expiry_str), "%Y-%m-%d %H:%M:%S", &expiry_info);
      }
    }

    ESP_LOGI(TAG, "ID [%d]: %s, Prio=%d, Line=%d, Zone=%d, Added=%s, Expires=%s", message_id,
             is_enabled ? "ENABLED" : "disabled", priority, line_number, tarif_zone, time_str, expiry_str);

    std::string scroll_msg = scrolling_message ? scrolling_message : "";
    ESP_LOGI(TAG, "  Intro: '%s', Message: '%s%s' (len=%zu), Next: '%s', Source: '%s'",
             static_intro ? static_intro : "", scroll_msg.substr(0, 30).c_str(), scroll_msg.length() > 30 ? "..." : "",
             scroll_msg.length(), next_hint ? next_hint : "", source_info ? source_info : "");

    count++;

    // Reset watchdog timer periodically during long operations
    if (count % 5 == 0) {
      yield();
      esp_task_wdt_reset();
    }
  }

  sqlite3_finalize(stmt);
  esp_task_wdt_reset();  // Final watchdog reset

  ESP_LOGI(TAG, "======= DUMP COMPLETE: %d TOTAL MESSAGES (%d ENABLED) =======", count, get_message_count());
}

std::string B48DatabaseManager::convert_to_ascii(const std::string &str) {
  std::string result;
  result.reserve(str.length());  // Pre-allocate memory

  for (size_t i = 0; i < str.length(); ++i) {
    unsigned char c1 = static_cast<unsigned char>(str[i]);

    if (c1 <= 0x7F) {  // Standard ASCII (0-127)
      result += c1;
    } else if (c1 >= 0xC2 && c1 <= 0xDF) {  // Start of a 2-byte UTF-8 sequence
      if (i + 1 < str.length()) {
        unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
        if ((c2 & 0xC0) == 0x80) {             // Check if c2 is a valid continuation byte (10xxxxxx)
          uint16_t utf8_val = (c1 << 8) | c2;  // Combine bytes for switch case
          char replacement = ' ';              // Default replacement

          switch (utf8_val) {
            // Czech & Slovak
            case 0xC381:
              replacement = 'A';
              break;  // Á
            case 0xC3A1:
              replacement = 'a';
              break;  // á
            case 0xC48C:
              replacement = 'C';
              break;  // Č
            case 0xC48D:
              replacement = 'c';
              break;  // č
            case 0xC48E:
              replacement = 'D';
              break;  // Ď
            case 0xC48F:
              replacement = 'd';
              break;  // ď
            case 0xC389:
              replacement = 'E';
              break;  // É
            case 0xC3A9:
              replacement = 'e';
              break;  // é
            case 0xC49A:
              replacement = 'E';
              break;  // Ě
            case 0xC49B:
              replacement = 'e';
              break;  // ě
            case 0xC38D:
              replacement = 'I';
              break;  // Í
            case 0xC3AD:
              replacement = 'i';
              break;  // í
            case 0xC587:
              replacement = 'N';
              break;  // Ň
            case 0xC588:
              replacement = 'n';
              break;  // ň
            case 0xC393:
              replacement = 'O';
              break;  // Ó
            case 0xC3B3:
              replacement = 'o';
              break;  // ó
            case 0xC598:
              replacement = 'R';
              break;  // Ř
            case 0xC599:
              replacement = 'r';
              break;  // ř
            case 0xC5A0:
              replacement = 'S';
              break;  // Š
            case 0xC5A1:
              replacement = 's';
              break;  // š
            case 0xC5A4:
              replacement = 'T';
              break;  // Ť
            case 0xC5A5:
              replacement = 't';
              break;  // ť
            case 0xC39A:
              replacement = 'U';
              break;  // Ú
            case 0xC3BA:
              replacement = 'u';
              break;  // ú
            case 0xC5AE:
              replacement = 'U';
              break;  // Ů
            case 0xC5AF:
              replacement = 'u';
              break;  // ů
            case 0xC39D:
              replacement = 'Y';
              break;  // Ý
            case 0xC3BD:
              replacement = 'y';
              break;  // ý
            case 0xC5BD:
              replacement = 'Z';
              break;  // Ž
            case 0xC5BE:
              replacement = 'z';
              break;  // ž

            // German
            case 0xC384:
              replacement = 'A';
              break;  // Ä
            case 0xC3A4:
              replacement = 'a';
              break;  // ä
            case 0xC396:
              replacement = 'O';
              break;  // Ö
            case 0xC3B6:
              replacement = 'o';
              break;  // ö
            case 0xC39C:
              replacement = 'U';
              break;  // Ü
            case 0xC3BC:
              replacement = 'u';
              break;  // ü
            case 0xC39F:
              replacement = 's';
              break;  // ß (often 'ss', simplified to 's')

            // French (selected common)
            case 0xC380:
              replacement = 'A';
              break;  // À
            case 0xC3A0:
              replacement = 'a';
              break;  // à
            case 0xC382:
              replacement = 'A';
              break;  // Â
            case 0xC3A2:
              replacement = 'a';
              break;  // â
            case 0xC387:
              replacement = 'C';
              break;  // Ç
            case 0xC3A7:
              replacement = 'c';
              break;  // ç
            case 0xC388:
              replacement = 'E';
              break;  // È
            case 0xC3A8:
              replacement = 'e';
              break;  // è
            case 0xC38A:
              replacement = 'E';
              break;  // Ê
            case 0xC3AA:
              replacement = 'e';
              break;  // ê
            case 0xC38B:
              replacement = 'E';
              break;  // Ë
            case 0xC3AB:
              replacement = 'e';
              break;  // ë
            case 0xC38E:
              replacement = 'I';
              break;  // Î
            case 0xC3AE:
              replacement = 'i';
              break;  // î
            case 0xC394:
              replacement = 'O';
              break;  // Ô
            case 0xC3B4:
              replacement = 'o';
              break;  // ô
            case 0xC592:
              replacement = 'O';
              break;  // Œ (OE ligature, simplified)
            case 0xC593:
              replacement = 'o';
              break;  // œ (oe ligature, simplified)
            case 0xC399:
              replacement = 'U';
              break;  // Ù
            case 0xC3B9:
              replacement = 'u';
              break;  // ù
            case 0xC39B:
              replacement = 'U';
              break;  // Û
            case 0xC3BB:
              replacement = 'u';
              break;  // û

            // Spanish
            case 0xC391:
              replacement = 'N';
              break;  // Ñ
            case 0xC3B1:
              replacement = 'n';
              break;  // ñ

            // Polish (selected common)
            case 0xC484:
              replacement = 'A';
              break;  // Ą
            case 0xC485:
              replacement = 'a';
              break;  // ą
            case 0xC486:
              replacement = 'C';
              break;  // Ć
            case 0xC487:
              replacement = 'c';
              break;  // ć
            case 0xC498:
              replacement = 'E';
              break;  // Ę
            case 0xC499:
              replacement = 'e';
              break;  // ę
            case 0xC581:
              replacement = 'L';
              break;  // Ł
            case 0xC582:
              replacement = 'l';
              break;  // ł
            case 0xC583:
              replacement = 'N';
              break;  // Ń
            case 0xC584:
              replacement = 'n';
              break;  // ń
            // Óó covered by Czech
            case 0xC59A:
              replacement = 'S';
              break;  // Ś
            case 0xC59B:
              replacement = 's';
              break;  // ś
            case 0xC5B9:
              replacement = 'Z';
              break;  // Ź
            case 0xC5BA:
              replacement = 'z';
              break;  // ź
            case 0xC5BB:
              replacement = 'Z';
              break;  // Ż
            case 0xC5BC:
              replacement = 'z';
              break;  // ż

            // Add more 2-byte mappings as needed
            default:
              // If no specific mapping, use '?'
              break;
          }
          result += replacement;
          i++;  // Increment i because we've consumed c2
        } else {
          // Invalid UTF-8 sequence (c2 is not a continuation byte or string ends prematurely)
          result += ' ';
        }
      } else {
        // String ends prematurely after c1 indicated a 2-byte sequence
        result += ' ';
      }
    } else if ((c1 >= 0xE0 && c1 <= 0xEF) || (c1 >= 0xF0 && c1 <= 0xF4)) {
      // Start of a 3-byte or 4-byte UTF-8 sequence.
      // For simplicity in an ASCII conversion, map these to '?' and advance index.
      result += ' ';
      if (c1 >= 0xE0 && c1 <= 0xEF) {  // 3-byte
        i += 2;                        // Attempt to skip the next two bytes
      } else {                         // 4-byte
        i += 3;                        // Attempt to skip the next three bytes
      }
      // Ensure we don't skip past the end of the string
      if (i >= str.length()) {
        i = str.length() - 1;  // Adjust if we skipped too far
      }
    } else {  // Invalid UTF-8 start byte or other non-ASCII character not handled above
      result += ' ';
    }
  }
  return result;
}

std::string B48DatabaseManager::sanitize_for_czech_display(const std::string &str) {
  // Use the new character mapping system for consistent encoding
  // This will handle Czech characters, emojis, and other special symbols
  return CharacterMappingManager::get_instance().encode_for_display(str);
}

std::string B48DatabaseManager::sanitize_for_database_storage(const std::string &str) {
  // Minimal sanitization - only convert problematic Unicode characters to ASCII
  // Keep Czech characters and emojis intact for later encoding at display time
  std::string result;
  result.reserve(str.length());

  for (size_t i = 0; i < str.length(); ) {
    unsigned char c1 = static_cast<unsigned char>(str[i]);

    if (c1 <= 0x7F) {
      // Standard ASCII - keep as is
      result += c1;
      i++;
    } else if (c1 >= 0xC2 && c1 <= 0xDF && i + 1 < str.length()) {
      // 2-byte UTF-8 sequence
      unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
      if ((c2 & 0xC0) == 0x80) {
        uint16_t utf8_val = (c1 << 8) | c2;
        
        // Only convert specific problematic Unicode punctuation to ASCII
        switch (utf8_val) {
          case 0xE280A6:  // Unicode ellipsis … → ASCII dots
            result += "...";
            i += 2;
            break;
          case 0xE2809C:  // Left double quotation mark " → ASCII quote
          case 0xE2809D:  // Right double quotation mark " → ASCII quote
            result += "\"";
            i += 2;
            break;
          case 0xE28098:  // Left single quotation mark ' → ASCII apostrophe
          case 0xE28099:  // Right single quotation mark ' → ASCII apostrophe
            result += "'";
            i += 2;
            break;
          case 0xE28093:  // En dash – → ASCII hyphen
          case 0xE28094:  // Em dash — → ASCII hyphen
            result += "-";
            i += 2;
            break;
          default:
            // Keep all other characters (including Czech chars and emojis) as-is
            result += c1;
            result += c2;
            i += 2;
            break;
        }
      } else {
        result += c1;
        i++;
      }
    } else if (c1 >= 0xE0 && c1 <= 0xEF && i + 2 < str.length()) {
      // 3-byte UTF-8 sequence - handle specific Unicode punctuation
      unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
      unsigned char c3 = static_cast<unsigned char>(str[i + 2]);
      
      if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
        uint32_t utf8_val = (c1 << 16) | (c2 << 8) | c3;
        
        switch (utf8_val) {
          case 0xE280A6:  // Unicode ellipsis …
            result += "...";
            i += 3;
            break;
          case 0xE2809C:  // Left double quotation mark "
          case 0xE2809D:  // Right double quotation mark "
            result += "\"";
            i += 3;
            break;
          case 0xE28098:  // Left single quotation mark '
          case 0xE28099:  // Right single quotation mark '
            result += "'";
            i += 3;
            break;
          case 0xE28093:  // En dash –
          case 0xE28094:  // Em dash —
            result += "-";
            i += 3;
            break;
          default:
            // Keep all other 3-byte sequences (including emojis) as-is
            result += c1;
            result += c2;
            result += c3;
            i += 3;
            break;
        }
      } else {
        result += c1;
        i++;
      }
    } else {
      // 4-byte UTF-8 or invalid - keep as-is (includes most emojis)
      result += c1;
      i++;
    }
  }
  
  return result;
}

}  // namespace b48_display_controller
}  // namespace esphome