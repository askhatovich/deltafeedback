#include "server/ratelimit.h"

namespace deltafeedback::server {

TokenBucket::TokenBucket(double rate, double burst) : rate_(rate), burst_(burst) {}

bool TokenBucket::allow(const std::string& key) {
    auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> g(mu_);

    auto& b = buckets_[key];
    if (b.last.time_since_epoch().count() == 0) {
        b.tokens = burst_;
        b.last = now;
    } else {
        double dt = std::chrono::duration<double>(now - b.last).count();
        b.tokens = std::min(burst_, b.tokens + dt * rate_);
        b.last = now;
    }

    if (b.tokens >= 1.0) {
        b.tokens -= 1.0;
        return true;
    }
    return false;
}

}  // namespace deltafeedback::server
