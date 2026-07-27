#include "web_server.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pca9685.h"
#include "servos.h"
#include "state.h"
#include "web_assets.h"
#include <string.h>

static const char *TAG = "web";
static httpd_handle_t g_server = NULL;

extern bool g_pca9685_present;

static void shuffle_array(uint8_t *arr, size_t n) {
  for (size_t i = n - 1; i > 0; i--) {
    size_t j = rand() % (i + 1);
    uint8_t t = arr[j];
    arr[j] = arr[i];
    arr[i] = t;
  }
}

static void drop_sequence_task(void *arg) {
  uint8_t order[6] = {0, 1, 2, 3, 4, 5};
  shuffle_array(order, 6);
  state_save_sequence(order, 0);

  uint32_t interval = state_get_drop_interval_ms(g_state.difficulty);
  uint8_t i = 0;

  while (i < 6) {
    if (g_state.double_drop && i < 5) {
      uint8_t pair[2] = {order[i], order[i + 1]};
      servos_drop_batch(pair, 2);
      i += 2;
    } else {
      servos_drop(order[i]);
      i++;
    }
    state_save_sequence(order, i);
    state_increment_drop_count();

    if (i < 6) {
      if (g_state.difficulty == DIFFICULTY_RANDOM) {
        interval = state_get_drop_interval_ms(DIFFICULTY_RANDOM);
      }
      vTaskDelay(pdMS_TO_TICKS(interval));
    }
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

  const char *json = cJSON_Print(root);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, json);
  free((void *)json);
  cJSON_Delete(root);
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
    if (id >= 1 && id <= 6)
      servos_drop(id - 1);
  }

  cJSON_Delete(json);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
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
    NULL,
};

static bool is_captive_probe(const char *uri) {
  for (int i = 0; captive_probes[i]; i++) {
    if (strncmp(uri, captive_probes[i], strlen(captive_probes[i])) == 0)
      return true;
  }
  return false;
}

static esp_err_t redirect_to_portal(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

static esp_err_t wildcard_handler(httpd_req_t *req) {
  const char *uri = req->uri;

  if (is_captive_probe(uri))
    return redirect_to_portal(req);

  const char *lookup = uri;
  char normalized[64];
  if (strcmp(uri, "/") == 0) {
    lookup = "/index.html";
  } else if (uri[0] == '/') {
    lookup = uri;
  } else {
    normalized[0] = '/';
    strncpy(normalized + 1, uri, sizeof(normalized) - 2);
    normalized[sizeof(normalized) - 1] = 0;
    lookup = normalized;
  }

  for (size_t i = 0; i < web_assets_count; i++) {
    if (strcmp(lookup, web_assets[i].path) == 0) {
      httpd_resp_set_type(req, web_assets[i].mime);
      if (web_assets[i].len < web_assets[i].raw_len)
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
      httpd_resp_set_hdr(req, "Cache-Control", "public, max-age=3600");
      size_t send_len = web_assets[i].len;
      httpd_resp_send(req, (const char *)web_assets[i].data, send_len);
      return ESP_OK;
    }
  }

  httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not Found");
  return ESP_FAIL;
}

// ── URI handlers ─────────────────────────────────────────────────

static esp_err_t root_handler(httpd_req_t *req) {
  return wildcard_handler(req);
}

static esp_err_t captive_redirect_handler(httpd_req_t *req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "/");
  httpd_resp_send(req, NULL, 0);
  return ESP_OK;
}

// ── URI registration ─────────────────────────────────────────────

static const httpd_uri_t api_uris[] = {
    {.uri = "/api/status", .method = HTTP_GET, .handler = api_status_handler},
    {.uri = "/api/drop", .method = HTTP_POST, .handler = api_drop_handler},
    {.uri = "/api/config", .method = HTTP_POST, .handler = api_config_handler},
};

static const httpd_uri_t portal_uris[] = {
    {.uri = "/hotspot-detect.html",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/generate_204",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/connecttest.txt",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/check_network_status.txt",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/ncsi.txt",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/fwlink", .method = HTTP_GET, .handler = captive_redirect_handler},
    {.uri = "/success.txt",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/canonical.html",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/gen_204",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
    {.uri = "/redirect",
     .method = HTTP_GET,
     .handler = captive_redirect_handler},
};

esp_err_t web_server_start(void) {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 32;
  cfg.stack_size = 8192;
  cfg.lru_purge_enable = true;

  ESP_RETURN_ON_ERROR(httpd_start(&g_server, &cfg), TAG, "httpd start");

  // API routes
  for (size_t i = 0; i < sizeof(api_uris) / sizeof(api_uris[0]); i++)
    httpd_register_uri_handler(g_server, &api_uris[i]);

  // Captive portal detection probes -- these are hit by OS before any
  // browser URL is typed, and must redirect to the portal page.
  for (size_t i = 0; i < sizeof(portal_uris) / sizeof(portal_uris[0]); i++)
    httpd_register_uri_handler(g_server, &portal_uris[i]);

  // Root path -- serves the Blazor WASM app
  httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_handler};
  httpd_register_uri_handler(g_server, &root);

  // Wildcard catch-all for static assets
  httpd_uri_t wildcard = {
      .uri = "/*", .method = HTTP_GET, .handler = wildcard_handler};
  httpd_uri_t wpost = {
      .uri = "/*", .method = HTTP_POST, .handler = wildcard_handler};
  httpd_register_uri_handler(g_server, &wildcard);
  httpd_register_uri_handler(g_server, &wpost);

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
