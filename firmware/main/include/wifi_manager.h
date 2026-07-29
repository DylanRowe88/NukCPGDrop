#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_ap_start(void);
esp_err_t wifi_ap_stop(void);
const char *wifi_ap_get_ssid(void);
const char *wifi_ap_get_ip(void);
int wifi_ap_get_sta_count(void);
int wifi_ap_get_rssi(void);
int wifi_ap_get_sta_list(uint8_t *macs, int *rssis, int max_count);

#if defined(CONFIG_E2E_TEST) && CONFIG_E2E_TEST
esp_err_t wifi_sta_connect(const char *ssid);
void wifi_sta_http_test(const char *target_ssid);
#endif

#ifdef __cplusplus
}
#endif
