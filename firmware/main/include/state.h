#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NVS_NAMESPACE "nukcpgdrop"

typedef enum {
  DIFFICULTY_LONG = 0,
  DIFFICULTY_SHORT = 1,
  DIFFICULTY_RANDOM = 2,
} difficulty_t;

typedef struct {
  difficulty_t difficulty;
  bool double_drop;
  uint32_t drop_count;
  uint8_t last_sequence[6];
  uint8_t last_completed;
  uint32_t custom_interval;
  uint32_t range_min;
  uint32_t range_max;
} nukcpgdrop_state_t;

extern nukcpgdrop_state_t g_state;

esp_err_t state_init(void);
esp_err_t state_save(void);
esp_err_t state_load(nukcpgdrop_state_t *out);
void state_set_difficulty(difficulty_t d);
void state_set_double_drop(bool enabled);
void state_increment_drop_count(void);
void state_save_sequence(const uint8_t *order, uint8_t completed);

uint32_t state_get_drop_interval_ms(difficulty_t diff);

#ifdef __cplusplus
}
#endif
