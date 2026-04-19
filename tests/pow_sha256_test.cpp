#include "pow/sha256.h"

#include <gtest/gtest.h>

using deltafeedback::pow::hmac_sha256;
using deltafeedback::pow::leading_zero_bits;
using deltafeedback::pow::sha256;
using deltafeedback::pow::Sha256Digest;
using deltafeedback::pow::to_hex;

TEST(Sha256, EmptyAndKnownVector) {
    EXPECT_EQ(to_hex(sha256("")),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(to_hex(sha256("abc")),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(HmacSha256, KnownVector) {
    // RFC 4231 test case 1
    std::string key(20, '\x0b');
    EXPECT_EQ(to_hex(hmac_sha256(key, "Hi There")),
              "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
}

TEST(LeadingZeroBits, AllZero) {
    Sha256Digest d{};
    EXPECT_EQ(leading_zero_bits(d), 256u);
}

TEST(LeadingZeroBits, FirstByteHigh) {
    Sha256Digest d{};
    d[0] = 0x80;  // bit 7 set, no leading zeros
    EXPECT_EQ(leading_zero_bits(d), 0u);
}

TEST(LeadingZeroBits, ByteBoundary) {
    Sha256Digest d{};
    d[0] = 0x00;
    d[1] = 0x01;     // 8 zeros from byte 0, then 7 more zero bits in 0x01
    EXPECT_EQ(leading_zero_bits(d), 15u);
}
