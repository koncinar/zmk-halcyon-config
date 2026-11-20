/*
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>

#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/peripheral_status.h>
#include <zmk/display/widgets/output_status.h>

LV_IMAGE_DECLARE(straw_hat_img);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
static struct zmk_widget_battery_status battery_status_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
static struct zmk_widget_peripheral_status peripheral_status_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
static struct zmk_widget_output_status output_status_widget;
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lv_color_white(), LV_PART_MAIN);

    lv_obj_t *art = lv_image_create(screen);
    lv_image_set_src(art, &straw_hat_img);
    lv_obj_align(art, LV_ALIGN_CENTER, 0, 10);

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
