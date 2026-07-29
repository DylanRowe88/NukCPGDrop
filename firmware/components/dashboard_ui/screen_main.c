#include "screen_main.h"
#include "led.h"
#include "servos.h"
#include "state.h"
#include "wifi_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "screen_main";

#define SCREEN_W 240
#define SCREEN_H 320
#define CONTENT_H 1100

static lv_obj_t *main_screen = NULL;
static lv_obj_t *scrollable = NULL;

static lv_obj_t *title_label = NULL;
static lv_obj_t *rssi_label = NULL;

static lv_obj_t *can_indicators[SERVO_COUNT] = {NULL};
static lv_obj_t *can_labels[SERVO_COUNT] = {NULL};

static lv_obj_t *action_btn = NULL;
static lv_obj_t *action_btn_label = NULL;
static bool g_seq_running = false;

static lv_obj_t *progress_bar = NULL;
static lv_obj_t *progress_label = NULL;

static lv_obj_t *interval_slider = NULL;
static lv_obj_t *interval_label = NULL;

static lv_obj_t *diff_btn_long = NULL;
static lv_obj_t *diff_btn_short = NULL;
static lv_obj_t *diff_btn_random = NULL;

static lv_obj_t *range_min_slider = NULL;
static lv_obj_t *range_min_label = NULL;
static lv_obj_t *range_max_slider = NULL;
static lv_obj_t *range_max_label = NULL;

static lv_obj_t *sv_start_slider = NULL;
static lv_obj_t *sv_start_label = NULL;
static lv_obj_t *sv_stop_slider = NULL;
static lv_obj_t *sv_stop_label = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *battery_bar = NULL;
static lv_obj_t *battery_label = NULL;
static lv_obj_t *version_label = NULL;

static lv_obj_t *led_indicator = NULL;
static lv_obj_t *sound_switch = NULL;
static lv_obj_t *client_count_label = NULL;
static lv_obj_t *audio_bar = NULL;
static lv_obj_t *pca9685_label = NULL;

static void action_btn_cb(lv_event_t *e) {
  if (g_seq_running)
    return;
  bool all_held = true;
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (!servos_is_held(i)) {
      all_held = false;
      break;
    }
  }
  if (all_held) {
    g_seq_running = true;
    servos_start_sequence();
  } else {
    servos_hold_all();
  }
}

static void interval_slider_cb(lv_event_t *e) {
  uint32_t val = (uint32_t)lv_slider_get_value(lv_event_get_target(e));
  g_state.custom_interval = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "%lu ms", val);
  lv_label_set_text(interval_label, buf);
}

static void diff_btn_cb(lv_event_t *e) {
  difficulty_t diff =
      (difficulty_t)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
  state_set_difficulty(diff);
  lv_obj_add_state(diff_btn_long, diff == DIFFICULTY_LONG ? LV_STATE_CHECKED
                                                           : LV_STATE_DEFAULT);
  lv_obj_add_state(diff_btn_short, diff == DIFFICULTY_SHORT ? LV_STATE_CHECKED
                                                             : LV_STATE_DEFAULT);
  lv_obj_add_state(diff_btn_random, diff == DIFFICULTY_RANDOM
                                        ? LV_STATE_CHECKED
                                        : LV_STATE_DEFAULT);
  bool show = (diff == DIFFICULTY_RANDOM);
  if (show) {
    lv_obj_clear_flag(range_min_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(range_min_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(range_max_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(range_max_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(interval_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(interval_label, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(range_min_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(range_min_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(range_max_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(range_max_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(interval_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(interval_label, LV_OBJ_FLAG_HIDDEN);
  }
}

static void range_min_cb(lv_event_t *e) {
  uint32_t val = (uint32_t)lv_slider_get_value(lv_event_get_target(e));
  g_state.range_min = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "Min: %lu ms", val);
  lv_label_set_text(range_min_label, buf);
}

static void range_max_cb(lv_event_t *e) {
  uint32_t val = (uint32_t)lv_slider_get_value(lv_event_get_target(e));
  g_state.range_max = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "Max: %lu ms", val);
  lv_label_set_text(range_max_label, buf);
}

static void sv_start_cb(lv_event_t *e) {
  uint32_t val = (uint32_t)lv_slider_get_value(lv_event_get_target(e));
  g_state.sv_start_pos = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "Start: %lu\xB0", val);
  lv_label_set_text(sv_start_label, buf);
  state_save();
}

static void sv_stop_cb(lv_event_t *e) {
  uint32_t val = (uint32_t)lv_slider_get_value(lv_event_get_target(e));
  g_state.sv_stop_pos = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "Stop: %lu\xB0", val);
  lv_label_set_text(sv_stop_label, buf);
  state_save();
}

static void sound_switch_cb(lv_event_t *e) {
  g_state.sound_enabled =
      lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static void can_tap_cb(lv_event_t *e) {
  lv_obj_t *btn = lv_event_get_target(e);
  intptr_t idx = (intptr_t)lv_obj_get_user_data(btn);
  if (idx >= 0 && idx < SERVO_COUNT) {
    if (servos_is_held((uint8_t)idx))
      servos_drop((uint8_t)idx);
    else
      servos_set((uint8_t)idx, SERVO_POSITION_HOLD);
  }
}

static void set_can_color(uint8_t idx, bool held) {
  lv_color_t c = held ? lv_color_hex(0x28a745) : lv_color_hex(0xdc3545);
  lv_obj_set_style_bg_color(can_indicators[idx], c, LV_STATE_DEFAULT);
  lv_label_set_text(can_labels[idx], held ? "HELD" : "DROP");
}

static lv_obj_t *create_section_label(lv_obj_t *parent, const char *text,
                                      int y) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, text);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xcccccc), LV_STATE_DEFAULT);
  lv_obj_set_pos(lbl, 8, y);
  return lbl;
}

static void create_top_bar(lv_obj_t *parent) {
  lv_obj_t *top = lv_obj_create(parent);
  lv_obj_set_size(top, SCREEN_W, 36);
  lv_obj_set_pos(top, 0, 0);
  lv_obj_set_style_bg_color(top, lv_color_hex(0x1a1a1a), LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(top, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(top, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_radius(top, 0, LV_STATE_DEFAULT);

  title_label = lv_label_create(top);
  lv_label_set_text(title_label, "NukCPGDrop");
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 6, 0);

  rssi_label = lv_label_create(top);
  lv_label_set_text(rssi_label, "RSSI:--");
  lv_obj_set_style_text_color(rssi_label, lv_color_hex(0x888888),
                               LV_STATE_DEFAULT);
  lv_obj_align(rssi_label, LV_ALIGN_RIGHT_MID, -6, 0);
}

static void create_can_grid(lv_obj_t *parent) {
  int cols = 4;
  int cell_w = 54;
  int cell_h = 44;
  int gap = 6;
  int start_x = (SCREEN_W - (cell_w * cols + gap * (cols - 1))) / 2;
  int start_y = 48;

  create_section_label(parent, "Cans", 44);

  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  for (int i = 0; i < SERVO_COUNT; i++) {
    int row = i / cols;
    int col = i % cols;
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, cell_w, cell_h);
    lv_obj_set_pos(btn, start_x + col * (cell_w + gap),
                   start_y + row * (cell_h + gap));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x28a745), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_user_data(btn, (void *)(intptr_t)i);
    lv_obj_add_event_cb(btn, can_tap_cb, LV_EVENT_CLICKED, NULL);
    can_indicators[i] = btn;
    if (i >= n)
      lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "HELD");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_STATE_DEFAULT);
    lv_obj_center(lbl);
    can_labels[i] = lbl;
  }
}

static void create_action_buttons(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4;
  create_section_label(parent, "Actions", y);

  action_btn = lv_btn_create(parent);
  lv_obj_set_size(action_btn, 224, 40);
  lv_obj_set_pos(action_btn, 8, y + 18);
  lv_obj_set_style_radius(action_btn, 8, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(action_btn, action_btn_cb, LV_EVENT_CLICKED, NULL);
  action_btn_label = lv_label_create(action_btn);
  lv_label_set_text(action_btn_label, "DROP ALL");
  lv_obj_center(action_btn_label);
}

static void create_progress(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12;
  create_section_label(parent, "Progress", y);

  progress_bar = lv_bar_create(parent);
  lv_obj_set_size(progress_bar, 224, 16);
  lv_obj_set_pos(progress_bar, 8, y + 18);
  lv_bar_set_range(progress_bar, 0, SERVO_COUNT);
  lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x333333),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_radius(progress_bar, 4, LV_STATE_DEFAULT);
  lv_obj_set_style_anim_time(progress_bar, 200, LV_STATE_DEFAULT);

  progress_label = lv_label_create(parent);
  lv_label_set_text(progress_label, "0/16");
  lv_obj_set_style_text_color(progress_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_align_to(progress_label, progress_bar, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
}

static void create_interval_control(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 28 + 16;
  create_section_label(parent, "Drop Interval", y);

  lv_obj_t *int_label = lv_label_create(parent);
  lv_label_set_text(int_label, "Interval:");
  lv_obj_set_style_text_color(int_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(int_label, 8, y + 18);

  interval_slider = lv_slider_create(parent);
  lv_obj_set_size(interval_slider, 130, 14);
  lv_obj_set_pos(interval_slider, 68, y + 20);
  lv_slider_set_range(interval_slider, 200, 5000);
  lv_slider_set_value(interval_slider, g_state.custom_interval, LV_ANIM_OFF);
  lv_obj_add_event_cb(interval_slider, interval_slider_cb,
                       LV_EVENT_VALUE_CHANGED, NULL);

  interval_label = lv_label_create(parent);
  char ibuf[16];
  lv_snprintf(ibuf, sizeof(ibuf), "%lu ms", g_state.custom_interval);
  lv_label_set_text(interval_label, ibuf);
  lv_obj_set_style_text_color(interval_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_align_to(interval_label, interval_slider, LV_ALIGN_OUT_RIGHT_MID, 6,
                   0);
  bool rand_start = (g_state.difficulty == DIFFICULTY_RANDOM);
  if (rand_start) {
    lv_obj_add_flag(interval_slider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(interval_label, LV_OBJ_FLAG_HIDDEN);
  }
}

static void create_difficulty_selector(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 28 + 16 + 18 + 20 + 14 + 14;
  create_section_label(parent, "Difficulty", y);

  int bw = 72, bh = 34;
  int gap = 6;
  int start_x = (SCREEN_W - (bw * 3 + gap * 2)) / 2;

  diff_btn_long = lv_btn_create(parent);
  lv_obj_set_size(diff_btn_long, bw, bh);
  lv_obj_set_pos(diff_btn_long, start_x, y + 18);
  lv_obj_set_style_radius(diff_btn_long, 6, LV_STATE_DEFAULT);
  lv_obj_add_flag(diff_btn_long, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_user_data(diff_btn_long, (void *)DIFFICULTY_LONG);
  lv_obj_add_state(diff_btn_long, g_state.difficulty == DIFFICULTY_LONG
                                      ? LV_STATE_CHECKED
                                      : LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(diff_btn_long, lv_color_hex(0x007bff),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(diff_btn_long, lv_color_hex(0x0056b3),
                             LV_STATE_CHECKED);
  lv_obj_add_event_cb(diff_btn_long, diff_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_t *l1 = lv_label_create(diff_btn_long);
  lv_label_set_text(l1, "Long");
  lv_obj_center(l1);

  diff_btn_short = lv_btn_create(parent);
  lv_obj_set_size(diff_btn_short, bw, bh);
  lv_obj_set_pos(diff_btn_short, start_x + bw + gap, y + 18);
  lv_obj_set_style_radius(diff_btn_short, 6, LV_STATE_DEFAULT);
  lv_obj_add_flag(diff_btn_short, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_user_data(diff_btn_short, (void *)DIFFICULTY_SHORT);
  lv_obj_add_state(diff_btn_short, g_state.difficulty == DIFFICULTY_SHORT
                                      ? LV_STATE_CHECKED
                                      : LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(diff_btn_short, lv_color_hex(0x007bff),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(diff_btn_short, lv_color_hex(0x0056b3),
                             LV_STATE_CHECKED);
  lv_obj_add_event_cb(diff_btn_short, diff_btn_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_t *l2 = lv_label_create(diff_btn_short);
  lv_label_set_text(l2, "Short");
  lv_obj_center(l2);

  diff_btn_random = lv_btn_create(parent);
  lv_obj_set_size(diff_btn_random, bw, bh);
  lv_obj_set_pos(diff_btn_random, start_x + 2 * (bw + gap), y + 18);
  lv_obj_set_style_radius(diff_btn_random, 6, LV_STATE_DEFAULT);
  lv_obj_add_flag(diff_btn_random, LV_OBJ_FLAG_CHECKABLE);
  lv_obj_set_user_data(diff_btn_random, (void *)DIFFICULTY_RANDOM);
  lv_obj_add_state(diff_btn_random, g_state.difficulty == DIFFICULTY_RANDOM
                                        ? LV_STATE_CHECKED
                                        : LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(diff_btn_random, lv_color_hex(0x007bff),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_bg_color(diff_btn_random, lv_color_hex(0x0056b3),
                             LV_STATE_CHECKED);
  lv_obj_add_event_cb(diff_btn_random, diff_btn_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  lv_obj_t *l3 = lv_label_create(diff_btn_random);
  lv_label_set_text(l3, "Random");
  lv_obj_center(l3);

  bool is_random = (g_state.difficulty == DIFFICULTY_RANDOM);
  int ry = y + 18 + bh + 8;

  range_min_slider = lv_slider_create(parent);
  lv_obj_set_size(range_min_slider, 100, 14);
  lv_obj_set_pos(range_min_slider, 50, ry);
  lv_slider_set_range(range_min_slider, 100, 5000);
  lv_slider_set_value(range_min_slider, g_state.range_min, LV_ANIM_OFF);
  lv_obj_add_event_cb(range_min_slider, range_min_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  if (!is_random)
    lv_obj_add_flag(range_min_slider, LV_OBJ_FLAG_HIDDEN);

  range_min_label = lv_label_create(parent);
  char mnb[16];
  lv_snprintf(mnb, sizeof(mnb), "Min: %lu ms", g_state.range_min);
  lv_label_set_text(range_min_label, mnb);
  lv_obj_set_style_text_color(range_min_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(range_min_label, 8, ry);
  if (!is_random)
    lv_obj_add_flag(range_min_label, LV_OBJ_FLAG_HIDDEN);

  range_max_slider = lv_slider_create(parent);
  lv_obj_set_size(range_max_slider, 100, 14);
  lv_obj_set_pos(range_max_slider, 50, ry + 20);
  lv_slider_set_range(range_max_slider, 100, 5000);
  lv_slider_set_value(range_max_slider, g_state.range_max, LV_ANIM_OFF);
  lv_obj_add_event_cb(range_max_slider, range_max_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
  if (!is_random)
    lv_obj_add_flag(range_max_slider, LV_OBJ_FLAG_HIDDEN);

  range_max_label = lv_label_create(parent);
  char mxb[16];
  lv_snprintf(mxb, sizeof(mxb), "Max: %lu ms", g_state.range_max);
  lv_label_set_text(range_max_label, mxb);
  lv_obj_set_style_text_color(range_max_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(range_max_label, 8, ry + 18);
  if (!is_random)
    lv_obj_add_flag(range_max_label, LV_OBJ_FLAG_HIDDEN);
}

static void create_servo_positions(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 28 + 16 + 18 + 20 + 14 + 14
          + 18 + 34 + 8 + 20 + 20 + 12;
  create_section_label(parent, "Servo Positions", y);

  int sy = y + 18;

  sv_start_slider = lv_slider_create(parent);
  lv_obj_set_size(sv_start_slider, 130, 14);
  lv_obj_set_pos(sv_start_slider, 60, sy);
  lv_slider_set_range(sv_start_slider, 0, 180);
  lv_slider_set_value(sv_start_slider, g_state.sv_start_pos, LV_ANIM_OFF);
  lv_obj_add_event_cb(sv_start_slider, sv_start_cb, LV_EVENT_VALUE_CHANGED, NULL);

  sv_start_label = lv_label_create(parent);
  char sb[16];
  lv_snprintf(sb, sizeof(sb), "Start: %u\xB0", g_state.sv_start_pos);
  lv_label_set_text(sv_start_label, sb);
  lv_obj_set_style_text_color(sv_start_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(sv_start_label, 8, sy);

  sv_stop_slider = lv_slider_create(parent);
  lv_obj_set_size(sv_stop_slider, 130, 14);
  lv_obj_set_pos(sv_stop_slider, 60, sy + 22);
  lv_slider_set_range(sv_stop_slider, 0, 180);
  lv_slider_set_value(sv_stop_slider, g_state.sv_stop_pos, LV_ANIM_OFF);
  lv_obj_add_event_cb(sv_stop_slider, sv_stop_cb, LV_EVENT_VALUE_CHANGED, NULL);

  sv_stop_label = lv_label_create(parent);
  char stb[16];
  lv_snprintf(stb, sizeof(stb), "Stop: %u\xB0", g_state.sv_stop_pos);
  lv_label_set_text(sv_stop_label, stb);
  lv_obj_set_style_text_color(sv_stop_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(sv_stop_label, 8, sy + 20);
}

static void create_system_status(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 28 + 16 + 18 + 20 + 14 + 14
          + 18 + 34 + 8 + 20 + 20 + 12 + 18 + 22 + 22 + 16;
  create_section_label(parent, "System", y);

  pca9685_label = lv_label_create(parent);
  lv_label_set_text(pca9685_label, "PCA9685: --");
  lv_obj_set_style_text_color(pca9685_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(pca9685_label, 8, y + 18);

  client_count_label = lv_label_create(parent);
  lv_label_set_text(client_count_label, "Clients: 0");
  lv_obj_set_style_text_color(client_count_label, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(client_count_label, 8, y + 34);

  led_indicator = lv_label_create(parent);
  lv_label_set_text(led_indicator, "LED: ---");
  lv_obj_set_style_text_color(led_indicator, lv_color_hex(0xf0f0f0),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(led_indicator, 120, y + 18);
}

static void create_sound_toggle(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 28 + 16 + 18 + 20 + 14 + 14
          + 18 + 34 + 8 + 20 + 20 + 12 + 18 + 22 + 22 + 16 + 18 + 34 + 16;
  create_section_label(parent, "Sound", y);

  lv_obj_t *sl = lv_label_create(parent);
  lv_label_set_text(sl, "Enable:");
  lv_obj_set_style_text_color(sl, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(sl, 8, y + 18);

  sound_switch = lv_switch_create(parent);
  lv_obj_set_pos(sound_switch, 62, y + 16);
  if (g_state.sound_enabled)
    lv_obj_add_state(sound_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sound_switch, sound_switch_cb, LV_EVENT_VALUE_CHANGED,
                      NULL);
}

static void create_audio_level(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 28 + 16 + 18 + 20 + 14 + 14
          + 18 + 34 + 8 + 20 + 20 + 12 + 18 + 22 + 22 + 16 + 18 + 34 + 16
          + 18 + 24 + 16;
  create_section_label(parent, "Mic Level", y);

  audio_bar = lv_bar_create(parent);
  lv_obj_set_size(audio_bar, 224, 16);
  lv_obj_set_pos(audio_bar, 8, y + 18);
  lv_bar_set_range(audio_bar, 0, 4096);
  lv_obj_set_style_bg_color(audio_bar, lv_color_hex(0x333333),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_radius(audio_bar, 4, LV_STATE_DEFAULT);
}

static void create_status_line(lv_obj_t *parent) {
  int y = CONTENT_H - 86;
  status_label = lv_label_create(parent);
  lv_label_set_text(status_label, "Drops: 0  Clients: 0");
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(status_label, 8, y);
}

static void create_battery(lv_obj_t *parent) {
  int y = CONTENT_H - 64;
  battery_label = lv_label_create(parent);
  lv_label_set_text(battery_label, "Battery: --%");
  lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888888),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(battery_label, 8, y);

  battery_bar = lv_bar_create(parent);
  lv_obj_set_size(battery_bar, 100, 12);
  lv_obj_set_pos(battery_bar, 86, y);
  lv_bar_set_range(battery_bar, 0, 100);
  lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x333333),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_radius(battery_bar, 3, LV_STATE_DEFAULT);

  version_label = lv_label_create(parent);
  lv_label_set_text(version_label, "NukCPGDrop v1.0");
  lv_obj_set_style_text_color(version_label, lv_color_hex(0x555555),
                               LV_STATE_DEFAULT);
  lv_obj_set_pos(version_label, 8, y + 16);
}

esp_err_t screen_main_create(void) {
  main_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x0d0d0d),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(main_screen, 0, LV_STATE_DEFAULT);

  scrollable = lv_obj_create(main_screen);
  lv_obj_set_size(scrollable, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(scrollable, 0, 0);
  lv_obj_set_style_bg_color(scrollable, lv_color_hex(0x0d0d0d),
                             LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(scrollable, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(scrollable, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_radius(scrollable, 0, LV_STATE_DEFAULT);
  lv_obj_set_scrollbar_mode(scrollable, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_height(scrollable, CONTENT_H);

  create_top_bar(scrollable);
  create_can_grid(scrollable);
  create_action_buttons(scrollable);
  create_progress(scrollable);
  create_interval_control(scrollable);
  create_difficulty_selector(scrollable);
  create_servo_positions(scrollable);
  create_system_status(scrollable);
  create_sound_toggle(scrollable);
  create_audio_level(scrollable);
  create_status_line(scrollable);
  create_battery(scrollable);

  lv_scr_load(main_screen);
  ESP_LOGI(TAG, "main screen created (%dx%d, content %dpx)", SCREEN_W, SCREEN_H,
           CONTENT_H);
  return ESP_OK;
}

lv_obj_t *screen_main_get(void) { return main_screen; }

void screen_main_update_indicators(void) {
  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  for (int i = 0; i < SERVO_COUNT; i++) {
    bool hidden = (i >= n);
    if (can_indicators[i]) {
      if (hidden)
        lv_obj_add_flag(can_indicators[i], LV_OBJ_FLAG_HIDDEN);
      else
        lv_obj_clear_flag(can_indicators[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!hidden) {
      bool held = servos_is_held((uint8_t)i);
      set_can_color((uint8_t)i, held);
    }
  }
}

void screen_main_update_progress(void) {
  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  uint32_t completed = g_state.last_completed;
  if (completed > n) completed = n;
  lv_bar_set_range(progress_bar, 0, n);
  lv_bar_set_value(progress_bar, completed, LV_ANIM_ON);
  char buf[8];
  lv_snprintf(buf, sizeof(buf), "%lu/%d", completed, n);
  lv_label_set_text(progress_label, buf);
}

void screen_main_update_rssi(int rssi) {
  char buf[32];
  lv_snprintf(buf, sizeof(buf), "RSSI: %d dBm", rssi);
  lv_label_set_text(rssi_label, buf);
}

void screen_main_update_interval(uint32_t interval_ms) {
  lv_slider_set_value(interval_slider, interval_ms, LV_ANIM_OFF);
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "%lu ms", interval_ms);
  lv_label_set_text(interval_label, buf);
}

void screen_main_update_audio_level(int level) {
  if (audio_bar) {
    lv_bar_set_value(audio_bar, level > 4096 ? 4096 : level, LV_ANIM_OFF);
  }
}

void screen_main_set_seq_running(bool running) {
  g_seq_running = running;
}

void screen_main_update_status(void) {
  char buf[48];
  lv_snprintf(buf, sizeof(buf), "Drops: %lu  Clients: %d", g_state.drop_count,
              wifi_ap_get_sta_count());
  lv_label_set_text(status_label, buf);

  if (client_count_label)
    lv_label_set_text_fmt(client_count_label, "Clients: %d",
                          wifi_ap_get_sta_count());

  if (pca9685_label) {
    extern bool g_pca9685_present;
    lv_label_set_text(pca9685_label,
                      g_pca9685_present ? "PCA9685: OK" : "PCA9685: MISSING");
  }

  // Dynamic action button + auto-clear g_seq_running
  bool all_held = true;
  for (int i = 0; i < SERVO_COUNT; i++) {
    if (!servos_is_held(i)) {
      all_held = false;
      break;
    }
  }
  if (action_btn && action_btn_label) {
    if (g_seq_running) {
      lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x555555),
                                 LV_STATE_DEFAULT);
      lv_label_set_text(action_btn_label, "RUNNING...");
      if (all_held) {
        g_seq_running = false;
        lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x28a745),
                                   LV_STATE_DEFAULT);
        lv_label_set_text(action_btn_label, "DROP ALL");
      }
    } else if (all_held) {
      lv_obj_set_style_bg_color(action_btn, lv_color_hex(0xe63946),
                                 LV_STATE_DEFAULT);
      lv_label_set_text(action_btn_label, "DROP ALL");
    } else {
      lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x28a745),
                                 LV_STATE_DEFAULT);
      lv_label_set_text(action_btn_label, "RESET");
    }
  }

  // LED indicator color
  if (led_indicator) {
    extern void led_get_color(led_color_t * c);
    led_color_t lc;
    led_get_color(&lc);
    char l[24];
    lv_snprintf(l, sizeof(l), "LED: rgb(%d,%d,%d)", lc.r, lc.g, lc.b);
    lv_label_set_text(led_indicator, l);
  }
}

void screen_main_update_battery(void) {
  int pct = g_state.battery_percent;
  if (pct < 0 || pct > 100) {
    lv_label_set_text(battery_label, "Battery: --%");
    return;
  }
  char buf[24];
  lv_snprintf(buf, sizeof(buf), "Battery: %d%%", pct);
  lv_label_set_text(battery_label, buf);
  lv_bar_set_value(battery_bar, pct, LV_ANIM_OFF);
  lv_color_t bc = pct > 20 ? lv_color_hex(0x28a745) : lv_color_hex(0xe63946);
  lv_obj_set_style_bg_color(battery_bar, bc, LV_PART_INDICATOR);
}
