/*
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/led.h>
#include <zephyr/pm/device.h>
#include <zephyr/init.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>
#include <zmk/activity.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/activity_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_DISPLAY) && DT_HAS_CHOSEN(zephyr_display) && DT_HAS_CHOSEN(zmk_display_led)

extern struct k_timer display_timer;
extern const struct init_entry __init_POST_KERNEL_start[];
extern const struct init_entry __init_APPLICATION_start[];

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *display_led = DEVICE_DT_GET(DT_PARENT(DT_CHOSEN(zmk_display_led)));
static const uint8_t display_led_idx = DT_NODE_CHILD_IDX(DT_CHOSEN(zmk_display_led));

static bool state_initialized = false;
static bool display_enabled = false;

static void reinit_display_device(const struct device *dev) {
    for (const struct init_entry *entry = __init_POST_KERNEL_start;
         entry < __init_APPLICATION_start; entry++) {
        if (entry->dev == dev && entry->init_fn.dev != NULL) {
            entry->init_fn.dev(dev);
            return;
        }
    }
}

static void apply_usb_display_power(struct k_work *work) {
    if (!device_is_ready(display) || !device_is_ready(display_led)) {
        return;
    }

    bool should_enable = zmk_usb_is_powered() &&
                         (zmk_activity_get_state() != ZMK_ACTIVITY_SLEEP);

    if (state_initialized && (should_enable == display_enabled)) {
        return;
    }
    state_initialized = true;

    if (should_enable) {
        k_timer_stop(&display_timer);
        k_sleep(K_MSEC(40));
        reinit_display_device(display);
#if IS_ENABLED(CONFIG_PM_DEVICE)
        pm_device_action_run(display, PM_DEVICE_ACTION_RESUME);
#endif
        lv_obj_t *scr = lv_scr_act();
        if (scr != NULL) {
            lv_obj_invalidate(scr);
            lv_refr_now(NULL);
        }
        display_blanking_off(display);
        led_on(display_led, display_led_idx);
        k_timer_start(&display_timer, K_MSEC(CONFIG_ZMK_DISPLAY_TICK_PERIOD_MS),
                      K_MSEC(CONFIG_ZMK_DISPLAY_TICK_PERIOD_MS));
        display_enabled = true;
    } else {
        k_timer_stop(&display_timer);
        led_off(display_led, display_led_idx);
        display_blanking_on(display);
#if IS_ENABLED(CONFIG_PM_DEVICE)
        pm_device_action_run(display, PM_DEVICE_ACTION_SUSPEND);
#endif
        display_enabled = false;
    }
}

K_WORK_DEFINE(usb_display_power_work, apply_usb_display_power);

static void init_check_work_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(usb_display_init_check_work, init_check_work_cb);

static void init_check_work_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        k_work_reschedule(&usb_display_init_check_work, K_MSEC(20));
        return;
    }
    k_work_submit_to_queue(zmk_display_work_q(), &usb_display_power_work);
}

static int usb_display_power_event_listener(const zmk_event_t *eh) {
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &usb_display_power_work);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(usb_display_power, usb_display_power_event_listener);
ZMK_SUBSCRIPTION(usb_display_power, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(usb_display_power, zmk_activity_state_changed);

static int usb_display_power_init(void) {
    if (!zmk_usb_is_powered() && device_is_ready(display_led)) {
        led_off(display_led, display_led_idx);
    }
    k_work_schedule(&usb_display_init_check_work, K_MSEC(10));
    return 0;
}

SYS_INIT(usb_display_power_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#endif
