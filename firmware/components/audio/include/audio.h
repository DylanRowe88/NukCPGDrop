#pragma once
#include "driver/i2s_std.h"
#include "esp_err.h"

typedef enum {
  AUDIO_PROMPT_DROP_1,
  AUDIO_PROMPT_DROP_ALL,
  AUDIO_PROMPT_RESET,
  AUDIO_PROMPT_LOW_BATTERY,
  AUDIO_PROMPT_COUNT,
} audio_prompt_t;

extern i2s_chan_handle_t i2s_tx_handle;
extern i2s_chan_handle_t i2s_rx_handle;

esp_err_t audio_init(void);
esp_err_t i2s_audio_init(void);
void audio_play_prompt(audio_prompt_t prompt);
