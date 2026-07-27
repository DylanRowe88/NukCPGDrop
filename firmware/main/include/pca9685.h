#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PCA9685_I2C_ADDR_BASE 0x40
#define PCA9685_SERVO_FREQ_HZ 50

typedef struct {
    uint8_t  addr;
    uint8_t  sda_gpio;
    uint8_t  scl_gpio;
    uint32_t clk_speed;
} pca9685_config_t;

typedef struct pca9685_t pca9685_t;

esp_err_t pca9685_init(pca9685_t **out, const pca9685_config_t *cfg);
esp_err_t pca9685_deinit(pca9685_t *dev);

esp_err_t pca9685_set_pwm_freq(pca9685_t *dev, uint16_t freq_hz);
esp_err_t pca9685_set_channel(pca9685_t *dev, uint8_t channel, uint16_t on, uint16_t off);
esp_err_t pca9685_set_channel_raw(pca9685_t *dev, uint8_t channel, uint16_t value);

esp_err_t pca9685_write_batch(pca9685_t *dev, const uint8_t *channels,
                              const uint16_t *values, uint8_t count);

esp_err_t pca9685_set_outputs(pca9685_t *dev, uint8_t outputs_enabled);
bool      pca9685_is_present(pca9685_t *dev);

#ifdef __cplusplus
}
#endif
