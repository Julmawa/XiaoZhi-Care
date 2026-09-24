#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace xiaozhi_care::listening {

enum class ListeningProfile : uint8_t {
    Fast = 0,
    Medium = 1,
    Slow = 2,
};

class ListeningSettings {
public:
    static ListeningSettings& GetInstance();

    bool Init();

    ListeningProfile GetProfile() const;
    uint32_t GetSilenceMs() const;
    const char* GetProfileName() const;

    bool SetProfile(ListeningProfile profile);

    static bool ParseProfile(const std::string& text, ListeningProfile& profile);
    static const char* ProfileText(ListeningProfile profile);
    static uint32_t SilenceMs(ListeningProfile profile);

private:
    ListeningSettings() = default;

    std::atomic<uint8_t> profile_{static_cast<uint8_t>(ListeningProfile::Medium)};
    std::atomic<bool> initialized_{false};
};

}  // namespace xiaozhi_care::listening
