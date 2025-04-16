#include "b48_display_controller.h"
#include "esphome/core/log.h"

namespace esphome {
namespace b48_display_controller {

static const char *const TAG = "b48_display_controller";

void B48DisplayController::dump_config() {
  ESP_LOGCONFIG(TAG, "B48 Display Controller:");
  // Zde můžeš přidat výpis konfigurace, např.:
  // ESP_LOGCONFIG(TAG, "  Update interval: %ums", this->update_interval_);
}

// Implementace dalších metod třídy přijde sem

}  // namespace b48_display_controller
}  // namespace esphome 