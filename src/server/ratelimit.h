#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace deltafeedback::server {

// Per-key (typically: client IP) token bucket.
//
// In-memory only — restart resets the buckets, that's intentional. Eviction is
// lazy: a key is dropped from the map when its tokens are full and it's been
// idle for 2× refill interval.
class TokenBucket {
public:
    TokenBucket(double tokens_per_second, double burst);

    // Returns true if a token was available and consumed.
    bool allow(const std::string& key);

private:
    struct Bucket {
        double                                tokens;
        std::chrono::steady_clock::time_point last;
    };
    double rate_;
    double burst_;
    std::mutex mu_;
    std::unordered_map<std::string, Bucket> buckets_;
};

}  // namespace deltafeedback::server
