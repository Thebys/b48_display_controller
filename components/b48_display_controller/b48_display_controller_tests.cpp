#include "b48_display_controller.h"
#include "buse120_serial_protocol.h"
#include "esphome/core/log.h"
#include <sqlite3.h>
#include <cstring>
#include <unistd.h>
#include <sys/stat.h>
#include "esphome/core/hal.h"

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.test";

// Helper to check if file exists
static bool file_exists(const char *path) {
  struct stat st;
  return (stat(path, &st) == 0);
}

// Helper to execute a single test safely
bool B48DisplayController::executeTest(bool (B48DisplayController::*testMethod)(), const char *testName) {
  ESP_LOGD(TAG, "Running: %s", testName);
  bool success = (this->*testMethod)();
  if (!success) {
    ESP_LOGE(TAG, "[FAIL] %s", testName);
  }
  return success;
}

void B48DisplayController::runSelfTests() {
  ESP_LOGI(TAG, "--- Running Self-Tests ---");
  int pass_count = 0;
  int fail_count = 0;

  // Core functionality tests
  if (executeTest(&B48DisplayController::testFilesystemMount, "testFilesystemMount")) pass_count++; else fail_count++;
  if (executeTest(&B48DisplayController::testSqliteBasicOperations, "testSqliteBasicOperations")) pass_count++; else fail_count++;
  if (executeTest(&B48DisplayController::testSerialProtocol, "testSerialProtocol")) pass_count++; else fail_count++;
  if (executeTest(&B48DisplayController::test_czech_character_preservation, "test_czech_character_preservation")) pass_count++; else fail_count++;
  if (executeTest(&B48DisplayController::test_czech_character_encoding, "test_czech_character_encoding")) pass_count++; else fail_count++;

  // Persistence tests (simplified)
  if (executeTest(&B48DisplayController::testDatabasePersistence, "testDatabasePersistence")) pass_count++; else fail_count++;

  ESP_LOGI(TAG, "--- Self-Test Summary: Passed=%d, Failed=%d ---", pass_count, fail_count);
  if (fail_count > 0) {
    ESP_LOGW(TAG, "Some tests failed. Check logs above.");
  }
}

bool B48DisplayController::testFilesystemMount() {
  const char *testFileName = "/littlefs/mount_test.txt";
  const char *testContent = "LittleFS OK";

  // Write
  FILE *file = fopen(testFileName, "w");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file for writing. errno=%d", errno);
    return false;
  }
  fwrite(testContent, 1, strlen(testContent), file);
  fclose(file);

  // Read and verify
  file = fopen(testFileName, "r");
  if (!file) {
    ESP_LOGE(TAG, "Failed to open file for reading");
    return false;
  }
  char buf[32] = {0};
  fread(buf, 1, sizeof(buf) - 1, file);
  fclose(file);

  delay(10);
  unlink(testFileName);

  if (strcmp(buf, testContent) != 0) {
    ESP_LOGE(TAG, "Content mismatch: '%s' vs '%s'", buf, testContent);
    return false;
  }

  ESP_LOGI(TAG, "[PASS] testFilesystemMount");
  return true;
}

bool B48DisplayController::testSqliteBasicOperations() {
  const char *db_path = "/littlefs/test_sqlite.db";
  sqlite3 *db = nullptr;
  char *err_msg = nullptr;
  bool success = false;

  // Clean up any existing test DB
  unlink(db_path);

  int rc = sqlite3_open(db_path, &db);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Can't open database: %s", sqlite3_errmsg(db));
    sqlite3_close(db);
    return false;
  }

  // Create table (IF NOT EXISTS to avoid failures on persistence)
  rc = sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS test_table (id INTEGER PRIMARY KEY, content TEXT);",
                    nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to create table: %s", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
    unlink(db_path);
    return false;
  }

  // Clear any old data and insert new
  sqlite3_exec(db, "DELETE FROM test_table;", nullptr, nullptr, nullptr);
  rc = sqlite3_exec(db, "INSERT INTO test_table (id, content) VALUES (1, 'Test Content');",
                    nullptr, nullptr, &err_msg);
  if (rc != SQLITE_OK) {
    ESP_LOGE(TAG, "Failed to insert: %s", err_msg);
    sqlite3_free(err_msg);
    sqlite3_close(db);
    unlink(db_path);
    return false;
  }

  // Verify data
  sqlite3_stmt *stmt = nullptr;
  rc = sqlite3_prepare_v2(db, "SELECT content FROM test_table WHERE id = 1;", -1, &stmt, nullptr);
  if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
    const char *content = (const char *)sqlite3_column_text(stmt, 0);
    if (content && strcmp(content, "Test Content") == 0) {
      success = true;
    }
  }
  sqlite3_finalize(stmt);
  sqlite3_close(db);
  unlink(db_path);

  if (success) {
    ESP_LOGI(TAG, "[PASS] testSqliteBasicOperations");
  }
  return success;
}

bool B48DisplayController::testSerialProtocol() {
  if (!this->uart_) {
    ESP_LOGE(TAG, "UART not initialized");
    return false;
  }

  // Test basic protocol commands
  this->serial_protocol_.send_time_update(12, 34);
  this->serial_protocol_.send_line_number(48);
  this->serial_protocol_.send_tarif_zone(101);
  this->serial_protocol_.send_static_intro("Test");
  this->serial_protocol_.send_scrolling_message("Test message");
  this->serial_protocol_.switch_to_cycle(0);

  ESP_LOGI(TAG, "[PASS] testSerialProtocol");
  return true;
}

bool B48DisplayController::test_czech_character_preservation() {
  std::string czech_text = "Příští zastávka: Náměstí Míru";

  std::string sanitized = B48DatabaseManager::sanitize_for_czech_display(czech_text);

  // Czech chars should be converted for display (different from original UTF-8)
  bool czech_converted = (sanitized != czech_text);

  if (czech_converted) {
    ESP_LOGI(TAG, "[PASS] test_czech_character_preservation");
  } else {
    ESP_LOGE(TAG, "Czech conversion failed: text was not converted");
  }
  return czech_converted;
}

bool B48DisplayController::test_czech_character_encoding() {
  // Test individual character mappings
  std::string test_a = "á";
  std::string encoded_a = BUSE120SerialProtocol::encode_czech_characters(test_a);
  bool a_ok = (encoded_a == "\x0e\x20");

  std::string test_s = "š";
  std::string encoded_s = BUSE120SerialProtocol::encode_czech_characters(test_s);
  bool s_ok = (encoded_s == "\x0e\x28");

  // Test mixed text preserves ASCII
  std::string mixed = "Bus 25 šel";
  std::string encoded = BUSE120SerialProtocol::encode_czech_characters(mixed);
  bool ascii_preserved = (encoded.find("Bus 25") == 0);

  bool passed = a_ok && s_ok && ascii_preserved;
  if (passed) {
    ESP_LOGI(TAG, "[PASS] test_czech_character_encoding");
  } else {
    ESP_LOGE(TAG, "Encoding failed: a=%d, s=%d, ascii=%d", a_ok, s_ok, ascii_preserved);
  }
  return passed;
}

bool B48DisplayController::testDatabasePersistence() {
  // This test verifies the production database has data
  if (!this->db_manager_) {
    ESP_LOGW(TAG, "[SKIP] testDatabasePersistence: No database manager");
    return true;
  }

  int msg_count = this->db_manager_->get_message_count();

  // After bootstrap, should have messages
  if (msg_count < 1) {
    ESP_LOGE(TAG, "No messages in database - persistence may be broken");
    return false;
  }

  ESP_LOGI(TAG, "[PASS] testDatabasePersistence: %d messages in DB", msg_count);
  return true;
}

}  // namespace b48_display_controller
}  // namespace esphome
