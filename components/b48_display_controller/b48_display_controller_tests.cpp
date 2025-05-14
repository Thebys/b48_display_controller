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

} // namespace b48_display_controller
} // namespace esphome