/*
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/peripheral_status.h>
#include <zmk/display/widgets/output_status.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/usb.h>

LV_IMAGE_DECLARE(straw_hat_img);

#define STAR_POOL_SIZE 8
#define SCREEN_W 135
#define SCREEN_H 240
#define STAR_SIZE 11

/* 11x11 anti-aliased 4-point sparkle star (A8 alpha mask, recolorable) */
static const uint8_t star_alpha_map[STAR_SIZE * STAR_SIZE] = {
      0,   0,   0,   0,   0,  80,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0, 150,   0,   0,   0,   0,   0,
      0,   0,   0,   0,  40, 210,  40,   0,   0,   0,   0,
      0,   0,   0,  40, 120, 240, 120,  40,   0,   0,   0,
      0,   0,  40, 120, 220, 255, 220, 120,  40,   0,   0,
     80, 150, 210, 240, 255, 255, 255, 240, 210, 150,  80,
      0,   0,  40, 120, 220, 255, 220, 120,  40,   0,   0,
      0,   0,   0,  40, 120, 240, 120,  40,   0,   0,   0,
      0,   0,   0,   0,  40, 210,  40,   0,   0,   0,   0,
      0,   0,   0,   0,   0, 150,   0,   0,   0,   0,   0,
      0,   0,   0,   0,   0,  80,   0,   0,   0,   0,   0,
};

static const lv_image_dsc_t star_img = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_A8,
        .flags = 0,
        .w = STAR_SIZE,
        .h = STAR_SIZE,
        .stride = STAR_SIZE,
    },
    .data_size = sizeof(star_alpha_map),
    .data = star_alpha_map,
};

static const uint32_t star_palette[] = {
    0xFFFFFF, /* pure white */
    0xFFD166, /* straw gold */
    0xFFE66D, /* warm yellow */
    0x90E0EF, /* sky gleam */
};

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
static struct zmk_widget_battery_status battery_status_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
static struct zmk_widget_peripheral_status peripheral_status_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
static struct zmk_widget_output_status output_status_widget;
#endif

static lv_obj_t *star_objs[STAR_POOL_SIZE];
static uint8_t star_next_idx = 0;
static lv_obj_t *key_count_label = NULL;

static atomic_t total_key_presses = ATOMIC_INIT(0);
static atomic_t pending_star_blinks = ATOMIC_INIT(0);
static uint32_t rng_state = 0x5A17C0DEu;

static uint32_t next_rand(void) {
    uint32_t x = rng_state ^ k_uptime_get_32();
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x ? x : 0xA341316Cu;
    return rng_state;
}

static void hat_rot_exec_cb(void *var, int32_t val) {
    lv_image_set_rotation((lv_obj_t *)var, val);
}

static void hat_ty_exec_cb(void *var, int32_t val) {
    lv_obj_set_style_translate_y((lv_obj_t *)var, val, LV_PART_MAIN);
}

static void hat_tx_exec_cb(void *var, int32_t val) {
    lv_obj_set_style_translate_x((lv_obj_t *)var, val, LV_PART_MAIN);
}

static void star_opa_exec_cb(void *var, int32_t val) {
    lv_obj_set_style_image_opa((lv_obj_t *)var, (lv_opa_t)val, LV_PART_MAIN);
}

static void trigger_star_blink(void) {
    lv_obj_t *star = star_objs[star_next_idx];
    star_next_idx = (star_next_idx + 1) % STAR_POOL_SIZE;
    if (star == NULL) {
        return;
    }

    uint32_t r1 = next_rand();
    uint32_t r2 = next_rand();
    uint32_t r3 = next_rand();

    /* Random position in the open area between top status bar and bottom counter */
    int32_t x = 4 + (int32_t)(r1 % (SCREEN_W - STAR_SIZE - 8));
    int32_t y = 24 + (int32_t)(r2 % (SCREEN_H - 64));

    uint32_t color_hex = star_palette[r3 % ARRAY_SIZE(star_palette)];

    lv_anim_delete(star, star_opa_exec_cb);
    lv_obj_set_pos(star, x, y);
    lv_obj_set_style_image_recolor(star, lv_color_hex(color_hex), LV_PART_MAIN);
    lv_obj_set_style_image_opa(star, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, star);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 110);
    lv_anim_set_playback_duration(&a, 420);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, star_opa_exec_cb);
    lv_anim_start(&a);
}

static void keypress_ui_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (key_count_label != NULL) {
        unsigned int count = (unsigned int)atomic_get(&total_key_presses);
        lv_label_set_text_fmt(key_count_label, "KEYS: %u", count);
    }

    atomic_val_t blinks = atomic_set(&pending_star_blinks, 0);
    if (zmk_usb_is_powered() && blinks > 0) {
        if (blinks > 3) {
            blinks = 3;
        }
        for (atomic_val_t i = 0; i < blinks; i++) {
            trigger_star_blink();
        }
    }
}

K_WORK_DEFINE(keypress_ui_work, keypress_ui_work_handler);

static int keypress_event_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    atomic_inc(&total_key_presses);
    atomic_inc(&pending_star_blinks);

    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &keypress_ui_work);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(straw_hat_keypress, keypress_event_listener);
ZMK_SUBSCRIPTION(straw_hat_keypress, zmk_position_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lv_color_white(), LV_PART_MAIN);

    /* Straw hat artwork with slow organic wind-wave animation */
    lv_obj_t *art = lv_image_create(screen);
    lv_image_set_src(art, &straw_hat_img);
    lv_image_set_pivot(art, straw_hat_img.header.w / 2, straw_hat_img.header.h / 2);
    lv_obj_align(art, LV_ALIGN_CENTER, 0, -4);

    lv_anim_t anim_rot;
    lv_anim_init(&anim_rot);
    lv_anim_set_var(&anim_rot, art);
    lv_anim_set_values(&anim_rot, -55, 55); /* +/- 5.5 degrees */
    lv_anim_set_duration(&anim_rot, 3800);
    lv_anim_set_playback_duration(&anim_rot, 3800);
    lv_anim_set_repeat_count(&anim_rot, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim_rot, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim_rot, hat_rot_exec_cb);
    lv_anim_start(&anim_rot);

    lv_anim_t anim_ty;
    lv_anim_init(&anim_ty);
    lv_anim_set_var(&anim_ty, art);
    lv_anim_set_values(&anim_ty, -5, 5);
    lv_anim_set_duration(&anim_ty, 2900);
    lv_anim_set_playback_duration(&anim_ty, 2900);
    lv_anim_set_repeat_count(&anim_ty, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim_ty, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim_ty, hat_ty_exec_cb);
    lv_anim_start(&anim_ty);

    lv_anim_t anim_tx;
    lv_anim_init(&anim_tx);
    lv_anim_set_var(&anim_tx, art);
    lv_anim_set_values(&anim_tx, -3, 3);
    lv_anim_set_duration(&anim_tx, 4700);
    lv_anim_set_playback_duration(&anim_tx, 4700);
    lv_anim_set_repeat_count(&anim_tx, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_path_cb(&anim_tx, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim_tx, hat_tx_exec_cb);
    lv_anim_start(&anim_tx);

    /* Pre-allocated pool of blinking sparkle stars */
    for (int i = 0; i < STAR_POOL_SIZE; i++) {
        lv_obj_t *star = lv_image_create(screen);
        lv_image_set_src(star, &star_img);
        lv_obj_set_style_image_recolor(star, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_image_recolor_opa(star, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_image_opa(star, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_pos(star, 0, 0);
        star_objs[i] = star;
    }

    /* Bottom key press counter */
    key_count_label = lv_label_create(screen);
    lv_obj_set_style_text_font(key_count_label, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(key_count_label, lv_color_hex(0xFFD166), LV_PART_MAIN);
    lv_label_set_text_fmt(key_count_label, "KEYS: %u",
                          (unsigned int)atomic_get(&total_key_presses));
    lv_obj_align(key_count_label, LV_ALIGN_BOTTOM_MID, 0, -10);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
    zmk_widget_peripheral_status_init(&peripheral_status_widget, screen);
    lv_obj_align(zmk_widget_peripheral_status_obj(&peripheral_status_widget),
                 LV_ALIGN_TOP_LEFT, 4, 2);
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget),
                 LV_ALIGN_TOP_LEFT, 4, 2);
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
    zmk_widget_battery_status_init(&battery_status_widget, screen);
    lv_obj_align(zmk_widget_battery_status_obj(&battery_status_widget),
                 LV_ALIGN_TOP_RIGHT, -4, 2);
#endif

    return screen;
}
