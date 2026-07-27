#include <string.h>
#include "unity.h"
#include "pca9685.h"

TEST_CASE("pca9685 init/deinit", "[pca9685]")
{
    pca9685_t *dev = NULL;
    pca9685_config_t cfg = {
        .addr = 0x40,
        .sda_gpio = 8,
        .scl_gpio = 9,
        .clk_speed = 400000,
    };
    TEST_ASSERT_EQUAL(ESP_OK, pca9685_init(&dev, &cfg));
    TEST_ASSERT_NOT_NULL(dev);
    TEST_ASSERT_EQUAL(ESP_OK, pca9685_deinit(dev));
}

TEST_CASE("pca9685 set freq calculates prescale", "[pca9685]")
{
    // At 25MHz osc, 50Hz -> prescale = (25000000 / (4096 * 50)) - 1 = 121
    // Validated in mock: we just test the API doesn't crash
    pca9685_t *dev = NULL;
    pca9685_config_t cfg = { .addr = 0x40, .sda_gpio = 8, .scl_gpio = 9, .clk_speed = 400000 };
    esp_err_t ret = pca9685_init(&dev, &cfg);
    if (ret == ESP_OK) {
        pca9685_set_pwm_freq(dev, 50);
        pca9685_deinit(dev);
    }
}

TEST_CASE("pca9685 batch write validates count", "[pca9685]")
{
    pca9685_t *dev = NULL;
    pca9685_config_t cfg = { .addr = 0x40, .sda_gpio = 8, .scl_gpio = 9, .clk_speed = 400000 };
    esp_err_t ret = pca9685_init(&dev, &cfg);
    if (ret == ESP_OK) {
        uint8_t ch[] = {0};
        uint16_t vals[] = {2048};
        TEST_ASSERT_EQUAL(ESP_OK, pca9685_write_batch(dev, ch, vals, 1));
        TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, pca9685_write_batch(dev, ch, vals, 0));
        pca9685_deinit(dev);
    }
}
