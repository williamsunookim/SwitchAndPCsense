// SPDX-License-Identifier: MIT
#include "mode_led.h"
#include "config.h"
#include "usb_mode.h"
#include "pico/cyw43_arch.h"
#include "pico/time.h"

void mode_led_task() {
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    const bool wireless = usb_mode_is_wireless() || usb_mode_is_switch();
    static bool initialized = false, previous_mode = false;
    static uint32_t cycle_start = 0;
    if (!initialized || previous_mode != wireless) {
        initialized = true; previous_mode = wireless; cycle_start = now;
    }
    // 120 ms pulses, 120 ms between the Switch pair, then a long pause.
    // Unsigned subtraction keeps the pattern valid across clock rollover.
    const uint32_t phase = (now - cycle_start) % 2000;
    const bool on = phase < 120 || (wireless && phase >= 240 && phase < 360);
    static bool last_on = false;
    static uint32_t last_write = 0;
    const bool desired = on && !get_config().disable_pico_led;
    // Periodically reassert our value after another LED owner releases it;
    // avoid sending a CYW43 GPIO transaction on every main-loop iteration.
    if (desired != last_on || now - last_write >= 50) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, desired);
        last_on = desired; last_write = now;
    }
}
