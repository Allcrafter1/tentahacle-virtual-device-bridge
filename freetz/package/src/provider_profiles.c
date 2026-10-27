// SPDX-License-Identifier: MIT OR Apache-2.0
/* Exact packet layouts for the fingerprinted 8.25 ARM AHA receiver. Keep
 * protocol constants and offsets adjacent to the bytes they describe. */
#include "provider_profiles.h"
#include "provider_codec.h"
#include <string.h>

static void header(struct provider_encoder *encoder, unsigned char*p, unsigned n, unsigned type) {
    memset(p, 0, n);
    p[0] = type;
    p[1] = 3;
    write_be16(p+2, n);
    write_be32(p+4, encoder->handle);
}

int emit_relay(struct provider_encoder *encoder, unsigned remote_id, unsigned value) {
    unsigned char p[28];
    header(encoder, p, sizeof(p), 7);
    write_be16(p+8, remote_id);
    write_be32(p+12, 12);
    write_be32(p+16, 15);
    write_be16(p+20, 4);
    write_be32(p+24, value);
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_options(struct provider_encoder *encoder, unsigned remote_id) {
    unsigned char p[32];
    header(encoder, p, sizeof(p), 7);
    write_be16(p+8, remote_id);
    write_be32(p+12, 16);
    write_be32(p+16, 35);
    write_be16(p+20, 8);
    /* Options=0 and DeviceLock/SwitchLock=0. */
    write_be32(p+24, 0);
    write_be32(p+28, 0);
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_switch_config(struct provider_encoder *encoder) {
    unsigned char p[168];
    header(encoder, p, sizeof(p), 4);
    write_be16(p+8, 450);
    p[10] = 2;
    write_be32(p+12, 1);
    write_be32(p+16, 0x200);
    strcpy((char*) p+20, "LAB Virtual HA Steckdose");
    write_be32(p+100, 0xb74);
    write_be32(p+104, 0x70001);
    strcpy((char*) p+112, "VIRT000000000001");
    strcpy((char*) p+132, "0.2-lab");
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_light_config(struct provider_encoder *encoder) {
    unsigned char p[168];
    header(encoder, p, sizeof(p), 4);
    write_be16(p+8, 451);
    p[10] = 2;
    write_be32(p+12, 1);
    /* SIMPLE_ONOFF | LIGHT | LEVEL_CONTROL listener flags. */
    write_be32(p+16, 0x00580000);
    strcpy((char*) p+20, "LAB Virtual HA Licht");
    write_be32(p+100, 0xb74);
    write_be32(p+104, 0x000a0000);
    strcpy((char*) p+112, "VIRT000000000002");
    strcpy((char*) p+132, "0.1-lab");
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

static int emit_light_modes_for(struct provider_encoder *encoder, unsigned remote_id) {
    unsigned char p[26];
    header(encoder, p, sizeof(p), 7);
    write_be16(p+8, 451);
    write_be16(p+8, remote_id);
    write_be32(p+12, 10);
    write_be32(p+16, 114);
    write_be16(p+20, 2);
    /* DIM and SWITCHABLE. */
    write_be16(p+24, 0x000c);
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

static int emit_light_level_for(struct provider_encoder *encoder, unsigned remote_id) {
    unsigned char p[32];
    header(encoder, p, sizeof(p), 7);
    write_be16(p+8, 451);
    write_be16(p+8, remote_id);
    write_be32(p+12, 16);
    write_be32(p+16, 110);
    write_be16(p+20, 8);
    /* Report an absolute percentage.  Raw-byte commands from FRITZ!OS are
     * normalized before this point, so the status describes the same value
     * without exposing the command representation. */
    p[24] = (unsigned char) encoder->state->light_level;
    write_be32(p+28, 1);
    /* percentage */
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_light_modes(struct provider_encoder *encoder) {
    return emit_light_modes_for(encoder, 451);
}

int emit_light_level(struct provider_encoder *encoder) {
    return emit_light_level_for(encoder, 451);
}

int emit_hanfun_light_config(struct provider_encoder *encoder) {
    unsigned char p[168];
    header(encoder, p, sizeof(p), 4);
    write_be16(p+8, 452);
    p[10] = 2;
    /* Network peers advertise HAN-FUN through the listener bit.  The device
     * type itself remains generic; type 14 is the local AHA representation
     * created after the ETSI configuration has been consumed. */
    write_be32(p+12, 0);
    write_be32(p+16, 0x00010000);
    /* HANFUN listener */
    strcpy((char*) p+20, "LAB Virtual HANFUN Licht");
    write_be32(p+100, 0xb74);
    write_be32(p+104, 0x000a0000);
    strcpy((char*) p+112, "VIRT000000000003");
    strcpy((char*) p+132, "0.1-lab");
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_color_light_config(struct provider_encoder *encoder) {
    unsigned char p[168];
    header(encoder, p, sizeof(p), 4);
    write_be16(p+8, 453);
    p[10] = 2;
    write_be32(p+12, 0);
    write_be32(p+16, 0x00010000);
    strcpy((char*) p+20, "LAB Virtual HA Farbtemperatur");
    write_be32(p+100, 0xb74);
    write_be32(p+104, 0x000a0000);
    strcpy((char*) p+112, "VIRT000000000004");
    strcpy((char*) p+132, "0.1-lab");
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_cover_config(struct provider_encoder *encoder) {
    unsigned char p[168];
    header(encoder, p, sizeof(p), 4);
    write_be16(p+8, 454);
    p[10] = 2;
    write_be32(p+12, 0);
    write_be32(p+16, 0x00010000);
    strcpy((char*) p+20, "LAB Virtual HA Rollladen");
    write_be32(p+100, 0xb74);
    write_be32(p+104, 0x000a0000);
    strcpy((char*) p+112, "VIRT000000000005");
    strcpy((char*) p+132, "0.1-lab");
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_thermostat_config(struct provider_encoder *encoder) {
    unsigned char p[168];
    header(encoder, p, sizeof(p), 4);
    write_be16(p+8, 455);
    p[10] = 2;
    write_be32(p+12, 1);
    /* classic flat AVM/DECT device */
    write_be32(p+16, 0x00000140);
    /* external listener: HKR | temperature */
    strcpy((char*) p+20, "LAB Virtual HA Thermostat");
    write_be32(p+100, 0xb74);
    write_be32(p+104, 0x6000b);
    /* AVM / Thermo 302 profile */
    strcpy((char*) p+112, "VIRT000000000006");
    strcpy((char*) p+132, "0.1-lab");
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

static int emit_direct_payload(struct provider_encoder *encoder, unsigned remote_id, unsigned function, const void *body,
    unsigned body_length) {
    unsigned char p[64];
    if (body_length > sizeof(p) - 24) return 0;
    unsigned length = 24 + body_length;
    header(encoder, p, length, 7);
    write_be16(p+8, remote_id);
    write_be32(p+12, 8+body_length);
    write_be32(p+16, function);
    write_be16(p+20, body_length);
    if (body_length) memcpy(p+24, body, body_length);
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, length) == (ssize_t) length;
}

static int emit_thermostat_temperature(struct provider_encoder *encoder) {
    unsigned char body[8] = {
        0
    };
    write_be32(body, (uint32_t) encoder->state->thermostat_actual_deci);
    write_be32(body+4, 0);
    return emit_direct_payload(encoder, 455, 23, body, sizeof(body));
}

int emit_thermostat_values(struct provider_encoder *encoder) {
    unsigned char body[16] = {
        0
    };
    body[0] = (unsigned char) encoder->state->thermostat_target;
    body[1] = (unsigned char) encoder->state->thermostat_reduced;
    body[2] = (unsigned char) encoder->state->thermostat_comfort;
    body[3] = (unsigned char) encoder->state->thermostat_offset;
    body[4] = (unsigned char) encoder->state->thermostat_actual;
    write_be32(body+8, encoder->state->thermostat_activated);
    return emit_direct_payload(encoder, 455, 57, body, sizeof(body));
}

int emit_thermostat_actual(struct provider_encoder *encoder) {
    return emit_thermostat_temperature(encoder) && emit_thermostat_values(encoder);
}

static int emit_thermostat_state(struct provider_encoder *encoder) {
    unsigned char body[4] = {
        0, 0, 0, 0
    };
    body[3] = (unsigned char) encoder->state->thermostat_battery;
    return emit_direct_payload(encoder, 455, 58, body, sizeof(body));
}

int emit_thermostat_timer(struct provider_encoder *encoder) {
    unsigned char body[16] = {
        0
    };
    write_be32(body, encoder->state->thermostat_timer_mode);
    write_be32(body+4, encoder->state->thermostat_timer_end);
    write_be32(body+8, encoder->state->thermostat_timer_activated);
    body[12] = 255;
    /* These two constant bytes are also present in commands generated by
     * FRITZ!OS 8.25 for both Boost and window-open/cold overrides. */
    body[13] = 0x20;
    body[14] = 0x02;
    return emit_direct_payload(encoder, 455, 117, body, sizeof(body));
}

int emit_thermostat_schedule(struct provider_encoder *encoder) {
    unsigned char body[16] = {
        0
    };
    write_be32(body, 1);
    /* weekly table */
    if (!encoder->state->thermostat_schedule_enabled) return emit_direct_payload(encoder, 455, 55, body,
        8);
    write_be16(body+4, 1);
    write_be16(body+6, 2);
    unsigned current_native = (encoder->state->thermostat_schedule_current_minute+1440)%10080;
    unsigned next_native = (encoder->state->thermostat_schedule_next_minute+1440)%10080;
    uint32_t current_entry = (current_native<<8)|(1u<<5)|(1u<<2);
    uint32_t next_entry = (next_native<<8)|(0u<<5)|(1u<<2);
    /* Function 55 is ordered in its Sunday-based native week.  The loop bit
     * is set on every entry except the last array element. */
    if (current_native < next_native) {
        write_be32(body+8, current_entry|1u);
        write_be32(body+12, next_entry);
    }
    else {
        write_be32(body+8, next_entry|1u);
        write_be32(body+12, current_entry);
    }
    return emit_direct_payload(encoder, 455, 55, body, sizeof(body));
}

int emit_thermostat_status(struct provider_encoder *encoder) {
    return emit_thermostat_temperature(encoder) && emit_thermostat_values(encoder) &&
    emit_thermostat_state(encoder) && emit_thermostat_timer(encoder) &&
    emit_thermostat_schedule(encoder);
}

static int emit_hanfun_device_config_for(struct provider_encoder *encoder, unsigned remote_id, unsigned ipui_suffix,
    unsigned discriminator) {
    unsigned char p[48];
    header(encoder, p, sizeof(p), 7);
    write_be16(p+8, 452);
    write_be16(p+8, remote_id);
    write_be32(p+12, 32);
    write_be32(p+16, 95);
    write_be16(p+20, 24);
    /* Stable five-byte IPUI, no application-protocol extension. */
    p[24] = 0x76;
    p[25] = 0x69;
    p[26] = 0x72;
    p[27] = 0x74;
    p[28] = (unsigned char) ipui_suffix;
    write_be16(p+32, discriminator);
    write_be64(p+40, encoder->state->hanfun_timestamp);
    /* strictly newer device-config timestamp */
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_hanfun_device_config(struct provider_encoder *encoder) {
    return emit_hanfun_device_config_for(encoder, 452, 3, 3);
}

int emit_color_device_config(struct provider_encoder *encoder) {
    return emit_hanfun_device_config_for(encoder, 453, 4, 4);
}

int emit_cover_device_config(struct provider_encoder *encoder) {
    return emit_hanfun_device_config_for(encoder, 454, 5, 5);
}

static int emit_hanfun_unit_config_for(struct provider_encoder *encoder, unsigned remote_id, const char *name,
    unsigned unit_type, int color) {
    unsigned char p[216];
    header(encoder, p, sizeof(p), 7);
    write_be16(p+8, 452);
    write_be16(p+8, remote_id);
    write_be32(p+12, 200);
    write_be32(p+16, 98);
    write_be16(p+20, 192);
    strncpy((char*) p+24, name, 79);
    write_be32(p+104, 0);
    /* legacy sort index */
    write_be32(p+108, unit_type);
    write_be32(p+112, 512);
    /* HF_IF_ON_OFF */
    write_be32(p+116, 513);
    /* HF_IF_LEVEL_CTRL */
    if (color) write_be32(p+120, 514);
    /* HF_IF_COLOR_CTRL */
    write_be16(p+152, 1);
    /* unit id */
    write_be64(p+156, encoder->state->hanfun_timestamp);
    /* strictly newer unit-config timestamp */
    /* Body+140 contains a mandatory 52-byte switch_action_config header.
     * It remains zeroed, including its action count at Body+166. */
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

int emit_hanfun_unit_config(struct provider_encoder *encoder) {
    return emit_hanfun_unit_config_for(encoder, 452, "LAB Virtual HA Licht", 265, 0);
}

int emit_color_unit_config(struct provider_encoder *encoder) {
    return emit_hanfun_unit_config_for(encoder, 453, "LAB Virtual HA Farbtemperatur", 278, 1);
}

int emit_cover_unit_config(struct provider_encoder *encoder) {
    unsigned char p[216];
    header(encoder, p, sizeof(p), 7);
    write_be16(p+8, 454);
    write_be32(p+12, 200);
    write_be32(p+16, 98);
    write_be16(p+20, 192);
    strcpy((char*) p+24, "LAB Virtual HA Rollladen");
    write_be32(p+104, 0);
    write_be32(p+108, 281);
    /* HF_UNIT_TYPE_BLIND */
    write_be32(p+112, 513);
    /* HF_IF_LEVEL_CTRL */
    write_be32(p+116, 516);
    /* HF_IF_OPEN_CLOSE */
    write_be32(p+120, 517);
    /* HF_IF_OPEN_CLOSE_CONFIG */
    write_be16(p+152, 1);
    write_be64(p+156, encoder->state->hanfun_timestamp);
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, sizeof(p)) == (ssize_t) sizeof(p);
}

static int emit_hanfun_unit_payload_for(struct provider_encoder *encoder, unsigned remote_id, unsigned function,
    const void *body, unsigned body_length) {
    unsigned char p[64];
    if (body_length > sizeof(p) - 36) return 0;
    unsigned length = 36 + body_length;
    header(encoder, p, length, 7);
    write_be16(p+8, remote_id);
    write_be32(p+12, length-16);
    write_be32(p+16, 118);
    write_be16(p+20, length-24);
    write_be16(p+24, 1);
    write_be32(p+28, function);
    write_be16(p+32, body_length);
    if (body_length) memcpy(p+36, body, body_length);
    return encoder->fd >= 0 && encoder->write(encoder->fd, p, length) == (ssize_t) length;
}

static int emit_hanfun_unit_payload(struct provider_encoder *encoder, unsigned function, const void *body, unsigned body_length) {
    return emit_hanfun_unit_payload_for(encoder, 452, function, body, body_length);
}

int emit_hanfun_relay(struct provider_encoder *encoder) {
    unsigned char body[4];
    write_be32(body, encoder->state->hanfun_light_state);
    return emit_hanfun_unit_payload(encoder, 15, body, sizeof(body));
}

int emit_hanfun_level(struct provider_encoder *encoder) {
    unsigned char body[8] = {
        0
    };
    body[0] = (unsigned char) encoder->state->hanfun_light_level;
    write_be32(body+4, 1);
    return emit_hanfun_unit_payload(encoder, 110, body, sizeof(body));
}

int emit_color_capabilities(struct provider_encoder *encoder) {
    unsigned char body[4] = {
        0
    };
    write_be16(body, encoder->state->color_full ? 0x0005:0x0004);
    body[2] = (unsigned char) encoder->state->color_full;
    return emit_hanfun_unit_payload_for(encoder, 453, 109, body, sizeof(body));
}

int emit_color_relay(struct provider_encoder *encoder) {
    unsigned char body[4];
    write_be32(body, encoder->state->color_light_state);
    return emit_hanfun_unit_payload_for(encoder, 453, 15, body, sizeof(body));
}

int emit_color_level(struct provider_encoder *encoder) {
    unsigned char body[8] = {
        0
    };
    body[0] = (unsigned char) encoder->state->color_light_level;
    write_be32(body+4, 1);
    return emit_hanfun_unit_payload_for(encoder, 453, 110, body, sizeof(body));
}

int emit_color_value(struct provider_encoder *encoder) {
    unsigned char body[16] = {
        0
    };
    write_be32(body, encoder->state->color_mode);
    if (encoder->state->color_mode == 2) {
        write_be16(body+4, encoder->state->color_temperature);
        return emit_hanfun_unit_payload_for(encoder, 453, 108, body, 10);
    }
    write_be16(body+4, encoder->state->color_hue);
    body[6] = (unsigned char) encoder->state->color_saturation;
    body[8] = (unsigned char) encoder->state->color_is_unmapped;
    return emit_hanfun_unit_payload_for(encoder, 453, 108, body, 16);
}

int emit_color_status(struct provider_encoder *encoder) {
    return emit_color_capabilities(encoder) && emit_color_relay(encoder) &&
    emit_color_level(encoder) && emit_color_value(encoder);
}

static int emit_cover_level(struct provider_encoder *encoder) {
    unsigned char body[8] = {
        0
    };
    body[0] = (unsigned char) encoder->state->cover_level;
    write_be32(body+4, 1);
    return emit_hanfun_unit_payload_for(encoder, 454, 110, body, sizeof(body));
}

int emit_cover_openclose_status(struct provider_encoder *encoder) {
    unsigned char body[6] = {
        0
    };
    write_be32(body, 15);
    write_be16(body+4, encoder->state->cover_openclose_state);
    return emit_hanfun_unit_payload_for(encoder, 454, 125, body, sizeof(body));
}

int emit_cover_status(struct provider_encoder *encoder) {
    return emit_cover_level(encoder) && emit_cover_openclose_status(encoder);
}
