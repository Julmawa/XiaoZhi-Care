from pathlib import Path
from datetime import datetime
import shutil
import re
import sys

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")

MCP_DIR = ROOT / "components" / "care-mcp"
WEB_DIR = ROOT / "components" / "care-web"
NEWS_DIR = ROOT / "components" / "care-news"

MCP_SRC = MCP_DIR / "care_mcp_service.cc"
MCP_H = MCP_DIR / "include" / "care_mcp_service.h"
MCP_XIAO = MCP_DIR / "include" / "care_mcp_xiaozhi.h"
MCP_CMAKE = MCP_DIR / "CMakeLists.txt"

WEB_PAGE = WEB_DIR / "care_web_page.cc"
WEB_SERVER = WEB_DIR / "care_web_server.cc"
WEB_DATA = WEB_DIR / "care_web_data.cc"
WEB_H = WEB_DIR / "include" / "care_web_server.h"
WEB_CMAKE = WEB_DIR / "CMakeLists.txt"

FILES = [
    MCP_SRC, MCP_H, MCP_XIAO, MCP_CMAKE,
    WEB_PAGE, WEB_SERVER, WEB_DATA, WEB_H, WEB_CMAKE,
]

for p in FILES:
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

texts = {p: p.read_text(encoding="utf-8") for p in FILES}

MARKER = "DP041B_NEWS_SYSTEM_CONFIG"
if any(MARKER in texts[p] for p in FILES) or NEWS_DIR.exists():
    print("DP-041B Noticias configurable ya parece aplicada. No se hicieron cambios.")
    sys.exit(0)

mcp_src = texts[MCP_SRC]
mcp_h = texts[MCP_H]
mcp_xiao = texts[MCP_XIAO]
mcp_cmake = texts[MCP_CMAKE]
page = texts[WEB_PAGE]
server = texts[WEB_SERVER]
web_data = texts[WEB_DATA]
web_h = texts[WEB_H]
web_cmake = texts[WEB_CMAKE]

required = {
    "DP041 implementación": "CareMcpService::GetLatestNews" in mcp_src,
    "DP041 tool": '"care.get_latest_news"' in mcp_xiao,
    "DP041 marker": "DP041_LATEST_NEWS" in mcp_src,
    "Sistema ordenado": "DP040D_SYSTEM_ORDER_1_TO_7" in page,
    "Volumen web": "saveAudioVolumeBtn" in page,
    "Mantenimiento status": "HandleMaintenanceStatus" in web_data,
    "Ruta listening": "/api/maintenance/listening-profile" in server,
}
bad = [k for k, ok in required.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: el proyecto no coincide con el estado esperado:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )

def add_require(cmake_text, dep):
    if dep in cmake_text:
        return cmake_text
    anchor = "    REQUIRES\n"
    pos = cmake_text.find(anchor)
    if pos < 0:
        raise SystemExit(f"ERROR: no encontré REQUIRES para agregar {dep}")
    insert_pos = pos + len(anchor)
    return cmake_text[:insert_pos] + f"        {dep}\n" + cmake_text[insert_pos:]

def add_include_before_namespace(text, include_line, label):
    if include_line in text:
        return text
    marker = "\nnamespace xiaozhi_care"
    pos = text.find(marker)
    if pos < 0:
        raise SystemExit(f"ERROR: {label}: no encontré namespace xiaozhi_care")
    return text[:pos] + "\n" + include_line + text[pos:]

news_h = r'''#pragma once

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
'''

news_cc = r'''#include "care_news/news_settings.h"

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
'''

news_cmake = r'''idf_component_register(
    SRCS "news_settings.cc"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash
)
'''

mcp_src = add_include_before_namespace(
    mcp_src,
    '#include "care_news/news_settings.h"',
    "care_mcp_service.cc include care-news",
)

old_decl = 'std::string GetLatestNews(const std::string& category = "argentina");'
new_decl = 'std::string GetLatestNews(const std::string& category = "default");'
if old_decl not in mcp_h:
    raise SystemExit("ERROR: no encontré la declaración DP-041 esperada en care_mcp_service.h.")
mcp_h = mcp_h.replace(old_decl, new_decl, 1)

impl_start = mcp_src.find("// DP041_LATEST_NEWS")
namespace_end = mcp_src.rfind("}  // namespace xiaozhi_care")
if impl_start < 0 or namespace_end < 0 or impl_start >= namespace_end:
    raise SystemExit("ERROR: no pude ubicar de forma segura la implementación DP-041.")

sep_start = mcp_src.rfind("// -----------------------------------------------------------------------------", 0, impl_start)
if sep_start >= 0 and impl_start - sep_start < 200:
    impl_start = sep_start

new_impl = r'''// -----------------------------------------------------------------------------
// DP041_LATEST_NEWS
// DP041B_NEWS_SYSTEM_CONFIG
// Noticias recientes para XiaoZhi Care.
// Fuente: Google News RSS (español / Argentina).
// Categoría predeterminada y cantidad configurables desde Sistema.
// -----------------------------------------------------------------------------
std::string CareMcpService::GetLatestNews(const std::string& category) {
    struct NewsHttpBuffer {
        std::string data;
        size_t complete_items = 0;
        size_t wanted_items = 3;
    };

    auto replace_all = [](std::string& value,
                          const std::string& from,
                          const std::string& to) {
        if (from.empty()) return;
        size_t pos = 0;
        while ((pos = value.find(from, pos)) != std::string::npos) {
            value.replace(pos, from.size(), to);
            pos += to.size();
        }
    };

    auto decode_xml = [&](std::string value) -> std::string {
        const std::string cdata_open = "<![CDATA[";
        const std::string cdata_close = "]]>";
        if (value.size() >= cdata_open.size() + cdata_close.size() &&
            value.compare(0, cdata_open.size(), cdata_open) == 0 &&
            value.compare(value.size() - cdata_close.size(),
                          cdata_close.size(), cdata_close) == 0) {
            value = value.substr(
                cdata_open.size(),
                value.size() - cdata_open.size() - cdata_close.size()
            );
        }

        replace_all(value, "&amp;", "&");
        replace_all(value, "&quot;", "\"");
        replace_all(value, "&apos;", "'");
        replace_all(value, "&#39;", "'");
        replace_all(value, "&#039;", "'");
        replace_all(value, "&#34;", "\"");
        replace_all(value, "&lt;", "<");
        replace_all(value, "&gt;", ">");

        while (!value.empty() &&
               (value.front() == ' ' || value.front() == '\n' ||
                value.front() == '\r' || value.front() == '\t')) {
            value.erase(value.begin());
        }
        while (!value.empty() &&
               (value.back() == ' ' || value.back() == '\n' ||
                value.back() == '\r' || value.back() == '\t')) {
            value.pop_back();
        }
        return value;
    };

    auto extract_tag = [&](const std::string& block,
                           const std::string& tag) -> std::string {
        const std::string open = "<" + tag;
        const std::string close = "</" + tag + ">";

        const size_t start = block.find(open);
        if (start == std::string::npos) return "";

        const size_t content_start = block.find('>', start);
        if (content_start == std::string::npos) return "";

        const size_t end = block.find(close, content_start + 1);
        if (end == std::string::npos) return "";

        return decode_xml(
            block.substr(content_start + 1, end - content_start - 1)
        );
    };

    auto lower_ascii = [](std::string value) -> std::string {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return value;
    };

    auto& news_settings = news::NewsSettings::GetInstance();
    news_settings.Init();

    std::string requested = lower_ascii(category);
    std::string normalized;

    if (requested.empty() || requested == "default" ||
        requested == "general" || requested == "actualidad") {
        normalized = news_settings.GetDefaultCategoryName();
    } else {
        news::NewsCategory parsed_category;
        if (news::NewsSettings::ParseCategory(requested, parsed_category)) {
            normalized = news::NewsSettings::CategoryText(parsed_category);
        } else {
            normalized = news_settings.GetDefaultCategoryName();
        }
    }

    const size_t headline_limit =
        static_cast<size_t>(news_settings.GetHeadlineCount());

    const char* url = nullptr;
    if (normalized == "mundo") {
        url =
            "https://news.google.com/rss/headlines/section/topic/WORLD"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    } else if (normalized == "tecnologia") {
        url =
            "https://news.google.com/rss/headlines/section/topic/TECHNOLOGY"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    } else if (normalized == "deportes") {
        url =
            "https://news.google.com/rss/headlines/section/topic/SPORTS"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    } else {
        url =
            "https://news.google.com/rss"
            "?hl=es-419&gl=AR&ceid=AR:es-419";
    }

    static std::mutex cache_mutex;
    static std::string cache_category;
    static std::string cache_json;
    static size_t cache_count = 0;
    static uint32_t cache_ms = 0;

    const uint32_t now_ms = esp_log_timestamp();
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (!cache_json.empty() &&
            cache_category == normalized &&
            cache_count == headline_limit &&
            static_cast<uint32_t>(now_ms - cache_ms) < 600000U) {
            ESP_LOGI("CARE_MCP",
                     "News cache hit category=%s count=%u",
                     normalized.c_str(),
                     static_cast<unsigned>(headline_limit));
            return cache_json;
        }
    }

    NewsHttpBuffer response;
    response.wanted_items = headline_limit;
    response.data.reserve(16384);

    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = 10000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.user_data = &response;
    config.event_handler = [](esp_http_client_event_t* evt) -> esp_err_t {
        if (evt == nullptr ||
            evt->event_id != HTTP_EVENT_ON_DATA ||
            evt->user_data == nullptr ||
            evt->data == nullptr ||
            evt->data_len <= 0) {
            return ESP_OK;
        }

        auto* out = static_cast<NewsHttpBuffer*>(evt->user_data);
        if (out->complete_items >= out->wanted_items ||
            out->data.size() >= 32768U) {
            return ESP_OK;
        }

        const size_t room = 32768U - out->data.size();
        const size_t wanted = static_cast<size_t>(evt->data_len);
        const size_t take = std::min(room, wanted);

        out->data.append(static_cast<const char*>(evt->data), take);

        size_t count = 0;
        size_t scan = 0;
        while ((scan = out->data.find("</item>", scan)) != std::string::npos) {
            ++count;
            scan += 7;
            if (count >= out->wanted_items) break;
        }
        out->complete_items = count;
        return ESP_OK;
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "source", "google_news_rss");
        cJSON_AddStringToObject(
            root, "safe_message",
            "Perdón, no pude consultar las noticias en este momento."
        );
        cJSON_AddStringToObject(
            root, "instruction",
            "Responde en español usando safe_message. No inventes titulares."
        );
        return Stringify(root);
    }

    esp_http_client_set_header(client, "User-Agent", "XiaoZhi-Care/0.3");
    esp_http_client_set_header(
        client, "Accept", "application/rss+xml, application/xml, text/xml"
    );

    const esp_err_t err = esp_http_client_perform(client);
    const int status =
        (err == ESP_OK) ? esp_http_client_get_status_code(client) : -1;

    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (!cache_json.empty() &&
            cache_category == normalized &&
            cache_count == headline_limit) {
            ESP_LOGW("CARE_MCP",
                     "News query failed category=%s status=%d; using cache",
                     normalized.c_str(), status);
            return cache_json;
        }

        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddNumberToObject(root, "http_status", status);
        cJSON_AddStringToObject(root, "source", "google_news_rss");
        cJSON_AddStringToObject(
            root, "safe_message",
            "Perdón, no pude consultar las noticias en este momento."
        );
        cJSON_AddStringToObject(
            root, "instruction",
            "Responde en español usando safe_message. No inventes titulares."
        );
        ESP_LOGW("CARE_MCP",
                 "News query failed category=%s status=%d err=%s",
                 normalized.c_str(),
                 status,
                 esp_err_to_name(err));
        return Stringify(root);
    }

    struct Headline {
        std::string title;
        std::string source;
        std::string published;
    };

    std::vector<Headline> headlines;
    headlines.reserve(headline_limit);

    size_t cursor = 0;
    while (headlines.size() < headline_limit) {
        const size_t item_start = response.data.find("<item", cursor);
        if (item_start == std::string::npos) break;

        const size_t body_start = response.data.find('>', item_start);
        if (body_start == std::string::npos) break;

        const size_t item_end =
            response.data.find("</item>", body_start + 1);
        if (item_end == std::string::npos) break;

        const std::string block =
            response.data.substr(
                body_start + 1,
                item_end - body_start - 1
            );

        Headline h;
        h.title = extract_tag(block, "title");
        h.source = extract_tag(block, "source");
        h.published = extract_tag(block, "pubDate");

        if (!h.title.empty()) {
            if (!h.source.empty()) {
                const std::string suffix = " - " + h.source;
                if (h.title.size() > suffix.size() &&
                    h.title.compare(h.title.size() - suffix.size(),
                                    suffix.size(),
                                    suffix) == 0) {
                    h.title.erase(h.title.size() - suffix.size());
                }
            } else {
                const size_t sep = h.title.rfind(" - ");
                if (sep != std::string::npos &&
                    sep > 10 &&
                    h.title.size() - sep < 80) {
                    h.source = h.title.substr(sep + 3);
                    h.title.erase(sep);
                }
            }
            headlines.push_back(h);
        }

        cursor = item_end + 7;
    }

    if (headlines.empty()) {
        cJSON* root = cJSON_CreateObject();
        cJSON_AddBoolToObject(root, "ok", false);
        cJSON_AddStringToObject(root, "source", "google_news_rss");
        cJSON_AddStringToObject(
            root, "safe_message",
            "Encontré el servicio de noticias, pero no pude leer los titulares."
        );
        cJSON_AddStringToObject(
            root, "instruction",
            "Responde en español usando safe_message. No inventes titulares."
        );
        ESP_LOGW("CARE_MCP",
                 "News RSS parsed zero items category=%s bytes=%u",
                 normalized.c_str(),
                 static_cast<unsigned>(response.data.size()));
        return Stringify(root);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "ok", true);
    cJSON_AddBoolToObject(root, "found", true);
    cJSON_AddStringToObject(root, "source", "google_news_rss");
    cJSON_AddStringToObject(root, "category", normalized.c_str());
    cJSON_AddNumberToObject(
        root, "count", static_cast<double>(headlines.size())
    );

    cJSON* array = cJSON_AddArrayToObject(root, "headlines");
    const char* ordinals[] = {
        "Primera: ", "Segunda: ", "Tercera: ", "Cuarta: ", "Quinta: "
    };

    std::string safe = "Estas son las noticias principales.";
    for (size_t i = 0; i < headlines.size(); ++i) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "title", headlines[i].title.c_str());
        if (!headlines[i].source.empty()) {
            cJSON_AddStringToObject(
                item, "publisher", headlines[i].source.c_str()
            );
        }
        if (!headlines[i].published.empty()) {
            cJSON_AddStringToObject(
                item, "published", headlines[i].published.c_str()
            );
        }
        cJSON_AddItemToArray(array, item);

        safe += " ";
        safe += ordinals[i < 5 ? i : 4];
        safe += headlines[i].title;
        if (!headlines[i].source.empty()) {
            safe += ", según ";
            safe += headlines[i].source;
        }
        safe += ".";
    }

    cJSON_AddStringToObject(root, "safe_message", safe.c_str());
    cJSON_AddStringToObject(
        root,
        "instruction",
        "RESULTADO AUTORITATIVO DE NOTICIAS. "
        "Responde en español y lee solamente los titulares de safe_message, "
        "de forma clara y breve. No inventes detalles, contexto ni noticias "
        "adicionales. Si el usuario pide ampliar una noticia, aclara que esta "
        "herramienta sólo obtuvo el titular y la fuente."
    );

    const std::string result = Stringify(root);

    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        cache_category = normalized;
        cache_count = headline_limit;
        cache_json = result;
        cache_ms = now_ms;
    }

    ESP_LOGI("CARE_MCP",
             "News query category=%s status=%d items=%u bytes=%u",
             normalized.c_str(),
             status,
             static_cast<unsigned>(headlines.size()),
             static_cast<unsigned>(response.data.size()));

    return result;
}

'''

mcp_src = mcp_src[:impl_start] + new_impl + "\n" + mcp_src[namespace_end:]

tool_marker = mcp_xiao.find("    // DP041_LATEST_NEWS")
registered_pos = mcp_xiao.find("    registered = true;", tool_marker)
if tool_marker < 0 or registered_pos < 0:
    raise SystemExit("ERROR: no pude ubicar el bloque MCP de noticias.")

new_tool = r'''    // DP041_LATEST_NEWS
    // DP041B_NEWS_SYSTEM_CONFIG
    server.AddTool(
        "care.get_latest_news",
        "NOTICIAS Y ACTUALIDAD. Usa esta herramienta cuando el usuario pida "
        "noticias o quiera saber que esta pasando, por ejemplo: 'leeme las "
        "noticias', 'quiero saber que pasa', 'que paso hoy', 'que novedades "
        "hay', 'poneme al dia', 'que esta pasando en Argentina', 'que esta "
        "pasando en el mundo', 'noticias de tecnologia' o 'noticias de "
        "deportes'. IMPORTANTE: si pide noticias sin especificar categoria, "
        "NO preguntes que categoria quiere. Llama inmediatamente a esta "
        "herramienta con category='default'; la categoria predeterminada se "
        "configura desde Sistema en XiaoZhi Care. Si menciona Argentina, mundo, "
        "tecnologia o deportes, usa esa categoria. NO la uses para agenda "
        "personal: 'que tengo para hacer hoy', 'que me falta hacer' o 'que "
        "tengo pendiente' siguen usando rutinas/pendientes. Lee la cantidad "
        "de titulares configurada en Sistema, con titulo y fuente.",
        PropertyList({
            Property("category", kPropertyTypeString, std::string("default")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_latest_news");
            return service.GetLatestNews(
                properties["category"].value<std::string>()
            );
        });

'''
mcp_xiao = mcp_xiao[:tool_marker] + new_tool + mcp_xiao[registered_pos:]

mcp_cmake = add_require(mcp_cmake, "care-news")

web_data = add_include_before_namespace(
    web_data,
    '#include "care_news/news_settings.h"',
    "care_web_data.cc include care-news",
)
web_cmake = add_require(web_cmake, "care-news")

news_route = '        {"/api/maintenance/news-settings", HTTP_PUT, HandleMaintenanceNewsSettings},\n'
if "/api/maintenance/news-settings" not in server:
    anchor = '        {"/api/maintenance/audio-volume", HTTP_PUT, HandleMaintenanceAudioVolume},\n'
    if anchor not in server:
        anchor = '        {"/api/maintenance/listening-profile", HTTP_PUT, HandleMaintenanceListeningProfile},\n'
    if anchor not in server:
        raise SystemExit("ERROR: no encontré ancla segura para la ruta de noticias.")
    server = server.replace(anchor, anchor + news_route, 1)

if "HandleMaintenanceNewsSettings" not in web_h:
    anchor = "    static esp_err_t HandleMaintenanceAudioVolume(httpd_req_t* req);\n"
    if anchor not in web_h:
        anchor = "    static esp_err_t HandleMaintenanceListeningProfile(httpd_req_t* req);\n"
    if anchor not in web_h:
        raise SystemExit("ERROR: no encontré ancla de handlers en care_web_server.h")
    web_h = web_h.replace(
        anchor,
        anchor + "    static esp_err_t HandleMaintenanceNewsSettings(httpd_req_t* req);\n",
        1
    )

status_start = web_data.find("esp_err_t CareWebServer::HandleMaintenanceStatus(httpd_req_t* req) {")
if status_start < 0:
    raise SystemExit("ERROR: no encontré HandleMaintenanceStatus.")
next_handler = web_data.find("\nesp_err_t CareWebServer::Handle", status_start + 10)
if next_handler < 0:
    raise SystemExit("ERROR: no encontré el final de HandleMaintenanceStatus.")
status_block = web_data[status_start:next_handler]

if '"news"' not in status_block:
    success_anchor = '    cJSON_AddBoolToObject(root, "success", true);\n'
    if success_anchor not in status_block:
        raise SystemExit("ERROR: no encontré ancla de success en maintenance status.")
    news_status = r'''    // DP041B_NEWS_SYSTEM_CONFIG
    auto& news_settings = news::NewsSettings::GetInstance();
    news_settings.Init();
    cJSON* news_json = cJSON_CreateObject();
    if (news_json != nullptr) {
        cJSON_AddStringToObject(news_json, "default_category",
                                news_settings.GetDefaultCategoryName());
        cJSON_AddNumberToObject(news_json, "headline_count",
                                news_settings.GetHeadlineCount());
        cJSON_AddStringToObject(news_json, "source", "Google News RSS");
        cJSON_AddItemToObject(data, "news", news_json);
    }

'''
    status_block = status_block.replace(success_anchor, news_status + success_anchor, 1)
    web_data = web_data[:status_start] + status_block + web_data[next_handler:]

handler_impl = r'''
// DP041B_NEWS_SYSTEM_CONFIG
esp_err_t CareWebServer::HandleMaintenanceNewsSettings(httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) return ESP_OK;

    if (req->content_len <= 0 || req->content_len > 192) {
        return SendNamedError(req, "400 Bad Request", "INVALID_NEWS_SETTINGS");
    }

    std::string body(static_cast<size_t>(req->content_len), '\0');
    size_t received = 0;
    while (received < body.size()) {
        int n = httpd_req_recv(req, body.data() + received, body.size() - received);
        if (n <= 0) {
            return SendNamedError(req, "400 Bad Request", "INVALID_BODY");
        }
        received += static_cast<size_t>(n);
    }

    cJSON* json = cJSON_ParseWithLength(body.data(), body.size());
    if (json == nullptr) {
        return SendNamedError(req, "400 Bad Request", "INVALID_JSON");
    }

    cJSON* category_json =
        cJSON_GetObjectItemCaseSensitive(json, "default_category");
    cJSON* count_json =
        cJSON_GetObjectItemCaseSensitive(json, "headline_count");

    if (!cJSON_IsString(category_json) ||
        category_json->valuestring == nullptr ||
        !cJSON_IsNumber(count_json)) {
        cJSON_Delete(json);
        return SendNamedError(req, "400 Bad Request", "INVALID_NEWS_SETTINGS");
    }

    const std::string category_text = category_json->valuestring;
    const int count = count_json->valueint;
    cJSON_Delete(json);

    news::NewsCategory category;
    if (!news::NewsSettings::ParseCategory(category_text, category) ||
        count < 1 || count > 5) {
        return SendNamedError(req, "400 Bad Request", "INVALID_NEWS_SETTINGS");
    }

    auto& settings = news::NewsSettings::GetInstance();
    settings.Init();

    if (!settings.Set(category, static_cast<uint8_t>(count))) {
        return SendNamedError(req, "500 Internal Server Error",
                              "NEWS_SETTINGS_SAVE_FAILED");
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* out = cJSON_CreateObject();
    if (root == nullptr || out == nullptr) {
        if (root) cJSON_Delete(root);
        if (out) cJSON_Delete(out);
        return SendJsonText(req, "500 Internal Server Error",
                            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }

    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddStringToObject(out, "default_category",
                            settings.GetDefaultCategoryName());
    cJSON_AddNumberToObject(out, "headline_count",
                            settings.GetHeadlineCount());
    cJSON_AddStringToObject(out, "source", "Google News RSS");
    cJSON_AddItemToObject(root, "data", out);

    ESP_LOGI("CARE_WEB", "News settings saved: category=%s count=%u",
             settings.GetDefaultCategoryName(),
             static_cast<unsigned>(settings.GetHeadlineCount()));

    return SendRoot(req, root);
}

'''

if "CareWebServer::HandleMaintenanceNewsSettings" not in web_data:
    clear_anchor = "esp_err_t CareWebServer::HandleMaintenanceClearExecutions(httpd_req_t* req) {\n"
    if clear_anchor not in web_data:
        raise SystemExit("ERROR: no encontré ancla para insertar handler de noticias.")
    web_data = web_data.replace(clear_anchor, handler_impl + clear_anchor, 1)

css = r'''
/* DP041B_NEWS_SYSTEM_CONFIG */
.newsSettingsGrid{
  display:grid;
  grid-template-columns:repeat(2,minmax(0,1fr));
  gap:12px;
  margin:14px 0 10px;
}
.newsSettingsGrid label{
  display:flex;
  flex-direction:column;
  gap:6px;
  font-size:12px;
  font-weight:800;
  color:#4f6485;
}
.newsSettingsGrid select{
  width:100%;
}
@media(max-width:620px){
  .newsSettingsGrid{grid-template-columns:1fr}
}
'''
style_end = page.find("</style>")
if style_end < 0:
    raise SystemExit("ERROR: no encontré </style> en care_web_page.cc")
page = page[:style_end] + css + "\n" + page[style_end:]

volume_pattern = re.compile(
    r'(<div class="card">\s*'
    r'<div class="panelTitle"><h2>Volumen del parlante</h2>.*?'
    r'<button id="saveAudioVolumeBtn".*?</button></div>\s*'
    r'<div class="itemNotes">.*?</div>\s*'
    r'</div>)',
    re.S
)
vm = volume_pattern.search(page)
if not vm:
    raise SystemExit(
        "ERROR: no pude ubicar de forma segura la tarjeta Volumen del parlante."
    )

news_card = r'''
        <div class="card">
          <div class="panelTitle"><h2>Noticias</h2><span id="newsSettingsStatus" class="counter">Argentina · 3</span></div>
          <p class="muted">Define qué noticias se leen cuando decís “Leeme las noticias” o “Quiero saber qué pasa”. Si no mencionás un tema, XiaoZhi usa esta configuración sin repreguntar.</p>
          <div class="newsSettingsGrid">
            <label>Categoría predeterminada
              <select id="newsDefaultCategory">
                <option value="argentina">Argentina</option>
                <option value="mundo">Mundo</option>
                <option value="tecnologia">Tecnología</option>
                <option value="deportes">Deportes</option>
              </select>
            </label>
            <label>Cantidad de titulares
              <select id="newsHeadlineCount">
                <option value="1">1 titular</option>
                <option value="2">2 titulares</option>
                <option value="3" selected>3 titulares</option>
                <option value="4">4 titulares</option>
                <option value="5">5 titulares</option>
              </select>
            </label>
          </div>
          <div id="newsSettingsHelp" class="itemNotes">Fuente: Google News RSS. La configuración queda guardada en la ESP32.</div>
          <div class="actions"><button id="saveNewsSettingsBtn" class="primary" type="button">Guardar noticias</button></div>
        </div>'''

page = page[:vm.end()] + news_card + page[vm.end():]

js_anchor = "function usageTitle(key)"
if js_anchor not in page:
    raise SystemExit("ERROR: no encontré function usageTitle(key)")

news_js = r'''let selectedNewsCategory='argentina';
let selectedNewsCount=3;

function newsCategoryLabel(value){
  return {
    argentina:'Argentina',
    mundo:'Mundo',
    tecnologia:'Tecnología',
    deportes:'Deportes'
  }[value]||'Argentina';
}

function renderNewsSettings(){
  const cfg=((maintenance||{}).news)||{};
  const cat=['argentina','mundo','tecnologia','deportes'].includes(cfg.default_category)
    ?cfg.default_category:'argentina';
  const rawCount=Number(cfg.headline_count);
  const count=Number.isFinite(rawCount)&&rawCount>=1&&rawCount<=5?rawCount:3;

  selectedNewsCategory=cat;
  selectedNewsCount=count;

  if($('newsDefaultCategory'))$('newsDefaultCategory').value=cat;
  if($('newsHeadlineCount'))$('newsHeadlineCount').value=String(count);
  if($('newsSettingsStatus'))$('newsSettingsStatus').textContent=newsCategoryLabel(cat)+' · '+count;
  if($('newsSettingsHelp'))$('newsSettingsHelp').textContent=
    'Fuente: Google News RSS. Sin categoría explícita se usan '+newsCategoryLabel(cat)+' y '+count+' titular'+(count===1?'':'es')+'.';
}

function updateNewsSettingsPreview(){
  const cat=$('newsDefaultCategory');
  const count=$('newsHeadlineCount');
  if(cat)selectedNewsCategory=cat.value;
  if(count)selectedNewsCount=Math.max(1,Math.min(5,Number(count.value)||3));
  if($('newsSettingsStatus'))$('newsSettingsStatus').textContent=
    newsCategoryLabel(selectedNewsCategory)+' · '+selectedNewsCount;
}

async function saveNewsSettings(){
  updateNewsSettingsPreview();
  try{
    const j=await api('/api/maintenance/news-settings',{
      method:'PUT',
      body:{
        default_category:selectedNewsCategory,
        headline_count:selectedNewsCount
      },
      csrfRequired:true
    });

    if(!maintenance)maintenance={};
    maintenance.news=(j&&j.data)?j.data:{
      default_category:selectedNewsCategory,
      headline_count:selectedNewsCount,
      source:'Google News RSS'
    };

    renderNewsSettings();
    showMsg('Noticias guardadas: '+newsCategoryLabel(selectedNewsCategory)+' · '+selectedNewsCount+' titular'+(selectedNewsCount===1?'':'es'));
  }catch(e){
    showMsg('No se pudo guardar la configuración de noticias: '+readableError(e.message),'error');
  }
}

'''
page = page.replace(js_anchor, news_js + js_anchor, 1)

render_anchor = "renderListeningProfile();renderAudioVolume();"
if render_anchor in page:
    page = page.replace(
        render_anchor,
        render_anchor + "renderNewsSettings();",
        1
    )
else:
    alt = "renderAudioVolume();"
    if alt not in page:
        raise SystemExit("ERROR: no encontré el punto de render de mantenimiento.")
    page = page.replace(alt, alt + "renderNewsSettings();", 1)

event_anchor = "if($('saveAudioVolumeBtn'))$('saveAudioVolumeBtn').onclick=saveAudioVolume;\n"
if event_anchor not in page:
    raise SystemExit("ERROR: no encontré el evento saveAudioVolumeBtn.")

events = event_anchor + r'''if($('newsDefaultCategory'))$('newsDefaultCategory').onchange=updateNewsSettingsPreview;
if($('newsHeadlineCount'))$('newsHeadlineCount').onchange=updateNewsSettingsPreview;
if($('saveNewsSettingsBtn'))$('saveNewsSettingsBtn').onclick=saveNewsSettings;
'''
page = page.replace(event_anchor, events, 1)

old_order_pair = "    'Volumen del parlante',\n    'Agregar audio',"
new_order_pair = "    'Volumen del parlante',\n    'Noticias',\n    'Agregar audio',"
if old_order_pair not in page:
    raise SystemExit("ERROR: no encontré el array orderedTitles de Sistema.")
page = page.replace(old_order_pair, new_order_pair, 1)

old_usage = "  const usage=orderedCards[6];"
new_usage = "  const usage=orderedCards[7];"
if old_usage not in page:
    raise SystemExit("ERROR: no encontré el índice actual de Uso de XiaoZhi Care.")
page = page.replace(old_usage, new_usage, 1)

page = page.replace(
    "/* DP040D_SYSTEM_ORDER_1_TO_7 */",
    "/* DP040D_SYSTEM_ORDER_1_TO_7 */\n/* DP041B_NEWS_SYSTEM_CONFIG: Noticias se inserta como tarjeta 3; total 8. */",
    1
)

checks = {
    "nuevo componente header": "class NewsSettings" in news_h,
    "nuevo componente cc": "NewsSettings::Set" in news_cc,
    "mcp include": '#include "care_news/news_settings.h"' in mcp_src,
    "mcp default": 'category = "default"' in mcp_h,
    "mcp sin repregunta": "NO preguntes que categoria quiere" in mcp_xiao,
    "mcp property default": 'std::string("default")' in mcp_xiao,
    "mcp count configurable": "GetHeadlineCount()" in mcp_src,
    "mcp dependency": "care-news" in mcp_cmake,
    "web dependency": "care-news" in web_cmake,
    "web status": 'cJSON_AddItemToObject(data, "news", news_json);' in web_data,
    "web handler": "HandleMaintenanceNewsSettings" in web_data and "HandleMaintenanceNewsSettings" in web_h,
    "web route": "/api/maintenance/news-settings" in server,
    "web card": "<h2>Noticias</h2>" in page,
    "web save": "saveNewsSettingsBtn" in page and "saveNewsSettings()" in page,
    "system order": "'Noticias'," in page and "orderedCards[7]" in page,
}
failed = [k for k, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(failed)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")

for p in FILES:
    backup = p.with_name(p.name + f".before-dp041b-news-system-{stamp}.bak")
    shutil.copy2(p, backup)
    print("Backup:", backup)

NEWS_DIR.mkdir(parents=True, exist_ok=False)
(NEWS_DIR / "include" / "care_news").mkdir(parents=True, exist_ok=True)

(NEWS_DIR / "include" / "care_news" / "news_settings.h").write_text(news_h, encoding="utf-8")
(NEWS_DIR / "news_settings.cc").write_text(news_cc, encoding="utf-8")
(NEWS_DIR / "CMakeLists.txt").write_text(news_cmake, encoding="utf-8")

MCP_SRC.write_text(mcp_src, encoding="utf-8")
MCP_H.write_text(mcp_h, encoding="utf-8")
MCP_XIAO.write_text(mcp_xiao, encoding="utf-8")
MCP_CMAKE.write_text(mcp_cmake, encoding="utf-8")

WEB_PAGE.write_text(page, encoding="utf-8")
WEB_SERVER.write_text(server, encoding="utf-8")
WEB_DATA.write_text(web_data, encoding="utf-8")
WEB_H.write_text(web_h, encoding="utf-8")
WEB_CMAKE.write_text(web_cmake, encoding="utf-8")

print()
print("====================================================")
print(" DP-041B - NOTICIAS CONFIGURABLES")
print("====================================================")
print()
print("Cambios funcionales:")
print(" - 'Leeme las noticias' ya NO repregunta categoría.")
print(" - Usa la categoría predeterminada guardada en Sistema.")
print(" - Categorías: Argentina / Mundo / Tecnología / Deportes.")
print(" - Cantidad configurable: 1 a 5 titulares.")
print(" - Valores iniciales: Argentina + 3 titulares.")
print(" - Persistencia en NVS: namespace care_news.")
print(" - Cache RSS sigue en 10 minutos y considera categoría + cantidad.")
print()
print("Sistema queda ordenado:")
print("  1. Velocidad de escucha")
print("  2. Volumen del parlante")
print("  3. Noticias")
print("  4. Agregar audio")
print("  5. Audios asignados")
print("  6. Limpieza segura")
print("  7. Backup completo")
print("  8. Uso de XiaoZhi Care")
print()
print("No modifica pastillero, medicación, recordatorios, rutinas, presión, DP-039, LEDs, OLED ni audio.")
print()
print("Ahora ejecutá:")
print("  idf.py build")
