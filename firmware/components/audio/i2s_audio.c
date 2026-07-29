#include "audio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#define TAG "i2s_audio"

i2s_chan_handle_t i2s_tx_handle = NULL;
i2s_chan_handle_t i2s_rx_handle = NULL;

esp_err_t i2s_audio_init(void) {
  i2s_chan_config_t chan_cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.auto_clear = true;

  i2s_std_config_t std_cfg = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                      I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = 4,
              .bclk = 5,
              .ws = 7,
              .dout = 8,
              .din = 6,
          },
  };
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_384;

  ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &i2s_tx_handle, &i2s_rx_handle));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(i2s_rx_handle, &std_cfg));
  ESP_ERROR_CHECK(i2s_channel_enable(i2s_tx_handle));
  ESP_ERROR_CHECK(i2s_channel_enable(i2s_rx_handle));
  ESP_LOGI(TAG, "I2S audio initialized (TX+RX, MCLK=4, BCK=5, WS=7, DOUT=8, DIN=6)");
  return ESP_OK;
}
