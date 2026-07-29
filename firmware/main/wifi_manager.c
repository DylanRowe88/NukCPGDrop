#include "wifi_manager.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "wifi_mgr";
static char g_ssid[32];
static esp_netif_t *g_netif = NULL;

static void get_mac_suffix(char *out, size_t len) {
  uint8_t mac[6];
  esp_efuse_mac_get_default(mac);
  snprintf(out, len, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static void wifi_common_init(void) {
  esp_netif_init();
  esp_event_loop_create_default();
}

esp_err_t wifi_ap_start(void) {
  char suffix[8];
  get_mac_suffix(suffix, sizeof(suffix));
  snprintf(g_ssid, sizeof(g_ssid), "NukCPGDrop-%s", suffix);

  wifi_common_init();

  esp_netif_t *netif = esp_netif_create_default_wifi_ap();
  if (!netif) {
    ESP_LOGW(TAG, "cannot create WiFi netif — continuing without WiFi");
    return ESP_ERR_NOT_FOUND;
  }
  g_netif = netif;

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
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

#if defined(CONFIG_E2E_TEST) && CONFIG_E2E_TEST
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "STA disconnected, retrying...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "STA got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

esp_err_t wifi_sta_connect(const char *ssid) {
  s_wifi_event_group = xEventGroupCreate();

  esp_err_t nvs_ret = nvs_flash_init();
  if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    nvs_ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(nvs_ret);

  wifi_common_init();

  esp_netif_t *netif = esp_netif_create_default_wifi_sta();
  if (!netif)
    return ESP_FAIL;
  g_netif = netif;

  esp_event_handler_instance_t instance_any_id;
  esp_event_handler_instance_t instance_got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &instance_any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &instance_got_ip));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wifi_config = {
      .sta =
          {
              .threshold.authmode = WIFI_AUTH_OPEN,
          },
  };
  strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "STA connecting to %s...", ssid);

  EventBits_t bits = xEventGroupWaitBits(
      s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdFALSE, pdFALSE,
      pdMS_TO_TICKS(30000));

  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(TAG, "STA connected to %s", ssid);
    return ESP_OK;
  }
  ESP_LOGE(TAG, "STA connection to %s failed", ssid);
  return ESP_FAIL;
}

static int http_get(const char *host, int port, const char *path, char *buf,
                    size_t buf_size) {
  struct sockaddr_in dest_addr;
  dest_addr.sin_addr.s_addr = inet_addr(host);
  dest_addr.sin_family = AF_INET;
  dest_addr.sin_port = htons(port);

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return -1;

  struct timeval to = {5, 0};
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));
  setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &to, sizeof(to));

  if (connect(sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr)) < 0) {
    close(sock);
    return -1;
  }

  char req[512];
  int req_len = snprintf(
      req, sizeof(req),
      "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);

  if (send(sock, req, req_len, 0) < 0) {
    close(sock);
    return -1;
  }

  int total = 0;
  int len;
  while (total < (int)buf_size - 1 &&
         (len = recv(sock, buf + total, buf_size - 1 - total, 0)) > 0) {
    total += len;
  }
  buf[total] = 0;
  close(sock);
  return total;
}

void wifi_sta_http_test(const char *target_ssid) {
  ESP_LOGI(TAG, "=== E2E HTTP Test ===");
  ESP_LOGI(TAG, "Target AP: %s", target_ssid);
  ESP_LOGI(TAG, "Target URL: http://192.168.4.1/");

  esp_err_t ret = wifi_sta_connect(target_ssid);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to connect to AP");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(1000));

  char buf[4096];
  int len;

  ESP_LOGI(TAG, "--- GET /api/status ---");
  len = http_get("192.168.4.1", 80, "/api/status", buf, sizeof(buf));
  if (len > 0) {
    char *body = strstr(buf, "\r\n\r\n");
    if (body)
      body += 4;
    else
      body = buf;
    ESP_LOGI(TAG, "Response (%d bytes): %.500s", len, body);
  } else {
    ESP_LOGE(TAG, "HTTP GET /api/status FAILED (err=%d)", len);
  }

  ESP_LOGI(TAG, "--- GET / (root) ---");
  len = http_get("192.168.4.1", 80, "/", buf, sizeof(buf));
  if (len > 0) {
    char *body = strstr(buf, "\r\n\r\n");
    if (body)
      body += 4;
    else
      body = buf;
    char *ct = strstr(buf, "Content-Type:");
    ESP_LOGI(TAG, "Response (%d bytes) type=%.60s body=%.200s", len,
             ct ? ct : "", body);
  } else {
    ESP_LOGE(TAG, "HTTP GET / FAILED (err=%d)", len);
  }

  ESP_LOGI(TAG, "=== E2E HTTP Test Complete ===");
}
#endif

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
  return sta.sta[0].rssi;
}

int wifi_ap_get_sta_list(uint8_t *macs, int *rssis, int max_count) {
  wifi_sta_list_t sta;
  esp_wifi_ap_get_sta_list(&sta);
  int count = (int)sta.num;
  if (count > max_count)
    count = max_count;
  for (int i = 0; i < count; i++) {
    memcpy(macs + i * 6, sta.sta[i].mac, 6);
    rssis[i] = sta.sta[i].rssi;
  }
  return count;
}
