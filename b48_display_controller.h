#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace b48_display_controller {

class B48DisplayController : public Component {
 public:
  void setup() override {
    // Kód, který se spustí jednou při startu
  }

  void loop() override {
    // Kód, který se spouští opakovaně v hlavní smyčce
  }

  void dump_config() override;

  float get_setup_priority() const override { return esphome::setup_priority::LATE; }

  // Zde později přidáš metody specifické pro tvůj display controller
};

}  // namespace b48_display_controller
}  // namespace esphome 