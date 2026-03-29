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
 * Wrap sys_poweroff via --wrap linker flag to clear GPIO SENSE on all
 * pins before the real sys_poweroff enters nRF52 SYSTEMOFF.
 *
 * We only clear SENSE when the user explicitly triggered soft-off via
 * the display_soft_off behavior (flag: hard_off_requested).  Idle deep
 * sleep also calls sys_poweroff, but in that case we leave SENSE intact
 * so a key press can wake the board.
 */
#ifdef CONFIG_SOC_SERIES_NRF52X
/* ZMK keys.h defines short macros (P, OUT, IN, SET, etc.) that conflict
 * with Nordic HAL register field names and token-paste macros.
 * Undefine the known offenders before including the HAL header. */
#undef P
#undef OUT
#undef IN
#undef SET
#undef DIR
#include <hal/nrf_gpio.h>

static bool hard_off_requested;

extern void __real_sys_poweroff(void);

void __wrap_sys_poweroff(void) {
    if (hard_off_requested) {
        for (uint32_t pin = 0; pin < 32; pin++) {
            nrf_gpio_cfg_sense_set(pin, NRF_GPIO_PIN_NOSENSE);
        }
#if defined(NRF_P1)
        for (uint32_t pin = 32; pin < 48; pin++) {
            nrf_gpio_cfg_sense_set(pin, NRF_GPIO_PIN_NOSENSE);
        }
#endif
    }
    __real_sys_poweroff();
}
#endif /* CONFIG_SOC_SERIES_NRF52X */

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
#ifdef CONFIG_SOC_SERIES_NRF52X
        hard_off_requested = true;
#endif
        zmk_pm_soft_off();
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    /* On central (or non-split), blank the display while I2C is still
     * active, then enter soft off. */
    blank_display();
#ifdef CONFIG_SOC_SERIES_NRF52X
    hard_off_requested = true;
#endif
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
