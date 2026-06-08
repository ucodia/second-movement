/* SPDX-License-Identifier: MIT */

/*
 * MIT License
 *
 * Copyright © 2023-2026 Lionel Ringenbach <ucodia@live.com>
 * Copyright © 2021-2023 Joey Castillo <joeycastillo@utexas.edu> <jose.castillo@gmail.com>
 * Copyright © 2022 David Keck <davidskeck@users.noreply.github.com>
 * Copyright © 2022 TheOnePerson <a.nebinger@web.de>
 * Copyright © 2023 Jeremy O'Brien <neutral@fastmail.com>
 * Copyright © 2023 Mikhail Svarichevsky <3@14.by>
 * Copyright © 2023 Wesley Aptekar-Cassels <me@wesleyac.com>
 * Copyright © 2024 Matheus Afonso Martins Moreira <matheus.a.m.moreira@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include "flowtime_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "watch_common_display.h"
#include "filesystem.h"
#include "flowtime.h"

#define FLOWTIME_BEEP_TOGGLE_THRESHOLD_SECONDS 10
#define FLOWTIME_BEEP_FILENAME "flowtime_collision.u8"

static bool flowtime_collision_beep_enabled(void) {
    uint8_t val = 1;  // enabled by default
    if (filesystem_file_exists(FLOWTIME_BEEP_FILENAME)) {
        filesystem_read_file(FLOWTIME_BEEP_FILENAME, (char *)&val, sizeof(val));
    }
    return val != 0;
}

static void flowtime_collision_beep_set(bool enabled) {
    uint8_t val = enabled ? 1 : 0;
    filesystem_write_file(FLOWTIME_BEEP_FILENAME, (char *)&val, sizeof(val));
}

static void clock_indicate(watch_indicator_t indicator, bool on) {
    if (on) {
        watch_set_indicator(indicator);
    } else {
        watch_clear_indicator(indicator);
    }
}

static void clock_indicate_alarm(void) {
    clock_indicate(WATCH_INDICATOR_SIGNAL, movement_alarm_enabled());
}

static void clock_indicate_24h(void) {
    clock_indicate(WATCH_INDICATOR_24H, !!movement_clock_mode_24h());
}

static void flowtime_indicate_beep(void) {
    clock_indicate(WATCH_INDICATOR_BELL, flowtime_collision_beep_enabled());
}

static bool clock_is_pm(watch_date_time_t date_time) {
    return date_time.unit.hour >= 12;
}

static void clock_indicate_pm(watch_date_time_t date_time) {
    if (movement_clock_mode_24h()) { return; }
    clock_indicate(WATCH_INDICATOR_PM, clock_is_pm(date_time));
}

static watch_date_time_t clock_24h_to_12h(watch_date_time_t date_time) {
    date_time.unit.hour %= 12;

    if (date_time.unit.hour == 0) {
        date_time.unit.hour = 12;
    }

    return date_time;
}

static void clock_display_all(watch_date_time_t date_time) {
    char buf[8 + 1];

    snprintf(
        buf,
        sizeof(buf),
        movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_024H ? "%02d%02d%02d%02d" : "%2d%2d%02d%02d",
        date_time.unit.day,
        date_time.unit.hour,
        date_time.unit.minute,
        date_time.unit.second
    );

    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(date_time), watch_utility_get_weekday(date_time));
    watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
    watch_display_text(WATCH_POSITION_BOTTOM, buf + 2);
}

static bool clock_display_some(watch_date_time_t current, watch_date_time_t previous) {
    if ((current.reg >> 6) == (previous.reg >> 6)) {
        // everything before seconds is the same, don't waste cycles setting those segments.

        watch_display_character_lp_seconds('0' + current.unit.second / 10, 8);
        watch_display_character_lp_seconds('0' + current.unit.second % 10, 9);

        return true;

    } else if ((current.reg >> 12) == (previous.reg >> 12)) {
        // everything before minutes is the same.

        char buf[4 + 1];

        snprintf(
            buf,
            sizeof(buf),
            "%02d%02d",
            current.unit.minute,
            current.unit.second
        );

        watch_display_text(WATCH_POSITION_MINUTES, buf);
        watch_display_text(WATCH_POSITION_SECONDS, buf + 2);

        return true;

    } else {
        // other stuff changed; let's do it all.
        return false;
    }
}

static void clock_display_clock(flowtime_state_t *state, watch_date_time_t current) {
    if (!clock_display_some(current, state->date_time.previous)) {
        if (movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_12H) {
            clock_indicate_pm(current);
            current = clock_24h_to_12h(current);
        }
        clock_display_all(current);
    }
}

static void clock_display_low_energy(watch_date_time_t date_time) {
    if (movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_12H) {
        clock_indicate_pm(date_time);
        date_time = clock_24h_to_12h(date_time);
    }
    char buf[8 + 1];

    snprintf(
        buf,
        sizeof(buf),
        movement_clock_mode_24h() == MOVEMENT_CLOCK_MODE_024H ? "%02d%02d%02d  " : "%2d%2d%02d  ",
        date_time.unit.day,
        date_time.unit.hour,
        date_time.unit.minute
    );

    watch_display_text_with_fallback(WATCH_POSITION_TOP_LEFT, watch_utility_get_long_weekday(date_time), watch_utility_get_weekday(date_time));
    watch_display_text(WATCH_POSITION_TOP_RIGHT, buf);
    watch_display_text(WATCH_POSITION_BOTTOM, buf + 2);
}

static void clock_start_tick_tock_animation(void) {
    if (!watch_sleep_animation_is_running()) {
        watch_start_sleep_animation(500);
        watch_start_indicator_blink_if_possible(WATCH_INDICATOR_COLON, 500);
    }
}

static void clock_stop_tick_tock_animation(void) {
    if (watch_sleep_animation_is_running()) {
        watch_stop_sleep_animation();
        watch_stop_blink();
    }
}

void flowtime_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;

    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(flowtime_state_t));
        flowtime_state_t *state = (flowtime_state_t *) *context_ptr;
        state->watch_face_index = watch_face_index;
        state->alarm_hold_ticks = 0;
        state->led_armed = false;
    }
}

void flowtime_face_activate(void *context) {
    flowtime_state_t *state = (flowtime_state_t *) context;

    clock_stop_tick_tock_animation();

    clock_indicate_alarm();
    clock_indicate_24h();
    flowtime_indicate_beep();

    watch_set_colon();

    // this ensures that none of the timestamp fields will match, so we can re-render them all.
    state->date_time.previous.reg = 0xFFFFFFFF;
}

bool flowtime_face_loop(movement_event_t event, void *context) {
    flowtime_state_t *state = (flowtime_state_t *) context;
    watch_date_time_t current;
    watch_date_time_t display;

    switch (event.event_type) {
        case EVENT_LOW_ENERGY_UPDATE:
            clock_start_tick_tock_animation();
            current = movement_get_local_date_time();
            display = date_to_flowtime(&current);
            clock_display_low_energy(display);
            break;
        case EVENT_TICK:
        case EVENT_ACTIVATE:
            current = movement_get_local_date_time();
            if (HAL_GPIO_BTN_ALARM_read()) {
                if (state->alarm_hold_ticks < 255) state->alarm_hold_ticks++;
                if (state->alarm_hold_ticks == FLOWTIME_BEEP_TOGGLE_THRESHOLD_SECONDS) {
                    bool was_enabled = flowtime_collision_beep_enabled();
                    flowtime_collision_beep_set(!was_enabled);
                    flowtime_indicate_beep();
                    if (!was_enabled) movement_play_signal();
                }
                display = current;
            } else {
                state->alarm_hold_ticks = 0;
                if (state->led_armed) {
                    // recover the LED if a button-up event was lost in the movement event-dispatch race.
                    state->led_armed = false;
                    movement_force_led_off();
                }
                display = date_to_flowtime(&current);
            }
            clock_display_clock(state, display);
            state->date_time.previous = display;
            break;
        case EVENT_ALARM_BUTTON_DOWN:
            state->alarm_hold_ticks = 0;
            state->led_armed = true;
            movement_force_led_on(255, 0, 0);
            current = movement_get_local_date_time();
            clock_display_clock(state, current);
            state->date_time.previous = current;
            break;
        case EVENT_ALARM_LONG_UP:
        case EVENT_ALARM_BUTTON_UP:
            state->alarm_hold_ticks = 0;
            state->led_armed = false;
            movement_force_led_off();
            current = movement_get_local_date_time();
            display = date_to_flowtime(&current);
            clock_display_clock(state, display);
            state->date_time.previous = display;
            break;
        case EVENT_BACKGROUND_TASK:
            movement_play_signal();
            break;
        default:
            return movement_default_loop_handler(event);
    }

    return true;
}

void flowtime_face_resign(void *context) {
    flowtime_state_t *state = (flowtime_state_t *) context;
    if (state->led_armed) {
        state->led_armed = false;
        movement_force_led_off();
    }
}

movement_watch_face_advisory_t flowtime_face_advise(void *context) {
    (void) context;
    movement_watch_face_advisory_t retval = { 0 };
    if (flowtime_collision_beep_enabled()) {
        watch_date_time_t now = movement_get_local_date_time();
        watch_date_time_t ft = date_to_flowtime(&now);
        retval.wants_background_task = (ft.unit.hour == now.unit.hour && ft.unit.minute == now.unit.minute);
    }
    return retval;
}
