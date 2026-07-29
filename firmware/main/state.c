#include "state.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_check.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <stdlib.h>
#include <string.h>

nukcpgdrop_state_t g_state;

static const char *TAG = "state";

static uint32_t interval_map[3] = {
    [DIFFICULTY_LONG] = 2000,
    [DIFFICULTY_SHORT] = 500,
    [DIFFICULTY_RANDOM] = 0,
};

esp_err_t state_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "nvs erase");
    ret = nvs_flash_init();
  }
  if (ret != ESP_OK)
    goto defaults;

  nvs_handle_t handle;
  ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
  if (ret != ESP_OK)
    goto defaults;

  size_t len = sizeof(g_state);
  ret = nvs_get_blob(handle, "state", &g_state, &len);
  if (ret != ESP_OK) {
    nvs_close(handle);
    goto defaults;
  }

  nvs_close(handle);
  return ESP_OK;

defaults:
  memset(&g_state, 0, sizeof(g_state));
  g_state.difficulty = DIFFICULTY_SHORT;
  g_state.custom_interval = 2000;
  g_state.range_min = 300;
  g_state.range_max = 2000;
  g_state.sound_enabled = true;
  g_state.sv_start_pos = 0;
  g_state.sv_stop_pos = 180;
  g_state.active_servos = 16;
  return ESP_OK;
}

esp_err_t state_save(void) {
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle), TAG,
                      "nvs open");
  esp_err_t ret = nvs_set_blob(handle, "state", &g_state, sizeof(g_state));
  if (ret == ESP_OK)
    ret = nvs_commit(handle);
  nvs_close(handle);
  return ret;
}

esp_err_t state_load(nukcpgdrop_state_t *out) {
  nvs_handle_t handle;
  ESP_RETURN_ON_ERROR(nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle), TAG,
                      "nvs open");
  size_t len = sizeof(*out);
  esp_err_t ret = nvs_get_blob(handle, "state", out, &len);
  nvs_close(handle);
  return ret;
}

void state_set_difficulty(difficulty_t d) {
  g_state.difficulty = d;
  state_save();
}

void state_increment_drop_count(void) {
  g_state.drop_count++;
  state_save();
}

void state_save_sequence(const uint8_t *order, uint8_t completed) {
  memcpy(g_state.last_sequence, order, 16);
  g_state.last_completed = completed;
  state_save();
}

uint32_t state_get_drop_interval_ms(difficulty_t diff) {
  if (diff == DIFFICULTY_RANDOM) {
    uint32_t range = g_state.range_max - g_state.range_min;
    return g_state.range_min + (rand() % (range + 1));
  }
  if (g_state.custom_interval > 0)
    return g_state.custom_interval;
  return interval_map[diff];
}

#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_8

void battery_init(void) {
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_12);
}

int battery_read_mv(void) {
  int raw = adc1_get_raw(BATTERY_ADC_CHANNEL);
  esp_adc_cal_characteristics_t chars;
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_12, ADC_WIDTH_BIT_12, 0,
                           &chars);
  uint32_t voltage = esp_adc_cal_raw_to_voltage(raw, &chars);
  return (int)(voltage * 2);
}

int battery_pct(int mv) {
  if (mv <= 2500)
    return 0;
  if (mv >= 4200)
    return 100;
  return (mv - 2500) * 100 / (4200 - 2500);
}
