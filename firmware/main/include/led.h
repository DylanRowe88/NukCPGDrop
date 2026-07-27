#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t r, g, b;
} led_color_t;

extern const led_color_t LED_OFF;
extern const led_color_t LED_GREEN;
extern const led_color_t LED_RED;
extern const led_color_t LED_BLUE;
extern const led_color_t LED_YELLOW;
extern const led_color_t LED_WHITE;

esp_err_t led_init(void);
esp_err_t led_set_color(led_color_t color);
void led_get_color(led_color_t *color);

#ifdef __cplusplus
}
#endif
