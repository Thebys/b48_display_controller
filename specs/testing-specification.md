---
description: Defines the lightweight self-testing approach for the b48_display_controller component
globs: 
alwaysApply: true
---
# B48 Display Controller Self-Testing Specification
**File:** `testing-specification.md`  
**Date:** April 17, 2025  
**Version:** 1.1

## 1. Goals and Objectives
This specification outlines a simple self-testing approach for the `b48_display_controller` external component with the following objectives:

* Validate core functionality directly on the ESP32 device during startup.
* Catch common configuration or integration errors early.
* Provide basic diagnostic output through serial logs if tests fail.

## 2. Testing Approach
The strategy involves running a set of internal self-test methods when the component initializes, if enabled via configuration. These tests perform basic checks on essential functions like database access and UART communication.

### 2.1 Test Categories (Examples)
* **Database Checks:** Verify database file accessibility and basic query execution.
* **Hardware Checks:** Confirm UART interface is available.
* **Configuration Checks:** Validate critical configuration parameters.

## 3. Test Execution

### 3.1 Automatic Execution on Startup
Tests are executed within the component's `setup()` or `loop()` methods if the `run_tests_on_startup` configuration flag is set to `true`.

```cpp
// Example conceptual placement in component code
void B48DisplayController::setup() {
  // ... regular setup ...
  
  // Run self-tests if enabled in YAML
  if (this->run_tests_on_startup_) {
    runSelfTests(); 
  }
  
  // ... continue with normal operation ...
}

void B48DisplayController::runSelfTests() {
  ESP_LOGI(TAG, "Running self-tests...");
  bool success = true;

  // Example test calls
  if (!testDatabaseConnection()) {
      ESP_LOGE(TAG, "Self-test failed: Database connection");
      success = false;
  }
  if (!testUartCommunication()) {
      ESP_LOGE(TAG, "Self-test failed: UART communication");
      success = false;
  }
  
  if (success) {
      ESP_LOGI(TAG, "All self-tests passed.");
  } else {
      ESP_LOGW(TAG, "One or more self-tests failed. Check logs for details.");
      // Optional: Enter a safe mode or halt further operations
  }
}
```

### 3.2 Enabling Tests
Enable self-testing via the ESPHome YAML configuration:

```yaml
# In your ESPHome YAML configuration
b48_display_controller:
  # ... other configuration ...
  run_tests_on_startup: true # Default is false
```

## 4. Test Implementation
Self-tests are implemented as private methods within the `B48DisplayController` C++ class. They should be simple, return a boolean indicating success/failure, and log diagnostic information, especially on failure.

```cpp
// Example test method structure within the component's .cpp file
bool B48DisplayController::testDatabaseConnection() {
    // Logic to check database accessibility
    // ...
    if (/* check fails */) {
        return false;
    }
    return true;
}
```

## 5. Considerations
- Keep self-tests lightweight to minimize impact on startup time.
- Focus tests on critical initialization steps and basic functionality checks.
- Provide clear log messages for failures to aid debugging.
- The `run_tests_on_startup` flag should default to `false` for typical production use. 