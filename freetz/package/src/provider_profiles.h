// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef TVB_PROVIDER_PROFILES_H
#define TVB_PROVIDER_PROFILES_H
#include <stddef.h>
#include <sys/types.h>
#include "provider_state.h"

/* The caller holds the provider mutex throughout encoding and delivery.
 * write substitutes the selected identity and preserves the native transport
 * policy. Encoders neither lock nor open sockets nor own device identities. */
struct provider_encoder {
    struct provider_state *state;
    uint32_t handle;
    int fd;
    ssize_t (*write)(int fd, const void *data, size_t size);
};

#pragma GCC visibility push(hidden)

int emit_relay(struct provider_encoder *encoder, unsigned remote_id,unsigned value);
int emit_options(struct provider_encoder *encoder, unsigned remote_id);
int emit_switch_config(struct provider_encoder *encoder);
int emit_light_config(struct provider_encoder *encoder);
int emit_light_modes(struct provider_encoder *encoder);
int emit_light_level(struct provider_encoder *encoder);
int emit_hanfun_light_config(struct provider_encoder *encoder);
int emit_color_light_config(struct provider_encoder *encoder);
int emit_cover_config(struct provider_encoder *encoder);
int emit_thermostat_config(struct provider_encoder *encoder);
int emit_thermostat_values(struct provider_encoder *encoder);
int emit_thermostat_timer(struct provider_encoder *encoder);
int emit_thermostat_schedule(struct provider_encoder *encoder);
int emit_thermostat_status(struct provider_encoder *encoder);
int emit_hanfun_device_config(struct provider_encoder *encoder);
int emit_color_device_config(struct provider_encoder *encoder);
int emit_cover_device_config(struct provider_encoder *encoder);
int emit_hanfun_unit_config(struct provider_encoder *encoder);
int emit_color_unit_config(struct provider_encoder *encoder);
int emit_cover_unit_config(struct provider_encoder *encoder);
int emit_hanfun_relay(struct provider_encoder *encoder);
int emit_hanfun_level(struct provider_encoder *encoder);
int emit_color_capabilities(struct provider_encoder *encoder);
int emit_color_relay(struct provider_encoder *encoder);
int emit_color_level(struct provider_encoder *encoder);
int emit_color_value(struct provider_encoder *encoder);
int emit_color_status(struct provider_encoder *encoder);
int emit_cover_openclose_status(struct provider_encoder *encoder);
int emit_cover_status(struct provider_encoder *encoder);
#pragma GCC visibility pop
#endif
