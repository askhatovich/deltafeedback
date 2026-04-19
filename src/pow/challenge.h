#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace deltafeedback::pow {

// On-the-wire challenge format (single ASCII string, no padding):
//   <id_b64>.<expires_unix>.<difficulty>.<sig_b64>
//
//   id        : 16 random bytes, base64url, used as replay-protection key
//   expires   : unix seconds when this challenge stops being accepted
//   difficulty: required leading zero bits of SHA256(challenge || ":" || nonce)
//   sig       : HMAC-SHA256(secret, "<id>.<expires>.<difficulty>"), base64url
//
// Stateless on the server: the only state we need is the replay set keyed by id.

struct Challenge {
    std::string token;            // exact string returned to the client
    std::string id;               // raw bytes (16) — pass to ReplayStore
    std::int64_t expires_unix;
    unsigned difficulty_bits;
};

enum class VerifyResult {
    Ok,
    Malformed,
    BadSignature,
    Expired,
    InsufficientWork,
    Replay,
};

class ChallengeIssuer {
public:
    using Clock = std::function<std::int64_t()>;  // returns unix seconds

    ChallengeIssuer(std::string hmac_secret_raw,
                    unsigned difficulty_bits,
                    std::chrono::seconds ttl,
                    Clock clock = nullptr);

    // Generates a fresh challenge. `random_id` is injected for testability;
    // production caller should pass 16 bytes from a CSPRNG.
    Challenge issue(std::string_view random_id) const;

    // Verifies signature, expiry, and POW. Does NOT touch any replay store —
    // the caller threads that in (so the test impl can be in-memory).
    // On success, fills `out_id` with the challenge id (raw bytes).
    VerifyResult verify(std::string_view token,
                        std::string_view nonce,
                        std::string* out_id) const;

private:
    std::string secret_;
    unsigned difficulty_bits_;
    std::chrono::seconds ttl_;
    Clock clock_;
};

}  // namespace deltafeedback::pow
