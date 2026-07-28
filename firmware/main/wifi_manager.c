#include "wifi_manager.h"
#include "esp_check.h"
#include "esp_chip_info.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "wifi_mgr";
static char g_ssid[32];

static void get_mac_suffix(char *out, size_t len) {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  snprintf(out, len, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

esp_err_t wifi_ap_start(void) {
  char suffix[8];
  get_mac_suffix(suffix, sizeof(suffix));
  snprintf(g_ssid, sizeof(g_ssid), "NukCPGDrop-%s", suffix);

  esp_netif_init();
  esp_event_loop_create_default();

  // Try to create WiFi AP. If WiFi hardware / NVS is unavailable (e.g. in
  // QEMU) we still let the HTTP server bind via the default network stack.
  esp_netif_t *netif = esp_netif_create_default_wifi_ap();
  if (!netif) {
    ESP_LOGW(TAG, "cannot create WiFi netif — continuing without WiFi");
    return ESP_ERR_NOT_FOUND;
  }

  // Configure the AP IP on the netif even before WiFi init, so that the
  // HTTP server can bind and accept connections in QEMU (where WiFi ROM
  // is unavailable and NVS for calibration data doesn't exist).
  esp_netif_ip_info_t ap_ip = {
      .ip = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)},
      .gw = {.addr = ESP_IP4TOADDR(192, 168, 4, 1)},
      .netmask = {.addr = ESP_IP4TOADDR(255, 255, 255, 0)},
  };
  esp_netif_set_ip_info(netif, &ap_ip);

  // Disable NVS for WiFi calibration in QEMU (no real RF hardware, no
  // valid calibration data). On real hardware, keep NVS enabled so the
  // driver persists calibration data as normal.
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  if (chip.revision == 0)
    cfg.nvs_enable = false;
  esp_err_t w_ret = esp_wifi_init(&cfg);
  if (w_ret != ESP_OK) {
    ESP_LOGW(TAG, "wifi init failed (0x%x) — continuing without WiFi", w_ret);
    esp_wifi_deinit();
    return w_ret;
  }
  ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set mode");

  wifi_config_t ap_config = {
      .ap =
          {
              .ssid_len = 0,
              .channel = 1,
              .max_connection = 4,
              .authmode = WIFI_AUTH_OPEN,
          },
  };
  strncpy((char *)ap_config.ap.ssid, g_ssid, sizeof(ap_config.ap.ssid) - 1);

  ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG,
                      "set ap config");

  ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");
  ESP_LOGI(TAG, "AP started: %s (192.168.4.1)", g_ssid);
  return ESP_OK;
}

esp_err_t wifi_ap_stop(void) {
  esp_wifi_stop();
  esp_wifi_deinit();
  return ESP_OK;
}

const char *wifi_ap_get_ssid(void) { return g_ssid; }

const char *wifi_ap_get_ip(void) { return "192.168.4.1"; }

int wifi_ap_get_sta_count(void) {
  wifi_sta_list_t sta;
  esp_wifi_ap_get_sta_list(&sta);
  return sta.num;
}

int wifi_ap_get_rssi(void) {
  wifi_sta_list_t sta;
  esp_wifi_ap_get_sta_list(&sta);
  if (sta.num == 0)
    return -100;
  // Return RSSI of first connected station
  return sta.sta[0].rssi;
}
