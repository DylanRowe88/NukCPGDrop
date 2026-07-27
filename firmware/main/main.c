#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mdns.h"
#include "servos.h"
#include "state.h"
#include "wifi_manager.h"
#include "dns_server.h"
#include "web_server.h"

static const char *TAG = "nukcpgdrop";

static void start_mdns(void)
{
    mdns_init();
    mdns_hostname_set("nukcpgdrop");
    mdns_instance_name_set("NukCPGDrop Rig");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "mDNS: nukcpgdrop.local");
}

void app_main(void)
{
    ESP_LOGI(TAG, "NukCPGDrop starting...");

    ESP_ERROR_CHECK(state_init());
    ESP_LOGI(TAG, "state loaded (difficulty=%d, drops=%lu)",
             g_state.difficulty, g_state.drop_count);

    ESP_ERROR_CHECK(servos_init());
    ESP_LOGI(TAG, "servos initialized (%d channels)", SERVO_COUNT);

    ESP_ERROR_CHECK(wifi_ap_start());
    start_mdns();
    ESP_ERROR_CHECK(dns_server_start());
    ESP_ERROR_CHECK(web_server_start());

    ESP_LOGI(TAG, "Ready. Connect to SSID: %s", wifi_ap_get_ssid());
    ESP_LOGI(TAG, "Open http://nukcpgdrop.local or http://192.168.4.1");
}
