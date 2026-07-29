#include "audio.h"
#include "dashboard.h"
#include "screen_main.h"
#include "servos.h"
#include "state.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dashboard";

static TaskHandle_t dashboard_task_handle = NULL;

static void dashboard_update_task(void *arg) {
  TickType_t last_wake_tick = xTaskGetTickCount();
  TickType_t rssi_tick = 0;
  TickType_t battery_tick = 0;
  TickType_t audio_tick = 0;
  int last_rssi = -120;

  while (1) {
    vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(200));

    screen_main_update_indicators();

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

    audio_tick += 200;
    if (audio_tick >= 500) {
      audio_tick = 0;
      int16_t *buf = heap_caps_malloc(64 * sizeof(int16_t), MALLOC_CAP_8BIT);
      if (buf && i2s_rx_handle) {
        size_t read = 0;
        i2s_channel_read(i2s_rx_handle, buf, 64 * sizeof(int16_t), &read,
                         pdMS_TO_TICKS(50));
        int peak = 0;
        for (int i = 0; i < (int)(read / 2); i++) {
          int v = abs(buf[i]);
          if (v > peak)
            peak = v;
        }
        screen_main_update_audio_level(peak);
        free(buf);
      } else if (buf) {
        free(buf);
      }
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
