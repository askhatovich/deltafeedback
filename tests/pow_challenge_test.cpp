#include "pow/challenge.h"
#include "pow/sha256.h"

#include <gtest/gtest.h>

#include <chrono>

using namespace deltafeedback::pow;

namespace {

// 16-byte fixed id for reproducibility.
const std::string kId = std::string("0123456789ABCDEF", 16);
const std::string kSecret = "test-hmac-secret";

// Brute-force a nonce that satisfies `bits` of difficulty against `token`.
// Used to drive the verifier in tests; production clients do this in the browser.
std::string find_nonce(const std::string& token, unsigned bits) {
    for (std::uint64_t n = 0; n < (1ULL << 32); ++n) {
        std::string nonce = std::to_string(n);
        std::string buf = token + ":" + nonce;
        if (leading_zero_bits(sha256(buf)) >= bits) return nonce;
    }
    return {};
}

}  // namespace

TEST(Challenge, IssueAndVerifyHappyPath) {
    ChallengeIssuer issuer(kSecret, /*difficulty=*/8, std::chrono::seconds(60),
                           [] { return std::int64_t{1'700'000'000}; });
    auto c = issuer.issue(kId);

    auto nonce = find_nonce(c.token, c.difficulty_bits);
    ASSERT_FALSE(nonce.empty());

    std::string id_out;
    EXPECT_EQ(issuer.verify(c.token, nonce, &id_out), VerifyResult::Ok);
    EXPECT_EQ(id_out, kId);
}

TEST(Challenge, RejectsBadSignature) {
    ChallengeIssuer issuer(kSecret, 8, std::chrono::seconds(60),
                           [] { return std::int64_t{1'700'000'000}; });
    auto c = issuer.issue(kId);

    // Corrupt last char of signature.
    std::string bad = c.token;
    bad.back() = (bad.back() == 'A') ? 'B' : 'A';
    EXPECT_EQ(issuer.verify(bad, "0", nullptr), VerifyResult::BadSignature);
}

TEST(Challenge, RejectsExpired) {
    std::int64_t fake_now = 1'700'000'000;
    auto clock = [&] { return fake_now; };
    ChallengeIssuer issuer(kSecret, 4, std::chrono::seconds(10), clock);

    auto c = issuer.issue(kId);
    fake_now += 11;

    auto nonce = find_nonce(c.token, c.difficulty_bits);
    EXPECT_EQ(issuer.verify(c.token, nonce, nullptr), VerifyResult::Expired);
}

TEST(Challenge, RejectsInsufficientWork) {
    ChallengeIssuer issuer(kSecret, /*difficulty=*/24, std::chrono::seconds(60),
                           [] { return std::int64_t{1'700'000'000}; });
    auto c = issuer.issue(kId);
    // "0" almost certainly does not satisfy 24 bits of difficulty.
    EXPECT_EQ(issuer.verify(c.token, "0", nullptr), VerifyResult::InsufficientWork);
}

TEST(Challenge, RejectsMalformed) {
    ChallengeIssuer issuer(kSecret, 8, std::chrono::seconds(60), [] { return std::int64_t{0}; });
    EXPECT_EQ(issuer.verify("garbage", "x", nullptr),       VerifyResult::Malformed);
    EXPECT_EQ(issuer.verify("a.b.c",   "x", nullptr),       VerifyResult::Malformed);
    EXPECT_EQ(issuer.verify("a.b.c.d.e", "x", nullptr),     VerifyResult::Malformed);
}
