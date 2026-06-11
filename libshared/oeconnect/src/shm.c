/* libshared/oeconnect/src/shm.c */
#define OEC_BUILDING_LIB
#include "oeconnect/shm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
  #include <windows.h>
#else
  #include <fcntl.h>
  #include <sys/mman.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include <errno.h>
#endif

struct oec_shm {
#if defined(_WIN32)
    HANDLE mapping;
#else
    int fd;
    char *name_copy;
#endif
    void *mapped;
    size_t mapped_size;
};

oec_status_t oec_shm_make_name(int pid, char *out_buf, size_t out_buf_len) {
    if (!out_buf || out_buf_len < 64) return OEC_E_INVALID_ARG;
#if defined(_WIN32)
    int n = snprintf(out_buf, out_buf_len, "Local\\oeconnect.%d.shm", pid);
#else
    int n = snprintf(out_buf, out_buf_len, "/oeconnect.%d.shm", pid);
#endif
    if (n < 0 || (size_t)n >= out_buf_len) return OEC_E_INVALID_ARG;
    return OEC_OK;
}

oec_status_t oec_shm_create(
    const char *name, size_t size_bytes, int truncate,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size)
{
    if (!name || !out || !out_mapped) return OEC_E_INVALID_ARG;
    oec_shm_t *s = (oec_shm_t *)calloc(1, sizeof(*s));
    if (!s) return OEC_E_SYSCALL;

#if defined(_WIN32)
    (void)truncate;
    DWORD hi = (DWORD)((uint64_t)size_bytes >> 32);
    DWORD lo = (DWORD)((uint64_t)size_bytes & 0xFFFFFFFFu);
    s->mapping = CreateFileMappingA(
        INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, hi, lo, name);
    if (!s->mapping) { free(s); return OEC_E_SYSCALL; }
    s->mapped = MapViewOfFile(s->mapping, FILE_MAP_ALL_ACCESS, 0, 0, size_bytes);
    if (!s->mapped) { CloseHandle(s->mapping); free(s); return OEC_E_SYSCALL; }
    s->mapped_size = size_bytes;
    ZeroMemory(s->mapped, size_bytes);
#else
    s->fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (s->fd < 0) { free(s); return OEC_E_SYSCALL; }
    if (truncate && ftruncate(s->fd, (off_t)size_bytes) < 0) {
        close(s->fd); free(s); return OEC_E_SYSCALL;
    }
    s->mapped = mmap(NULL, size_bytes, PROT_READ | PROT_WRITE,
                     MAP_SHARED, s->fd, 0);
    if (s->mapped == MAP_FAILED) {
        close(s->fd); free(s); return OEC_E_SYSCALL;
    }
    s->mapped_size = size_bytes;
    s->name_copy = strdup(name);
    if (truncate) memset(s->mapped, 0, size_bytes);
#endif

    *out = s;
    *out_mapped = s->mapped;
    if (out_mapped_size) *out_mapped_size = s->mapped_size;
    return OEC_OK;
}

oec_status_t oec_shm_open(
    const char *name, size_t expected_size,
    oec_shm_t **out, void **out_mapped, size_t *out_mapped_size)
{
    if (!name || !out || !out_mapped) return OEC_E_INVALID_ARG;
    oec_shm_t *s = (oec_shm_t *)calloc(1, sizeof(*s));
    if (!s) return OEC_E_SYSCALL;

#if defined(_WIN32)
    s->mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name);
    if (!s->mapping) { free(s); return OEC_E_SYSCALL; }
    s->mapped = MapViewOfFile(s->mapping, FILE_MAP_ALL_ACCESS, 0, 0, expected_size);
    if (!s->mapped) { CloseHandle(s->mapping); free(s); return OEC_E_SYSCALL; }
    s->mapped_size = expected_size;
#else
    s->fd = shm_open(name, O_RDWR, 0600);
    if (s->fd < 0) { free(s); return OEC_E_SYSCALL; }
    s->mapped = mmap(NULL, expected_size, PROT_READ | PROT_WRITE,
                     MAP_SHARED, s->fd, 0);
    if (s->mapped == MAP_FAILED) {
        close(s->fd); free(s); return OEC_E_SYSCALL;
    }
    s->mapped_size = expected_size;
    s->name_copy = strdup(name);
#endif

    *out = s;
    *out_mapped = s->mapped;
    if (out_mapped_size) *out_mapped_size = s->mapped_size;
    return OEC_OK;
}

void oec_shm_close(oec_shm_t *s) {
    if (!s) return;
#if defined(_WIN32)
    if (s->mapped) UnmapViewOfFile(s->mapped);
    if (s->mapping) CloseHandle(s->mapping);
#else
    if (s->mapped && s->mapped != MAP_FAILED) munmap(s->mapped, s->mapped_size);
    if (s->fd >= 0) close(s->fd);
    free(s->name_copy);
#endif
    free(s);
}

oec_status_t oec_shm_unlink(const char *name) {
    if (!name) return OEC_E_INVALID_ARG;
#if defined(_WIN32)
    (void)name;
    return OEC_OK;
#else
    return shm_unlink(name) == 0 ? OEC_OK : OEC_E_SYSCALL;
#endif
}
