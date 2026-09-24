#include "care_news/news_settings.h"

#include <esp_log.h>
#include <nvs.h>

namespace xiaozhi_care::news {
namespace {

constexpr char kTag[] = "CARE_NEWS";
constexpr char kNamespace[] = "care_news";
constexpr char kCategoryKey[] = "category";
constexpr char kCountKey[] = "count";

bool ValidCategoryRaw(uint8_t value) {
    return value <= static_cast<uint8_t>(NewsCategory::Deportes);
}

bool ValidCount(uint8_t value) {
    return value >= 1 && value <= 5;
}

}  // namespace

NewsSettings& NewsSettings::GetInstance() {
    static NewsSettings instance;
    return instance;
}

bool NewsSettings::Init() {
    if (initialized_.load()) {
        return true;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Unable to open news settings NVS: %s", esp_err_to_name(err));
        return false;
    }

    uint8_t category = static_cast<uint8_t>(NewsCategory::Argentina);
    uint8_t count = 3;

    esp_err_t category_err = nvs_get_u8(handle, kCategoryKey, &category);
    esp_err_t count_err = nvs_get_u8(handle, kCountKey, &count);

    bool needs_commit = false;

    if (category_err == ESP_ERR_NVS_NOT_FOUND || !ValidCategoryRaw(category)) {
        category = static_cast<uint8_t>(NewsCategory::Argentina);
        if (nvs_set_u8(handle, kCategoryKey, category) == ESP_OK) {
            needs_commit = true;
        }
    } else if (category_err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGW(kTag, "Unable to read news category: %s", esp_err_to_name(category_err));
        return false;
    }

    if (count_err == ESP_ERR_NVS_NOT_FOUND || !ValidCount(count)) {
        count = 3;
        if (nvs_set_u8(handle, kCountKey, count) == ESP_OK) {
            needs_commit = true;
        }
    } else if (count_err != ESP_OK) {
        nvs_close(handle);
        ESP_LOGW(kTag, "Unable to read news count: %s", esp_err_to_name(count_err));
        return false;
    }

    if (needs_commit) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Unable to initialize news settings: %s", esp_err_to_name(err));
        return false;
    }

    category_.store(category);
    headline_count_.store(count);
    initialized_.store(true);

    ESP_LOGI(kTag, "News settings loaded: category=%s count=%u",
             GetDefaultCategoryName(),
             static_cast<unsigned>(GetHeadlineCount()));
    return true;
}

NewsCategory NewsSettings::GetDefaultCategory() const {
    uint8_t raw = category_.load();
    if (!ValidCategoryRaw(raw)) {
        return NewsCategory::Argentina;
    }
    return static_cast<NewsCategory>(raw);
}

const char* NewsSettings::GetDefaultCategoryName() const {
    return CategoryText(GetDefaultCategory());
}

uint8_t NewsSettings::GetHeadlineCount() const {
    uint8_t value = headline_count_.load();
    return ValidCount(value) ? value : 3;
}

bool NewsSettings::Set(NewsCategory category, uint8_t headline_count) {
    const uint8_t raw_category = static_cast<uint8_t>(category);
    if (!ValidCategoryRaw(raw_category) || !ValidCount(headline_count)) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kCategoryKey, raw_category);
    }
    if (err == ESP_OK) {
        err = nvs_set_u8(handle, kCountKey, headline_count);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (handle != 0) {
        nvs_close(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(kTag, "Unable to save news settings: %s", esp_err_to_name(err));
        return false;
    }

    category_.store(raw_category);
    headline_count_.store(headline_count);
    initialized_.store(true);

    ESP_LOGI(kTag, "News settings changed: category=%s count=%u",
             CategoryText(category),
             static_cast<unsigned>(headline_count));
    return true;
}

bool NewsSettings::ParseCategory(const std::string& text, NewsCategory& category) {
    if (text == "argentina" || text == "general" || text == "actualidad") {
        category = NewsCategory::Argentina;
        return true;
    }
    if (text == "mundo" || text == "world" || text == "internacional") {
        category = NewsCategory::Mundo;
        return true;
    }
    if (text == "tecnologia" || text == "technology" || text == "tech") {
        category = NewsCategory::Tecnologia;
        return true;
    }
    if (text == "deportes" || text == "deporte" || text == "sports") {
        category = NewsCategory::Deportes;
        return true;
    }
    return false;
}

const char* NewsSettings::CategoryText(NewsCategory category) {
    switch (category) {
        case NewsCategory::Argentina: return "argentina";
        case NewsCategory::Mundo: return "mundo";
        case NewsCategory::Tecnologia: return "tecnologia";
        case NewsCategory::Deportes: return "deportes";
    }
    return "argentina";
}

}  // namespace xiaozhi_care::news
