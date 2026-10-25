// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef TVB_PROVIDER_STATE_H
#define TVB_PROVIDER_STATE_H

#include <stdint.h>

/* One independent value set per registered device. The provider mutex guards
 * both the selected pointer and these values. Cover level is AVM's closed
 * percentage; thermostat temperatures are half degrees Celsius. */
struct provider_state {
    uint32_t switch_state;
    uint32_t light_state;
    uint32_t light_level;
    uint32_t hanfun_light_state;
    uint32_t hanfun_light_level;
    uint32_t color_light_state;
    uint32_t color_light_level;
    uint32_t color_mode;
    uint32_t color_temperature;
    uint32_t color_hue;
    uint32_t color_saturation;
    uint32_t color_full;
    uint32_t color_is_unmapped;
    uint32_t cover_level;
    uint32_t cover_openclose_state;
    uint32_t thermostat_target;
    uint32_t thermostat_reduced;
    uint32_t thermostat_comfort;
    uint32_t thermostat_actual;
    uint32_t thermostat_offset;
    uint32_t thermostat_battery;
    uint32_t thermostat_heat_target;
    uint32_t thermostat_activated;
    uint32_t thermostat_timer_mode;
    uint32_t thermostat_timer_end;
    uint32_t thermostat_timer_activated;
    uint32_t thermostat_schedule_enabled;
    uint32_t thermostat_schedule_current;
    uint32_t thermostat_schedule_next;
    uint32_t thermostat_schedule_current_minute;
    uint32_t thermostat_schedule_next_minute;
    uint32_t thermostat_schedule_reject_until;
    /* Native Function 95/98 revisions are Unix microseconds, high word first. */
    uint64_t hanfun_timestamp;
};

/* Keep revisions newer even when the wall clock repeats or moves backwards.
 * Revisions belong to their parent/unit, not to the whole network connection. */
static inline int provider_advance_hanfun_timestamp(struct provider_state *state,
                                                   uint64_t now) {
    if (state->hanfun_timestamp == UINT64_MAX) return 0;
    state->hanfun_timestamp = now > state->hanfun_timestamp ? now : state->hanfun_timestamp + 1;
    return 1;
}

/* Defaults are shared by the legacy test endpoints and newly added devices.
 * The constructor supplies the initial HAN-FUN timestamp separately. */
#define PROVIDER_STATE_INITIALIZER { \
    .light_level = 100, \
    .hanfun_light_level = 100, \
    .color_light_level = 100, \
    .color_mode = 2, \
    .color_temperature = 2700, \
    .color_hue = 120, \
    .color_saturation = 255, \
    .cover_level = 50, \
    .thermostat_target = 40, \
    .thermostat_reduced = 36, \
    .thermostat_comfort = 44, \
    .thermostat_actual = 40, \
    .thermostat_battery = 100, \
    .thermostat_heat_target = 40, \
    .thermostat_timer_mode = 255, \
    .thermostat_schedule_current = 40, \
    .thermostat_schedule_next = 36, \
}

#endif
