from pathlib import Path
from datetime import datetime
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
COMP = ROOT / "components" / "care-mcp"

SRC = COMP / "care_mcp_service.cc"
CMAKE = COMP / "CMakeLists.txt"

def find_one(name):
    hits = list(COMP.rglob(name))
    if not hits:
        raise SystemExit(f"ERROR: no encontré {name} dentro de {COMP}")
    direct = COMP / "include" / name
    if direct in hits:
        return direct
    if len(hits) == 1:
        return hits[0]
    raise SystemExit(
        "ERROR: encontré más de un archivo posible para "
        f"{name}:\n" + "\n".join(f" - {p}" for p in hits)
    )

SERVICE_H = find_one("care_mcp_service.h")
XIAOZHI_H = find_one("care_mcp_xiaozhi.h")

for p in (SRC, SERVICE_H, XIAOZHI_H, CMAKE):
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

src = SRC.read_text(encoding="utf-8")
service_h = SERVICE_H.read_text(encoding="utf-8")
xiaozhi_h = XIAOZHI_H.read_text(encoding="utf-8")
cmake = CMAKE.read_text(encoding="utf-8")

MARKER = "DP041_LATEST_NEWS"

if MARKER in src or MARKER in xiaozhi_h or MARKER in service_h:
    print("DP-041 Noticias ya parece aplicada. No se hicieron cambios.")
    raise SystemExit(0)

checks = {
    "CareMcpService": "class CareMcpService" in service_h,
    "registro MCP": "server.AddTool(" in xiaozhi_h,
    "fin registro": "registered = true;" in xiaozhi_h,
    "namespace final": "}  // namespace xiaozhi_care" in src,
    "cJSON": "#include <cJSON.h>" in src,
}
failed = [k for k, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: la estructura actual no coincide con la esperada: "
        + ", ".join(failed)
        + ". No se modificó ningún archivo."
    )

include_anchor = "#include <esp_log.h>\n"
if "#include <esp_http_client.h>" not in src:
    if include_anchor not in src:
        raise SystemExit("ERROR: no encontré el ancla de includes en care_mcp_service.cc")
    src = src.replace(include_anchor, include_anchor + "#include <esp_http_client.h>\n", 1)

if "#include <esp_crt_bundle.h>" not in src:
    anchor = "#include <esp_http_client.h>\n"
    src = src.replace(anchor, anchor + "#include <esp_crt_bundle.h>\n", 1)

decl = '''    // DP041_LATEST_NEWS
    // Titulares recientes para lectura por voz. No guarda noticias en NVS.
    std::string GetLatestNews(const std::string& category = "argentina");

'''
family_decl = "    std::string FindFamilyMember("
pos = service_h.find(family_decl)
if pos < 0:
    raise SystemExit(
        "ERROR: no encontré FindFamilyMember en care_mcp_service.h. "
        "No se modificó ningún archivo."
    )
service_h = service_h[:pos] + decl + service_h[pos:]

impl = r'''
// -----------------------------------------------------------------------------
// DP041_LATEST_NEWS
// Noticias recientes para XiaoZhi Care.
// Fuente: Google News RSS (español / Argentina).
// No persiste contenido; sólo mantiene un cache corto en RAM.
// -----------------------------------------------------------------------------
std::string CareMcpService::GetLatestNews(const std::string& category) {
    struct NewsHttpBuffer {
        std::string data;
        size_t complete_items = 0;
        static constexpr size_t kMaxBytes = 32768;
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

    auto normalize_category = [](std::string value) -> std::string {
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });

        if (value.empty() || value == "general" ||
            value == "actualidad" || value == "argentina") {
            return "argentina";
        }
        if (value == "mundo" || value == "world" ||
            value == "internacional") {
            return "mundo";
        }
        if (value == "tecnologia" || value == "technology" ||
            value == "tech") {
            return "tecnologia";
        }
        if (value == "deportes" || value == "deporte" ||
            value == "sports") {
            return "deportes";
        }
        return "argentina";
    };

    const std::string normalized = normalize_category(category);

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
    static uint32_t cache_ms = 0;

    const uint32_t now_ms = esp_log_timestamp();
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (!cache_json.empty() &&
            cache_category == normalized &&
            static_cast<uint32_t>(now_ms - cache_ms) < 600000U) {
            ESP_LOGI("CARE_MCP",
                     "News cache hit category=%s",
                     normalized.c_str());
            return cache_json;
        }
    }

    NewsHttpBuffer response;
    response.data.reserve(12288);

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
        if (out->complete_items >= 3 ||
            out->data.size() >= NewsHttpBuffer::kMaxBytes) {
            return ESP_OK;
        }

        const size_t room = NewsHttpBuffer::kMaxBytes - out->data.size();
        const size_t wanted = static_cast<size_t>(evt->data_len);
        const size_t take = std::min(room, wanted);

        out->data.append(static_cast<const char*>(evt->data), take);

        size_t count = 0;
        size_t scan = 0;
        while ((scan = out->data.find("</item>", scan)) != std::string::npos) {
            ++count;
            scan += 7;
            if (count >= 3) break;
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
        if (!cache_json.empty() && cache_category == normalized) {
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
    headlines.reserve(3);

    size_t cursor = 0;
    while (headlines.size() < 3) {
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
            headlines.push_back(std::move(h));
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
        if (i == 0) safe += "Primera: ";
        else if (i == 1) safe += "Segunda: ";
        else safe += "Tercera: ";

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

end_marker = "}  // namespace xiaozhi_care"
end_pos = src.rfind(end_marker)
if end_pos < 0:
    raise SystemExit("ERROR: no encontré el cierre de namespace en care_mcp_service.cc")
src = src[:end_pos] + impl + "\n" + src[end_pos:]

tool_block = r'''
    // DP041_LATEST_NEWS
    server.AddTool(
        "care.get_latest_news",
        "NOTICIAS Y ACTUALIDAD. Usa esta herramienta cuando el usuario pida "
        "noticias o quiera saber que esta pasando, por ejemplo: 'leeme las "
        "noticias', 'quiero saber que pasa', 'que paso hoy', 'que novedades "
        "hay', 'poneme al dia', 'que esta pasando en Argentina', 'que esta "
        "pasando en el mundo', 'noticias de tecnologia' o 'noticias de "
        "deportes'. NO la uses para agenda personal: frases como 'que tengo "
        "para hacer hoy', 'que me falta hacer' o 'que tengo pendiente' deben "
        "seguir usando las herramientas de rutinas/pendientes de XiaoZhi Care. "
        "Devuelve hasta 3 titulares recientes en español con su fuente. "
        "category puede ser argentina, mundo, tecnologia, deportes o general; "
        "si el usuario no especifica tema usa argentina.",
        PropertyList({
            Property("category", kPropertyTypeString, std::string("argentina")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_latest_news");
            return service.GetLatestNews(
                properties["category"].value<std::string>()
            );
        });

'''

reg_pos = xiaozhi_h.find("    registered = true;")
if reg_pos < 0:
    raise SystemExit("ERROR: no encontré 'registered = true;' en care_mcp_xiaozhi.h")
xiaozhi_h = xiaozhi_h[:reg_pos] + tool_block + xiaozhi_h[reg_pos:]

count_pattern = re.compile(r'Registered (\d+) XiaoZhi Care MCP tools')
matches = list(count_pattern.finditer(xiaozhi_h))
if len(matches) == 1:
    old_n = int(matches[0].group(1))
    new_n = old_n + 1
    xiaozhi_h = count_pattern.sub(
        f"Registered {new_n} XiaoZhi Care MCP tools",
        xiaozhi_h,
        count=1
    )
    print(f"Contador MCP: {old_n} -> {new_n}")
elif len(matches) == 0:
    print("AVISO: no encontré contador literal de herramientas; no es funcional.")
else:
    raise SystemExit("ERROR: encontré más de un contador MCP. No se modificó ningún archivo.")

for dep in ("esp_http_client", "esp-tls"):
    if dep not in cmake:
        anchor = "        cjson\n"
        if anchor not in cmake:
            raise SystemExit(
                f"ERROR: falta dependencia {dep} y no encontré ancla cjson en CMake."
            )
        cmake = cmake.replace(anchor, anchor + f"        {dep}\n", 1)

final_checks = {
    "declaración": "GetLatestNews" in service_h,
    "implementación": "CareMcpService::GetLatestNews" in src,
    "registro tool": '"care.get_latest_news"' in xiaozhi_h,
    "frase leeme": "leeme las" in xiaozhi_h,
    "desambiguación agenda": "agenda personal" in xiaozhi_h,
    "Google News RSS": "news.google.com/rss" in src,
    "cache": "600000U" in src,
    "3 titulares": "headlines.size() < 3" in src,
    "TLS bundle": "esp_crt_bundle_attach" in src,
    "HTTP dep": "esp_http_client" in cmake,
}
bad = [k for k, ok in final_checks.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: validación final falló: " + ", ".join(bad)
        + ". No se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
for p in (SRC, SERVICE_H, XIAOZHI_H, CMAKE):
    backup = p.with_name(p.name + f".before-dp041-news-{stamp}.bak")
    shutil.copy2(p, backup)
    print("Backup:", backup)

SRC.write_text(src, encoding="utf-8")
SERVICE_H.write_text(service_h, encoding="utf-8")
XIAOZHI_H.write_text(xiaozhi_h, encoding="utf-8")
CMAKE.write_text(cmake, encoding="utf-8")

print()
print("====================================================")
print(" DP-041 NOTICIAS: APLICADA")
print("====================================================")
print()
print("Nueva herramienta: care.get_latest_news")
print("Categorías: argentina / mundo / tecnologia / deportes / general")
print("Lee hasta 3 titulares con fuente.")
print("Cache RAM: 10 minutos.")
print("No guarda noticias en NVS.")
print("No modifica agenda, recordatorios, pastillero ni rutinas.")
print()
print("Siguiente:")
print("  idf.py build")
print("  idf.py flash")
