#include "b48_display_controller.h"
#include "esphome/core/log.h"
#include <stdexcept> // Include for std::exception
#include <functional> // Include for std::function if needed, but pointer works
#include <LittleFS.h> // Include LittleFS header

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48c.test"; // Separate tag for test logs

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

    // --- Add more test method calls here using executeTest ---
    // Example:
    // if (executeTest(&B48DisplayController::testDatabaseConnection, "testDatabaseConnection")) { pass_count++; } else { fail_count++; }

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
    String readContent = file.readStringUntil('\\n'); // Assuming single line write
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