/* libshared/oeconnect/include/oeconnect/sidecar.h */
#ifndef OEC_SIDECAR_H
#define OEC_SIDECAR_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct oec_sidecar {
    int      pid;
    char     shm_region[64];
    char     data_event[64];
    char     cmd_event[64];
    char     zmq_fallback_endpoint[64];
    char     zmq_cmd_endpoint[64];
    char     spec_version[16];
    uint64_t started_unix_ns;
} oec_sidecar_t;

/* Resolve the platform-specific sidecar directory (creates if missing). */
OEC_API oec_status_t oec_sidecar_dir(char *out_buf, size_t out_buf_len);

/* Compute full sidecar path for a pid. */
OEC_API oec_status_t oec_sidecar_path(int pid, char *out_buf, size_t out_buf_len);

/* Write JSON sidecar to disk. */
OEC_API oec_status_t oec_sidecar_write(const oec_sidecar_t *s);

/* Read JSON sidecar. */
OEC_API oec_status_t oec_sidecar_read(int pid, oec_sidecar_t *out);

/* Delete sidecar file. */
OEC_API oec_status_t oec_sidecar_remove(int pid);

#ifdef __cplusplus
}
#endif

#endif /* OEC_SIDECAR_H */
