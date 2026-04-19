#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace deltafeedback::pow {

using Sha256Digest = std::array<std::uint8_t, 32>;

// Thin wrappers over OpenSSL EVP/HMAC. Returning a fixed-size array avoids any
// allocation in hot paths (POW verify is hot) and makes test comparisons trivial.
Sha256Digest sha256(std::string_view data);

// HMAC-SHA256(key, data). `key` is raw bytes (any length).
Sha256Digest hmac_sha256(std::string_view key, std::string_view data);

// Hex helpers — lowercase, no separator.
std::string to_hex(const Sha256Digest& d);
std::string to_hex(std::string_view bytes);

// Returns the count of leading zero BITS in `d`. Used by POW verifier to compare
// against the configured difficulty without materialising any string.
unsigned leading_zero_bits(const Sha256Digest& d);

}  // namespace deltafeedback::pow
