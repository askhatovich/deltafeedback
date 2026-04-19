#include "pow/challenge.h"

#include "pow/sha256.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace deltafeedback::pow {

namespace {

// base64url without padding (RFC 4648 §5). We don't need the streaming OpenSSL
// API for these tiny inputs.
const char* kAlpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string b64url_encode(std::string_view in) {
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    size_t i = 0;
    while (i + 3 <= in.size()) {
        auto a = static_cast<unsigned char>(in[i++]);
        auto b = static_cast<unsigned char>(in[i++]);
        auto c = static_cast<unsigned char>(in[i++]);
        out.push_back(kAlpha[a >> 2]);
        out.push_back(kAlpha[((a & 0x03) << 4) | (b >> 4)]);
        out.push_back(kAlpha[((b & 0x0F) << 2) | (c >> 6)]);
        out.push_back(kAlpha[c & 0x3F]);
    }
    if (i < in.size()) {
        auto a = static_cast<unsigned char>(in[i++]);
        if (i < in.size()) {
            auto b = static_cast<unsigned char>(in[i++]);
            out.push_back(kAlpha[a >> 2]);
            out.push_back(kAlpha[((a & 0x03) << 4) | (b >> 4)]);
            out.push_back(kAlpha[(b & 0x0F) << 2]);
        } else {
            out.push_back(kAlpha[a >> 2]);
            out.push_back(kAlpha[(a & 0x03) << 4]);
        }
    }
    return out;
}

std::optional<std::string> b64url_decode(std::string_view in) {
    static signed char rev[256];
    static bool init = false;
    if (!init) {
        std::memset(rev, -1, sizeof(rev));
        for (int i = 0; i < 64; ++i) rev[static_cast<unsigned char>(kAlpha[i])] = static_cast<signed char>(i);
        init = true;
    }
    std::string out;
    out.reserve(in.size() * 3 / 4 + 2);
    int val = 0, bits = 0;
    for (char ch : in) {
        signed char v = rev[static_cast<unsigned char>(ch)];
        if (v < 0) return std::nullopt;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
        }
    }
    return out;
}

std::string i64_to_str(std::int64_t v) {
    return std::to_string(v);
}

bool parse_i64(std::string_view s, std::int64_t* out) {
    if (s.empty()) return false;
    std::int64_t v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + (c - '0');
    }
    *out = v;
    return true;
}

bool parse_uint(std::string_view s, unsigned* out) {
    if (s.empty()) return false;
    unsigned v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    *out = v;
    return true;
}

// constant-time eq for the HMAC compare
bool ct_eq(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    unsigned char d = 0;
    for (size_t i = 0; i < a.size(); ++i)
        d |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return d == 0;
}

}  // namespace

ChallengeIssuer::ChallengeIssuer(std::string secret,
                                 unsigned difficulty_bits,
                                 std::chrono::seconds ttl,
                                 Clock clock)
    : secret_(std::move(secret)),
      difficulty_bits_(difficulty_bits),
      ttl_(ttl),
      clock_(clock ? std::move(clock) : [] {
          return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
              std::chrono::system_clock::now().time_since_epoch()).count());
      }) {}

Challenge ChallengeIssuer::issue(std::string_view random_id) const {
    Challenge c;
    c.id = std::string(random_id);
    c.expires_unix = clock_() + ttl_.count();
    c.difficulty_bits = difficulty_bits_;

    std::string id_b64  = b64url_encode(c.id);
    std::string payload = id_b64 + "." + i64_to_str(c.expires_unix) + "." + std::to_string(c.difficulty_bits);
    auto sig = hmac_sha256(secret_, payload);
    std::string sig_b64 = b64url_encode(std::string_view(reinterpret_cast<const char*>(sig.data()), sig.size()));

    c.token = payload + "." + sig_b64;
    return c;
}

VerifyResult ChallengeIssuer::verify(std::string_view token,
                                     std::string_view nonce,
                                     std::string* out_id) const {
    // split into 4 fields
    std::string_view parts[4];
    size_t start = 0, idx = 0;
    for (size_t i = 0; i <= token.size() && idx < 4; ++i) {
        if (i == token.size() || token[i] == '.') {
            if (idx == 3 && i != token.size()) return VerifyResult::Malformed;
            parts[idx++] = token.substr(start, i - start);
            start = i + 1;
        }
    }
    if (idx != 4) return VerifyResult::Malformed;

    std::int64_t expires;
    unsigned difficulty;
    if (!parse_i64(parts[1], &expires))   return VerifyResult::Malformed;
    if (!parse_uint(parts[2], &difficulty)) return VerifyResult::Malformed;

    auto id_raw = b64url_decode(parts[0]);
    auto sig_raw = b64url_decode(parts[3]);
    if (!id_raw || !sig_raw) return VerifyResult::Malformed;

    std::string payload(parts[0]);
    payload.push_back('.');
    payload.append(parts[1]);
    payload.push_back('.');
    payload.append(parts[2]);
    auto expected = hmac_sha256(secret_, payload);
    std::string_view expected_sv(reinterpret_cast<const char*>(expected.data()), expected.size());
    if (!ct_eq(expected_sv, *sig_raw)) return VerifyResult::BadSignature;

    if (clock_() > expires) return VerifyResult::Expired;

    // POW: SHA256(token || ":" || nonce) must have >= difficulty leading zero bits.
    std::string buf;
    buf.reserve(token.size() + 1 + nonce.size());
    buf.append(token.data(), token.size());
    buf.push_back(':');
    buf.append(nonce.data(), nonce.size());
    auto digest = sha256(buf);
    if (leading_zero_bits(digest) < difficulty) return VerifyResult::InsufficientWork;

    if (out_id) *out_id = std::move(*id_raw);
    return VerifyResult::Ok;
}

}  // namespace deltafeedback::pow
