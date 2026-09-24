#pragma once

#include <atomic>
#include <cstdint>
#include <string>

namespace xiaozhi_care::news {

enum class NewsCategory : uint8_t {
    Argentina = 0,
    Mundo = 1,
    Tecnologia = 2,
    Deportes = 3,
};

class NewsSettings {
public:
    static NewsSettings& GetInstance();

    bool Init();

    NewsCategory GetDefaultCategory() const;
    const char* GetDefaultCategoryName() const;
    uint8_t GetHeadlineCount() const;

    bool Set(NewsCategory category, uint8_t headline_count);

    static bool ParseCategory(const std::string& text, NewsCategory& category);
    static const char* CategoryText(NewsCategory category);

private:
    NewsSettings() = default;

    std::atomic<uint8_t> category_{static_cast<uint8_t>(NewsCategory::Argentina)};
    std::atomic<uint8_t> headline_count_{3};
    std::atomic<bool> initialized_{false};
};

}  // namespace xiaozhi_care::news
