import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

# Definuje namespace pro tuto komponentu
b48_display_controller_ns = cg.esphome_ns.namespace('b48_display_controller')
# Definuje třídu komponenty, která dědí z esphome.Component
B48DisplayController = b48_display_controller_ns.class_('B48DisplayController', cg.Component)

# Definuje konfigurační schéma. Prozatím prázdné, jen vyžaduje ID.
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(B48DisplayController),
}).extend(cv.COMPONENT_SCHEMA) # Rozšiřuje základní schéma komponenty

async def to_code(config):
    # Tato funkce generuje C++ kód pro komponentu
    var = cg.new_Pvariable(config[CONF_ID]) # Vytvoří globální C++ proměnnou pro komponentu
    await cg.register_component(var, config) # Zaregistruje komponentu v ESPHome

    # Zde později přidáš kód pro inicializaci a logiku komponenty
    # např. cg.add(var.set_some_value(config[CONF_SOME_VALUE]))
    # např. await cg.register_uart_device(var, config) pokud by používala UART
