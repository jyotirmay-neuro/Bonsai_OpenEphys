/* libshared/oeconnect/src/sidecar.c */
#define OEC_BUILDING_LIB
#include "oeconnect/sidecar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
  #include <direct.h>
  #define OEC_MKDIR(p) _mkdir(p)
  #define OEC_PATH_SEP '\\'
#else
  #include <sys/stat.h>
  #include <sys/types.h>
  #include <unistd.h>
  #define OEC_MKDIR(p) mkdir(p, 0700)
  #define OEC_PATH_SEP '/'
#endif

static oec_status_t resolve_tmp_root(char *out, size_t out_len) {
#if defined(_WIN32)
    DWORD n = GetTempPathA((DWORD)out_len, out);
    if (n == 0 || n >= out_len) return OEC_E_SYSCALL;
    /* GetTempPathA returns trailing backslash */
    return OEC_OK;
#else
    const char *t = getenv("TMPDIR");
    if (!t || !*t) t = "/tmp";
    int n = snprintf(out, out_len, "%s/", t);
    if (n < 0 || (size_t)n >= out_len) return OEC_E_SYSCALL;
    return OEC_OK;
#endif
}

oec_status_t oec_sidecar_dir(char *out_buf, size_t out_buf_len) {
    if (!out_buf || out_buf_len < 64) return OEC_E_INVALID_ARG;
    char tmp[256];
    if (resolve_tmp_root(tmp, sizeof(tmp)) != OEC_OK) return OEC_E_SYSCALL;
    int n = snprintf(out_buf, out_buf_len, "%soeconnect%csessions", tmp, OEC_PATH_SEP);
    if (n < 0 || (size_t)n >= out_buf_len) return OEC_E_INVALID_ARG;

    char step[256];
    n = snprintf(step, sizeof(step), "%soeconnect", tmp);
    if (n < 0) return OEC_E_SYSCALL;
    OEC_MKDIR(step);
    OEC_MKDIR(out_buf);
    return OEC_OK;
}

oec_status_t oec_sidecar_path(int pid, char *out_buf, size_t out_buf_len) {
    if (!out_buf || out_buf_len < 64) return OEC_E_INVALID_ARG;
    char dir[256];
    if (oec_sidecar_dir(dir, sizeof(dir)) != OEC_OK) return OEC_E_SYSCALL;
    int n = snprintf(out_buf, out_buf_len, "%s%c%d.json", dir, OEC_PATH_SEP, pid);
    if (n < 0 || (size_t)n >= out_buf_len) return OEC_E_INVALID_ARG;
    return OEC_OK;
}

oec_status_t oec_sidecar_write(const oec_sidecar_t *s) {
    if (!s) return OEC_E_INVALID_ARG;
    char path[512];
    if (oec_sidecar_path(s->pid, path, sizeof(path)) != OEC_OK) return OEC_E_SYSCALL;
    FILE *f = fopen(path, "wb");
    if (!f) return OEC_E_SYSCALL;
    fprintf(f,
        "{\n"
        "  \"pid\": %d,\n"
        "  \"shm_region\": \"%s\",\n"
        "  \"data_event\": \"%s\",\n"
        "  \"cmd_event\": \"%s\",\n"
        "  \"zmq_fallback_endpoint\": \"%s\",\n"
        "  \"zmq_cmd_endpoint\": \"%s\",\n"
        "  \"spec_version\": \"%s\",\n"
        "  \"started_unix_ns\": %llu\n"
        "}\n",
        s->pid, s->shm_region, s->data_event, s->cmd_event,
        s->zmq_fallback_endpoint, s->zmq_cmd_endpoint,
        s->spec_version, (unsigned long long)s->started_unix_ns);
    fclose(f);
    return OEC_OK;
}

/* Extremely small fixed-schema JSON reader.
 * Expects whitespace-tolerant matches for known keys. */
static int extract_string(const char *buf, const char *key, char *out, size_t out_len) {
    const char *p = strstr(buf, key);
    if (!p) return -1;
    p = strchr(p, ':'); if (!p) return -1;
    p = strchr(p, '"'); if (!p) return -1;
    ++p;
    const char *e = strchr(p, '"'); if (!e) return -1;
    size_t n = (size_t)(e - p);
    if (n >= out_len) n = out_len - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return 0;
}

static int extract_u64(const char *buf, const char *key, uint64_t *out) {
    const char *p = strstr(buf, key);
    if (!p) return -1;
    p = strchr(p, ':'); if (!p) return -1;
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    unsigned long long v = 0;
    if (sscanf(p, "%llu", &v) != 1) return -1;
    *out = (uint64_t)v;
    return 0;
}

static int extract_int(const char *buf, const char *key, int *out) {
    uint64_t v = 0;
    if (extract_u64(buf, key, &v) != 0) return -1;
    *out = (int)v;
    return 0;
}

oec_status_t oec_sidecar_read(int pid, oec_sidecar_t *out) {
    if (!out) return OEC_E_INVALID_ARG;
    char path[512];
    if (oec_sidecar_path(pid, path, sizeof(path)) != OEC_OK) return OEC_E_SYSCALL;
    FILE *f = fopen(path, "rb");
    if (!f) return OEC_E_NO_SESSION;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 4096) { fclose(f); return OEC_E_PARSE; }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return OEC_E_SYSCALL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    buf[got] = '\0';

    memset(out, 0, sizeof(*out));
    if (extract_int(buf, "\"pid\"", &out->pid) != 0) { free(buf); return OEC_E_PARSE; }
    extract_string(buf, "\"shm_region\"",            out->shm_region,            sizeof(out->shm_region));
    extract_string(buf, "\"data_event\"",            out->data_event,            sizeof(out->data_event));
    extract_string(buf, "\"cmd_event\"",             out->cmd_event,             sizeof(out->cmd_event));
    extract_string(buf, "\"zmq_fallback_endpoint\"", out->zmq_fallback_endpoint, sizeof(out->zmq_fallback_endpoint));
    extract_string(buf, "\"zmq_cmd_endpoint\"",      out->zmq_cmd_endpoint,      sizeof(out->zmq_cmd_endpoint));
    extract_string(buf, "\"spec_version\"",          out->spec_version,          sizeof(out->spec_version));
    extract_u64   (buf, "\"started_unix_ns\"",       &out->started_unix_ns);
    free(buf);
    return OEC_OK;
}

oec_status_t oec_sidecar_remove(int pid) {
    char path[512];
    if (oec_sidecar_path(pid, path, sizeof(path)) != OEC_OK) return OEC_E_SYSCALL;
    return remove(path) == 0 ? OEC_OK : OEC_E_SYSCALL;
}
