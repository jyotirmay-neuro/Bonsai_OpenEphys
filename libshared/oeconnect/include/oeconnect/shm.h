/* libshared/oeconnect/include/oeconnect/shm.h */
#ifndef OEC_SHM_H
#define OEC_SHM_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oec_shm oec_shm_t;

/*
 * Create a shared-memory region.
 * `name` follows platform conventions:
 *   Windows: "Local\\oeconnect.<pid>.shm" (will be mapped to a Win32 file mapping)
 *   POSIX:   "/oeconnect.<pid>.shm"        (will be passed to shm_open)
 * Returns OEC_OK on success; on failure `*out` is NULL.
 * If `truncate` is non-zero, sets the region size to `size_bytes` and zeroes it.
 */
OEC_API oec_status_t oec_shm_create(
    const char *name, size_t size_bytes, int truncate,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size);

/* Open an existing region read/write. */
OEC_API oec_status_t oec_shm_open(
    const char *name, size_t expected_size,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size);

/* Close the handle; unmap. On POSIX, owner should also call oec_shm_unlink. */
OEC_API void oec_shm_close(oec_shm_t *s);

/* Remove the name from the system (POSIX shm_unlink). No-op on Windows. */
OEC_API oec_status_t oec_shm_unlink(const char *name);

/*
 * Convenience helper used by the OE plugin on startup.
 * Builds the platform-correct name for the given PID and processor node id, e.g.
 *   Windows: "Local\\oeconnect.<pid>.<node_id>.shm"
 *   POSIX:   "/oeconnect.<pid>.<node_id>.shm"
 * `out_buf` must be at least 64 bytes.
 *
 * The node id scopes the region: one GUI process can host several OEconnect
 * processors (e.g. one publishing raw, another a filtered branch), and each must
 * own its own single-producer rings.
 */
OEC_API oec_status_t oec_shm_make_name(int pid, int node_id, char *out_buf, size_t out_buf_len);

#ifdef __cplusplus
}
#endif

#endif /* OEC_SHM_H */
