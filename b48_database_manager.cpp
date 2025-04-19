#include "b48_database_manager.h"
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
  yield();  // Allow watchdog to reset before operation starts
  esp_task_wdt_reset();  // Reset watchdog timer

  // Convert all strings to ASCII
  std::string safe_static_intro = convert_to_ascii(static_intro);
  std::string safe_scrolling_message = convert_to_ascii(scrolling_message);
  std::string safe_next_message_hint = convert_to_ascii(next_message_hint);
  std::string safe_source_info = convert_to_ascii(source_info);

  // Log original vs converted if there were changes
  if (safe_scrolling_message != scrolling_message) {
    ESP_LOGW(TAG, "Original message contained non-ASCII chars, converted: '%s%s' -> '%s%s'", 
            scrolling_message.substr(0, 30).c_str(), scrolling_message.length() > 30 ? "..." : "",
            safe_scrolling_message.substr(0, 30).c_str(), safe_scrolling_message.length() > 30 ? "..." : "");
    ESP_LOGW(TAG, "Message lengths: original=%zu, converted=%zu", scrolling_message.length(), safe_scrolling_message.length());
  }

  // Validate scrolling_message is not empty
  if (safe_scrolling_message.empty()) {
    ESP_LOGE(TAG, "Cannot add message with empty scrolling text");
    return false;
  }

    ESP_LOGI(TAG, "Adding message: Priority=%d, Line=%d, Zone=%d, Text='%s%s' (len=%zu), CheckDup=%s", 
            priority, line_number, tarif_zone, 
            safe_scrolling_message.substr(0, 30).c_str(), safe_scrolling_message.length() > 30 ? "..." : "",
            safe_scrolling_message.length(),
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
          ESP_LOGW(TAG, "Similar message already exists in database, skipping duplicate. Use check_duplicates=false to override.");
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
      priority, line_number, tarif_zone, static_intro, scrolling_message, 
      next_message_hint, datetime_added, duration_seconds, source_info
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);
  )SQL";

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(this->db_));
    return false;
  }

  yield();  // Allow watchdog to reset after prepare
  esp_task_wdt_reset();  // Reset watchdog timer

  // Bind parameters with the safe ASCII versions
  sqlite3_bind_int(stmt, 1, priority);
  sqlite3_bind_int(stmt, 2, line_number);
  sqlite3_bind_int(stmt, 3, tarif_zone);
  sqlite3_bind_text(stmt, 4, safe_static_intro.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, safe_scrolling_message.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, safe_next_message_hint.c_str(), -1, SQLITE_STATIC);
  
  // Get current time (timestamp) and log it for debugging
  time_t now = time(nullptr);
  ESP_LOGI(TAG, "Current timestamp: %lld", (long long)now);
  sqlite3_bind_int64(stmt, 7, now);

  if (duration_seconds > 0) {
    ESP_LOGI(TAG, "Message will expire at timestamp: %lld", (long long)(now + duration_seconds));
    sqlite3_bind_int(stmt, 8, duration_seconds);
  } else {
    sqlite3_bind_null(stmt, 8);
  }

  if (!safe_source_info.empty()) {
    sqlite3_bind_text(stmt, 9, safe_source_info.c_str(), -1, SQLITE_STATIC);
  } else {
    sqlite3_bind_null(stmt, 9);
  }

  yield();  // Allow watchdog to reset after binding params
  esp_task_wdt_reset();  // Reset watchdog timer

  rc = sqlite3_step(stmt);

  yield();  // Allow watchdog to reset after step
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

  ESP_LOGD(TAG, "[STEP 1] Starting get_active_persistent_messages");
  
  // Prepare the query to get all active messages
  const char *query = R"SQL(
    SELECT message_id, priority, line_number, tarif_zone, static_intro, 
           scrolling_message, next_message_hint, datetime_added, duration_seconds
    FROM messages
    WHERE
      is_enabled = 1
      AND (
        duration_seconds IS NULL
        OR (datetime_added + duration_seconds) > strftime('%s', 'now')
      )
    ORDER BY
      priority DESC,
      message_id ASC;
  )SQL";

  ESP_LOGD(TAG, "[STEP 2] Executing query to get active messages");
  esp_task_wdt_reset();  // Reset watchdog timer before query
  
  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  ESP_LOGD(TAG, "[STEP 3] Prepare statement result: %d", rc);
  
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(this->db_));
    return messages;  // Return empty vector
  }

  // Fetch and process each row
  int count = 0;
  ESP_LOGD(TAG, "[STEP 4] Starting to fetch messages from database");
  esp_task_wdt_reset();  // Reset watchdog timer before fetching rows
  
  int step_result;
  int step_count = 0;
  unsigned long step_start_time = millis();
  while ((step_result = sqlite3_step(stmt)) == SQLITE_ROW) {
    step_count++;
    unsigned long current_step_time = millis() - step_start_time;
    if (step_count == 1 || step_count % 5 == 0) {
      ESP_LOGD(TAG, "[STEP 5] Processing row %d (step took %lu ms)", 
               count+1, current_step_time);
      step_start_time = millis(); // Reset timer for next batch
    }
    esp_task_wdt_reset();  // Reset watchdog timer for each row
    
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

    entry->last_display_time = 0;  // Never displayed yet

    // Calculate expiry time if duration is set
    if (duration_seconds > 0) {
      entry->expiry_time = added_time + duration_seconds;
    }
    
    ESP_LOGD(TAG, "Loaded message ID=%d, Priority=%d, Line=%d, Zone=%d, Text='%s%s' (len=%zu)", 
             entry->message_id, entry->priority, entry->line_number, entry->tarif_zone,
             entry->scrolling_message.substr(0, 30).c_str(), 
             entry->scrolling_message.length() > 30 ? "..." : "",
             entry->scrolling_message.length());

    messages.push_back(entry);
    count++;
    
    // Reset watchdog timer periodically during long operations
    if (count % 5 == 0) {
      yield();
      esp_task_wdt_reset();
    }
  }

  ESP_LOGD(TAG, "[STEP 6] Finished loop with step_result=%d (SQLITE_DONE=%d)", 
           step_result, SQLITE_DONE);
  
  // Properly handle non-SQLITE_DONE result
  if (step_result != SQLITE_DONE) {
    ESP_LOGE(TAG, "SQLite step error: %s", sqlite3_errmsg(this->db_));
  }

  ESP_LOGD(TAG, "[STEP 7] About to finalize statement");
  sqlite3_finalize(stmt);
  
  ESP_LOGD(TAG, "[STEP 8] Statement finalized");
  esp_task_wdt_reset();  // Final watchdog reset after fetching all rows
  
  ESP_LOGI(TAG, "Loaded %d messages from database", count);

  return messages;
}

int B48DatabaseManager::expire_old_messages() {
  const char *query = R"SQL(
    UPDATE messages
    SET is_enabled = 0
    WHERE
      is_enabled = 1
      AND duration_seconds IS NOT NULL
      AND (datetime_added + duration_seconds) <= strftime('%s', 'now');
  )SQL";

  char *err_msg = nullptr;
  int rc = sqlite3_exec(this->db_, query, nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "SQL error during expiry check: %s", err_msg);
    sqlite3_free(err_msg);
    return -1;  // Return -1 to indicate error
  }

  int changes = sqlite3_changes(this->db_);
  if (changes > 0) {
    ESP_LOGI(TAG, "Expired %d messages", changes);
  }

  return changes;  // Return number of expired messages
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
      {36, 48, 101, "Grilovacka", "Grilovacka v Base48 kazdy patek. . . Hackeri a pratele vitani !", "Loading", 0, "SQLiteBootstrap"},
      {36, 48, 101, "Barbecue", "Barbecue at Base48 every Friday. Food, hackers, friends, music, chill.", "Loading", 0, "SQLiteBootstrap"},
      {38, 48, 101, "Uklid", "Udrzujte poradek a cistotu, uklizejte na stolech.", "Cleaning", 0, "SQLiteBootstrap"},
      {38, 48, 101, "Cleanup", "Maintain order and cleanliness, clean the tables.", "Cleaning", 0, "SQLiteBootstrap"},
      {34, 48, 101, "Tech Stack", "Running ESPHome on o. g. ESP32. Messages saved in SQLite on LittleFS. Filesystem Partition 512 KB. Exposes interface to Home Assistant. ASCII messages and DPMB 2005 Firmware.", "UART2_TX_OVERF", 0, "SQLiteBootstrap"},
      {34, 48, 101, "Credits", "Panel and research - Filip. Serial IBIS protocol research by pavlik.space. Initial HW assistance by Vega (vega76.cz). ESP - ESPHome - HA software is C++ vibecoded by Thebys. ", "GOTO 0xBEEF", 0, "SQLiteBootstrap"},
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
    return -1; // Return -1 to indicate error
  }

  sqlite3_stmt *stmt;
  // Change query to only count active, non-expired messages
  const char *query = R"SQL(
    SELECT COUNT(*) FROM messages
    WHERE
      is_enabled = 1
      AND (
        duration_seconds IS NULL
        OR (datetime_added + duration_seconds) > strftime('%s', 'now')
      );
  )SQL";
  
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);

  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare count statement: %s", sqlite3_errmsg(this->db_));
    return -1; // Return -1 to indicate error
  }

  int count = -1; // Default to error value
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    count = sqlite3_column_int(stmt, 0);
  } else {
    ESP_LOGE(TAG, "Failed to execute count statement: %s", sqlite3_errmsg(this->db_));
  }
  
  sqlite3_finalize(stmt); // Finalize the statement
  
  if (count != -1) {
      ESP_LOGD(TAG, "Active message count: %d", count);
  }
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

    ESP_LOGI(TAG, "MSG[%d]: %s, Prio=%d, Line=%d, Zone=%d, Added=%s, Expires=%s", 
             message_id, is_enabled ? "ENABLED" : "disabled", priority, line_number, tarif_zone,
             time_str, expiry_str);
             
    std::string scroll_msg = scrolling_message ? scrolling_message : "";
    ESP_LOGI(TAG, "  Intro: '%s', Message: '%s%s' (len=%zu), Next: '%s', Source: '%s'",
             static_intro ? static_intro : "", 
             scroll_msg.substr(0, 30).c_str(), 
             scroll_msg.length() > 30 ? "..." : "",
             scroll_msg.length(),
             next_hint ? next_hint : "",
             source_info ? source_info : "");

    count++;
    
    // Reset watchdog timer periodically during long operations
    if (count % 5 == 0) {
      yield();
      esp_task_wdt_reset();
    }
  }

  sqlite3_finalize(stmt);
  esp_task_wdt_reset();  // Final watchdog reset
  
  ESP_LOGI(TAG, "======= DUMP COMPLETE: %d TOTAL MESSAGES (%d ENABLED) =======", 
           count, get_message_count());
}

std::string B48DatabaseManager::convert_to_ascii(const std::string &str) {
  std::string result;
  result.reserve(str.length());
  
  for (unsigned char c : str) {
    if (c <= 127) {
      // Already ASCII, pass through
      result += c;
    } else {
      // Non-ASCII, convert based on common mappings
      switch (c) {
        // Czech/Slovak characters
        case 0xC3: case 0xE1: result += 'a'; break; // á
        case 0xC4: case 0xE4: result += 'a'; break; // ä
        case 0xC8: case 0xE8: result += 'c'; break; // č
        case 0xC9: case 0xE9: result += 'e'; break; // é
        case 0xCC: case 0xEC: result += 'i'; break; // í
        case 0xCD: case 0xED: result += 'i'; break; // í
        case 0xF2: result += 'r'; break; // ř
        case 0xF3: result += 's'; break; // š
        case 0xF9: result += 'u'; break; // ů
        case 0xFA: case 0xDA: result += 'u'; break; // ú/Ú
        case 0xFD: result += 'y'; break; // ý
        case 0xFE: result += 'z'; break; // ž
        case 0xD0: case 0xF0: result += 'd'; break; // đ/ð
        case 0xD1: case 0xF1: result += 'n'; break; // ñ/Ñ
        
        // Fallback for other non-ASCII chars
        default: result += '_'; break;
      }
    }
  }
  
  return result;
}

}  // namespace b48_display_controller
}  // namespace esphome