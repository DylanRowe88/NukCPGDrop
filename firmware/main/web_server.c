#include "web_server.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "led.h"
#include "pca9685.h"
#include "servos.h"
#include "state.h"
#include "web_assets.h"
#include "web_server.h"
#include "wifi_manager.h"
#include <string.h>

static const char *TAG = "web";
static httpd_handle_t g_server = NULL;

extern bool g_pca9685_present;

static SemaphoreHandle_t g_servo_sem = NULL;
#define MAX_CONCURRENT_SERVOS 2

static esp_err_t ensure_servo_sem(void) {
  if (!g_servo_sem) {
    g_servo_sem =
        xSemaphoreCreateCounting(MAX_CONCURRENT_SERVOS, MAX_CONCURRENT_SERVOS);
    if (!g_servo_sem)
      return ESP_ERR_NO_MEM;
  }
  return ESP_OK;
}

static void shuffle_array(uint8_t *arr, size_t n) {
  for (size_t i = n - 1; i > 0; i--) {
    size_t j = (size_t)(esp_random() % (i + 1));
    uint8_t t = arr[j];
    arr[j] = arr[i];
    arr[i] = t;
  }
}

static void drop_sequence_task(void *arg) {
  ensure_servo_sem();
  uint8_t order[6] = {0, 1, 2, 3, 4, 5};
  shuffle_array(order, 6);
  state_save_sequence(order, 0);

  uint32_t interval = state_get_drop_interval_ms(g_state.difficulty);
  uint8_t i = 0;

  while (i < 6) {
    // Acquire semaphore slots before moving servos (max 2 concurrent)
    int batch = g_state.double_drop && i < 5 ? 2 : 1;
    for (int s = 0; s < batch; s++)
      xSemaphoreTake(g_servo_sem, portMAX_DELAY);

    if (batch == 2) {
      uint8_t pair[2] = {order[i], order[i + 1]};
      servos_drop_batch(pair, 2);
      i += 2;
    } else {
      servos_drop(order[i]);
      i++;
    }
    state_save_sequence(order, i);
    state_increment_drop_count();

    // Release semaphores after the interval delay
    if (i < 6) {
      if (g_state.difficulty == DIFFICULTY_RANDOM)
        interval = state_get_drop_interval_ms(DIFFICULTY_RANDOM);
      vTaskDelay(pdMS_TO_TICKS(interval));
    }
    for (int s = 0; s < batch; s++)
      xSemaphoreGive(g_servo_sem);
  }

  vTaskDelete(NULL);
}

// ── API handlers ─────────────────────────────────────────────────

static esp_err_t api_status_handler(httpd_req_t *req) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "difficulty", g_state.difficulty);
  cJSON_AddBoolToObject(root, "double_drop", g_state.double_drop);
  cJSON_AddNumberToObject(root, "drop_count", g_state.drop_count);

  cJSON *held = cJSON_CreateArray();
  for (int i = 0; i < 6; i++)
    cJSON_AddItemToArray(held, cJSON_CreateBool(servos_is_held(i)));
  cJSON_AddItemToObject(root, "held", held);

  cJSON_AddBoolToObject(root, "pca9685_present", g_pca9685_present);
  cJSON_AddNumberToObject(root, "custom_interval", g_state.custom_interval);
  cJSON_AddNumberToObject(root, "range_min", g_state.range_min);
  cJSON_AddNumberToObject(root, "range_max", g_state.range_max);

  led_color_t lc;
  led_get_color(&lc);
  cJSON *led = cJSON_CreateObject();
  cJSON_AddNumberToObject(led, "r", lc.r);
  cJSON_AddNumberToObject(led, "g", lc.g);
  cJSON_AddNumberToObject(led, "b", lc.b);
  cJSON_AddItemToObject(root, "led", led);

  cJSON *wifi = cJSON_CreateObject();
  cJSON_AddNumberToObject(wifi, "rssi", wifi_ap_get_rssi());
  cJSON_AddNumberToObject(wifi, "clients", wifi_ap_get_sta_count());
  cJSON_AddStringToObject(wifi, "version", "NukCPGDrop v1.0");
  cJSON_AddItemToObject(root, "wifi", wifi);

  cJSON *battery = cJSON_CreateObject();
  cJSON_AddNumberToObject(battery, "millivolts", g_state.battery_millivolts);
  cJSON_AddNumberToObject(battery, "percent", g_state.battery_percent);
  cJSON_AddItemToObject(root, "battery", battery);

  const char *json = cJSON_Print(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);
  free((void *)json);
  cJSON_Delete(root);
  return ESP_OK;
}

static esp_err_t api_hold_handler(httpd_req_t *req) {
  char buf[16];
  int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (len <= 0)
    return ESP_FAIL;
  buf[len] = 0;

  cJSON *json = cJSON_Parse(buf);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  cJSON *id_item = cJSON_GetObjectItem(json, "id");
  if (cJSON_IsNumber(id_item)) {
    uint8_t id = (uint8_t)id_item->valuedouble;
    if (id >= 1 && id <= 6) {
      ensure_servo_sem();
      xSemaphoreTake(g_servo_sem, portMAX_DELAY);
      servos_set(id - 1, SERVO_POSITION_HOLD);
      vTaskDelay(pdMS_TO_TICKS(200));
      xSemaphoreGive(g_servo_sem);
    }
  }

  cJSON_Delete(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}

static esp_err_t api_drop_handler(httpd_req_t *req) {
  char buf[16];
  int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (len <= 0) {
    xTaskCreate(drop_sequence_task, "drop_seq", 4096, NULL, 4, NULL);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"started\"}");
    return ESP_OK;
  }
  buf[len] = 0;

  cJSON *json = cJSON_Parse(buf);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  cJSON *id_item = cJSON_GetObjectItem(json, "id");
  if (cJSON_IsNumber(id_item)) {
    uint8_t id = (uint8_t)id_item->valuedouble;
    if (id >= 1 && id <= 6) {
      ensure_servo_sem();
      xSemaphoreTake(g_servo_sem, portMAX_DELAY);
      servos_drop(id - 1);
      vTaskDelay(pdMS_TO_TICKS(200));
      xSemaphoreGive(g_servo_sem);
    }
  }

  cJSON_Delete(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}

static esp_err_t api_reset_handler(httpd_req_t *req) {
  servos_hold_all();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"reset\"}");
  return ESP_OK;
}

static esp_err_t api_config_handler(httpd_req_t *req) {
  char buf[256];
  int len = httpd_req_recv(req, buf, sizeof(buf) - 1);
  if (len <= 0)
    return ESP_FAIL;
  buf[len] = 0;

  cJSON *json = cJSON_Parse(buf);
  if (!json) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad json");
    return ESP_FAIL;
  }

  cJSON *diff = cJSON_GetObjectItem(json, "difficulty");
  if (cJSON_IsNumber(diff))
    state_set_difficulty((difficulty_t)diff->valuedouble);

  cJSON *dd = cJSON_GetObjectItem(json, "double_drop");
  if (cJSON_IsBool(dd))
    state_set_double_drop(cJSON_IsTrue(dd));

  cJSON *ci = cJSON_GetObjectItem(json, "custom_interval");
  if (cJSON_IsNumber(ci))
    g_state.custom_interval = (uint32_t)ci->valuedouble;

  cJSON *rmin = cJSON_GetObjectItem(json, "range_min");
  if (cJSON_IsNumber(rmin))
    g_state.range_min = (uint32_t)rmin->valuedouble;

  cJSON *rmax = cJSON_GetObjectItem(json, "range_max");
  if (cJSON_IsNumber(rmax))
    g_state.range_max = (uint32_t)rmax->valuedouble;

  state_save();
  cJSON_Delete(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
  return ESP_OK;
}

// ── Captive portal & file serving ────────────────────────────────

static const char *captive_probes[] = {
    "/hotspot-detect.html",
    "/generate_204",
    "/connecttest.txt",
    "/check_network_status.txt",
    "/ncsi.txt",
    "/fwlink/",
    "/fwlink",
    "/success.txt",
    "/canonical.html",
    "/gen_204",
    "/redirect",
    "/favicon.ico",
    NULL,
};

static esp_err_t redirect_to_portal(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  char loc[64];
  snprintf(loc, sizeof(loc), "http://%s/", wifi_ap_get_ip());
  httpd_resp_set_hdr(req, "Location", loc);
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t serve_asset(httpd_req_t *req, size_t idx) {
  httpd_resp_set_status(req, "200 OK");
  httpd_resp_set_type(req, web_assets[idx].mime);
  if (web_assets[idx].len < web_assets[idx].raw_len)
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
  httpd_resp_send(req, (const char *)web_assets[idx].data, web_assets[idx].len);
  return ESP_OK;
}

// ── Captive portal probe handlers ────────────────────────────────

static esp_err_t captive_probe_handler(httpd_req_t *req) {
  return redirect_to_portal(req);
}

static esp_err_t asset_handler(httpd_req_t *req) {
  const char *uri = req->uri;
  // The bare "/" must serve index.html (no asset path is literally "/")
  if (strcmp(uri, "/") == 0)
    uri = "/wwwroot/index.html";
  for (size_t i = 0; i < web_assets_count; i++) {
    if (strcmp(uri, web_assets[i].path) == 0)
      return serve_asset(req, i);
    if (strncmp(web_assets[i].path, "/wwwroot", 8) == 0 &&
        strcmp(web_assets[i].path + 8, uri) == 0)
      return serve_asset(req, i);
  }
  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
  return ESP_FAIL;
}

// ── URI registration ─────────────────────────────────────────────

static const httpd_uri_t api_uris[] = {
    {.uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler},
    {.uri = "/api/drop", .method = HTTP_POST, .handler = api_drop_handler},
    {.uri = "/api/hold", .method = HTTP_POST, .handler = api_hold_handler},
    {.uri = "/api/reset", .method = HTTP_POST, .handler = api_reset_handler},
    {.uri = "/api/config", .method = HTTP_POST, .handler = api_config_handler},
};

esp_err_t web_server_start(void) {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 256;
  cfg.stack_size = 8192;
  cfg.send_wait_timeout = 60;
  cfg.recv_wait_timeout = 60;
  cfg.lru_purge_enable = true;

  ESP_RETURN_ON_ERROR(httpd_start(&g_server, &cfg), TAG, "httpd start");

  // API routes (exact match, highest priority)
  for (size_t i = 0; i < sizeof(api_uris) / sizeof(api_uris[0]); i++)
    httpd_register_uri_handler(g_server, &api_uris[i]);

  // Root path "/" needs an explicit registration — asset_handler has the
  // logic to rewrite it to /wwwroot/index.html but is only reached when
  // a registered URI pattern fires.
  httpd_uri_t root_uri = {
      .uri = "/", .method = HTTP_GET, .handler = asset_handler};
  httpd_register_uri_handler(g_server, &root_uri);

  // Register every captive probe path as an explicit handler
  for (int i = 0; captive_probes[i]; i++) {
    httpd_uri_t probe = {.uri = captive_probes[i],
                         .method = HTTP_GET,
                         .handler = captive_probe_handler};
    httpd_register_uri_handler(g_server, &probe);
  }

  // Register every asset as an explicit URI handler (up to 128 slots).
  // The embedded paths use the /wwwroot/ prefix but the browser
  // requests paths like /css/app.css and /_framework/foo.wasm,
  // so we register BOTH forms (with and without the prefix).
  // Each handler calls asset_handler() which looks up the asset by URI.
  for (size_t i = 0; i < web_assets_count; i++) {
    httpd_uri_t a = {.uri = web_assets[i].path,
                     .method = HTTP_GET,
                     .handler = asset_handler};
    httpd_register_uri_handler(g_server, &a);

    if (strncmp(web_assets[i].path, "/wwwroot", 8) == 0) {
      httpd_uri_t b = {.uri = web_assets[i].path + 8,
                       .method = HTTP_GET,
                       .handler = asset_handler};
      httpd_register_uri_handler(g_server, &b);
    }
  }

  ESP_LOGI(TAG, "HTTP server running on :80 (%u assets embedded)",
           web_assets_count);
  return ESP_OK;
}

esp_err_t web_server_stop(void) {
  if (g_server) {
    httpd_stop(g_server);
    g_server = NULL;
  }
  return ESP_OK;
}
