#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define TAG "i2s_audio"

esp_err_t i2s_audio_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                  I2S_SLOT_MODE_MONO),
      .gpio_cfg =
          {
              .mclk = 4,
              .bclk = 5,
              .ws = 7,
              .dout = 8,
              .din = I2S_GPIO_UNUSED,
          },
  };
  i2s_chan_handle_t tx_handle;
  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
  ESP_LOGI(TAG, "I2S audio initialized (MCLK=4, BCK=5, WS=7, DOUT=8)");
  return ESP_OK;
}
