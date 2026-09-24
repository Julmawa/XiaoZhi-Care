#include "care_listening/listening_settings.h"

#include <esp_log.h>
#include <nvs.h>

namespace xiaozhi_care::listening {
namespace {

constexpr char kTag[] = "CARE_LISTEN";
constexpr char kNamespace[] = "care_listen";
constexpr char kProfileKey[] = "profile";

bool IsValidRaw(uint8_t value) {
    return value <= static_cast<uint8_t>(ListeningProfile::Slow);
}

}  // namespace

ListeningSettings& ListeningSettings::GetInstance() {
    static ListeningSettings instance;
    return instance;
}

bool ListeningSettings::Init() {
    if (initialized_.load()) {
        return true;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Unable to open listening settings NVS: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t raw = static_cast<uint8_t>(ListeningProfile::Medium);
    err = nvs_get_u8(handle, kProfileKey, &raw);

    if (err == ESP_ERR_NVS_NOT_FOUND || !IsValidRaw(raw)) {
        raw = static_cast<uint8_t>(ListeningProfile::Medium);
        err = nvs_set_u8(handle, kProfileKey, raw);
        if (err == ESP_OK) {
            err = nvs_commit(handle);
        }
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Unable to load listening profile: %s", esp_err_to_name(err));
        return false;
    }

    profile_.store(raw);
    initialized_.store(true);

    ESP_LOGI(kTag, "Listening profile loaded: %s (%lu ms)",
             GetProfileName(),
             static_cast<unsigned long>(GetSilenceMs()));
    return true;
}

ListeningProfile ListeningSettings::GetProfile() const {
    uint8_t raw = profile_.load();
    if (!IsValidRaw(raw)) {
        return ListeningProfile::Medium;
    }
    return static_cast<ListeningProfile>(raw);
}

uint32_t ListeningSettings::GetSilenceMs() const {
    return SilenceMs(GetProfile());
}

const char* ListeningSettings::GetProfileName() const {
    return ProfileText(GetProfile());
}

bool ListeningSettings::SetProfile(ListeningProfile profile) {
    const uint8_t raw = static_cast<uint8_t>(profile);
    if (!IsValidRaw(raw)) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kProfileKey, raw);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Unable to save listening profile: %s", esp_err_to_name(err));
        return false;
    }

    profile_.store(raw);
    initialized_.store(true);

    ESP_LOGI(kTag, "Listening profile changed: %s (%lu ms)",
             ProfileText(profile),
             static_cast<unsigned long>(SilenceMs(profile)));
    return true;
}

bool ListeningSettings::ParseProfile(const std::string& text, ListeningProfile& profile) {
    if (text == "fast") {
        profile = ListeningProfile::Fast;
        return true;
    }
    if (text == "medium") {
        profile = ListeningProfile::Medium;
        return true;
    }
    if (text == "slow") {
        profile = ListeningProfile::Slow;
        return true;
    }
    return false;
}

const char* ListeningSettings::ProfileText(ListeningProfile profile) {
    switch (profile) {
        case ListeningProfile::Fast: return "fast";
        case ListeningProfile::Medium: return "medium";
        case ListeningProfile::Slow: return "slow";
    }
    return "medium";
}

uint32_t ListeningSettings::SilenceMs(ListeningProfile profile) {
    switch (profile) {
        case ListeningProfile::Fast: return 1000;  // DP039_TIEMPOS_MENOS_500MS
        case ListeningProfile::Medium: return 1500;
        case ListeningProfile::Slow: return 2500;
    }
    return 2500;
}

}  // namespace xiaozhi_care::listening
