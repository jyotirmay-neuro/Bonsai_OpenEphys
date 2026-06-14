#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

extern "C" {
#include "oeconnect/version.h"
#include "oeconnect/frame.h"
#include "oeconnect/hello.h"
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

TEST(Hello, BodyIsExactly16Bytes) {
    EXPECT_EQ(sizeof(oec_hello_body_t), 16u);
}

TEST(Hello, EmitPopulatesHeaderAndBody) {
    uint8_t buf[64] = {0};
    const uint32_t plugin_ver = 0x01000200u;
    const uint32_t lib_ver    = 0x01010000u;
    size_t n = oec_hello_emit(buf, sizeof(buf), plugin_ver, lib_ver);
    ASSERT_EQ(n, sizeof(oec_frame_header_t) + sizeof(oec_hello_body_t));

    const oec_frame_header_t *h = (const oec_frame_header_t *)buf;
    EXPECT_EQ(h->magic, OEC_FRAME_MAGIC);
    EXPECT_EQ(h->stream_id, OEC_STREAM_HELLO);
    EXPECT_EQ(h->payload_len, 16u);
    EXPECT_EQ(h->sample_index, 0u);
    EXPECT_EQ(h->host_qpc_ticks, 0u);
    EXPECT_EQ(h->flags, 0u);

    oec_hello_body_t b;
    std::memcpy(&b, buf + sizeof(*h), sizeof(b));
    EXPECT_EQ(b.protocol_major, OEC_PROTOCOL_VERSION_MAJOR);
    EXPECT_EQ(b.protocol_minor, OEC_PROTOCOL_VERSION_MINOR);
    EXPECT_EQ(b.plugin_version, plugin_ver);
    EXPECT_EQ(b.lib_version,    lib_ver);
    EXPECT_EQ(b.reserved,       0u);
}

TEST(Hello, EmitRejectsUndersizedBuffer) {
    uint8_t small[16] = {0};
    EXPECT_EQ(oec_hello_emit(small, sizeof(small), 0, 0), 0u);
}

TEST(Hello, MatchesGoldenCorpus) {
    /* Canonical inputs match tests/golden/v1.0/hello.bin. Any drift between
     * the emitter and the on-disk byte sequence is a wire-protocol break. */
    const uint8_t expected[48] = {
        /* header (32 B) */
        0x4F, 0x45, 0x43, 0x31,             /* magic 'OEC1' LE = 0x3143454F */
        0x01, 0x01,                         /* version_major, version_minor */
        0x30, 0x00,                         /* stream_id 0x0030 */
        0x10, 0x00, 0x00, 0x00,             /* payload_len 16 */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* sample_index */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  /* host_qpc_ticks */
        0x00, 0x00,                         /* flags */
        0x00, 0x00,                         /* crc16 */
        /* body (16 B) */
        0x01, 0x00,                         /* protocol_major 1 */
        0x01, 0x00,                         /* protocol_minor 1 */
        0x00, 0x00, 0x00, 0x01,             /* plugin_version 0x01000000 */
        0x00, 0x00, 0x01, 0x01,             /* lib_version 0x01010000 */
        0x00, 0x00, 0x00, 0x00              /* reserved */
    };
    uint8_t buf[64] = {0};
    size_t n = oec_hello_emit(buf, sizeof(buf), 0x01000000u, 0x01010000u);
    ASSERT_EQ(n, sizeof(expected));
    EXPECT_EQ(std::memcmp(buf, expected, n), 0);
}

TEST(Crc16, KnownVector_123456789) {
    /* CRC-16/CCITT-FALSE("123456789") == 0x29B1 */
    EXPECT_EQ(oec_crc16("123456789", 9), 0x29B1u);
}

TEST(Crc16, EmptyInputReturnsInit) {
    EXPECT_EQ(oec_crc16("", 0), 0xFFFFu);
}

/* ---- Golden wire corpus (tests/golden/v1.0) ----
 * Cross-runtime guard: every committed .bin must decode to a header whose
 * stream_id matches its filename and that passes oec_frame_validate. The
 * xUnit side (InteropTests.GoldenCorpus_*) reuses the same files so a drift in
 * either runtime is caught. */

namespace {
namespace fs = std::filesystem;

/* Walk up from the current directory until tests/golden/v1.0/hello.bin is found. */
fs::path FindCorpusDir() {
    for (fs::path d = fs::current_path(); ; d = d.parent_path()) {
        fs::path cand = d / "tests" / "golden" / "v1.0";
        if (fs::exists(cand / "hello.bin")) return cand;
        if (!d.has_parent_path() || d == d.parent_path()) break;
    }
    return {};
}

const std::map<std::string, uint16_t>& ExpectedStreams() {
    static const std::map<std::string, uint16_t> m = {
        {"raw_block.bin",   OEC_STREAM_RAW_BLOCK},
        {"ttl_event.bin",   OEC_STREAM_TTL_EVENT},
        {"spike.bin",       OEC_STREAM_SPIKE},
        {"sync.bin",        OEC_STREAM_SYNC},
        {"cmd_set_ttl.bin", OEC_STREAM_CMD},
        {"ack_ok.bin",      OEC_STREAM_ACK},
        {"hello.bin",       OEC_STREAM_HELLO},
    };
    return m;
}
}  // namespace

TEST(GoldenCorpus, EveryFrameDecodesAndValidates) {
    fs::path dir = FindCorpusDir();
    ASSERT_FALSE(dir.empty()) << "could not locate tests/golden/v1.0";

    for (const auto& [name, stream_id] : ExpectedStreams()) {
        fs::path path = dir / name;
        ASSERT_TRUE(fs::exists(path)) << "missing golden frame: " << name;

        std::ifstream f(path, std::ios::binary);
        std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
        ASSERT_GE(bytes.size(), sizeof(oec_frame_header_t)) << name;

        oec_frame_header_t h;
        std::memcpy(&h, bytes.data(), sizeof(h));
        EXPECT_EQ(oec_frame_validate(&h), OEC_OK) << name;
        EXPECT_EQ(h.stream_id, stream_id) << name;
        EXPECT_EQ(h.payload_len, bytes.size() - sizeof(oec_frame_header_t)) << name;
    }
}
