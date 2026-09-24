from pathlib import Path
from datetime import datetime
import shutil
import re

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")

RADIO_H = ROOT / "components" / "care-radio" / "include" / "care_radio" / "radio_service.h"
RADIO_CC = ROOT / "components" / "care-radio" / "radio_service.cc"

WEB = ROOT / "components" / "care-web"
WEB_PAGE = WEB / "care_web_page.cc"
WEB_SERVER = WEB / "care_web_server.cc"
WEB_DATA = WEB / "care_web_data.cc"
WEB_H = WEB / "include" / "care_web_server.h"
WEB_CMAKE = WEB / "CMakeLists.txt"

FILES = [RADIO_H, RADIO_CC, WEB_PAGE, WEB_SERVER, WEB_DATA, WEB_H, WEB_CMAKE]

for p in FILES:
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

texts = {p: p.read_text(encoding="utf-8") for p in FILES}
h = texts[RADIO_H]
cc = texts[RADIO_CC]
page = texts[WEB_PAGE]
server = texts[WEB_SERVER]
data = texts[WEB_DATA]
web_h = texts[WEB_H]
cmake = texts[WEB_CMAKE]

MARKER = "DP043A_RADIO_STATIONS_WEB"
if any(MARKER in t for t in texts.values()):
    print("DP-043A ya parece aplicado. No se hicieron cambios.")
    raise SystemExit(0)

app_text = (ROOT / "main" / "application.cc").read_text(encoding="utf-8")

required = {
    "DP042B buffer": "DP042B_RADIO_JITTER_BUFFER" in cc,
    "DP042C previo": "care_radio/radio_service.h" in app_text,
    "radio NVS actual": "kNvsName" in cc and "kNvsUrl" in cc,
    "radio Play": "bool RadioService::Play()" in cc,
    "radio SetStation": "bool RadioService::SetStation" in cc,
    "DP041B web": "DP041B_NEWS_SYSTEM_CONFIG" in page,
    "news card": "<h2>Noticias</h2>" in page,
    "system order": "DP040D_SYSTEM_ORDER_1_TO_7" in page,
    "news route": "/api/maintenance/news-settings" in server,
}
bad = [name for name, ok in required.items() if not ok]
if bad:
    raise SystemExit(
        "ERROR: el proyecto no coincide con el estado esperado:\n  - "
        + "\n  - ".join(bad)
        + "\nNo se modificó ningún archivo."
    )

def replace_function(text, signature, replacement, label):
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"ERROR: no encontré {label}. No se modificó ningún archivo.")
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit(f"ERROR: no encontré apertura de {label}.")
    depth = 0
    i = brace
    in_str = False
    str_ch = ""
    esc = False
    while i < len(text):
        ch = text[i]
        if in_str:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == str_ch:
                in_str = False
        else:
            if ch in ('"', "'"):
                in_str = True
                str_ch = ch
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[:start] + replacement.rstrip() + "\n" + text[i+1:]
        i += 1
    raise SystemExit(f"ERROR: no pude cerrar {label}. No se modificó ningún archivo.")

def add_require(text, dep):
    if re.search(rf'(^|[\s\n]){re.escape(dep)}([\s\n]|$)', text):
        return text
    anchor = "    REQUIRES\n"
    pos = text.find(anchor)
    if pos < 0:
        raise SystemExit(f"ERROR: no encontré REQUIRES para agregar {dep}")
    pos += len(anchor)
    return text[:pos] + f"        {dep}\n" + text[pos:]

# =============================================================================
# 1) care-radio: lista de hasta 10 emisoras + predeterminada
# =============================================================================

class_anchor = "namespace xiaozhi_care::radio {\n\nclass RadioService {"
if class_anchor not in h:
    raise SystemExit("ERROR: no encontré el inicio de RadioService.")

class_repl = '''namespace xiaozhi_care::radio {

// DP043A_RADIO_STATIONS_WEB
struct RadioStation {
    std::string name;
    std::string url;
};

class RadioService {'''
h = h.replace(class_anchor, class_repl, 1)

public_anchor = '''    bool SetStation(const std::string& name, const std::string& url);
    std::string GetStationName() const;
    std::string GetStationUrl() const;
'''
public_repl = '''    // Compatibilidad: reemplaza la emisora predeterminada actual.
    bool SetStation(const std::string& name, const std::string& url);

    // DP043A_RADIO_STATIONS_WEB
    static constexpr size_t kMaxStations = 10;
    std::vector<RadioStation> ListStations() const;
    int GetDefaultStationIndex() const;
    bool AddOrUpdateStation(int index,
                            const std::string& name,
                            const std::string& url,
                            int* saved_index = nullptr);
    bool DeleteStation(int index);
    bool SetDefaultStation(int index);

    std::string GetStationName() const;
    std::string GetStationUrl() const;
'''
if public_anchor not in h:
    raise SystemExit("ERROR: no encontré API pública de emisora actual.")
h = h.replace(public_anchor, public_repl, 1)

fields_anchor = '''    std::string station_name_;
    std::string station_url_;
    OutputCallback output_cb_;
'''
fields_repl = '''    std::string station_name_;
    std::string station_url_;

    // DP043A_RADIO_STATIONS_WEB
    std::vector<RadioStation> stations_;
    int default_station_index_ = 0;

    OutputCallback output_cb_;
'''
if fields_anchor not in h:
    raise SystemExit("ERROR: no encontré campos station_name_/station_url_.")
h = h.replace(fields_anchor, fields_repl, 1)

const_anchor = '''constexpr char kNvsName[] = "name";
constexpr char kNvsUrl[] = "url";
'''
const_repl = '''constexpr char kNvsName[] = "name";
constexpr char kNvsUrl[] = "url";

// DP043A_RADIO_STATIONS_WEB
constexpr char kNvsCount[] = "count";
constexpr char kNvsDefault[] = "default";
'''
if const_anchor not in cc:
    raise SystemExit("ERROR: no encontré constantes NVS legacy.")
cc = cc.replace(const_anchor, const_repl, 1)

load_impl = r'''bool RadioService::LoadSettings() {
    std::lock_guard<std::mutex> lock(mutex_);

    stations_.clear();
    default_station_index_ = 0;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        stations_.push_back({kDefaultStationName, kDefaultStationUrl});
        station_name_ = stations_[0].name;
        station_url_ = stations_[0].url;
        return false;
    }

    auto read_string = [&](const char* key, std::string& out) -> bool {
        size_t len = 0;
        if (nvs_get_str(handle, key, nullptr, &len) != ESP_OK ||
            len <= 1 || len > 512) {
            return false;
        }
        std::vector<char> buffer(len);
        if (nvs_get_str(handle, key, buffer.data(), &len) != ESP_OK) {
            return false;
        }
        out = buffer.data();
        return true;
    };

    uint8_t count = 0;
    const esp_err_t count_err = nvs_get_u8(handle, kNvsCount, &count);

    if (count_err == ESP_OK &&
        count >= 1 &&
        count <= static_cast<uint8_t>(kMaxStations)) {
        for (uint8_t i = 0; i < count; ++i) {
            char name_key[8];
            char url_key[8];
            snprintf(name_key, sizeof(name_key), "n%u",
                     static_cast<unsigned>(i));
            snprintf(url_key, sizeof(url_key), "u%u",
                     static_cast<unsigned>(i));

            std::string name;
            std::string url;
            if (read_string(name_key, name) &&
                read_string(url_key, url) &&
                !name.empty() &&
                (url.rfind("http://", 0) == 0 ||
                 url.rfind("https://", 0) == 0)) {
                stations_.push_back({name, url});
            }
        }
    }

    bool migrated = false;

    // Migración automática desde DP-042: una sola emisora name/url.
    if (stations_.empty()) {
        std::string legacy_name;
        std::string legacy_url;

        if (read_string(kNvsName, legacy_name) &&
            read_string(kNvsUrl, legacy_url) &&
            !legacy_name.empty() &&
            (legacy_url.rfind("http://", 0) == 0 ||
             legacy_url.rfind("https://", 0) == 0)) {
            stations_.push_back({legacy_name, legacy_url});
        } else {
            stations_.push_back(
                {kDefaultStationName, kDefaultStationUrl});
        }
        migrated = true;
    }

    uint8_t default_index = 0;
    if (nvs_get_u8(handle, kNvsDefault, &default_index) == ESP_OK &&
        default_index < stations_.size()) {
        default_station_index_ = default_index;
    } else {
        default_station_index_ = 0;
        migrated = true;
    }

    station_name_ =
        stations_[static_cast<size_t>(default_station_index_)].name;
    station_url_ =
        stations_[static_cast<size_t>(default_station_index_)].url;

    nvs_close(handle);

    if (migrated) {
        return SaveSettingsLocked();
    }

    ESP_LOGI(
        kTag,
        "Loaded %u radio stations; default=%d (%s)",
        static_cast<unsigned>(stations_.size()),
        default_station_index_,
        station_name_.c_str());

    return true;
}'''
cc = replace_function(cc, "bool RadioService::LoadSettings()", load_impl, "LoadSettings")

save_impl = r'''bool RadioService::SaveSettingsLocked() {
    if (stations_.empty() ||
        stations_.size() > kMaxStations ||
        default_station_index_ < 0 ||
        default_station_index_ >= static_cast<int>(stations_.size())) {
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);

    if (err == ESP_OK) {
        err = nvs_set_u8(
            handle,
            kNvsCount,
            static_cast<uint8_t>(stations_.size()));
    }

    if (err == ESP_OK) {
        err = nvs_set_u8(
            handle,
            kNvsDefault,
            static_cast<uint8_t>(default_station_index_));
    }

    for (size_t i = 0; err == ESP_OK && i < kMaxStations; ++i) {
        char name_key[8];
        char url_key[8];
        snprintf(name_key, sizeof(name_key), "n%u",
                 static_cast<unsigned>(i));
        snprintf(url_key, sizeof(url_key), "u%u",
                 static_cast<unsigned>(i));

        if (i < stations_.size()) {
            err = nvs_set_str(
                handle, name_key, stations_[i].name.c_str());
            if (err == ESP_OK) {
                err = nvs_set_str(
                    handle, url_key, stations_[i].url.c_str());
            }
        } else {
            nvs_erase_key(handle, name_key);
            nvs_erase_key(handle, url_key);
        }
    }

    if (err == ESP_OK) {
        const auto& def =
            stations_[static_cast<size_t>(default_station_index_)];
        err = nvs_set_str(handle, kNvsName, def.name.c_str());
        if (err == ESP_OK) {
            err = nvs_set_str(handle, kNvsUrl, def.url.c_str());
        }
    }

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    if (handle != 0) {
        nvs_close(handle);
    }

    if (err != ESP_OK) {
        ESP_LOGE(
            kTag,
            "Unable to save radio stations: %s",
            esp_err_to_name(err));
        return false;
    }

    return true;
}'''
cc = replace_function(cc, "bool RadioService::SaveSettingsLocked()", save_impl, "SaveSettingsLocked")

set_station_impl = r'''bool RadioService::SetStation(const std::string& name,
                              const std::string& url) {
    return AddOrUpdateStation(
        GetDefaultStationIndex(),
        name,
        url,
        nullptr);
}'''
cc = replace_function(cc, "bool RadioService::SetStation", set_station_impl, "SetStation")

play_impl = r'''bool RadioService::Play() {
    if (!initialized_.load()) {
        ESP_LOGW(kTag, "Play requested before initialization");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stations_.empty() ||
            default_station_index_ < 0 ||
            default_station_index_ >=
                static_cast<int>(stations_.size())) {
            ESP_LOGW(kTag, "No default radio station configured");
            return false;
        }

        const auto& def =
            stations_[static_cast<size_t>(default_station_index_)];
        station_name_ = def.name;
        station_url_ = def.url;
    }

    user_paused_.store(false);
    wants_playing_.store(true);

    ESP_LOGI(
        kTag,
        "Play requested: %s",
        GetStationName().c_str());

    return true;
}'''
cc = replace_function(cc, "bool RadioService::Play()", play_impl, "Play")

insert_anchor = "std::string RadioService::GetStationName() const {"
if insert_anchor not in cc:
    raise SystemExit("ERROR: no encontré GetStationName.")

multi_impl = r'''// DP043A_RADIO_STATIONS_WEB
std::vector<RadioStation> RadioService::ListStations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stations_;
}

int RadioService::GetDefaultStationIndex() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return default_station_index_;
}

bool RadioService::AddOrUpdateStation(int index,
                                      const std::string& name,
                                      const std::string& url,
                                      int* saved_index) {
    if (name.empty() || name.size() > 95 ||
        url.size() < 8 || url.size() > 511 ||
        !(url.rfind("http://", 0) == 0 ||
          url.rfind("https://", 0) == 0)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    int target = index;

    if (target < 0) {
        if (stations_.size() >= kMaxStations) {
            return false;
        }
        stations_.push_back({name, url});
        target = static_cast<int>(stations_.size()) - 1;
    } else {
        if (target >= static_cast<int>(stations_.size())) {
            return false;
        }
        stations_[static_cast<size_t>(target)] = {name, url};
    }

    if (!SaveSettingsLocked()) {
        return false;
    }

    if (saved_index != nullptr) {
        *saved_index = target;
    }

    ESP_LOGI(
        kTag,
        "Station %s index=%d name=%s",
        index < 0 ? "added" : "updated",
        target,
        name.c_str());

    return true;
}

bool RadioService::DeleteStation(int index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (stations_.size() <= 1 ||
        index < 0 ||
        index >= static_cast<int>(stations_.size())) {
        return false;
    }

    stations_.erase(stations_.begin() + index);

    if (default_station_index_ == index) {
        default_station_index_ = 0;
    } else if (default_station_index_ > index) {
        --default_station_index_;
    }

    const auto& def =
        stations_[static_cast<size_t>(default_station_index_)];

    if (!wants_playing_.load()) {
        station_name_ = def.name;
        station_url_ = def.url;
    }

    if (!SaveSettingsLocked()) {
        return false;
    }

    ESP_LOGI(
        kTag,
        "Station deleted index=%d remaining=%u",
        index,
        static_cast<unsigned>(stations_.size()));

    return true;
}

bool RadioService::SetDefaultStation(int index) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (index < 0 ||
        index >= static_cast<int>(stations_.size())) {
        return false;
    }

    default_station_index_ = index;

    if (!wants_playing_.load()) {
        station_name_ =
            stations_[static_cast<size_t>(index)].name;
        station_url_ =
            stations_[static_cast<size_t>(index)].url;
    }

    if (!SaveSettingsLocked()) {
        return false;
    }

    ESP_LOGI(
        kTag,
        "Default station changed: index=%d name=%s",
        index,
        stations_[static_cast<size_t>(index)].name.c_str());

    return true;
}

'''
cc = cc.replace(insert_anchor, multi_impl + insert_anchor, 1)

# =============================================================================
# 2) care-web backend
# =============================================================================

if '#include "care_radio/radio_service.h"' not in data:
    ns_pos = data.find("\nnamespace xiaozhi_care")
    if ns_pos < 0:
        raise SystemExit("ERROR: no encontré namespace en care_web_data.cc")
    data = (
        data[:ns_pos] +
        '\n#include "care_radio/radio_service.h"\n' +
        data[ns_pos:]
    )

cmake = add_require(cmake, "care-radio")

route_anchor = '        {"/api/maintenance/news-settings", HTTP_PUT, HandleMaintenanceNewsSettings},\n'
if route_anchor not in server:
    raise SystemExit("ERROR: no encontré ruta de news-settings.")

radio_routes = (
    '        {"/api/maintenance/radio-stations", HTTP_GET, HandleMaintenanceRadioStationsGet},\n'
    '        {"/api/maintenance/radio-stations", HTTP_PUT, HandleMaintenanceRadioStationsPut},\n'
)
server = server.replace(route_anchor, route_anchor + radio_routes, 1)

header_anchor = "    static esp_err_t HandleMaintenanceNewsSettings(httpd_req_t* req);\n"
if header_anchor not in web_h:
    raise SystemExit("ERROR: no encontré declaración HandleMaintenanceNewsSettings.")

web_h = web_h.replace(
    header_anchor,
    header_anchor +
    "    static esp_err_t HandleMaintenanceRadioStationsGet(httpd_req_t* req);\n"
    "    static esp_err_t HandleMaintenanceRadioStationsPut(httpd_req_t* req);\n",
    1
)

handler_anchor = "esp_err_t CareWebServer::HandleMaintenanceClearExecutions(httpd_req_t* req) {"
if handler_anchor not in data:
    raise SystemExit("ERROR: no encontré HandleMaintenanceClearExecutions.")

handlers = r'''
// DP043A_RADIO_STATIONS_WEB
static cJSON* BuildRadioStationsData() {
    auto& radio =
        xiaozhi_care::radio::RadioService::GetInstance();

    const auto stations = radio.ListStations();
    const int default_index = radio.GetDefaultStationIndex();

    cJSON* out = cJSON_CreateObject();
    if (out == nullptr) {
        return nullptr;
    }

    cJSON_AddNumberToObject(
        out,
        "max_stations",
        static_cast<double>(
            xiaozhi_care::radio::RadioService::kMaxStations));
    cJSON_AddNumberToObject(
        out,
        "default_index",
        default_index);
    cJSON_AddStringToObject(
        out,
        "active_station",
        radio.GetStationName().c_str());

    cJSON* items = cJSON_AddArrayToObject(out, "items");
    if (items == nullptr) {
        cJSON_Delete(out);
        return nullptr;
    }

    for (size_t i = 0; i < stations.size(); ++i) {
        cJSON* item = cJSON_CreateObject();
        if (item == nullptr) {
            continue;
        }

        cJSON_AddNumberToObject(
            item, "index", static_cast<double>(i));
        cJSON_AddStringToObject(
            item, "name", stations[i].name.c_str());
        cJSON_AddStringToObject(
            item, "url", stations[i].url.c_str());
        cJSON_AddBoolToObject(
            item,
            "is_default",
            static_cast<int>(i) == default_index);

        cJSON_AddItemToArray(items, item);
    }

    return out;
}

esp_err_t CareWebServer::HandleMaintenanceRadioStationsGet(
    httpd_req_t* req) {
    if (!GetInstance().Authorize(req, false)) {
        return ESP_OK;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* out = BuildRadioStationsData();

    if (root == nullptr || out == nullptr) {
        if (root) cJSON_Delete(root);
        if (out) cJSON_Delete(out);
        return SendJsonText(
            req,
            "500 Internal Server Error",
            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }

    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", out);

    return SendRoot(req, root);
}

esp_err_t CareWebServer::HandleMaintenanceRadioStationsPut(
    httpd_req_t* req) {
    if (!GetInstance().Authorize(req, true)) {
        return ESP_OK;
    }

    if (req->content_len <= 0 || req->content_len > 1024) {
        return SendNamedError(
            req,
            "400 Bad Request",
            "INVALID_RADIO_STATION");
    }

    std::string body(
        static_cast<size_t>(req->content_len),
        '\0');

    size_t received = 0;
    while (received < body.size()) {
        const int n = httpd_req_recv(
            req,
            body.data() + received,
            body.size() - received);

        if (n <= 0) {
            return SendNamedError(
                req,
                "400 Bad Request",
                "INVALID_BODY");
        }

        received += static_cast<size_t>(n);
    }

    cJSON* json =
        cJSON_ParseWithLength(body.data(), body.size());

    if (json == nullptr) {
        return SendNamedError(
            req,
            "400 Bad Request",
            "INVALID_JSON");
    }

    cJSON* op_json =
        cJSON_GetObjectItemCaseSensitive(json, "op");

    if (!cJSON_IsString(op_json) ||
        op_json->valuestring == nullptr) {
        cJSON_Delete(json);
        return SendNamedError(
            req,
            "400 Bad Request",
            "INVALID_RADIO_OPERATION");
    }

    const std::string op = op_json->valuestring;
    auto& radio =
        xiaozhi_care::radio::RadioService::GetInstance();

    bool ok = false;

    if (op == "save") {
        cJSON* index_json =
            cJSON_GetObjectItemCaseSensitive(json, "index");
        cJSON* name_json =
            cJSON_GetObjectItemCaseSensitive(json, "name");
        cJSON* url_json =
            cJSON_GetObjectItemCaseSensitive(json, "url");

        int index = -1;
        if (cJSON_IsNumber(index_json)) {
            index = index_json->valueint;
        }

        if (cJSON_IsString(name_json) &&
            name_json->valuestring != nullptr &&
            cJSON_IsString(url_json) &&
            url_json->valuestring != nullptr) {
            ok = radio.AddOrUpdateStation(
                index,
                name_json->valuestring,
                url_json->valuestring,
                nullptr);
        }
    } else if (op == "delete" || op == "default") {
        cJSON* index_json =
            cJSON_GetObjectItemCaseSensitive(json, "index");

        if (cJSON_IsNumber(index_json)) {
            const int index = index_json->valueint;

            if (op == "delete") {
                ok = radio.DeleteStation(index);
            } else {
                ok = radio.SetDefaultStation(index);
            }
        }
    }

    cJSON_Delete(json);

    if (!ok) {
        return SendNamedError(
            req,
            "400 Bad Request",
            "RADIO_OPERATION_FAILED");
    }

    cJSON* root = cJSON_CreateObject();
    cJSON* out = BuildRadioStationsData();

    if (root == nullptr || out == nullptr) {
        if (root) cJSON_Delete(root);
        if (out) cJSON_Delete(out);
        return SendJsonText(
            req,
            "500 Internal Server Error",
            R"({"success":false,"error":"INTERNAL_ERROR"})");
    }

    cJSON_AddBoolToObject(root, "success", true);
    cJSON_AddItemToObject(root, "data", out);

    return SendRoot(req, root);
}

'''
data = data.replace(handler_anchor, handlers + handler_anchor, 1)

# =============================================================================
# 3) UI
# =============================================================================

css = r'''
/* DP043A_RADIO_STATIONS_WEB */
.radioStationGrid{
  display:grid;
  grid-template-columns:minmax(0,.75fr) minmax(0,1.6fr);
  gap:12px;
  margin:14px 0 10px;
}
.radioStationGrid label{
  display:flex;
  flex-direction:column;
  gap:6px;
  font-size:12px;
  font-weight:800;
  color:#4f6485;
}
.radioStationList{
  display:flex;
  flex-direction:column;
  gap:9px;
  margin-top:14px;
}
.radioStationRow{
  display:grid;
  grid-template-columns:minmax(0,1fr) auto;
  gap:12px;
  align-items:center;
  padding:11px 12px;
  border:1px solid #dce8f7;
  border-radius:14px;
  background:#fff;
}
.radioStationName{
  font-weight:900;
  color:#17345f;
}
.radioStationUrl{
  margin-top:3px;
  font-size:11px;
  color:#7183a0;
  overflow-wrap:anywhere;
}
.radioStationBadge{
  display:inline-block;
  margin-left:7px;
  padding:2px 7px;
  border-radius:999px;
  background:#eafaf3;
  color:#0c7650;
  font-size:10px;
  font-weight:900;
}
.radioStationActions{
  display:flex;
  gap:6px;
  flex-wrap:wrap;
  justify-content:flex-end;
}
.radioStationActions button{
  padding:7px 9px;
  font-size:11px;
}
@media(max-width:720px){
  .radioStationGrid{grid-template-columns:1fr}
  .radioStationRow{grid-template-columns:1fr}
  .radioStationActions{justify-content:flex-start}
}
'''

style_end = page.find("</style>")
if style_end < 0:
    raise SystemExit("ERROR: no encontré </style>.")
page = page[:style_end] + css + "\n" + page[style_end:]

add_audio_pattern = re.compile(
    r'(<div class="card">\s*<div class="panelTitle"><h2>Agregar audio</h2>)',
    re.S
)
m = add_audio_pattern.search(page)
if not m:
    raise SystemExit("ERROR: no encontré la tarjeta Agregar audio.")

radio_card = r'''
        <div class="card">
          <div class="panelTitle">
            <h2>Radios de Internet</h2>
            <span id="radioStationCounter" class="counter">1 / 10</span>
          </div>
          <p class="muted">Guardá hasta 10 emisoras. La predeterminada es la que se reproduce cuando decís “Poné la radio”. Usá una URL directa de streaming MP3.</p>

          <input id="radioStationIndex" type="hidden" value="-1">

          <div class="radioStationGrid">
            <label>Nombre de la emisora
              <input id="radioStationName" maxlength="95" placeholder="Ej.: Radio Mitre">
            </label>
            <label>URL directa del stream
              <input id="radioStationUrl" maxlength="511" placeholder="https://servidor/radio.mp3">
            </label>
          </div>

          <div class="actions">
            <button id="saveRadioStationBtn" class="primary" type="button">Agregar emisora</button>
            <button id="cancelRadioStationBtn" class="secondary" type="button" style="display:none">Cancelar edición</button>
          </div>

          <div id="radioStationHelp" class="itemNotes">La emisora actual se conservará automáticamente como primera emisora al migrar.</div>
          <div id="radioStationList" class="radioStationList"></div>
        </div>

'''
page = page[:m.start()] + radio_card + page[m.start():]

js_anchor = "function usageTitle(key)"
if js_anchor not in page:
    raise SystemExit("ERROR: no encontré function usageTitle(key).")

radio_js = r'''let radioStationsData={items:[],default_index:0,max_stations:10};

function radioEsc(value){
  return String(value??'')
    .replaceAll('&','&amp;')
    .replaceAll('<','&lt;')
    .replaceAll('>','&gt;')
    .replaceAll('"','&quot;')
    .replaceAll("'","&#39;");
}

function resetRadioStationForm(){
  if($('radioStationIndex'))$('radioStationIndex').value='-1';
  if($('radioStationName'))$('radioStationName').value='';
  if($('radioStationUrl'))$('radioStationUrl').value='';
  if($('saveRadioStationBtn'))$('saveRadioStationBtn').textContent='Agregar emisora';
  if($('cancelRadioStationBtn'))$('cancelRadioStationBtn').style.display='none';
}

function renderRadioStations(){
  const items=Array.isArray(radioStationsData.items)?radioStationsData.items:[];
  const max=Number(radioStationsData.max_stations)||10;

  if($('radioStationCounter'))$('radioStationCounter').textContent=items.length+' / '+max;

  const list=$('radioStationList');
  if(!list)return;

  if(!items.length){
    list.innerHTML='<div class="empty">No hay emisoras guardadas.</div>';
    return;
  }

  list.innerHTML=items.map((s,i)=>{
    const def=!!s.is_default;
    return '<div class="radioStationRow">'+
      '<div>'+
        '<div class="radioStationName">'+radioEsc(s.name)+(def?'<span class="radioStationBadge">Predeterminada</span>':'')+'</div>'+
        '<div class="radioStationUrl">'+radioEsc(s.url)+'</div>'+
      '</div>'+
      '<div class="radioStationActions">'+
        (!def?'<button class="secondary" type="button" data-radio-default="'+i+'">Predeterminada</button>':'')+
        '<button class="secondary" type="button" data-radio-edit="'+i+'">Editar</button>'+
        (items.length>1?'<button class="danger" type="button" data-radio-delete="'+i+'">Eliminar</button>':'')+
      '</div>'+
    '</div>';
  }).join('');

  list.querySelectorAll('[data-radio-edit]').forEach(btn=>{
    btn.onclick=()=>editRadioStation(Number(btn.dataset.radioEdit));
  });
  list.querySelectorAll('[data-radio-default]').forEach(btn=>{
    btn.onclick=()=>setDefaultRadioStation(Number(btn.dataset.radioDefault));
  });
  list.querySelectorAll('[data-radio-delete]').forEach(btn=>{
    btn.onclick=()=>deleteRadioStation(Number(btn.dataset.radioDelete));
  });
}

async function loadRadioStations(){
  try{
    const j=await api('/api/maintenance/radio-stations');
    radioStationsData=(j&&j.data)?j.data:{items:[],default_index:0,max_stations:10};
    renderRadioStations();
  }catch(e){
    if($('radioStationHelp'))$('radioStationHelp').textContent='No se pudieron cargar las emisoras: '+readableError(e.message);
  }
}

function editRadioStation(index){
  const items=radioStationsData.items||[];
  const s=items[index];
  if(!s)return;

  $('radioStationIndex').value=String(index);
  $('radioStationName').value=s.name||'';
  $('radioStationUrl').value=s.url||'';
  $('saveRadioStationBtn').textContent='Guardar cambios';
  $('cancelRadioStationBtn').style.display='';
  $('radioStationName').focus();
}

async function saveRadioStation(){
  const index=Number($('radioStationIndex').value||-1);
  const name=$('radioStationName').value.trim();
  const url=$('radioStationUrl').value.trim();

  if(!name)return showMsg('Escribí el nombre de la emisora.','error');
  if(!/^https?:\/\//i.test(url))return showMsg('La URL debe comenzar con http:// o https://','error');

  try{
    const j=await api('/api/maintenance/radio-stations',{
      method:'PUT',
      body:{op:'save',index,name,url},
      csrfRequired:true
    });

    radioStationsData=j.data||radioStationsData;
    resetRadioStationForm();
    renderRadioStations();
    showMsg(index<0?'Emisora agregada':'Emisora actualizada');
  }catch(e){
    showMsg('No se pudo guardar la emisora: '+readableError(e.message),'error');
  }
}

async function setDefaultRadioStation(index){
  try{
    const j=await api('/api/maintenance/radio-stations',{
      method:'PUT',
      body:{op:'default',index},
      csrfRequired:true
    });
    radioStationsData=j.data||radioStationsData;
    renderRadioStations();
    showMsg('Emisora predeterminada actualizada');
  }catch(e){
    showMsg('No se pudo cambiar la emisora predeterminada: '+readableError(e.message),'error');
  }
}

async function deleteRadioStation(index){
  const items=radioStationsData.items||[];
  const s=items[index];
  if(!s)return;

  if(!confirm('¿Eliminar la emisora “'+s.name+'”?'))return;

  try{
    const j=await api('/api/maintenance/radio-stations',{
      method:'PUT',
      body:{op:'delete',index},
      csrfRequired:true
    });
    radioStationsData=j.data||radioStationsData;
    resetRadioStationForm();
    renderRadioStations();
    showMsg('Emisora eliminada');
  }catch(e){
    showMsg('No se pudo eliminar la emisora: '+readableError(e.message),'error');
  }
}

'''
page = page.replace(js_anchor, radio_js + js_anchor, 1)

script_end = page.rfind("</script>")
if script_end < 0:
    raise SystemExit("ERROR: no encontré </script>.")

init_js = r'''
// DP043A_RADIO_STATIONS_WEB
document.addEventListener('DOMContentLoaded',()=>{
  if($('saveRadioStationBtn'))$('saveRadioStationBtn').onclick=saveRadioStation;
  if($('cancelRadioStationBtn'))$('cancelRadioStationBtn').onclick=resetRadioStationForm;
  loadRadioStations();
});
'''
page = page[:script_end] + init_js + "\n" + page[script_end:]

old_order = "    'Noticias',\n    'Agregar audio',"
new_order = "    'Noticias',\n    'Radios de Internet',\n    'Agregar audio',"
if old_order not in page:
    raise SystemExit("ERROR: no encontré el orden Noticias -> Agregar audio.")
page = page.replace(old_order, new_order, 1)

if "  const usage=orderedCards[7];" in page:
    page = page.replace(
        "  const usage=orderedCards[7];",
        "  const usage=orderedCards[8];",
        1
    )
else:
    raise SystemExit("ERROR: no encontré índice usage=orderedCards[7].")

checks = {
    "RadioStation struct": "struct RadioStation" in h,
    "max 10": "kMaxStations = 10" in h,
    "list methods": "ListStations() const" in h,
    "NVS count": 'kNvsCount[] = "count"' in cc,
    "migration legacy": "Migración automática desde DP-042" in cc,
    "web include": '#include "care_radio/radio_service.h"' in data,
    "web GET route": '"/api/maintenance/radio-stations", HTTP_GET' in server,
    "web PUT route": '"/api/maintenance/radio-stations", HTTP_PUT' in server,
    "web handlers": "HandleMaintenanceRadioStationsGet" in data and "HandleMaintenanceRadioStationsPut" in data,
    "web cmake": "care-radio" in cmake,
    "UI card": "<h2>Radios de Internet</h2>" in page,
    "UI list": "radioStationList" in page,
    "UI order": "'Radios de Internet'," in page and "orderedCards[8]" in page,
}
failed = [name for name, ok in checks.items() if not ok]
if failed:
    raise SystemExit(
        "ERROR: validación final falló:\n  - "
        + "\n  - ".join(failed)
        + "\nNo se modificó ningún archivo."
    )

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
for p in FILES:
    backup = p.with_name(
        p.name + f".before-dp043a-radio-stations-{stamp}.bak"
    )
    shutil.copy2(p, backup)
    print("Backup:", backup)

RADIO_H.write_text(h, encoding="utf-8")
RADIO_CC.write_text(cc, encoding="utf-8")
WEB_PAGE.write_text(page, encoding="utf-8")
WEB_SERVER.write_text(server, encoding="utf-8")
WEB_DATA.write_text(data, encoding="utf-8")
WEB_H.write_text(web_h, encoding="utf-8")
WEB_CMAKE.write_text(cmake, encoding="utf-8")

print()
print("====================================================")
print(" DP-043A - RADIOS DE INTERNET EN SISTEMA")
print("====================================================")
print()
print("Incluye:")
print(" - hasta 10 emisoras guardadas en NVS")
print(" - migración automática de la emisora actual")
print(" - agregar emisora")
print(" - editar nombre / URL")
print(" - eliminar emisora")
print(" - elegir emisora predeterminada")
print(" - 'Poné la radio' usa la predeterminada")
print()
print("Sistema queda:")
print("  1. Velocidad de escucha")
print("  2. Volumen del parlante")
print("  3. Noticias")
print("  4. Radios de Internet")
print("  5. Agregar audio")
print("  6. Audios asignados")
print("  7. Limpieza segura")
print("  8. Backup completo")
print("  9. Uso de XiaoZhi Care")
print()
print("Todavía NO agrega selección por nombre por voz.")
print("Eso será DP-043B después de validar esta pantalla.")
print()
print("Ahora ejecutá:")
print("  idf.py build")
