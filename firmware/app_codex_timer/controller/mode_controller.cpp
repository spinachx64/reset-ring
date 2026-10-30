#include "mode_controller.h"
#include <nvs.h>
namespace quota {
void ModeController::load() {
    nvs_handle_t handle;
    if (nvs_open("quota_watch", NVS_READONLY, &handle) == ESP_OK) {
        uint8_t value = 0;
        if (nvs_get_u8(handle, "mode", &value) == ESP_OK && value < 2) current = value;
        nvs_close(handle);
    }
}
void ModeController::toggle() {
    current = 1-current;
    nvs_handle_t handle;
    if (nvs_open("quota_watch", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_u8(handle, "mode", current);
        nvs_commit(handle);
        nvs_close(handle);
    }
}
}
