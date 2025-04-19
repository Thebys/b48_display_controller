#include "b48_display_controller.h"
#include "esphome/core/log.h"
#include <stdexcept>        // Include for std::exception
#include <functional>       // Include for std::function if needed, but pointer works
#include <LittleFS.h>       // Include LittleFS header
#include <sqlite3.h>        // Include SQLite3 header
#include <vector>           // Include for std::vector
#include <string>           // Include for std::string
#include <Arduino.h>        // For delay() and yield()
#include <esp_task_wdt.h>   // For esp_task_wdt_reset()

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.test"; // Separate tag for test logs

// Helper structure to hold data expected from the callback
struct TestSqliteCallbackData {
    bool row_found = false;
    int expected_id = -1;
    std::string expected_content;
};

// Simple callback function for the SQLite test
static int testSqliteCallback(void *data, int argc, char **argv, char **azColName) {
    TestSqliteCallbackData* test_data = static_cast<TestSqliteCallbackData*>(data);
    test_data->row_found = true; // Mark that we got at least one row

    ESP_LOGD(TAG, "Callback: %d columns", argc);
    for (int i = 0; i < argc; i++){
        ESP_LOGD(TAG, "  %s = %s", azColName[i], argv[i] ? argv[i] : "NULL");
        // Basic validation: Check if column name matches and value matches expected
        if (strcmp(azColName[i], "id") == 0) {
            if (argv[i] && std::stoi(argv[i]) != test_data->expected_id) {
                ESP_LOGE(TAG, "[TEST][FAIL] SQLite Callback: ID mismatch. Expected %d, Got %s", test_data->expected_id, argv[i]);
                return 1; // Signal error
            }
        } else if (strcmp(azColName[i], "content") == 0) {
             if (!argv[i] || test_data->expected_content != argv[i]) {
                ESP_LOGE(TAG, "[TEST][FAIL] SQLite Callback: Content mismatch. Expected '%s', Got '%s'", test_data->expected_content.c_str(), argv[i] ? argv[i] : "NULL");
                return 1; // Signal error
            }
        }
    }
    return 0; // Success
}

// Helper function to execute a single test safely
bool B48DisplayController::executeTest(bool (B48DisplayController::*testMethod)(), const char* testName) {
    ESP_LOGD(TAG, "Running test: %s", testName);
    bool success = false;
    try {
        // Call the member function pointer on the current object
        success = (this->*testMethod)(); 
        if (!success) {
            // Specific failure logging is expected within the test method itself if it returns false
            // We log a generic failure marker here in case the test didn't log.
            ESP_LOGE(TAG, "[FAIL] %s reported failure.", testName);
        }
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "[CRASH] %s threw an exception: %s", testName, e.what());
        success = false;
    } catch (...) {
        ESP_LOGE(TAG, "[CRASH] %s threw an unknown exception.", testName);
        success = false;
    }
    return success;
}

void B48DisplayController::runSelfTests() {
    ESP_LOGI(TAG, "--- Running Self-Tests ---");
    int pass_count = 0;
    int fail_count = 0;

    // --- Test Suite ---

    // Use the helper function for each test
    if (executeTest(&B48DisplayController::testLittleFSMount, "testLittleFSMount")) {
        pass_count++;
    } else {
        fail_count++;
    }

    // Add the new SQLite test
    if (executeTest(&B48DisplayController::testSqliteBasicOperations, "testSqliteBasicOperations")) {
        pass_count++;
    } else {
        fail_count++;
    }
    
    // Add the Serial Protocol test
    if (executeTest(&B48DisplayController::testSerialProtocol, "testSerialProtocol")) {
        pass_count++;
    } else {
        fail_count++;
    }

    // --- Test Summary ---
    ESP_LOGI(TAG, "--- Self-Test Summary --- Passed: %d, Failed: %d ---", pass_count, fail_count);

    if (fail_count > 0) {
        ESP_LOGW(TAG, "One or more self-tests failed or crashed. Check logs above.");
        // Optionally: Add logic to handle failures
        // this->mark_failed(); 
    }
}

bool B48DisplayController::testLittleFSMount() {
    ESP_LOGD(TAG, "Starting LittleFS basic I/O test...");
    const char* testFileName = "/littlefs_test.txt";
    const char* testContent = "Hello LittleFS!";

    // 1. Check if mounted (redundant if begin() is called in setup, but good practice)
    // Note: ESP-IDF LittleFS doesn't have a direct isMounted(). We rely on begin() success in setup().
    // We'll proceed assuming it was mounted successfully in setup.

    // 2. Create and write to a file
    File file = LittleFS.open(testFileName, "w");
    if (!file) {
        ESP_LOGE(TAG, "[TEST][FAIL] LittleFS: Failed to open file '%s' for writing.", testFileName);
        return false;
    }
    if (file.print(testContent) != strlen(testContent)) {
        ESP_LOGE(TAG, "[TEST][FAIL] LittleFS: Failed to write complete content to '%s'.", testFileName);
        file.close();
        LittleFS.remove(testFileName); // Cleanup attempt
        return false;
    }
    file.close();
    ESP_LOGD(TAG, "LittleFS: Successfully wrote to '%s'.", testFileName);

    // 3. Read from the file and verify content
    file = LittleFS.open(testFileName, "r");
    if (!file) {
        ESP_LOGE(TAG, "[TEST][FAIL] LittleFS: Failed to open file '%s' for reading.", testFileName);
        LittleFS.remove(testFileName); // Cleanup attempt
        return false;
    }
    String readContent = file.readStringUntil('\n'); // Assuming single line write
    file.close();

    if (readContent != testContent) {
        ESP_LOGE(TAG, "[TEST][FAIL] LittleFS: Read content ('%s') does not match written content ('%s') in '%s'.", readContent.c_str(), testContent, testFileName);
        LittleFS.remove(testFileName); // Cleanup attempt
        return false;
    }
    ESP_LOGD(TAG, "LittleFS: Successfully read and verified content from '%s'.", testFileName);

    // 4. Delete the file
    if (!LittleFS.remove(testFileName)) {
        ESP_LOGE(TAG, "[TEST][FAIL] LittleFS: Failed to remove test file '%s'.", testFileName);
        return false; // Fail the test if cleanup fails
    }
    ESP_LOGD(TAG, "LittleFS: Successfully removed test file '%s'.", testFileName);

    ESP_LOGD(TAG, "LittleFS basic I/O test PASSED.");
    return true;
}

// New test for basic SQLite operations
bool B48DisplayController::testSqliteBasicOperations() {
    ESP_LOGD(TAG, "Starting SQLite basic operations test...");

    // Initialize SQLite library - REMOVED: Assume global init if needed
    // if (sqlite3_initialize() != SQLITE_OK) {
    //     ESP_LOGE(TAG, "[TEST][FAIL] SQLite: Failed to initialize library.");
    //     return false;
    // }

    const char* dbFilenameRelative = "/test_sqlite.db";     // Path relative to LittleFS root (starts with /)
    const char* littleFsBasePath = "/littlefs";         // Base path where LittleFS is mounted
    std::string fullDbPath = std::string(littleFsBasePath) + dbFilenameRelative; // Path for sqlite3_open

    sqlite3 *test_db = nullptr;
    char *err_msg = nullptr;
    int rc;
    bool success = false; // Assume failure initially

    // Ensure test DB doesn't exist using the path relative to LittleFS root
    ESP_LOGD(TAG, "SQLite Test: Checking/Removing existing file using relative path: %s", dbFilenameRelative);
    if (LittleFS.exists(dbFilenameRelative)) {
        ESP_LOGD(TAG, "SQLite Test: Removing existing test database using relative path: %s", dbFilenameRelative);
        if (!LittleFS.remove(dbFilenameRelative)) {
            ESP_LOGE(TAG, "[TEST][FAIL] SQLite: Failed to remove existing test database '%s'. Cannot proceed.", dbFilenameRelative);
            return false; // Early exit if cleanup fails
        }
    } else {
        ESP_LOGD(TAG, "SQLite Test: File '%s' does not exist.", dbFilenameRelative);
    }

    // 1. Open the database using the full path
    ESP_LOGD(TAG, "SQLite Test: Opening database using full path '%s'", fullDbPath.c_str());
    rc = sqlite3_open(fullDbPath.c_str(), &test_db);
    if (rc != SQLITE_OK) {
        ESP_LOGE(TAG, "[TEST][FAIL] SQLite: Can't open database '%s': %s", fullDbPath.c_str(), sqlite3_errmsg(test_db));
    } else {
        ESP_LOGD(TAG, "SQLite Test: Database opened successfully.");

        // 2. Create a table
        ESP_LOGD(TAG, "SQLite Test: Creating table 'test_table'");
        const char *sql_create = "CREATE TABLE test_table (id INTEGER PRIMARY KEY, content TEXT);";
        rc = sqlite3_exec(test_db, sql_create, nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            ESP_LOGE(TAG, "[TEST][FAIL] SQLite: Failed to create table: %s", err_msg);
            sqlite3_free(err_msg);
        } else {
            ESP_LOGD(TAG, "SQLite Test: Table created successfully.");

            // 3. Insert data
            ESP_LOGD(TAG, "SQLite Test: Inserting data");
            const char *sql_insert = "INSERT INTO test_table (id, content) VALUES (1, 'Test Content');";
            rc = sqlite3_exec(test_db, sql_insert, nullptr, nullptr, &err_msg);
            if (rc != SQLITE_OK) {
                ESP_LOGE(TAG, "[TEST][FAIL] SQLite: Failed to insert data: %s", err_msg);
                sqlite3_free(err_msg);
            } else {
                ESP_LOGD(TAG, "SQLite Test: Data inserted successfully.");

                // 4. Select and verify data
                ESP_LOGD(TAG, "SQLite Test: Selecting data for verification");
                const char *sql_select = "SELECT id, content FROM test_table WHERE id = 1;";
                TestSqliteCallbackData callback_data;
                callback_data.expected_id = 1;
                callback_data.expected_content = "Test Content";

                rc = sqlite3_exec(test_db, sql_select, testSqliteCallback, &callback_data, &err_msg);
                if (rc != SQLITE_OK) {
                    ESP_LOGE(TAG, "[TEST][FAIL] SQLite: Failed to select data: %s", err_msg);
                    sqlite3_free(err_msg);
                } else if (!callback_data.row_found) {
                    ESP_LOGE(TAG, "[TEST][FAIL] SQLite: No rows returned from select query.");
                } else {
                    // Further checks are done inside the callback
                    ESP_LOGD(TAG, "SQLite Test: Data selection and verification successful.");
                    // If all nested steps succeeded, mark the test as successful
                    success = true;
                }
            }
        }
    }

    // 5. Close the database (always attempt, sqlite3_close handles nullptr)
    ESP_LOGD(TAG, "SQLite Test: Closing database.");
    sqlite3_close(test_db);

    // 6. Remove the test database file using the path relative to LittleFS root
    ESP_LOGD(TAG, "SQLite Test: Removing test database file using relative path '%s'.", dbFilenameRelative);
    if (LittleFS.exists(dbFilenameRelative)) {
        if (!LittleFS.remove(dbFilenameRelative)) {
            ESP_LOGW(TAG, "SQLite Test: Failed to remove test database file '%s'.", dbFilenameRelative);
        } else {
            ESP_LOGD(TAG, "SQLite Test: Test database file '%s' removed.", dbFilenameRelative);
        }
    } else {
        ESP_LOGD(TAG, "SQLite Test: Test database file '%s' did not exist or was already removed.", dbFilenameRelative);
    }

    if (success) {
        ESP_LOGD(TAG, "SQLite basic operations test PASSED.");
    } else {
         ESP_LOGE(TAG, "SQLite basic operations test FAILED.");
    }

    // Shutdown SQLite library
    sqlite3_shutdown();
    ESP_LOGD(TAG, "SQLite Test: Library shutdown.");

    return success;
}

// Test for the serial protocol implementation
bool B48DisplayController::testSerialProtocol() {
    ESP_LOGD(TAG, "Starting Serial Protocol test...");
    
    if (!this->uart_) {
        ESP_LOGE(TAG, "[TEST][FAIL] Serial Protocol: UART not initialized for test");
        return false;
    }
    
    // Log the start of the test
    ESP_LOGD(TAG, "Serial Protocol: Testing BUSE120 protocol commands");
    
    try {
        // Send time update as a test command
        time_t now = time(nullptr);
        struct tm *timeinfo = localtime(&now);
        
        // Test time update command
        ESP_LOGD(TAG, "Serial Protocol: Testing time update command");
        this->serial_protocol_.send_time_update(01, 23);
        // Test line number command
        ESP_LOGD(TAG, "Serial Protocol: Testing line number command");
        this->serial_protocol_.send_line_number(48);
        // Test tarif zone command
        ESP_LOGD(TAG, "Serial Protocol: Testing tarif zone command");
        this->serial_protocol_.send_tarif_zone(101);
        // test Intro message command
        ESP_LOGD(TAG, "Serial Protocol: Testing emergency message command");
        this->serial_protocol_.send_static_intro("Selftest");
        // Set Scroll message
        ESP_LOGD(TAG, "Serial Protocol: Testing scroll message command");
        this->serial_protocol_.send_scrolling_message("Scrolling longer text message...   ");
        // Test cycle switch command
        ESP_LOGD(TAG, "Serial Protocol: Testing cycle switch command");
        this->serial_protocol_.switch_to_cycle(0);
        // Test invert command
        ESP_LOGD(TAG, "Serial Protocol: Testing invert command");
        this->serial_protocol_.send_invert_command();
        
        ESP_LOGD(TAG, "Serial Protocol test PASSED.");
        return true;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "[TEST][FAIL] Serial Protocol: Exception during test: %s", e.what());
        return false;
    } catch (...) {
        ESP_LOGE(TAG, "[TEST][FAIL] Serial Protocol: Unknown exception during test");
        return false;
    }
}

// Add the time test mode methods:

void B48DisplayController::start_time_test_mode() {
  if (this->time_test_mode_active_) {
    ESP_LOGW(TAG, "Time test mode already active");
    return;
  }

  ESP_LOGI(TAG, "Starting time test mode");
  this->time_test_mode_active_ = true;
  this->current_time_test_value_ = 0;
  this->last_time_test_update_ = millis();
  this->state_ = TIME_TEST_MODE;
  this->state_change_time_ = millis();

  // Send intro message
  auto test_msg = std::make_shared<MessageEntry>();
  test_msg->message_id = -1;
  test_msg->line_number = 99;
  test_msg->tarif_zone = 999;
  test_msg->static_intro = "Time Test";
  test_msg->scrolling_message = "Testing time values from u0000 to u2459";
  test_msg->next_message_hint = "Testing";
  test_msg->priority = 100;
  send_commands_for_message(test_msg);

  // Prepare for first time value
  switch_to_cycle(6);  // Make sure we're in the main cycle
}

void B48DisplayController::stop_time_test_mode() {
  if (!this->time_test_mode_active_) {
    ESP_LOGW(TAG, "Time test mode not active");
    return;
  }

  ESP_LOGI(TAG, "Stopping time test mode");
  this->time_test_mode_active_ = false;

  // Return to normal operation
  this->state_ = TRANSITION_MODE;
  this->state_change_time_ = millis();

  // Send completion message
  auto completion_msg = std::make_shared<MessageEntry>();
  completion_msg->message_id = -1;
  completion_msg->line_number = 48;
  completion_msg->tarif_zone = 0;
  completion_msg->static_intro = "Test Done";
  completion_msg->scrolling_message = "Time test complete. Returning to normal operation.";
  completion_msg->next_message_hint = "Normal";
  completion_msg->priority = 100;
  send_commands_for_message(completion_msg);
}

void B48DisplayController::run_time_test_mode() {
  // Check if we should stop the test
  if (!this->time_test_mode_active_) {
    ESP_LOGW(TAG, "Time test mode flag is false, stopping");
    this->state_ = TRANSITION_MODE;
    this->state_change_time_ = millis();
    return;
  }

  // Check if we completed all time values
  if (this->current_time_test_value_ > 2459) {
    ESP_LOGI(TAG, "Time test complete, all values sent");
    stop_time_test_mode();
    return;
  }

  // Send time update at regular intervals
  unsigned long now = millis();
  if (now - this->last_time_test_update_ >= TIME_TEST_INTERVAL_MS) {
    // Calculate current time value (hour and minute)
    int hour = this->current_time_test_value_ / 100;
    int minute = this->current_time_test_value_ % 100;

    // Log the test progress periodically
    if (this->current_time_test_value_ % 100 == 0) {
      ESP_LOGI(TAG, "Time test progress: u%04d", this->current_time_test_value_);
    } else {
      ESP_LOGD(TAG, "Time test: u%04d", this->current_time_test_value_);
    }

    // Send the time value directly
    this->serial_protocol_.send_time_update(hour, minute);
    switch_to_cycle(0);
    switch_to_cycle(6);
    yield();
    esp_task_wdt_reset();

    // Increment the value for next iteration
    this->current_time_test_value_++;

    // Update the last update time
    this->last_time_test_update_ = now;
  }
}



// --- Add implementations for other test methods here ---
// bool B48DisplayController::testDatabaseConnection() {
//     ESP_LOGD(TAG, "Checking database connection...");
//     if (!this->db_) {
//         ESP_LOGE(TAG, "[TEST][FAIL] Database pointer is null.");
//         return false;
//     }
//     // Add more sophisticated checks if needed
//     ESP_LOGD(TAG, "Database connection appears ok.");
//     return true;
// }

} // namespace b48_display_controller
} // namespace esphome