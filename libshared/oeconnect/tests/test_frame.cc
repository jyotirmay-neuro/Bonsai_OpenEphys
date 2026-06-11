#include <gtest/gtest.h>
#include <cstring>
#include <vector>

extern "C" {
#include "oeconnect/version.h"
#include "oeconnect/frame.h"
}

TEST(Frame, HeaderIsExactly32Bytes) {
    EXPECT_EQ(sizeof(oec_frame_header_t), 32u);
}

TEST(Frame, BlockSubheaderIsExactly8Bytes) {
    EXPECT_EQ(sizeof(oec_block_subheader_t), 8u);
}

TEST(Frame, InitPopulatesAllFields) {
    oec_frame_header_t h;
    std::memset(&h, 0xCC, sizeof(h));
    oec_frame_init(&h, OEC_STREAM_RAW_BLOCK, 1024, 5000, 999000, OEC_FLAG_LOST_DATA);

    EXPECT_EQ(h.magic, OEC_FRAME_MAGIC);
    EXPECT_EQ(h.version_major, OEC_PROTOCOL_VERSION_MAJOR);
    EXPECT_EQ(h.version_minor, OEC_PROTOCOL_VERSION_MINOR);
    EXPECT_EQ(h.stream_id, OEC_STREAM_RAW_BLOCK);
    EXPECT_EQ(h.payload_len, 1024u);
    EXPECT_EQ(h.sample_index, 5000u);
    EXPECT_EQ(h.host_qpc_ticks, 999000u);
    EXPECT_EQ(h.flags, OEC_FLAG_LOST_DATA);
    EXPECT_EQ(h.crc16, 0u);
}

TEST(Frame, ValidateAcceptsWellFormed) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    EXPECT_EQ(oec_frame_validate(&h), OEC_OK);
}

TEST(Frame, ValidateRejectsBadMagic) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    h.magic = 0xDEADBEEFu;
    EXPECT_EQ(oec_frame_validate(&h), OEC_E_BAD_MAGIC);
}

TEST(Frame, ValidateRejectsMajorVersionMismatch) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    h.version_major = OEC_PROTOCOL_VERSION_MAJOR + 1;
    EXPECT_EQ(oec_frame_validate(&h), OEC_E_VERSION_MISMATCH);
}

TEST(Frame, ValidateAcceptsForwardMinor) {
    oec_frame_header_t h;
    oec_frame_init(&h, OEC_STREAM_TTL_EVENT, 4, 1, 2, 0);
    h.version_minor = OEC_PROTOCOL_VERSION_MINOR + 1;
    EXPECT_EQ(oec_frame_validate(&h), OEC_OK);
}

TEST(Crc16, KnownVector_123456789) {
    /* CRC-16/CCITT-FALSE("123456789") == 0x29B1 */
    EXPECT_EQ(oec_crc16("123456789", 9), 0x29B1u);
}

TEST(Crc16, EmptyInputReturnsInit) {
    EXPECT_EQ(oec_crc16("", 0), 0xFFFFu);
}
