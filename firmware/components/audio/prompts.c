#include "audio.h"
#include "esp_log.h"

static const char *TAG = "prompts";

void audio_play_prompt(audio_prompt_t prompt) {
  ESP_LOGI(TAG, "Playing prompt: %d", prompt);
}
