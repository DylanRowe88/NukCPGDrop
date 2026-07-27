#include <string.h>
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "wifi_manager.h"

static const char *TAG = "wifi_mgr";
static char g_ssid[32];

static void get_mac_suffix(char *out, size_t len)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(out, len, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

esp_err_t wifi_ap_start(void)
{
    char suffix[8];
    get_mac_suffix(suffix, sizeof(suffix));
    snprintf(g_ssid, sizeof(g_ssid), "NukCPGDrop-%s", suffix);

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set mode");

    wifi_config_t ap_config = {
        .ap = {
            .ssid_len = 0,
            .channel = 1,
            .max_connection = 4,
            .authmode = WIFI_AUTH_OPEN,
        },
    };
    strncpy((char *)ap_config.ap.ssid, g_ssid, sizeof(ap_config.ap.ssid) - 1);

    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "set ap config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start");

    ESP_LOGI(TAG, "AP started: %s (192.168.4.1)", g_ssid);
    return ESP_OK;
}

esp_err_t wifi_ap_stop(void)
{
    esp_wifi_stop();
    esp_wifi_deinit();
    return ESP_OK;
}

const char *wifi_ap_get_ssid(void) { return g_ssid; }

const char *wifi_ap_get_ip(void) { return "192.168.4.1"; }
