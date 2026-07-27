#include <string.h>
#include "esp_check.h"
#include "pca9685.h"
#include "servos.h"

#define SERVO_PULSE_MIN_US   500
#define SERVO_PULSE_MAX_US   2500
#define SERVO_CYCLE_US       20000
#define PCA9685_MAX_VALUE    4095

static pca9685_t *g_pca = NULL;
static bool g_held[SERVO_COUNT];

static const char *TAG = "servos";

static uint16_t pulse_to_pca9685(uint16_t pulse_us)
{
    return (uint16_t)((uint32_t)pulse_us * PCA9685_MAX_VALUE / SERVO_CYCLE_US);
}

static uint16_t position_to_pulse(servo_position_t pos)
{
    return pos == SERVO_POSITION_HOLD ? SERVO_PULSE_MIN_US : SERVO_PULSE_MAX_US;
}

esp_err_t servos_init(void)
{
    memset(g_held, true, sizeof(g_held));

    pca9685_config_t cfg = {
        .addr      = PCA9685_I2C_ADDR_BASE,
        .sda_gpio  = 8,
        .scl_gpio  = 9,
        .clk_speed = 400000,
    };

    ESP_RETURN_ON_ERROR(pca9685_init(&g_pca, &cfg), TAG, "pca9685 init");
    ESP_RETURN_ON_ERROR(pca9685_set_pwm_freq(g_pca, PCA9685_SERVO_FREQ_HZ), TAG, "set freq");

    for (int i = 0; i < SERVO_COUNT; i++) {
        uint16_t val = pulse_to_pca9685(SERVO_PULSE_MIN_US);
        pca9685_set_channel_raw(g_pca, i, val);
    }

    return ESP_OK;
}

esp_err_t servos_set(uint8_t index, servo_position_t pos)
{
    if (index >= SERVO_COUNT) return ESP_ERR_INVALID_ARG;
    uint16_t pulse = position_to_pulse(pos);
    uint16_t val = pulse_to_pca9685(pulse);
    g_held[index] = (pos == SERVO_POSITION_HOLD);
    return pca9685_set_channel_raw(g_pca, index, val);
}

esp_err_t servos_drop(uint8_t index)
{
    return servos_set(index, SERVO_POSITION_RELEASE);
}

esp_err_t servos_drop_batch(const uint8_t *indices, uint8_t count)
{
    uint16_t values[16];
    uint8_t channels[16];

    for (uint8_t i = 0; i < count; i++) {
        uint8_t idx = indices[i];
        if (idx >= SERVO_COUNT) return ESP_ERR_INVALID_ARG;
        channels[i] = idx;
        values[i] = pulse_to_pca9685(SERVO_PULSE_MAX_US);
        g_held[idx] = false;
    }

    return pca9685_write_batch(g_pca, channels, values, count);
}

esp_err_t servos_hold_all(void)
{
    for (int i = 0; i < SERVO_COUNT; i++) {
        servos_set(i, SERVO_POSITION_HOLD);
    }
    return ESP_OK;
}

esp_err_t servos_release_all(void)
{
    uint8_t all[6] = {0, 1, 2, 3, 4, 5};
    return servos_drop_batch(all, 6);
}

bool servos_is_held(uint8_t index)
{
    if (index >= SERVO_COUNT) return false;
    return g_held[index];
}
