---
description: Defines the lightweight testing approach for the b48_display_controller component
globs: 
alwaysApply: true
---
# B48 Display Controller Testing Specification
**File:** `testing-specification.md`  
**Date:** April 17, 2025  
**Version:** 1.0

## 1. Goals and Objectives
This specification defines a simple yet effective testing approach for the `b48_display_controller` external component with the following objectives:

* Validate core functionality directly on the ESP32 device
* Catch common errors through automated self-tests
* Prevent regressions during component evolution
* Provide clear diagnostic output through serial logs

## 2. Testing Approach
The testing strategy uses the AUnit framework to run lightweight tests directly on the ESP32. Tests automatically execute at component initialization, with results displayed in the serial log.

### 2.1 Test Categories
| Category | Description |
|----------|-------------|
| **Database Tests** | Validate database operations (create, read, migration) |
| **Scheduler Tests** | Verify message selection and prioritization logic |
| **Command Tests** | Confirm correct command building and checksums |
| **Hardware Integration** | Basic interfacing with UART and filesystem |

## 3. Implementation Details

### 3.1 Directory Structure
```
components/
└─ b48_display_controller/
   ├─ src/
   │  └─ ...
   └─ test/
      ├─ test_runner.h      # Test initialization and execution
      ├─ test_db.h          # Database operation tests
      ├─ test_scheduler.h   # Message scheduling tests
      └─ test_commands.h    # Command building tests
```

### 3.2 Core Test Cases

#### 3.2.1 Database Tests
| ID | Test Name | Description |
|----|-----------|-------------|
| **DB-001** | `CreateEmptyDB` | Verify empty database creation |
| **DB-002** | `FilterInactiveRecords` | Confirm disabled records are filtered |
| **DB-003** | `CorruptionHandling` | Test recovery from corruption |

#### 3.2.2 Scheduler Tests
| ID | Test Name | Description |
|----|-----------|-------------|
| **SCH-001** | `PrioritySelection` | Verify higher priority messages selected first |
| **SCH-002** | `EmergencyPreemption` | Confirm emergency messages bypass queue |
| **SCH-003** | `RecentlyShownRules` | Test anti-repetition logic |

#### 3.2.3 Command Tests
| ID | Test Name | Description |
|----|-----------|-------------|
| **CMD-001** | `ChecksumCalculation` | Verify checksum algorithm |
| **CMD-002** | `CommandFormatting` | Test command string formatting |

## 4. Test Execution

### 4.1 Automatic Execution
Tests automatically run at component initialization:

```cpp
// In component setup method
void B48DisplayController::setup() {
  // Regular setup
  setupUart();
  setupDatabase();
  
  // Run tests if test mode enabled
  #ifdef B48_TEST_MODE
    runDiagnosticTests();
  #endif
  
  // Continue with normal operation
  startMessageLoop();
}
```

### 4.2 Test Mode Configuration
Enable testing via ESPHome YAML configuration:

```yaml
# esphome/b48_display_controller.yaml
b48_display_controller:
  test_mode: true  # Enable test execution at startup
  test_level: full # Options: minimal, standard, full
```

## 5. Test Implementation

### 5.1 Test Runner
```cpp
// test_runner.h
#ifdef B48_TEST_MODE
#include <AUnit.h>

using namespace aunit;

void runDiagnosticTests() {
  Serial.println("B48 Display Controller - Running Diagnostic Tests");
  Serial.println("=================================================");
  
  // Run all tests
  TestRunner::run();
  
  // Print summary
  Serial.println("=================================================");
  Serial.print("Test Summary: ");
  Serial.print(TestRunner::getPassCount());
  Serial.print(" passed, ");
  Serial.print(TestRunner::getFailCount());
  Serial.print(" failed, ");
  Serial.print(TestRunner::getSkipCount());
  Serial.println(" skipped");
}
#endif
```

### 5.2 Database Test Example
```cpp
// test_db.h
#ifdef B48_TEST_MODE
#include <AUnit.h>
#include "b48_display_controller/database.h"

test(DatabaseCreateIfMissing) {
  DatabaseLayer db;
  bool result = db.initialize("/testdb.db");
  assertTrue(result);
  assertTrue(db.isInitialized());
  
  // Verify schema created correctly
  int version = db.getSchemaVersion();
  assertEqual(version, EXPECTED_SCHEMA_VERSION);
}

test(DatabaseFilterDisabled) {
  DatabaseLayer db;
  db.initialize("/testfilter_test.db");
  
  // Add test messages with different is_enabled values
  db.addTestMessage(1, true);   // Enabled 
  db.addTestMessage(2, false);  // Disabled
  
  // Load into cache
  db.refreshCache();
  
  // Verify only enabled messages in cache
  assertEqual(db.getCachedMessageCount(), 1);
}
#endif
```

### 5.3 Log Output Example
```
B48 Display Controller - Running Diagnostic Tests
=================================================
Test DatabaseCreateIfMissing passed
Test DatabaseFilterDisabled passed
Test SchedulerPriority passed
Test SchedulerEmergency passed
Test CommandChecksum passed
Test CommandFormat FAILED
  Location: test_commands.h:42
  Assertion: assertEqual(cmd.size(), expectedSize) failed
  Expected: 7, Actual: 6
=================================================
Test Summary: 5 passed, 1 failed, 0 skipped
```

## 6. Testing Best Practices

### 6.1 Test Execution Guidelines
- Keep tests lightweight to minimize startup delay
- Use conditional compilation to exclude tests from production builds
- Prioritize tests for critical functionality

### 6.2 Writing New Tests
```cpp
#ifdef B48_TEST_MODE
#include <AUnit.h>

test(NewFeatureTest) {
  // 1. Arrange - Set up test conditions
  ComponentUnderTest component;
  
  // 2. Act - Perform the operation
  bool result = component.operationToTest();
  
  // 3. Assert - Verify results
  assertTrue(result);
}
#endif
```

## 7. Production Considerations

### 7.1 Disabling Tests
For production deployment, disable tests via YAML configuration:

```yaml
# Production configuration
b48_display_controller:
  test_mode: false
```

### 7.2 Conditional Compilation
Use build flags to completely remove test code:

```yaml
# esphome/production.yaml
esphome:
  name: b48_controller
  platformio_options:
    build_flags:
      - -DB48_DISABLE_TESTS
``` 