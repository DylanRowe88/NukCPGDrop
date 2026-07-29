#include "dashboard.h"
#include "screen_main.h"
#include "servos.h"
#include "state.h"

#include "driver/i2s_std.h"
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

    audio_tick += 200;
    if (audio_tick >= 500) {
      audio_tick = 0;
      int16_t *buf = heap_caps_malloc(64 * sizeof(int16_t), MALLOC_CAP_8BIT);
      if (buf) {
        size_t read = 0;
        i2s_chan_handle_t rx_h;
        i2s_chan_config_t cc =
            I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
        i2s_std_config_t sc = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
            .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {.mclk = 4, .bclk = 5, .ws = 7, .dout = 8, .din = 6},
        };
        if (i2s_new_channel(&cc, NULL, &rx_h) == ESP_OK &&
            i2s_channel_init_std_mode(rx_h, &sc) == ESP_OK &&
            i2s_channel_enable(rx_h) == ESP_OK) {
          i2s_channel_read(rx_h, buf, 64 * sizeof(int16_t), &read,
                           pdMS_TO_TICKS(50));
          i2s_channel_disable(rx_h);
          i2s_del_channel(rx_h);
        }
        int peak = 0;
        for (int i = 0; i < (int)(read / 2); i++) {
          int v = abs(buf[i]);
          if (v > peak)
            peak = v;
        }
        screen_main_update_audio_level(peak);
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
