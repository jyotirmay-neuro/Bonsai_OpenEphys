#include <gtest/gtest.h>
#include <cstring>
#include <string>

extern "C" {
#include "oeconnect/shm.h"
}

#if defined(_WIN32)
  #include <windows.h>
  #include <process.h>
#else
  #include <unistd.h>
#endif

namespace {
std::string unique_name() {
#if defined(_WIN32)
    return std::string("Local\\oeconnect.test.") +
           std::to_string(static_cast<unsigned long long>(::GetCurrentProcessId())) +
           "." + std::to_string(static_cast<unsigned long long>(rand()));
#else
    return std::string("/oeconnect.test.") +
           std::to_string(static_cast<unsigned long long>(getpid())) +
           "." + std::to_string(static_cast<unsigned long long>(rand()));
#endif
}
}  // namespace

TEST(Shm, MakeNameProducesValidPattern) {
    char buf[64] = {0};
    EXPECT_EQ(oec_shm_make_name(18432, buf, sizeof(buf)), OEC_OK);
#if defined(_WIN32)
    EXPECT_STREQ(buf, "Local\\oeconnect.18432.shm");
#else
    EXPECT_STREQ(buf, "/oeconnect.18432.shm");
#endif
}

TEST(Shm, CreateOpenRoundtrip) {
    std::string name = unique_name();
    oec_shm_t *create_h = nullptr;
    void *mapped = nullptr;
    size_t mapped_size = 0;
    ASSERT_EQ(oec_shm_create(name.c_str(), 4096, /*truncate=*/1,
                             &create_h, &mapped, &mapped_size), OEC_OK);
    ASSERT_NE(mapped, nullptr);
    EXPECT_GE(mapped_size, 4096u);

    /* writer pattern */
    std::memset(mapped, 0x5A, 4096);

    oec_shm_t *open_h = nullptr;
    void *opened = nullptr;
    size_t opened_size = 0;
    ASSERT_EQ(oec_shm_open(name.c_str(), 4096,
                           &open_h, &opened, &opened_size), OEC_OK);
    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(*(uint8_t *)opened, 0x5A);
    EXPECT_EQ(((uint8_t *)opened)[4095], 0x5A);

    oec_shm_close(open_h);
    oec_shm_close(create_h);
    oec_shm_unlink(name.c_str());
}

TEST(Shm, OpenMissingFails) {
    std::string name = unique_name();
    oec_shm_t *h = nullptr;
    void *m = nullptr;
    size_t sz = 0;
    EXPECT_NE(oec_shm_open(name.c_str(), 4096, &h, &m, &sz), OEC_OK);
}
