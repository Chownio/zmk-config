/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Custom behavior: blanks the display (sends SSD1306 display-off command
 * over I2C) before entering soft off. This works around the issue where
 * zmk_pm_soft_off() suspends the I2C controller before the display driver
 * gets a chance to send its display-off command, leaving OLEDs stuck on.
 */

#define DT_DRV_COMPAT zmk_behavior_display_soft_off

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/pm.h>

#if DT_HAS_CHOSEN(zephyr_display)
#include <zephyr/drivers/display.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define IS_SPLIT_PERIPHERAL \
    (IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

/*
 * Disconnect the key matrix GPIO pins before entering soft off.
 * On nRF52, GPIO SENSE can persist into SYSTEMOFF and any key press that
 * bridges a column to a row would generate a DETECT wakeup signal.
 * Reconfiguring every matrix pin as a plain input (no pull, no SENSE)
 * prevents that.
 */
#define KSCAN_NODE DT_CHOSEN(zmk_kscan)

static void disconnect_kscan_gpios(void) {
#if DT_NODE_HAS_PROP(KSCAN_NODE, row_gpios)
    static const struct gpio_dt_spec rows[] = {
        DT_FOREACH_PROP_ELEM_SEP(KSCAN_NODE, row_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))
    };
    static const struct gpio_dt_spec cols[] = {
        DT_FOREACH_PROP_ELEM_SEP(KSCAN_NODE, col_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))
    };

    for (size_t i = 0; i < ARRAY_SIZE(rows); i++) {
        gpio_pin_configure(rows[i].port, rows[i].pin, GPIO_INPUT);
    }
    for (size_t i = 0; i < ARRAY_SIZE(cols); i++) {
        gpio_pin_configure(cols[i].port, cols[i].pin, GPIO_INPUT);
    }
#endif
}

static void blank_display(void) {
#if DT_HAS_CHOSEN(zephyr_display)
    const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

    if (device_is_ready(display)) {
        LOG_INF("Blanking display before soft off");
        display_blanking_on(display);
    }
#endif
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    /* On split peripheral, blank the display and power off immediately.
     * This mirrors &soft_off's split-peripheral-off-on-press behavior. */
    if (IS_SPLIT_PERIPHERAL) {
        blank_display();
        disconnect_kscan_gpios();
        zmk_pm_soft_off();
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    /* On central (or non-split), blank the display while I2C is still
     * active, then enter soft off. */
    blank_display();
    disconnect_kscan_gpios();
    zmk_pm_soft_off();

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_display_soft_off_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

#define DSO_INST(n)                                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_display_soft_off_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DSO_INST)
