#include "screen_main.h"
#include "led.h"
#include "servos.h"
#include "state.h"
#include "wifi_manager.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "screen_main";

#define SCREEN_W 240
#define SCREEN_H 320
#define CONTENT_H 700

static lv_obj_t *main_screen = NULL;
static lv_obj_t *scrollable = NULL;

static lv_obj_t *title_label = NULL;
static lv_obj_t *rssi_label = NULL;

static lv_obj_t *can_indicators[SERVO_COUNT] = {NULL};
static lv_obj_t *can_labels[SERVO_COUNT] = {NULL};

static lv_obj_t *action_btn = NULL;
static lv_obj_t *action_btn_label = NULL;
static bool g_seq_running = false;

static lv_obj_t *range_min_slider = NULL;
static lv_obj_t *range_min_label = NULL;
static lv_obj_t *range_max_slider = NULL;
static lv_obj_t *range_max_label = NULL;

static lv_obj_t *sv_held_label = NULL;
static lv_obj_t *sv_held_slider = NULL;
static lv_obj_t *sv_dropped_label = NULL;
static lv_obj_t *sv_dropped_slider = NULL;

static lv_obj_t *sv_count_label = NULL;
static lv_obj_t *sv_count_val = NULL;

static lv_obj_t *status_label = NULL;
static lv_obj_t *battery_bar = NULL;
static lv_obj_t *battery_label = NULL;
static lv_obj_t *version_label = NULL;

static lv_obj_t *led_indicator = NULL;
static lv_obj_t *sound_switch = NULL;
static lv_obj_t *client_count_label = NULL;
static lv_obj_t *audio_bar = NULL;
#define AUDIO_BINS 8
static lv_obj_t *audio_bins[AUDIO_BINS] = {NULL};
static lv_obj_t *pca9685_label = NULL;

static void action_btn_cb(lv_event_t *e) {
  bool all_held = true;
  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  for (int i = 0; i < n; i++) {
    if (!servos_is_held(i)) { all_held = false; break; }
  }
  if (all_held) {
    g_seq_running = true;
    servos_start_sequence();
  } else {
    servos_hold_all();
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

static void sv_held_cb(lv_event_t *e) {
  uint32_t val = (uint32_t)lv_slider_get_value(lv_event_get_target(e));
  g_state.sv_start_pos = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "Held: %lu\xB0", val);
  lv_label_set_text(sv_held_label, buf);
  state_save();
  // Move all currently-held servos to new position
  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  for (int i = 0; i < n; i++) {
    if (servos_is_held(i))
      servos_set(i, SERVO_POSITION_HOLD);
  }
}

static void sv_dropped_cb(lv_event_t *e) {
  uint32_t val = (uint32_t)lv_slider_get_value(lv_event_get_target(e));
  g_state.sv_stop_pos = val;
  char buf[16];
  lv_snprintf(buf, sizeof(buf), "Dropped: %lu\xB0", val);
  lv_label_set_text(sv_dropped_label, buf);
  state_save();
  // Move all currently-dropped servos to new position
  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  for (int i = 0; i < n; i++) {
    if (!servos_is_held(i))
      servos_set(i, SERVO_POSITION_RELEASE);
  }
}

static void sv_count_minus_cb(lv_event_t *e) {
  if (g_state.active_servos > 1) {
    g_state.active_servos--;
    state_save();
    char buf[4];
    lv_snprintf(buf, sizeof(buf), "%u", g_state.active_servos);
    lv_label_set_text(sv_count_val, buf);
  }
}

static void sv_count_plus_cb(lv_event_t *e) {
  if (g_state.active_servos < SERVO_COUNT) {
    g_state.active_servos++;
    state_save();
    char buf[4];
    lv_snprintf(buf, sizeof(buf), "%u", g_state.active_servos);
    lv_label_set_text(sv_count_val, buf);
  }
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

static lv_obj_t *create_section_label(lv_obj_t *parent, const char *text, int y) {
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
  lv_obj_set_style_text_color(title_label, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_align(title_label, LV_ALIGN_LEFT_MID, 6, 0);

  rssi_label = lv_label_create(top);
  lv_label_set_text(rssi_label, "RSSI:--");
  lv_obj_set_style_text_color(rssi_label, lv_color_hex(0x888888), LV_STATE_DEFAULT);
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
    lv_obj_set_pos(btn, start_x + col * (cell_w + gap), start_y + row * (cell_h + gap));
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x28a745), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 6, LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_STATE_DEFAULT);
    lv_obj_set_user_data(btn, (void *)(intptr_t)i);
    lv_obj_add_event_cb(btn, can_tap_cb, LV_EVENT_CLICKED, NULL);
    can_indicators[i] = btn;
    if (i >= n) lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "HELD");
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), LV_STATE_DEFAULT);
    lv_obj_center(lbl);
    can_labels[i] = lbl;
  }
}

static void create_action_buttons(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4;
  action_btn = lv_btn_create(parent);
  lv_obj_set_size(action_btn, 224, 40);
  lv_obj_set_pos(action_btn, 8, y + 18);
  lv_obj_set_style_radius(action_btn, 8, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(action_btn, action_btn_cb, LV_EVENT_CLICKED, NULL);
  action_btn_label = lv_label_create(action_btn);
  lv_label_set_text(action_btn_label, "DROP ALL");
  lv_obj_center(action_btn_label);
}

static void create_interval_control(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12;
  create_section_label(parent, "Interval Range", y);

  int ry = y + 18;

  range_min_slider = lv_slider_create(parent);
  lv_obj_set_size(range_min_slider, 100, 14);
  lv_obj_set_pos(range_min_slider, 50, ry);
  lv_slider_set_range(range_min_slider, 100, 5000);
  lv_slider_set_value(range_min_slider, g_state.range_min, LV_ANIM_OFF);
  lv_obj_add_event_cb(range_min_slider, range_min_cb, LV_EVENT_VALUE_CHANGED, NULL);

  range_min_label = lv_label_create(parent);
  char mnb[16];
  lv_snprintf(mnb, sizeof(mnb), "Min: %lu ms", g_state.range_min);
  lv_label_set_text(range_min_label, mnb);
  lv_obj_set_style_text_color(range_min_label, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(range_min_label, 8, ry);

  range_max_slider = lv_slider_create(parent);
  lv_obj_set_size(range_max_slider, 100, 14);
  lv_obj_set_pos(range_max_slider, 50, ry + 20);
  lv_slider_set_range(range_max_slider, 100, 5000);
  lv_slider_set_value(range_max_slider, g_state.range_max, LV_ANIM_OFF);
  lv_obj_add_event_cb(range_max_slider, range_max_cb, LV_EVENT_VALUE_CHANGED, NULL);

  range_max_label = lv_label_create(parent);
  char mxb[16];
  lv_snprintf(mxb, sizeof(mxb), "Max: %lu ms", g_state.range_max);
  lv_label_set_text(range_max_label, mxb);
  lv_obj_set_style_text_color(range_max_label, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(range_max_label, 8, ry + 18);
}

static void create_servo_positions(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 24 + 24 + 16;
  create_section_label(parent, "Servo Positions", y);

  int sy = y + 18;

  sv_held_slider = lv_slider_create(parent);
  lv_obj_set_size(sv_held_slider, 130, 14);
  lv_obj_set_pos(sv_held_slider, 60, sy);
  lv_slider_set_range(sv_held_slider, 0, 180);
  lv_slider_set_value(sv_held_slider, g_state.sv_start_pos, LV_ANIM_OFF);
  lv_obj_add_event_cb(sv_held_slider, sv_held_cb, LV_EVENT_VALUE_CHANGED, NULL);

  sv_held_label = lv_label_create(parent);
  char sb[16];
  lv_snprintf(sb, sizeof(sb), "Held: %u\xB0", g_state.sv_start_pos);
  lv_label_set_text(sv_held_label, sb);
  lv_obj_set_style_text_color(sv_held_label, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(sv_held_label, 8, sy);

  sv_dropped_slider = lv_slider_create(parent);
  lv_obj_set_size(sv_dropped_slider, 130, 14);
  lv_obj_set_pos(sv_dropped_slider, 60, sy + 22);
  lv_slider_set_range(sv_dropped_slider, 0, 180);
  lv_slider_set_value(sv_dropped_slider, g_state.sv_stop_pos, LV_ANIM_OFF);
  lv_obj_add_event_cb(sv_dropped_slider, sv_dropped_cb, LV_EVENT_VALUE_CHANGED, NULL);

  sv_dropped_label = lv_label_create(parent);
  char stb[16];
  lv_snprintf(stb, sizeof(stb), "Dropped: %u\xB0", g_state.sv_stop_pos);
  lv_label_set_text(sv_dropped_label, stb);
  lv_obj_set_style_text_color(sv_dropped_label, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(sv_dropped_label, 8, sy + 20);
}

static void create_servo_count(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 24 + 24 + 16 + 18 + 24 + 24 + 8;
  create_section_label(parent, "Servo Count", y);

  int cy = y + 18;

  lv_obj_t *minus_btn = lv_btn_create(parent);
  lv_obj_set_size(minus_btn, 36, 32);
  lv_obj_set_pos(minus_btn, 50, cy);
  lv_obj_set_style_radius(minus_btn, 6, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(minus_btn, sv_count_minus_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *minus_lbl = lv_label_create(minus_btn);
  lv_label_set_text(minus_lbl, "-");
  lv_obj_center(minus_lbl);

  sv_count_val = lv_label_create(parent);
  char buf[4];
  lv_snprintf(buf, sizeof(buf), "%u", g_state.active_servos);
  lv_label_set_text(sv_count_val, buf);
  lv_obj_set_style_text_color(sv_count_val, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(sv_count_val, 93, cy + 6);

  lv_obj_t *plus_btn = lv_btn_create(parent);
  lv_obj_set_size(plus_btn, 36, 32);
  lv_obj_set_pos(plus_btn, 120, cy);
  lv_obj_set_style_radius(plus_btn, 6, LV_STATE_DEFAULT);
  lv_obj_add_event_cb(plus_btn, sv_count_plus_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *plus_lbl = lv_label_create(plus_btn);
  lv_label_set_text(plus_lbl, "+");
  lv_obj_center(plus_lbl);
}

static void create_system_status(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 24 + 24 + 16 + 18 + 24 + 24 + 8 + 18 + 36 + 16;
  create_section_label(parent, "System", y);

  pca9685_label = lv_label_create(parent);
  lv_label_set_text(pca9685_label, "PCA9685: --");
  lv_obj_set_style_text_color(pca9685_label, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(pca9685_label, 8, y + 18);

  client_count_label = lv_label_create(parent);
  lv_label_set_text(client_count_label, "Clients: 0");
  lv_obj_set_style_text_color(client_count_label, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(client_count_label, 8, y + 34);

  led_indicator = lv_label_create(parent);
  lv_label_set_text(led_indicator, "LED: ---");
  lv_obj_set_style_text_color(led_indicator, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(led_indicator, 120, y + 18);
}

static void create_sound_toggle(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 24 + 24 + 16 + 18 + 24 + 24 + 8 + 18 + 36 + 16 + 18 + 34 + 16;
  create_section_label(parent, "Sound", y);

  lv_obj_t *sl = lv_label_create(parent);
  lv_label_set_text(sl, "Enable:");
  lv_obj_set_style_text_color(sl, lv_color_hex(0xf0f0f0), LV_STATE_DEFAULT);
  lv_obj_set_pos(sl, 8, y + 18);

  sound_switch = lv_switch_create(parent);
  lv_obj_set_pos(sound_switch, 62, y + 16);
  if (g_state.sound_enabled) lv_obj_add_state(sound_switch, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sound_switch, sound_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void create_audio_level(lv_obj_t *parent) {
  int y = 48 + 4 * 50 + 4 + 18 + 40 + 12 + 18 + 24 + 24 + 16 + 18 + 24 + 24 + 8 + 18 + 36 + 16 + 18 + 34 + 16 + 18 + 24 + 16;
  create_section_label(parent, "Mic Spectrum", y);

  int bar_w = 24, bar_gap = 4, bar_h = 40;
  int start_x = (SCREEN_W - (AUDIO_BINS * (bar_w + bar_gap) - bar_gap)) / 2;
  int by = y + 18;
  for (int i = 0; i < AUDIO_BINS; i++) {
    audio_bins[i] = lv_bar_create(parent);
    lv_obj_set_size(audio_bins[i], bar_w, bar_h);
    lv_obj_set_pos(audio_bins[i], start_x + i * (bar_w + bar_gap), by);
    lv_bar_set_range(audio_bins[i], 0, 4096);
    lv_obj_set_style_bg_color(audio_bins[i], lv_color_hex(0x222222), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(audio_bins[i], 2, LV_STATE_DEFAULT);
    lv_obj_set_style_anim_time(audio_bins[i], 100, LV_STATE_DEFAULT);
    lv_color_t c;
    if (i < 3) c = lv_color_hex(0x28a745);
    else if (i < 6) c = lv_color_hex(0xe6a700);
    else c = lv_color_hex(0xe63946);
    lv_obj_set_style_bg_color(audio_bins[i], c, LV_PART_INDICATOR);
  }
}

static void create_status_line(lv_obj_t *parent) {
  int y = CONTENT_H - 86;
  status_label = lv_label_create(parent);
  lv_label_set_text(status_label, "Drops: 0  Clients: 0");
  lv_obj_set_style_text_color(status_label, lv_color_hex(0x888888), LV_STATE_DEFAULT);
  lv_obj_set_pos(status_label, 8, y);
}

static void create_battery(lv_obj_t *parent) {
  int y = CONTENT_H - 64;
  battery_label = lv_label_create(parent);
  lv_label_set_text(battery_label, "Battery: --%");
  lv_obj_set_style_text_color(battery_label, lv_color_hex(0x888888), LV_STATE_DEFAULT);
  lv_obj_set_pos(battery_label, 8, y);

  battery_bar = lv_bar_create(parent);
  lv_obj_set_size(battery_bar, 100, 12);
  lv_obj_set_pos(battery_bar, 86, y);
  lv_bar_set_range(battery_bar, 0, 100);
  lv_obj_set_style_bg_color(battery_bar, lv_color_hex(0x333333), LV_STATE_DEFAULT);
  lv_obj_set_style_radius(battery_bar, 3, LV_STATE_DEFAULT);

  version_label = lv_label_create(parent);
  lv_label_set_text(version_label, "NukCPGDrop v1.0");
  lv_obj_set_style_text_color(version_label, lv_color_hex(0x555555), LV_STATE_DEFAULT);
  lv_obj_set_pos(version_label, 8, y + 16);
}

esp_err_t screen_main_create(void) {
  main_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(main_screen, lv_color_hex(0x0d0d0d), LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(main_screen, 0, LV_STATE_DEFAULT);

  scrollable = lv_obj_create(main_screen);
  lv_obj_set_size(scrollable, SCREEN_W, SCREEN_H);
  lv_obj_set_pos(scrollable, 0, 0);
  lv_obj_set_style_bg_color(scrollable, lv_color_hex(0x0d0d0d), LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(scrollable, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(scrollable, 0, LV_STATE_DEFAULT);
  lv_obj_set_style_radius(scrollable, 0, LV_STATE_DEFAULT);
  lv_obj_set_scrollbar_mode(scrollable, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_height(scrollable, CONTENT_H);

  create_top_bar(scrollable);
  create_can_grid(scrollable);
  create_action_buttons(scrollable);
  create_interval_control(scrollable);
  create_servo_positions(scrollable);
  create_servo_count(scrollable);
  create_system_status(scrollable);
  create_sound_toggle(scrollable);
  create_audio_level(scrollable);
  create_status_line(scrollable);
  create_battery(scrollable);

  lv_scr_load(main_screen);
  ESP_LOGI(TAG, "main screen created (%dx%d, content %dpx)", SCREEN_W, SCREEN_H, CONTENT_H);
  return ESP_OK;
}

lv_obj_t *screen_main_get(void) { return main_screen; }

void screen_main_update_indicators(void) {
  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  for (int i = 0; i < SERVO_COUNT; i++) {
    bool hide = (i >= n);
    if (can_indicators[i]) {
      if (hide) lv_obj_add_flag(can_indicators[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_clear_flag(can_indicators[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (!hide) {
      bool held = servos_is_held((uint8_t)i);
      set_can_color((uint8_t)i, held);
    }
  }
}

void screen_main_update_status(void) {
  char buf[48];
  lv_snprintf(buf, sizeof(buf), "Drops: %lu  Clients: %d", g_state.drop_count, wifi_ap_get_sta_count());
  lv_label_set_text(status_label, buf);

  if (client_count_label)
    lv_label_set_text_fmt(client_count_label, "Clients: %d", wifi_ap_get_sta_count());

  if (pca9685_label) {
    extern bool g_pca9685_present;
    lv_label_set_text(pca9685_label, g_pca9685_present ? "PCA9685: OK" : "PCA9685: MISSING");
  }

  uint8_t n = g_state.active_servos > 0 ? g_state.active_servos : SERVO_COUNT;
  if (n > SERVO_COUNT) n = SERVO_COUNT;
  bool all_held = true;
  for (int i = 0; i < n; i++) {
    if (!servos_is_held(i)) { all_held = false; break; }
  }

  if (action_btn && action_btn_label) {
    if (g_seq_running && !all_held) {
      lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x555555), LV_STATE_DEFAULT);
      lv_label_set_text(action_btn_label, "RUNNING...");
    } else {
      if (g_seq_running) g_seq_running = false;
      if (all_held) {
        lv_obj_set_style_bg_color(action_btn, lv_color_hex(0xe63946), LV_STATE_DEFAULT);
        lv_label_set_text(action_btn_label, "DROP ALL");
      } else {
        lv_obj_set_style_bg_color(action_btn, lv_color_hex(0x28a745), LV_STATE_DEFAULT);
        lv_label_set_text(action_btn_label, "RESET");
      }
    }
  }

  if (led_indicator) {
    extern void led_get_color(led_color_t *c);
    led_color_t lc;
    led_get_color(&lc);
    char l[24];
    lv_snprintf(l, sizeof(l), "LED: rgb(%d,%d,%d)", lc.r, lc.g, lc.b);
    lv_label_set_text(led_indicator, l);
  }
}

void screen_main_update_rssi(int rssi) {
  char buf[32];
  lv_snprintf(buf, sizeof(buf), "RSSI: %d dBm", rssi);
  lv_label_set_text(rssi_label, buf);
}

void screen_main_update_audio_level(int level) {
  // Legacy single-bar fallback
  if (audio_bar) {
    lv_bar_set_value(audio_bar, level > 4096 ? 4096 : level, LV_ANIM_OFF);
  }
}

void screen_main_update_audio_spectrum(const int *bins, int count) {
  for (int i = 0; i < AUDIO_BINS && i < count; i++) {
    if (audio_bins[i]) {
      int val = bins[i];
      if (val > 4096) val = 4096;
      lv_bar_set_value(audio_bins[i], val, LV_ANIM_ON);
    }
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

void screen_main_set_seq_running(bool running) { g_seq_running = running; }
