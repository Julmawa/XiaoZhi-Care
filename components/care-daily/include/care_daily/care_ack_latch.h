#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <set>

namespace xiaozhi_care::daily {
// Only a successfully presented single occurrence can arm the input.
// Deliberately independent from medication execution history.
class CareAckLatch {
public:
    void Arm(std::string key, int64_t now, std::string scope = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (scope_ != scope) {
            scope_ = std::move(scope);
            acknowledged_.clear();
        }
        ++serial_;
        key_ = std::move(key);
        deadline_ = now + 90000000;
    }
    uint64_t Capture(int64_t now) {
        std::lock_guard<std::mutex> lock(mutex_);
        return Eligible(now) ? serial_ : 0;
    }
    template<class Store>
    bool Accept(uint64_t captured, int64_t now, Store store) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!captured || captured != serial_ || !Eligible(now) || acknowledged_.size() >= 512 || !store(key_)) return false;
        acknowledged_.insert(key_);
        return true;
    }
private:
    bool Eligible(int64_t now) const { return !key_.empty() && !acknowledged_.count(key_) && now < deadline_; }
    std::mutex mutex_;
    std::string key_;
    std::string scope_;
    std::set<std::string> acknowledged_;
    int64_t deadline_ = 0;
    uint64_t serial_ = 0;
};
}
