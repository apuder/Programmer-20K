// Simple upload receiver interface — streaming API for handing uploaded bytes to storage backends
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize receiver for a new upload. 'target' is a hint (e.g. "flash" or "sram").
// 'filename_hint' is optional and may be NULL. Return true if ready to receive data.
bool upload_receiver_init(const char *target, const char *filename_hint);

// Write a chunk of bytes. Return number of bytes consumed (should be len on success), or 0 on fatal failure.
size_t upload_receiver_write(const uint8_t *buf, size_t len);

// Called when upload completes; success indicates whether upload finished normally.
void upload_receiver_finish(bool success);

// Optional: return an assigned filename or NULL. The receiver implementation may return a pointer
// to internal storage that remains valid until the next upload_receiver_init() call.
const char* upload_receiver_get_filename(void);

#ifdef __cplusplus
}
#endif
