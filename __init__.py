import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UART_ID
from esphome.components import uart

# Define namespace for this component
b48_display_controller_ns = cg.esphome_ns.namespace('b48_display_controller')
# Define the component class, which inherits from esphome.Component
B48DisplayController = b48_display_controller_ns.class_('B48DisplayController', cg.Component)

# Configuration constants
CONF_DATABASE_PATH = "database_path"
CONF_TRANSITION_DURATION = "transition_duration"
CONF_TIME_SYNC_INTERVAL = "time_sync_interval"
CONF_EMERGENCY_PRIORITY_THRESHOLD = "emergency_priority_threshold"
CONF_MIN_SECONDS_BETWEEN_REPEATS = "min_seconds_between_repeats"
CONF_RUN_TESTS_ON_STARTUP = "run_tests_on_startup"

# Configuration schema with all required parameters
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(B48DisplayController),
    cv.Required(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    cv.Required(CONF_DATABASE_PATH): cv.string,
    cv.Optional(CONF_TRANSITION_DURATION, default=4): cv.positive_int,
    cv.Optional(CONF_TIME_SYNC_INTERVAL, default=60): cv.positive_int,
    cv.Optional(CONF_EMERGENCY_PRIORITY_THRESHOLD, default=95): cv.int_range(min=0, max=100),
    cv.Optional(CONF_MIN_SECONDS_BETWEEN_REPEATS, default=30): cv.positive_int,
    cv.Optional(CONF_RUN_TESTS_ON_STARTUP, default=False): cv.boolean,
}).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    # This function generates C++ code for the component
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Get the UART device reference
    uart = await cg.get_variable(config[CONF_UART_ID])
    cg.add(var.set_uart(uart))
    
    # Set configuration values
    cg.add(var.set_database_path(config[CONF_DATABASE_PATH]))
    cg.add(var.set_transition_duration(config[CONF_TRANSITION_DURATION]))
    cg.add(var.set_time_sync_interval(config[CONF_TIME_SYNC_INTERVAL]))
    cg.add(var.set_emergency_priority_threshold(config[CONF_EMERGENCY_PRIORITY_THRESHOLD]))
    cg.add(var.set_min_seconds_between_repeats(config[CONF_MIN_SECONDS_BETWEEN_REPEATS]))
    cg.add(var.set_run_tests_on_startup(config[CONF_RUN_TESTS_ON_STARTUP]))

    cg.add_library(
        name="Sqlite3Esp32",
        repository="https://github.com/siara-cc/esp32_arduino_sqlite3_lib.git",
        version="2.5.0",
    )