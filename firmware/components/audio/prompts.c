#include "audio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "prompts";

#define SAMPLE_RATE 16000
#define AMPLITUDE 8000
#define DURATION_MS 200

static void play_tone(int frequency_hz) {
  if (!i2s_tx_handle) {
    ESP_LOGW(TAG, "I2S TX not initialized");
    return;
  }
  int num_samples = SAMPLE_RATE * DURATION_MS / 1000;
  int16_t *buf = heap_caps_malloc(num_samples * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (!buf) {
    ESP_LOGE(TAG, "OOM for tone buffer");
    return;
  }
  for (int i = 0; i < num_samples; i++) {
    float t = (float)i / SAMPLE_RATE;
    buf[i] = (int16_t)(AMPLITUDE * sinf(2 * 3.14159f * frequency_hz * t));
  }
  size_t written = 0;
  esp_err_t ret = i2s_channel_write(i2s_tx_handle, buf, num_samples * sizeof(int16_t), &written, pdMS_TO_TICKS(500));
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "I2S write failed: %d", ret);
  }
  free(buf);
}

static void play_sweep(int f_start, int f_end, int duration_ms) {
  if (!i2s_tx_handle) return;
  int num_samples = SAMPLE_RATE * duration_ms / 1000;
  int16_t *buf = heap_caps_malloc(num_samples * sizeof(int16_t), MALLOC_CAP_8BIT);
  if (!buf) return;
  for (int i = 0; i < num_samples; i++) {
    float t = (float)i / SAMPLE_RATE;
    float f = f_start + (f_end - f_start) * t / (duration_ms / 1000.0f);
    buf[i] = (int16_t)(AMPLITUDE * sinf(2 * 3.14159f * f * t));
  }
  size_t written = 0;
  i2s_channel_write(i2s_tx_handle, buf, num_samples * sizeof(int16_t), &written, pdMS_TO_TICKS(duration_ms + 100));
  free(buf);
}

void audio_play_prompt(audio_prompt_t prompt) {
  ESP_LOGI(TAG, "Playing prompt: %d", prompt);
  switch (prompt) {
    case AUDIO_PROMPT_DROP_1:
      play_tone(440); break;
    case AUDIO_PROMPT_DROP_ALL:
      play_sweep(200, 800, 300); break;
    case AUDIO_PROMPT_RESET:
      play_sweep(600, 200, 200); break;
    case AUDIO_PROMPT_LOW_BATTERY:
      play_tone(120);
      vTaskDelay(pdMS_TO_TICKS(100));
      play_tone(120);
      break;
    default: break;
  }
}
