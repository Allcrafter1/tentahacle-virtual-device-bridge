// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef TVB_PROVIDER_EVENTS_H
#define TVB_PROVIDER_EVENTS_H
#include <stddef.h>
#include <stdint.h>

/* Snapshot copied under the provider lock, serialized by the worker after
 * releasing it. No device pointers escape into the delivery thread. */
struct command_event {
    char endpoint[20];
    unsigned sequence,state,remote_id,level,has_level,has_color,has_cover,cover_action;
    unsigned has_thermostat,target_temperature;
    unsigned has_thermostat_timer,thermostat_timer_mode,thermostat_timer_previous;
    uint32_t thermostat_timer_end,thermostat_timer_duration;
    unsigned color_mode,color_temperature,hue,saturation;
};

/* Preserve the existing bounded drop-oldest policy: 32 ring slots hold 31
 * pending events. The caller must serialize push/pop with its mutex. */
enum { PROVIDER_EVENT_SLOTS = 32 };
struct provider_event_queue {
    struct command_event events[PROVIDER_EVENT_SLOTS];
    unsigned read_index;
    unsigned write_index;
    unsigned dropped;
};
#pragma GCC visibility push(hidden)

void provider_event_push(struct provider_event_queue *queue,
                         const struct command_event *event);
int provider_event_pop(struct provider_event_queue *queue,
                       struct command_event *event);

int provider_event_format(const struct command_event *event,
                          char *message, size_t capacity);
#pragma GCC visibility pop
#endif
