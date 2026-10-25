// SPDX-License-Identifier: MIT OR Apache-2.0
#ifndef PROVIDER_CATALOG_SYNC_H
#define PROVIDER_CATALOG_SYNC_H
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#define TVB_CATALOG_CLIENTS 8
struct provider_catalog_client { int used, fd, peer, native_partial; uint64_t generation; unsigned char frame[12]; };
struct provider_catalog_ticket { int fd, peer; uint64_t generation, token; unsigned char frame[12]; };
struct provider_catalog_sync {
 struct provider_catalog_client clients[TVB_CATALOG_CLIENTS];
 uint64_t serial, active_token;
 int pending, inflight, overflow;
};
/* Zero initialize. Caller serializes every operation and must identify native
 * cR/TX before capture; this module cannot authenticate a peer from bytes.
 * Keep the same lock through take, write_frame and finish to prevent fd reuse.
 * Never feed injected writes back into capture. No allocation or timers. */
int provider_catalog_capture(struct provider_catalog_sync *, int fd, int peer, const void *, size_t);
void provider_catalog_forget_fd(struct provider_catalog_sync *, int fd);
/* Called under the same lock as every native cR/TX write. Short positive
 * writes block injection; errors/zero preserve the previous state. A complete
 * write clears it. Assumes each completed native write ends a protocol frame
 * or batch: application-level fragments need an additional framing guard. */
void provider_catalog_note_native_write(struct provider_catalog_sync *, int fd, size_t requested, ssize_t result);
void provider_catalog_request(struct provider_catalog_sync *);
/* 1 claimed; 0 no pending/inflight; -1 no unique valid client/overflow/native partial write. */
int provider_catalog_take(struct provider_catalog_sync *, struct provider_catalog_ticket *);
/* 1 finalized; 0 stale ticket. Failure leaves pending for an explicit later
 * event, never an immediate retry loop. Success clears only the claimed work;
 * requests arriving during flight remain pending. */
int provider_catalog_finish(struct provider_catalog_sync *, const struct provider_catalog_ticket *, int success);
#endif
