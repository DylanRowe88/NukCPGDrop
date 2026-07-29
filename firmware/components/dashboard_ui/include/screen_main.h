#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t screen_main_create(void);

lv_obj_t *screen_main_get(void);

void screen_main_update_indicators(void);

void screen_main_update_status(void);

void screen_main_update_progress(void);

void screen_main_update_battery(void);

void screen_main_update_rssi(int rssi);

void screen_main_update_interval(uint32_t interval_ms);

void screen_main_update_double_drop(bool enabled);
void screen_main_update_audio_level(int level);
void screen_main_set_seq_running(bool running);
void screen_main_set_drop_anim(int idx);

#ifdef __cplusplus
}
#endif
