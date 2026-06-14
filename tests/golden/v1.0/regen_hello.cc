/* tests/golden/v1.0/regen_hello.cc
 *
 * One-shot encoder that regenerates `hello.bin` for protocol v1.x.
 *
 * Build manually (not wired into CMake by default; this is a maintenance tool):
 *
 *   cl /std:c++17 /I libshared/oeconnect/include `
 *      tests/golden/v1.0/regen_hello.cc `
 *      libshared/oeconnect/src/frame.c `
 *      libshared/oeconnect/src/hello.c `
 *      /Fe:regen_hello.exe
 *   regen_hello.exe > tests/golden/v1.0/hello.bin
 *
 * Future intentional changes to the HELLO frame must bump
 * OEC_PROTOCOL_VERSION_MINOR / _MAJOR in libshared/oeconnect/include/oeconnect/version.h
 * and regenerate this file in the same commit.
 */

#include <cstdint>
#include <cstdio>

extern "C" {
#include "oeconnect/hello.h"
}

int main(void) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    uint8_t buf[64] = {0};
    const uint32_t plugin_version = 0x01000000u;
    const uint32_t lib_version    = 0x01010000u;
    size_t n = oec_hello_emit(buf, sizeof(buf), plugin_version, lib_version);
    if (n == 0) return 1;
    std::fwrite(buf, 1, n, stdout);
    return 0;
}
