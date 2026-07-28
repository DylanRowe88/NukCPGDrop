#include "dns_server.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_log.h"
#include "esp_phy_init.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led.h"
#include "mdns.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "pca9685.h"
#include "servos.h"
#include "state.h"
#include "web_server.h"
#include "wifi_manager.h"
#include <stdio.h>

static const char *TAG = "nukcpgdrop";
bool g_pca9685_present = false;

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

  // On QEMU the PHY calibration NVS entries are absent. Store a dummy
  // calibration blob (via the public API) so esp_wifi_start() finds
  // valid data and skips full RF calibration (which would hang).
  // Heap-allocate the large (1904 B) calibration struct.
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  if (chip.revision == 0) {
    esp_phy_calibration_data_t *cal = calloc(1, sizeof(*cal));
    if (cal) {
      esp_phy_store_cal_data_to_nvs(cal);
      free(cal);
    }
  }

  esp_err_t wifi_ret = wifi_ap_start();
  if (wifi_ret != ESP_OK)
    ESP_LOGW(TAG, "WiFi AP not available — HTTP/DNS may not bind");
  start_mdns();
  ESP_ERROR_CHECK(dns_server_start());
  ESP_ERROR_CHECK(web_server_start());

  ESP_LOGI(TAG, "Ready. SSID: %s  IP: %s", wifi_ap_get_ssid(),
           wifi_ap_get_ip());
  ESP_LOGI(TAG, "Open http://nukcpgdrop.local or http://192.168.4.1");
}
