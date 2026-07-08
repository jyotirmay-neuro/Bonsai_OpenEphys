#include <gtest/gtest.h>
#include <cstring>

extern "C" {
#include "oeconnect/sidecar.h"
}

TEST(Sidecar, DirIsCreatable) {
    char buf[256] = {0};
    EXPECT_EQ(oec_sidecar_dir(buf, sizeof(buf)), OEC_OK);
    EXPECT_GT(std::strlen(buf), 0u);
}

TEST(Sidecar, WriteThenReadRoundtrips) {
    oec_sidecar_t in;
    std::memset(&in, 0, sizeof(in));
    in.pid = 90001;
    in.node_id = 3;
    std::strncpy(in.shm_region, "Local\\oeconnect.90001.shm", sizeof(in.shm_region) - 1);
    std::strncpy(in.zmq_fallback_endpoint, "tcp://127.0.0.1:5557", sizeof(in.zmq_fallback_endpoint) - 1);
    std::strncpy(in.zmq_cmd_endpoint, "tcp://127.0.0.1:5558", sizeof(in.zmq_cmd_endpoint) - 1);
    std::strncpy(in.spec_version, "1.0", sizeof(in.spec_version) - 1);
    in.started_unix_ns = 1717900000000000000ULL;

    ASSERT_EQ(oec_sidecar_write(&in), OEC_OK);

    oec_sidecar_t out;
    std::memset(&out, 0, sizeof(out));
    ASSERT_EQ(oec_sidecar_read(90001, 3, &out), OEC_OK);
    EXPECT_EQ(out.pid, in.pid);
    EXPECT_EQ(out.node_id, in.node_id);
    EXPECT_STREQ(out.shm_region, in.shm_region);
    EXPECT_STREQ(out.zmq_fallback_endpoint, in.zmq_fallback_endpoint);
    EXPECT_STREQ(out.spec_version, in.spec_version);
    EXPECT_EQ(out.started_unix_ns, in.started_unix_ns);

    EXPECT_EQ(oec_sidecar_remove(90001, 3), OEC_OK);
}

TEST(Sidecar, ReadMissingFails) {
    oec_sidecar_t out;
    EXPECT_NE(oec_sidecar_read(999999, 0, &out), OEC_OK);
}
