import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UART_ID, CONF_PIN

# Local constant for web_ui enabled flag
CONF_ENABLED = "enabled"
from esphome.components import uart, sensor
from esphome.components.sensor import Sensor
from esphome.core import CORE

# Declare dependencies
DEPENDENCIES = ["uart", "sensor"]

# Define namespace for this component
b48_display_controller_ns = cg.esphome_ns.namespace('b48_display_controller')
# Define the component class, which inherits from esphome.Component
B48DisplayController = b48_display_controller_ns.class_('B48DisplayController', cg.Component)
# Define the web handler class for web UI
B48WebHandler = b48_display_controller_ns.class_('B48WebHandler', cg.Component)

# Configuration constants
CONF_DATABASE_PATH = "database_path"
CONF_TRANSITION_DURATION = "transition_duration"
CONF_TIME_SYNC_INTERVAL = "time_sync_interval"
CONF_EMERGENCY_PRIORITY_THRESHOLD = "emergency_priority_threshold"
CONF_RUN_TESTS_ON_STARTUP = "run_tests_on_startup"
CONF_WIPE_DATABASE_ON_BOOT = "wipe_database_on_boot"
CONF_DISPLAY_ENABLE_PIN = "display_enable_pin"  # New testing-only configuration
CONF_MESSAGE_QUEUE_SIZE_SENSOR = "message_queue_size_sensor"
CONF_PURGE_INTERVAL_HOURS = "purge_interval_hours"  # New configuration for database maintenance
CONF_WEB_UI = "web_ui"  # Web UI configuration block
CONF_WEB_HANDLER_ID = "web_handler_id"  # Internal ID for web handler


def _validate_web_ui(config):
    """Validate and prepare web_ui configuration."""
    if CONF_WEB_UI in config:
        web_ui = config[CONF_WEB_UI]
        if web_ui.get(CONF_ENABLED, True):
            # Ensure web_server_base is available
            from esphome.components import web_server_base
    return config


# Web UI sub-schema
WEB_UI_SCHEMA = cv.Schema({
    cv.GenerateID(CONF_WEB_HANDLER_ID): cv.declare_id(B48WebHandler),
    cv.Optional(CONF_ENABLED, default=True): cv.boolean,
})

# Configuration schema with all required parameters
CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(B48DisplayController),
        cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
        cv.Required(CONF_DATABASE_PATH): cv.string,
        cv.Optional(CONF_TRANSITION_DURATION, default=4): cv.positive_int,
        cv.Optional(CONF_TIME_SYNC_INTERVAL, default=60): cv.positive_int,
        cv.Optional(CONF_EMERGENCY_PRIORITY_THRESHOLD, default=95): cv.int_range(min=0, max=100),
        cv.Optional(CONF_RUN_TESTS_ON_STARTUP, default=False): cv.boolean,
        cv.Optional(CONF_WIPE_DATABASE_ON_BOOT, default=False): cv.boolean,
        # New option for testing purposes - pulls a pin high to enable the display
        cv.Optional(CONF_DISPLAY_ENABLE_PIN): cv.int_range(min=0, max=39),
        cv.Optional(CONF_MESSAGE_QUEUE_SIZE_SENSOR): cv.use_id(Sensor),
        cv.Optional(CONF_PURGE_INTERVAL_HOURS, default=24): cv.positive_int,  # Default to 24 hours
        cv.Optional(CONF_WEB_UI): WEB_UI_SCHEMA,  # Web UI configuration
    }).extend(cv.COMPONENT_SCHEMA),
    _validate_web_ui,
)


async def to_code(config):
    # This function generates C++ code for the component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Get the UART device reference
    uart_dev = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(uart_dev))

    # Set configuration values
    cg.add(var.set_database_path(config[CONF_DATABASE_PATH]))
    cg.add(var.set_transition_duration(config[CONF_TRANSITION_DURATION]))
    cg.add(var.set_time_sync_interval(config[CONF_TIME_SYNC_INTERVAL]))
    cg.add(var.set_emergency_priority_threshold(config[CONF_EMERGENCY_PRIORITY_THRESHOLD]))
    cg.add(var.set_run_tests_on_startup(config[CONF_RUN_TESTS_ON_STARTUP]))
    cg.add(var.set_wipe_database_on_boot(config[CONF_WIPE_DATABASE_ON_BOOT]))

    # Configure display enable pin for testing if specified
    if CONF_DISPLAY_ENABLE_PIN in config:
        cg.add(var.set_display_enable_pin(config[CONF_DISPLAY_ENABLE_PIN]))

    # Set database maintenance configuration
    cg.add(var.set_purge_interval_hours(config[CONF_PURGE_INTERVAL_HOURS]))

    # Connect sensors if specified
    if CONF_MESSAGE_QUEUE_SIZE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_MESSAGE_QUEUE_SIZE_SENSOR])
        cg.add(var.set_message_queue_size_sensor(sens))

    # SQLite library for ESP32
    # Using esp32-idf-sqlite3 for ESP-IDF framework which has proper fsync handling
    # For Arduino framework, we use esp32_arduino_sqlite3_lib
    if CORE.using_arduino:
        # Arduino framework - use PlatformIO library
        cg.add_library(
            name="Sqlite3Esp32",
            repository="https://github.com/siara-cc/esp32_arduino_sqlite3_lib.git",
            version="2.5.0",
        )
        cg.add_library(
            name="esp_littlefs",
            repository="https://github.com/joltwallet/esp_littlefs.git",
            version="1.20.4",
        )
    else:
        # ESP-IDF framework - use local IDF component with ESP-IDF 5.x fix
        from esphome.components.esp32 import add_idf_component
        import os

        # Use local sqlite3 component with spi_flash fix for ESP-IDF 5.x
        # The upstream esp32-idf-sqlite3 CMakeLists.txt is missing spi_flash in PRIV_REQUIRES,
        # causing 'spi_flash_mmap.h: No such file' errors on ESP-IDF 5.x
        script_dir = os.path.dirname(__file__)
        sqlite_path = os.path.join(script_dir, "esp32-idf-sqlite3")
        add_idf_component(
            name="esp32-idf-sqlite3",
            path=sqlite_path,
        )

        # LittleFS component
        add_idf_component(
            name="joltwallet/esp_littlefs",
            repo="https://github.com/joltwallet/esp_littlefs.git",
            ref="v1.20.4",
        )

    # Web UI configuration
    if CONF_WEB_UI in config:
        web_ui_config = config[CONF_WEB_UI]
        if web_ui_config.get(CONF_ENABLED, True):
            # Import web_server_base component
            from esphome.components import web_server_base

            # Use the global web_server_base instance
            cg.add_define("USE_B48_WEB_UI")

            # Create web handler and register with web_server_base
            # The handler takes (WebServerBase*, B48DisplayController*)
            web_handler = cg.new_Pvariable(
                web_ui_config[CONF_WEB_HANDLER_ID],
                cg.RawExpression("esphome::web_server_base::global_web_server_base"),
                var
            )
            await cg.register_component(web_handler, web_ui_config)
