#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SERVO_COUNT 6

typedef enum {
  SERVO_POSITION_HOLD = 0,
  SERVO_POSITION_RELEASE = 1,
} servo_position_t;

esp_err_t servos_init(void);
esp_err_t servos_set(uint8_t index, servo_position_t pos);
esp_err_t servos_drop(uint8_t index);
esp_err_t servos_drop_batch(const uint8_t *indices, uint8_t count);
esp_err_t servos_hold_all(void);
esp_err_t servos_release_all(void);
bool servos_is_held(uint8_t index);
bool servos_pca9685_present(void);

#ifdef __cplusplus
}
#endif
