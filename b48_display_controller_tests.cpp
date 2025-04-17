#include "b48_display_controller.h"
#include "esphome/core/log.h"
#include <stdexcept> // Include for std::exception
#include <functional> // Include for std::function if needed, but pointer works

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
    if (executeTest(&B48DisplayController::testAlwaysPasses, "testAlwaysPasses")) {
        pass_count++;
    } else {
        fail_count++;
    }

    if (executeTest(&B48DisplayController::testAlwaysFails, "testAlwaysFails")) {
        // This test is expected to fail, so passing is a warning
        pass_count++; 
        ESP_LOGW(TAG, "[WARN] testAlwaysFails completed successfully (unexpected). Check test logic.");
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

bool B48DisplayController::testAlwaysPasses() {
    // Keep logs clean for passing tests.
    return true;
}

bool B48DisplayController::testAlwaysFails() {
    // Log failure reason here
    ESP_LOGE(TAG, "[TEST][FAIL] testAlwaysFails: Failing intentionally as designed.");
    return false;
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