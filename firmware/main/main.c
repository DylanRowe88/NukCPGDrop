#include "audio.h"
#include "dashboard.h"
#include "dns_server.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "lv_port_disp.h"
/* lv_port_indev.h merged into lv_port_disp.h */
#include "lvgl.h"
#include "mdns.h"
#include "pca9685.h"
#include "servos.h"
#include "state.h"
#include "web_server.h"
#include "wifi_manager.h"
#include <stdio.h>

static const char *TAG = "nukcpgdrop";
bool g_pca9685_present = false;

static void lvgl_tick_cb(void *arg);
static void lvgl_task(void *arg);

static void set_system_led(void) {
  if (g_pca9685_present) {
    led_set_color(LED_GREEN);
  } else {
    led_set_color(LED_BLUE);
  }
}

static void start_mdns(void) {
  mdns_init();
  mdns_hostname_set("nukcpgdrop");
  mdns_instance_name_set("NukCPGDrop Rig");
  mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
  ESP_LOGI(TAG, "mDNS: nukcpgdrop.local");
}

static void battery_update_timer(void *arg) {
  g_state.battery_millivolts = battery_read_mv();
  g_state.battery_percent = battery_pct(g_state.battery_millivolts);
}

void app_main(void) {
  ESP_LOGI(TAG, "NukCPGDrop starting...");

  // Init RGB LED first for boot feedback
  led_init();

  ESP_ERROR_CHECK(state_init());
  ESP_LOGI(TAG, "state loaded (difficulty=%d, drops=%lu)", g_state.difficulty,
           g_state.drop_count);

  esp_err_t sv_ret = servos_init();
  g_pca9685_present = servos_pca9685_present();
  if (sv_ret == ESP_OK && g_pca9685_present) {
    ESP_LOGI(TAG, "servos initialized, PCA9685 detected");
  } else {
    ESP_LOGW(TAG, "PCA9685 not found — continuing in headless mode");
  }

  set_system_led();

  battery_init();
  static esp_timer_handle_t batt_handle;
  esp_timer_create_args_t batt_timer = {.callback = &battery_update_timer,
                                        .name = "battery"};
  esp_timer_create(&batt_timer, &batt_handle);
  esp_timer_start_periodic(batt_handle, 10000000);

  esp_err_t wifi_ret = wifi_ap_start();
  if (wifi_ret != ESP_OK)
    ESP_LOGW(TAG, "WiFi AP not available — HTTP/DNS may not bind");
  start_mdns();
  ESP_ERROR_CHECK(dns_server_start());
  esp_err_t web_ret = web_server_start();
  if (web_ret != ESP_OK)
    ESP_LOGE(TAG, "Web server failed to start (0x%x)", web_ret);
  else
    ESP_LOGI(TAG, "Web server started OK");

  // LVGL init
  lv_init();
  lv_port_disp_init();
  lv_port_indev_init();
  dashboard_init();

  // Backlight PWM on GPIO45
  ledc_timer_config_t bl_timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = LEDC_TIMER_0,
      .duty_resolution = LEDC_TIMER_8_BIT,
      .freq_hz = 2000,
  };
  ledc_timer_config(&bl_timer);
  ledc_channel_config_t bl_chan = {
      .gpio_num = 45,
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
      .duty = 255,
      .hpoint = 0,
  };
  ledc_channel_config(&bl_chan);

  gpio_set_direction(1, GPIO_MODE_OUTPUT);
  gpio_set_level(1, 1); // Enable amp
  audio_init();
  i2s_audio_init();

  // LVGL tick timer (5ms)
  const esp_timer_create_args_t lvgl_tick_timer = {.callback = &lvgl_tick_cb,
                                                   .name = "lvgl_tick"};
  esp_timer_handle_t lvgl_tick_handle;
  esp_timer_create(&lvgl_tick_timer, &lvgl_tick_handle);
  esp_timer_start_periodic(lvgl_tick_handle, 5000);

  // LVGL task on core 0
  xTaskCreatePinnedToCore(lvgl_task, "lvgl", 4096, NULL, 5, NULL, 0);

  ESP_LOGI(TAG, "Ready. SSID: %s  IP: %s", wifi_ap_get_ssid(),
           wifi_ap_get_ip());
  ESP_LOGI(TAG, "Open http://nukcpgdrop.local or http://192.168.4.1");
}

static void lvgl_tick_cb(void *arg) { lv_tick_inc(5); }

static void lvgl_task(void *arg) {
  while (1) {
    lv_task_handler();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
