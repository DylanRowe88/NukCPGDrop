#include "dashboard.h"
#include "screen_main.h"
#include "servos.h"
#include "state.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dashboard";

static TaskHandle_t dashboard_task_handle = NULL;

static void dashboard_update_task(void *arg) {
  TickType_t last_wake_tick = xTaskGetTickCount();
  TickType_t rssi_tick = 0;
  TickType_t battery_tick = 0;
  int last_rssi = -120;

  while (1) {
    vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(200));

    screen_main_update_indicators();
    screen_main_update_progress();

    rssi_tick += 200;
    if (rssi_tick >= 2000) {
      rssi_tick = 0;
      screen_main_update_rssi(last_rssi);
    }

    battery_tick += 200;
    if (battery_tick >= 10000) {
      battery_tick = 0;
      screen_main_update_battery();
    }

    screen_main_update_status();
  }
}

esp_err_t dashboard_init(void) {
  ESP_LOGI(TAG, "creating dashboard UI");

  ESP_ERROR_CHECK(screen_main_create());

  xTaskCreatePinnedToCore(dashboard_update_task, "dashboard_update", 3072, NULL,
                          4, &dashboard_task_handle, 0);

  ESP_LOGI(TAG, "dashboard UI ready");
  return ESP_OK;
}

void dashboard_update_main(void) {
  screen_main_update_indicators();
  screen_main_update_status();
}
