#include "pow/sha256.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <cstring>
#include <stdexcept>

namespace deltafeedback::pow {

Sha256Digest sha256(std::string_view data) {
    Sha256Digest out{};
    unsigned int n = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, data.data(), data.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, out.data(), &n) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_Digest* failed");
    }
    EVP_MD_CTX_free(ctx);
    return out;
}

Sha256Digest hmac_sha256(std::string_view key, std::string_view data) {
    Sha256Digest out{};
    unsigned int n = 0;
    if (!HMAC(EVP_sha256(),
              key.data(), static_cast<int>(key.size()),
              reinterpret_cast<const unsigned char*>(data.data()), data.size(),
              out.data(), &n)) {
        throw std::runtime_error("HMAC failed");
    }
    return out;
}

std::string to_hex(const Sha256Digest& d) {
    return to_hex(std::string_view(reinterpret_cast<const char*>(d.data()), d.size()));
}

std::string to_hex(std::string_view bytes) {
    static const char* kHex = "0123456789abcdef";
    std::string s(bytes.size() * 2, '\0');
    for (size_t i = 0; i < bytes.size(); ++i) {
        auto b = static_cast<unsigned char>(bytes[i]);
        s[2 * i]     = kHex[b >> 4];
        s[2 * i + 1] = kHex[b & 0x0F];
    }
    return s;
}

unsigned leading_zero_bits(const Sha256Digest& d) {
    unsigned count = 0;
    for (auto b : d) {
        if (b == 0) { count += 8; continue; }
        for (int i = 7; i >= 0; --i) {
            if ((b >> i) & 1) return count;
            ++count;
        }
        break;
    }
    return count;
}

}  // namespace deltafeedback::pow
