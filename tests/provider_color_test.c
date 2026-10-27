// SPDX-License-Identifier: MIT OR Apache-2.0
/* Exercise the production dynamic full-colour path without AHA or MQTT. */
#include "../freetz/package/src/virtual_provider.c"
#include <assert.h>
#include <stdio.h>

static unsigned char captured[64];
static size_t captured_size;

static ssize_t capture(int fd, const void *data, size_t size) {
    (void)fd;
    assert(size <= sizeof(captured));
    memcpy(captured, data, size);
    captured_size = size;
    return (ssize_t)size;
}

static ssize_t forward(int fd, const void *data, size_t size, int flags) {
    (void)fd;
    (void)data;
    (void)flags;
    return (ssize_t)size;
}

static void hs_frame(unsigned char packet[52], unsigned remote_id) {
    memset(packet, 0, 52);
    packet[0] = 7;
    packet[1] = 3;
    write_be16(packet + 2, 52);
    write_be16(packet + 8, remote_id);
    write_be32(packet + 12, 36);
    write_be32(packet + 16, 118);
    write_be16(packet + 20, 28);
    write_be16(packet + 24, 1);
    write_be32(packet + 28, 108);
    write_be16(packet + 32, 16);
    write_be32(packet + 36, 0);
    write_be16(packet + 40, 120);
    packet[42] = 255;
    packet[44] = 1;
}

static void temperature_frame(unsigned char packet[46], unsigned remote_id,
                              unsigned temperature) {
    memset(packet, 0, 46);
    packet[0] = 7;
    packet[1] = 3;
    write_be16(packet + 2, 46);
    write_be16(packet + 8, remote_id);
    write_be32(packet + 12, 30);
    write_be32(packet + 16, 118);
    write_be16(packet + 20, 22);
    write_be16(packet + 24, 1);
    write_be32(packet + 28, 108);
    write_be16(packet + 32, 10);
    write_be32(packet + 36, 2);
    write_be16(packet + 40, temperature);
}

int main(void) {
    const char *uid = "TVB0123456789ABCDEF";
    char command[256];
    unsigned char packet[52];
    struct command_event event;

    tvb_registry_init(&registry);
    initial_values = legacy_state;
    strcpy(command, "ADD TVB0123456789ABCDEF color_temperature_light Colour");
    assert(dynamic_control(command, sizeof(command)) == 1);
    const tvb_device *device = tvb_registry_find(&registry, uid);
    assert(device && !device->color_full);

    strcpy(command, "HS TVB0123456789ABCDEF 120 255");
    assert(dynamic_control(command, sizeof(command)) == 0);
    strcpy(command, "COLOR_CAPABILITY TVB0123456789ABCDEF full");
    assert(dynamic_control(command, sizeof(command)) == 1);
    device = tvb_registry_find(&registry, uid);
    assert(device && device->color_full);

    encoder.fd = 1;
    encoder.write = capture;
    select_device(device);
    assert(emit_color_capabilities(&encoder));
    assert(captured_size == 40 && read_be32(captured + 28) == 109);
    assert(read_be16(captured + 36) == 5 && captured[38] == 1);
    unselect_device();

    generation = 1;
    unit_generation[0] = generation;
    incoming_fd = 42;
    enabled = 1;
    original_send = forward;
    hs_frame(packet, device->remote_id);
    assert(send_frame(incoming_fd, packet, sizeof(packet), 0) == (ssize_t)sizeof(packet));
    assert(provider_event_pop(&event_queue, &event));
    assert(event.has_color && event.color_mode == 0 && event.hue == 120 && event.saturation == 255);

    strcpy(command, "COLOR_CAPABILITY TVB0123456789ABCDEF temperature");
    encoder.fd = -1;
    assert(dynamic_control(command, sizeof(command)) == 1);
    assert(!registry.devices[0].color_full);
    assert(device_values[0].color_mode == 2 && !device_values[0].color_is_unmapped);

    /* FRITZ!OS presents its Kelvin palette as a cycle. Endpoint jumps are
     * ordinary absolute commands and must complete the same round trip as
     * neighboring steps. */
    unsigned char temperature[46];
    unsigned sequence = command_count;
    encoder.fd = 1;
    captured_size = 0;
    temperature_frame(temperature, device->remote_id, 6500);
    assert(send_frame(incoming_fd, temperature, sizeof(temperature), 0) ==
           (ssize_t)sizeof(temperature));
    assert(device_values[0].color_temperature == 6500);
    assert(command_count == sequence + 1 && provider_event_pop(&event_queue, &event));
    assert(event.has_color && event.color_mode == 2 &&
           event.color_temperature == 6500);
    assert(captured_size == 46 && read_be32(captured + 28) == 108 &&
           read_be16(captured + 40) == 6500);

    captured_size = 0;
    temperature_frame(temperature, device->remote_id, 2700);
    assert(send_frame(incoming_fd, temperature, sizeof(temperature), 0) ==
           (ssize_t)sizeof(temperature));
    assert(device_values[0].color_temperature == 2700);
    assert(command_count == sequence + 2 && provider_event_pop(&event_queue, &event));
    assert(event.has_color && event.color_mode == 2 &&
           event.color_temperature == 2700);
    assert(captured_size == 46 && read_be16(captured + 40) == 2700);

    /* Neighboring values still take the ordinary command path. */
    temperature_frame(temperature, device->remote_id, 3000);
    assert(send_frame(incoming_fd, temperature, sizeof(temperature), 0) ==
           (ssize_t)sizeof(temperature));
    assert(device_values[0].color_temperature == 3000 &&
           command_count == sequence + 3);
    assert(provider_event_pop(&event_queue, &event));
    assert(event.has_color && event.color_mode == 2 &&
           event.color_temperature == 3000);
    assert(!provider_event_pop(&event_queue, &event));

    /* The compatibility-only fixed endpoint uses the same native cycle. */
    legacy_enabled = 1;
    legacy_state.color_mode = 2;
    legacy_state.color_temperature = 6500;
    temperature_frame(temperature, 453, 2700);
    assert(send_frame(incoming_fd, temperature, sizeof(temperature), 0) ==
           (ssize_t)sizeof(temperature));
    assert(legacy_state.color_temperature == 2700 && command_count == sequence + 4);
    assert(provider_event_pop(&event_queue, &event));
    assert(event.has_color && event.color_temperature == 2700);

    puts("provider_color_test: OK");
    return 0;
}
