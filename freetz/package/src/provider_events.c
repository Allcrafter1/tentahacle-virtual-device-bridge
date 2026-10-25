// SPDX-License-Identifier: MIT OR Apache-2.0
#include "provider_events.h"
#include <stdio.h>

static const char *cover_action_name(unsigned action) {
    if (action == 12) return "open";
    if (action == 13) return "close";
    if (action == 14) return "stop";
    return "set_position";
}
/* A return value of -1 means no complete JSON message fits. Never pass a
 * snprintf required-length result to send(): it may exceed the buffer. */
int provider_event_format(const struct command_event *event,
    char *message, size_t capacity) {
    int length;
    if (event->has_thermostat_timer && event->thermostat_timer_mode == 255) length = snprintf(message,
        capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"timer_action\":\"cancel\",\"timer_previous\":\"%s\",\"sequence\":%u}\n",
        event->endpoint, event->thermostat_timer_previous == 1 ? "boost":(event->thermostat_timer_previous == 3 ? "cold":"none"),
        event->sequence);
    else if (event->has_thermostat_timer) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"timer_action\":\"%s\",\"end_time\":%u,\"duration\":%u,\"sequence\":%u}\n",
        event->endpoint, event->thermostat_timer_mode == 1 ? "boost":"cold", event->thermostat_timer_end,
        event->thermostat_timer_duration, event->sequence);
    else if (event->has_thermostat && event->target_temperature == 253) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"hvac_mode\":\"off\",\"sequence\":%u}\n",
        event->endpoint, event->sequence);
    else if (event->has_thermostat && event->target_temperature == 254) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"hvac_mode\":\"heat\",\"sequence\":%u}\n",
        event->endpoint, event->sequence);
    else if (event->has_thermostat) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"hvac_mode\":\"heat\",\"target_temperature\":%u.%u,\"sequence\":%u}\n",
        event->endpoint, event->target_temperature/2, (event->target_temperature%2)*5, event->sequence);
    else if (event->has_cover) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"action\":\"%s\",\"position\":%u,\"sequence\":%u}\n",
        event->endpoint, cover_action_name(event->cover_action), event->level, event->sequence);
    else if (event->has_color && event->color_mode == 2) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"state\":%u,\"color_mode\":\"temperature\",\"color_temperature\":%u,\"sequence\":%u}\n",
        event->endpoint, event->state, event->color_temperature, event->sequence);
    else if (event->has_color) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"state\":%u,\"color_mode\":\"hs\",\"hue\":%u,\"saturation\":%u,\"sequence\":%u}\n",
        event->endpoint, event->state, event->hue, event->saturation, event->sequence);
    else if (event->has_level) length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"state\":%u,\"level\":%u,\"sequence\":%u}\n",
        event->endpoint, event->state, event->level, event->sequence);
    else length = snprintf(message, capacity,
        "{\"event\":\"command\",\"endpoint\":\"%s\",\"state\":%u,\"sequence\":%u}\n",
        event->endpoint, event->state, event->sequence);
    return length < 0 || (size_t) length >= capacity ? -1 : length;
}

void provider_event_push(struct provider_event_queue *queue,
    const struct command_event *event) {
    unsigned next = (queue->write_index + 1) % PROVIDER_EVENT_SLOTS;
    if (next == queue->read_index) {
        queue->read_index = (queue->read_index + 1) % PROVIDER_EVENT_SLOTS;
        ++queue->dropped;
    }
    queue->events[queue->write_index] = *event;
    queue->write_index = next;
}

int provider_event_pop(struct provider_event_queue *queue,
    struct command_event *event) {
    if (queue->read_index == queue->write_index) return 0;
    *event = queue->events[queue->read_index];
    queue->read_index = (queue->read_index + 1) % PROVIDER_EVENT_SLOTS;
    return 1;
}
