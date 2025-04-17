## Good way to add external libraries.
Visit __init__.py and add it like this:
```
    cg.add_library(
        name="Sqlite3Esp32",
        repository="https://github.com/siara-cc/esp32_arduino_sqlite3_lib.git",
        version="2.5.0",
    )
```
This has great success rate in being picked up by our target ESPHome build chain! :)

Also, great for internal libraries, use ESPHome .yaml file like this, it works in overriding build platfomio.ini file. Actually add external libraries here too.
Good because changes made directly into pio.ini files seems to rarely work in this env.
```
esphome:
  name: espington
  platformio_options:
    lib_deps:      
      - FS @ ^2.0.0
      - LittleFS @ ^2.0.0
      - siara-cc/Sqlite3Esp32@^2.5
      - bxparks/AUnit@^1.7.1
```