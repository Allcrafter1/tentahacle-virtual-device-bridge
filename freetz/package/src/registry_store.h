/* SPDX-License-Identifier: MIT OR Apache-2.0 */
#ifndef TVB_REGISTRY_STORE_H
#define TVB_REGISTRY_STORE_H

#include "device_registry.h"
#include <stddef.h>

/* JSON is the persistent, versioned interchange form.  Returned serialized
 * strings are owned by the caller and must be released with
 * tvb_registry_serialized_free(). */
char *tvb_registry_serialize(const tvb_device_registry *registry);
void tvb_registry_serialized_free(char *serialized);
int tvb_registry_deserialize(const char *serialized,
                             tvb_device_registry *registry,
                             char *error,
                             size_t error_size);

/* save_file writes a sibling temporary file, fsyncs it and atomically renames
 * it over path.  load_file never changes registry when validation fails. */
int tvb_registry_save_file(const char *path,
                           const tvb_device_registry *registry,
                           char *error,
                           size_t error_size);
int tvb_registry_load_file(const char *path,
                           tvb_device_registry *registry,
                           char *error,
                           size_t error_size);

#endif
