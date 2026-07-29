#include "servos.h"
#include "esp_check.h"
#include "esp_random.h"
#include "pca9685.h"
#include "state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define SERVO_PULSE_MIN_US 500
#define SERVO_PULSE_MAX_US 2500
#define SERVO_CYCLE_US 20000
#define PCA9685_MAX_VALUE 4095

static pca9685_t *g_pca = NULL;
static bool g_held[SERVO_COUNT];
static bool g_pca9685_found = false;

static const char *TAG = "servos";

static uint16_t pulse_to_pca9685(uint16_t pulse_us) {
  return (uint16_t)((uint32_t)pulse_us * PCA9685_MAX_VALUE / SERVO_CYCLE_US);
}

static uint16_t deg_to_pulse(uint8_t deg) {
  return (uint16_t)(500 + (uint32_t)deg * 2000 / 180);
}

static uint16_t servo_pulse_for(uint8_t index, bool held) {
  return deg_to_pulse(held ? g_state.sv_start_pos : g_state.sv_stop_pos);
}

esp_err_t servos_init(void) {
  memset(g_held, true, sizeof(g_held));

  pca9685_config_t cfg = {
      .addr = PCA9685_I2C_ADDR_BASE,
      .sda_gpio = CONFIG_I2C_SDA_GPIO,
      .scl_gpio = CONFIG_I2C_SCL_GPIO,
      .clk_speed = 100000,
  };

  esp_err_t ret = pca9685_init(&g_pca, &cfg);
  if (ret != ESP_OK) {
    g_pca9685_found = false;
    return ret;
  }

  g_pca9685_found = pca9685_is_present(g_pca);
  if (!g_pca9685_found) {
    ESP_LOGW(TAG, "PCA9685 not responding on I2C bus");
    return ESP_ERR_NOT_FOUND;
  }

  ESP_RETURN_ON_ERROR(pca9685_set_pwm_freq(g_pca, PCA9685_SERVO_FREQ_HZ), TAG,
                      "set freq");

  for (int i = 0; i < SERVO_COUNT; i++) {
    uint16_t val = pulse_to_pca9685(SERVO_PULSE_MIN_US);
    pca9685_set_channel_raw(g_pca, i, val);
  }

  return ESP_OK;
}

esp_err_t servos_set(uint8_t index, servo_position_t pos) {
  if (index >= SERVO_COUNT)
    return ESP_ERR_INVALID_ARG;
  bool held = (pos == SERVO_POSITION_HOLD);
  uint16_t pulse = servo_pulse_for(index, held);
  uint16_t val = pulse_to_pca9685(pulse);
  g_held[index] = held;
  return pca9685_set_channel_raw(g_pca, index, val);
}

esp_err_t servos_drop(uint8_t index) {
  return servos_set(index, SERVO_POSITION_RELEASE);
}

esp_err_t servos_drop_batch(const uint8_t *indices, uint8_t count) {
  uint16_t values[16];
  uint8_t channels[16];

  for (uint8_t i = 0; i < count; i++) {
    uint8_t idx = indices[i];
    if (idx >= SERVO_COUNT)
      return ESP_ERR_INVALID_ARG;
    channels[i] = idx;
    values[i] = pulse_to_pca9685(servo_pulse_for(idx, false));
    g_held[idx] = false;
  }

  return pca9685_write_batch(g_pca, channels, values, count);
}

esp_err_t servos_hold_all(void) {
  for (int i = 0; i < SERVO_COUNT; i++) {
    servos_set(i, SERVO_POSITION_HOLD);
  }
  return ESP_OK;
}

esp_err_t servos_release_all(void) {
  uint8_t all[16];
  for (int i = 0; i < SERVO_COUNT; i++) all[i] = i;
  return servos_drop_batch(all, SERVO_COUNT);
}

bool servos_is_held(uint8_t index) {
  if (index >= SERVO_COUNT)
    return false;
  return g_held[index];
}

bool servos_pca9685_present(void) { return g_pca9685_found; }

static void shuffle(uint8_t *arr, size_t n) {
  for (size_t i = n - 1; i > 0; i--) {
    size_t j = (size_t)(esp_random() % (i + 1));
    uint8_t t = arr[j];
    arr[j] = arr[i];
    arr[i] = t;
  }
}

static void sequence_task(void *arg) {
  (void)arg;
  uint8_t order[16];
  for (int i = 0; i < SERVO_COUNT; i++) order[i] = i;
  shuffle(order, SERVO_COUNT);
  state_save_sequence(order, 0);

  uint8_t i = 0;
  while (i < SERVO_COUNT) {
    servos_drop(order[i]);
    i++;
    state_save_sequence(order, i);
    state_increment_drop_count();
    if (i < SERVO_COUNT) {
      uint32_t interval = state_get_drop_interval_ms(g_state.difficulty);
      vTaskDelay(pdMS_TO_TICKS(interval));
    }
  }
  vTaskDelete(NULL);
}

esp_err_t servos_start_sequence(void) {
  xTaskCreate(sequence_task, "drop_seq", 4096, NULL, 4, NULL);
  return ESP_OK;
}
