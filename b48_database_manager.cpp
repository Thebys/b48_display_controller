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
  // Assuming sqlite3_shutdown() is handled globally if needed, not here.
}

bool B48DatabaseManager::initialize() {
  ESP_LOGD(TAG, "Initializing database manager for path: %s", this->database_path_.c_str());

  // Note: Assuming sqlite3_initialize() is called globally once elsewhere (e.g., main setup)
  // If not, it should be called here or managed carefully.
  // if (sqlite3_initialize() != SQLITE_OK) {
  //     ESP_LOGE(TAG, "Failed to initialize SQLite library.");
  //     return false;
  // }

  int rc = sqlite3_open(this->database_path_.c_str(), &this->db_);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to open database at '%s': %s", this->database_path_.c_str(), sqlite3_errmsg(this->db_));
    sqlite3_close(this->db_);  // Close even if open failed partially
    this->db_ = nullptr;
    return false;
  }
  ESP_LOGI(TAG, "Successfully opened database connection at '%s'", this->database_path_.c_str());

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

  if (!check_and_create_schema()) {
    ESP_LOGE(TAG, "Failed to verify or create database schema.");
    sqlite3_close(this->db_);
    this->db_ = nullptr;
    return false;
  }

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
                                                const std::string &source_info) {
  yield();  // Allow watchdog to reset before operation starts

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

  // Bind parameters
  sqlite3_bind_int(stmt, 1, priority);
  sqlite3_bind_int(stmt, 2, line_number);
  sqlite3_bind_int(stmt, 3, tarif_zone);
  sqlite3_bind_text(stmt, 4, static_intro.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 5, scrolling_message.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_text(stmt, 6, next_message_hint.c_str(), -1, SQLITE_STATIC);
  sqlite3_bind_int64(stmt, 7, time(nullptr));

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

  yield();  // Allow watchdog to reset after binding params

  rc = sqlite3_step(stmt);

  yield();  // Allow watchdog to reset after step

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

  sqlite3_stmt *stmt;
  int rc = sqlite3_prepare_v2(this->db_, query, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to prepare statement: %s", sqlite3_errmsg(this->db_));
    return messages;  // Return empty vector
  }

  // Fetch and process each row
  int count = 0;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
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

    messages.push_back(entry);
    count++;
  }

  sqlite3_finalize(stmt);
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
      {40, 48, 621, "Barbecue", "Barbecue v Base48 kazdy patek. Hackeri a pratele vitani.", "Loading", 0, "SQLiteBootstrap"},
      {40, 48, 621, "Barbecue", "Barbecue at Base48 every Friday. Hackers and friends grilling.", "Loading", 0, "SQLiteBootstrap"},
      {38, 48, 48, "Uklid", "Udrzujte poradek a cistotu, uklizejte na stolech.", "Cleaning", 0, "SQLiteBootstrap"},
      {38, 48, 48, "Cleanup", "Maintain order and cleanliness, clean the tables.", "Cleaning", 0, "SQLiteBootstrap"},
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
                               msg.next_message_hint, msg.duration_seconds, msg.source_info);

    esp_task_wdt_reset();  // Reset watchdog timer
  }

  if (!success) {
    ESP_LOGE(TAG, "Failed to add one or more default messages during bootstrap.");
    return false;
  }

  ESP_LOGI(TAG, "Successfully bootstrapped default messages.");
  return true;
}

}  // namespace b48_display_controller
}  // namespace esphome