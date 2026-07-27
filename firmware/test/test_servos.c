#include "servos.h"
#include "unity.h"

TEST_CASE("servos bounds check", "[servos]") {
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, servos_set(6, SERVO_POSITION_HOLD));
  TEST_ASSERT_EQUAL(ESP_ERR_INVALID_ARG, servos_drop(6));
}

TEST_CASE("servos held state transitions", "[servos]") {
  servos_init();
  servos_hold_all();
  TEST_ASSERT_TRUE(servos_is_held(0));
  TEST_ASSERT_TRUE(servos_is_held(5));

  servos_drop(0);
  TEST_ASSERT_FALSE(servos_is_held(0));
  TEST_ASSERT_TRUE(servos_is_held(1));
}

TEST_CASE("servos batch drop", "[servos]") {
  servos_init();
  servos_hold_all();

  uint8_t batch[] = {0, 2, 4};
  servos_drop_batch(batch, 3);
  TEST_ASSERT_FALSE(servos_is_held(0));
  TEST_ASSERT_TRUE(servos_is_held(1));
  TEST_ASSERT_FALSE(servos_is_held(2));
  TEST_ASSERT_TRUE(servos_is_held(3));
  TEST_ASSERT_FALSE(servos_is_held(4));
  TEST_ASSERT_TRUE(servos_is_held(5));
}
