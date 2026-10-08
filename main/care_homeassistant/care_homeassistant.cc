#include "care_homeassistant.h"
#include "care_homeassistant_secrets.h"

#include "board.h"
#include "cJSON.h"

#include "esp_log.h"
#include "nvs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

static const char* TAG = "care_homeassistant";

constexpr char kHaNvsNamespace[] = "care_ha";
constexpr char kHaAllowlistKey[] = "allowlist";
constexpr size_t kMaxAllowedEntities = 64;
constexpr size_t kMaxEntityIdLength = 96;

bool IsValidEntityId(const std::string& entity_id) {
    if (entity_id.empty() || entity_id.size() > kMaxEntityIdLength) {
        return false;
    }

    const size_t dot = entity_id.find('.');
    return dot != std::string::npos &&
           dot > 0 &&
           dot + 1 < entity_id.size();
}


std::string NormalizeDeviceName(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(input[i]);

        if (c == 0xC3 && i + 1 < input.size()) {
            const unsigned char n = static_cast<unsigned char>(input[i + 1]);
            char replacement = '\0';

            switch (n) {
                case 0x81: case 0xA1: replacement = 'a'; break;
                case 0x89: case 0xA9: replacement = 'e'; break;
                case 0x8D: case 0xAD: replacement = 'i'; break;
                case 0x93: case 0xB3: replacement = 'o'; break;
                case 0x9A: case 0xBA:
                case 0x9C: case 0xBC: replacement = 'u'; break;
                case 0x91: case 0xB1: replacement = 'n'; break;
                default: break;
            }

            if (replacement != '\0') {
                out.push_back(replacement);
                ++i;
                continue;
            }
        }

        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == ' ' || c == '_' || c == '-' || c == '.') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
        }
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

bool SupportsPowerAction(const std::string& domain) {
    return
        domain == "light" ||
        domain == "switch" ||
        domain == "fan" ||
        domain == "media_player" ||
        domain == "remote" ||
        domain == "climate";
}

bool IsUsefulEntityDomain(const std::string& domain) {
    return
        domain == "light" ||
        domain == "switch" ||
        domain == "climate" ||
        domain == "media_player" ||
        domain == "remote" ||
        domain == "cover" ||
        domain == "fan" ||
        domain == "sensor" ||
        domain == "weather";
}

bool ParseNumericState(
    const std::string& json_response,
    float& value
) {
    cJSON* root = cJSON_Parse(json_response.c_str());

    if (root == nullptr) {
        ESP_LOGE(TAG, "Respuesta de Home Assistant no es JSON valido");
        return false;
    }

    cJSON* state = cJSON_GetObjectItem(root, "state");

    if (!cJSON_IsString(state)) {
        ESP_LOGE(TAG, "Estado de entidad invalido");
        cJSON_Delete(root);
        return false;
    }

    char* end = nullptr;
    const float parsed = std::strtof(state->valuestring, &end);

    if (end == state->valuestring || *end != '\0') {
        ESP_LOGE(
            TAG,
            "Estado de entidad no numerico: %s",
            state->valuestring
        );
        cJSON_Delete(root);
        return false;
    }

    value = parsed;
    cJSON_Delete(root);
    return true;
}

}  // namespace

CareHomeAssistant& CareHomeAssistant::GetInstance() {
    static CareHomeAssistant instance;
    return instance;
}

bool CareHomeAssistant::Init() {
    ESP_LOGI(TAG, "Inicializando care-homeassistant v0.1");

    ready_ = true;
    network_ready_ = false;

    {
        std::vector<std::string> loaded;

        nvs_handle_t handle = 0;
        esp_err_t err = nvs_open(
            kHaNvsNamespace,
            NVS_READONLY,
            &handle
        );

        if (err == ESP_OK) {
            size_t required = 0;
            err = nvs_get_str(
                handle,
                kHaAllowlistKey,
                nullptr,
                &required
            );

            if (err == ESP_OK && required > 1) {
                std::string raw(required, '\0');

                err = nvs_get_str(
                    handle,
                    kHaAllowlistKey,
                    raw.data(),
                    &required
                );

                if (err == ESP_OK) {
                    if (!raw.empty() && raw.back() == '\0') {
                        raw.pop_back();
                    }

                    size_t start = 0;

                    while (start <= raw.size()) {
                        const size_t end = raw.find('\n', start);
                        const std::string entity_id =
                            raw.substr(
                                start,
                                end == std::string::npos
                                    ? std::string::npos
                                    : end - start
                            );

                        if (IsValidEntityId(entity_id) &&
                            std::find(
                                loaded.begin(),
                                loaded.end(),
                                entity_id
                            ) == loaded.end() &&
                            loaded.size() < kMaxAllowedEntities) {
                            loaded.push_back(entity_id);
                        }

                        if (end == std::string::npos) {
                            break;
                        }

                        start = end + 1;
                    }
                }
            }

            nvs_close(handle);
        } else if (err != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(
                TAG,
                "No se pudo leer allowlist HA desde NVS: %s",
                esp_err_to_name(err)
            );
        }

        {
            std::lock_guard<std::mutex> lock(config_mutex_);
            allowed_entities_ = std::move(loaded);
        }

        ESP_LOGI(
            TAG,
            "Allowlist HA cargada: %u entidades",
            static_cast<unsigned>(allowed_entities_.size())
        );
    }

    ESP_LOGI(TAG, "care-homeassistant listo");
    return true;
}

bool CareHomeAssistant::IsReady() const {
    return ready_;
}

bool CareHomeAssistant::IsNetworkReady() const {
    return network_ready_;
}

void CareHomeAssistant::OnNetworkConnected() {
    network_ready_ = true;

    ESP_LOGI(
        TAG,
        "Red disponible - care-homeassistant puede comunicarse con Home Assistant"
    );
}

bool CareHomeAssistant::GetEntityState(
    const std::string& entity_id,
    std::string& response
) {
    std::lock_guard<std::mutex> lock(http_mutex_);

    if (!ready_) {
        ESP_LOGW(TAG, "Modulo no inicializado");
        return false;
    }

    if (!network_ready_) {
        ESP_LOGW(TAG, "Red no disponible");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();

    if (network == nullptr) {
        ESP_LOGE(TAG, "Interfaz de red no disponible");
        return false;
    }

    if (!http_) {
        http_ = network->CreateHttp(3);

        if (!http_) {
            ESP_LOGE(TAG, "No se pudo crear cliente HTTP");
            return false;
        }

        ESP_LOGI(TAG, "Cliente HTTP persistente creado");
    }

    std::string url =
        std::string(CARE_HA_URL) +
        "/api/states/" +
        entity_id;

    std::string authorization =
        "Bearer " +
        std::string(CARE_HA_TOKEN);

    http_->SetTimeout(5000);
    http_->SetHeader("Authorization", authorization);
    http_->SetHeader("Content-Type", "application/json");
    http_->SetHeader("Accept", "application/json");
    http_->SetHeader("Connection", "keep-alive");

    ESP_LOGI(TAG, "Consultando HA: %s", entity_id.c_str());

    auto opened = http_->Open("GET", url);

    if (!opened) {
        ESP_LOGE(
            TAG,
            "Error conectando con Home Assistant: %s",
            opened.error().ToString().c_str()
        );
        return false;
    }

    auto status = http_->GetStatusCode();

    if (!status) {
        ESP_LOGE(
            TAG,
            "No se pudo obtener HTTP status: %s",
            status.error().ToString().c_str()
        );
        return false;
    }

    ESP_LOGI(TAG, "Home Assistant HTTP %d", *status);

    response = http_->ReadAll();

    if (*status != 200) {
        ESP_LOGE(TAG, "Respuesta inesperada HA HTTP %d", *status);
        return false;
    }

    return true;
}

bool CareHomeAssistant::GetWeather(CareHaWeather& weather) {
    weather = CareHaWeather{};

    std::string response;

    if (!GetEntityState("weather.forecast_casa", response)) {
        ESP_LOGW(TAG, "No se pudo obtener el clima desde Home Assistant");
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());

    if (root == nullptr) {
        ESP_LOGE(TAG, "Respuesta de Home Assistant no es JSON valido");
        return false;
    }

    cJSON* state = cJSON_GetObjectItem(root, "state");
    cJSON* attributes = cJSON_GetObjectItem(root, "attributes");

    if (!cJSON_IsString(state) || !cJSON_IsObject(attributes)) {
        ESP_LOGE(TAG, "JSON de clima incompleto");
        cJSON_Delete(root);
        return false;
    }

    cJSON* temperature = cJSON_GetObjectItem(attributes, "temperature");
    cJSON* humidity = cJSON_GetObjectItem(attributes, "humidity");

    if (!cJSON_IsNumber(temperature)) {
        ESP_LOGE(TAG, "Home Assistant no envio temperatura valida");
        cJSON_Delete(root);
        return false;
    }

    weather.temperature = static_cast<float>(temperature->valuedouble);

    if (cJSON_IsNumber(humidity)) {
        weather.humidity = humidity->valueint;
    }

    weather.condition = state->valuestring;
    weather.valid = true;

    cJSON_Delete(root);

    ESP_LOGI(
        TAG,
        "Clima HA: temperatura=%.1f C humedad=%d%% estado=%s",
        weather.temperature,
        weather.humidity,
        weather.condition.c_str()
    );

    return true;
}

bool CareHomeAssistant::GetIndoorClimate(
    CareHaIndoorClimate& climate
) {
    climate = CareHaIndoorClimate{};

    std::string temperature_response;
    std::string humidity_response;

    if (!GetEntityState(
            "sensor.mini_4_temperatura",
            temperature_response)) {
        ESP_LOGW(TAG, "No se pudo obtener la temperatura interior");
        return false;
    }

    if (!GetEntityState(
            "sensor.mini_4_humedad",
            humidity_response)) {
        ESP_LOGW(TAG, "No se pudo obtener la humedad interior");
        return false;
    }

    float temperature = 0.0f;
    float humidity = 0.0f;

    if (!ParseNumericState(temperature_response, temperature)) {
        ESP_LOGE(TAG, "Temperatura interior invalida");
        return false;
    }

    if (!ParseNumericState(humidity_response, humidity)) {
        ESP_LOGE(TAG, "Humedad interior invalida");
        return false;
    }

    climate.temperature = temperature;
    climate.humidity = humidity;
    climate.valid = true;

    ESP_LOGI(
        TAG,
        "Ambiente interior HA: temperatura=%.1f C humedad=%.1f%%",
        climate.temperature,
        climate.humidity
    );

    return true;
}


bool CareHomeAssistant::GetEntities(
    std::vector<CareHaEntity>& entities
) {
    entities.clear();

    std::lock_guard<std::mutex> lock(http_mutex_);

    if (!ready_ || !network_ready_) {
        ESP_LOGW(TAG, "Home Assistant no esta listo para descubrir entidades");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "Interfaz de red no disponible");
        return false;
    }

    if (!http_) {
        http_ = network->CreateHttp(3);
        if (!http_) {
            ESP_LOGE(TAG, "No se pudo crear cliente HTTP");
            return false;
        }
        ESP_LOGI(TAG, "Cliente HTTP persistente creado");
    }

    const std::string url =
        std::string(CARE_HA_URL) + "/api/states";

    const std::string authorization =
        "Bearer " + std::string(CARE_HA_TOKEN);

    http_->SetTimeout(8000);
    http_->SetHeader("Authorization", authorization);
    http_->SetHeader("Content-Type", "application/json");
    http_->SetHeader("Accept", "application/json");
    http_->SetHeader("Connection", "keep-alive");

    ESP_LOGI(TAG, "Descubriendo entidades de Home Assistant");

    auto opened = http_->Open("GET", url);
    if (!opened) {
        ESP_LOGE(
            TAG,
            "Error consultando entidades HA: %s",
            opened.error().ToString().c_str()
        );
        return false;
    }

    auto status = http_->GetStatusCode();
    if (!status) {
        ESP_LOGE(
            TAG,
            "No se pudo obtener HTTP status: %s",
            status.error().ToString().c_str()
        );
        return false;
    }

    std::string response = http_->ReadAll();

    if (*status != 200) {
        ESP_LOGE(TAG, "Home Assistant /api/states HTTP %d", *status);
        return false;
    }

    cJSON* root = cJSON_Parse(response.c_str());
    if (!cJSON_IsArray(root)) {
        if (root) cJSON_Delete(root);
        ESP_LOGE(TAG, "Respuesta /api/states invalida");
        return false;
    }

    constexpr size_t kMaxEntities = 120;

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        if (entities.size() >= kMaxEntities) {
            break;
        }

        cJSON* id =
            cJSON_GetObjectItemCaseSensitive(item, "entity_id");
        cJSON* state =
            cJSON_GetObjectItemCaseSensitive(item, "state");
        cJSON* attributes =
            cJSON_GetObjectItemCaseSensitive(item, "attributes");

        if (!cJSON_IsString(id) || id->valuestring == nullptr) {
            continue;
        }

        const std::string entity_id = id->valuestring;
        const size_t dot = entity_id.find('.');

        if (dot == std::string::npos || dot == 0) {
            continue;
        }

        const std::string domain =
            entity_id.substr(0, dot);

        if (!IsUsefulEntityDomain(domain)) {
            continue;
        }

        CareHaEntity entity;
        entity.entity_id = entity_id;
        entity.domain = domain;

        if (cJSON_IsString(state) &&
            state->valuestring != nullptr) {
            entity.state = state->valuestring;
        }

        if (cJSON_IsObject(attributes)) {
            cJSON* friendly =
                cJSON_GetObjectItemCaseSensitive(
                    attributes,
                    "friendly_name"
                );

            if (cJSON_IsString(friendly) &&
                friendly->valuestring != nullptr) {
                entity.friendly_name =
                    friendly->valuestring;
            }
        }

        if (entity.friendly_name.empty()) {
            entity.friendly_name = entity.entity_id;
        }

        entities.push_back(std::move(entity));
    }

    cJSON_Delete(root);

    ESP_LOGI(
        TAG,
        "Entidades HA utiles detectadas: %u",
        static_cast<unsigned>(entities.size())
    );

    return true;
}



bool CareHomeAssistant::GetAllowedEntities(
    std::vector<std::string>& entities
) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    entities = allowed_entities_;
    return true;
}

bool CareHomeAssistant::SetAllowedEntities(
    const std::vector<std::string>& entities
) {
    if (entities.size() > kMaxAllowedEntities) {
        ESP_LOGW(TAG, "Allowlist HA demasiado grande: %u",
                 static_cast<unsigned>(entities.size()));
        return false;
    }

    std::vector<std::string> normalized;
    normalized.reserve(entities.size());

    for (const auto& entity_id : entities) {
        if (!IsValidEntityId(entity_id)) {
            ESP_LOGW(TAG, "Entity ID invalido en allowlist: %s",
                     entity_id.c_str());
            return false;
        }

        if (std::find(normalized.begin(), normalized.end(), entity_id) ==
            normalized.end()) {
            normalized.push_back(entity_id);
        }
    }

    std::string serialized;
    for (size_t i = 0; i < normalized.size(); ++i) {
        if (i > 0) serialized.push_back('\n');
        serialized += normalized[i];
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open(
        kHaNvsNamespace,
        NVS_READWRITE,
        &handle
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo abrir NVS HA: %s",
                 esp_err_to_name(err));
        return false;
    }

    err = nvs_set_str(
        handle,
        kHaAllowlistKey,
        serialized.c_str()
    );

    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar allowlist HA: %s",
                 esp_err_to_name(err));
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        allowed_entities_ = std::move(normalized);
    }

    ESP_LOGI(
        TAG,
        "Allowlist HA guardada: %u entidades",
        static_cast<unsigned>(allowed_entities_.size())
    );

    return true;
}

bool CareHomeAssistant::IsEntityAllowed(
    const std::string& entity_id
) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return std::find(
        allowed_entities_.begin(),
        allowed_entities_.end(),
        entity_id
    ) != allowed_entities_.end();
}



bool CareHomeAssistant::GetAllowedEntityDetails(
    std::vector<CareHaEntity>& entities
) {
    entities.clear();

    std::vector<std::string> allowed;
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        allowed = allowed_entities_;
    }

    if (allowed.empty()) {
        return true;
    }

    std::vector<CareHaEntity> discovered;
    if (!GetEntities(discovered)) {
        return false;
    }

    for (const auto& entity : discovered) {
        if (std::find(allowed.begin(), allowed.end(), entity.entity_id) != allowed.end()) {
            entities.push_back(entity);
        }
    }

    return true;
}

// DP-CF-002 — TTS determinista mediante Home Assistant + Piper.
bool CareHomeAssistant::GetTtsUrl(
    const std::string& message,
    std::string& audio_url
) {
    audio_url.clear();

    std::lock_guard<std::mutex> lock(http_mutex_);

    if (!ready_ || !network_ready_) {
        ESP_LOGW(TAG, "DP-CF-002: Home Assistant no esta listo");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "DP-CF-002: interfaz de red no disponible");
        return false;
    }

    if (!http_) {
        http_ = network->CreateHttp(3);
        if (!http_) {
            ESP_LOGE(TAG, "DP-CF-002: no se pudo crear cliente HTTP");
            return false;
        }
    }

    cJSON* root = cJSON_CreateObject();
    if (root == nullptr) {
        return false;
    }

    cJSON_AddStringToObject(root, "engine_id", "tts.piper");
    cJSON_AddStringToObject(root, "message", message.c_str());

    char* raw_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (raw_body == nullptr) {
        return false;
    }

    std::string body(raw_body);
    cJSON_free(raw_body);

    const std::string url = std::string(CARE_HA_URL) + "/api/tts_get_url";
    const std::string authorization = "Bearer " + std::string(CARE_HA_TOKEN);

    http_->SetTimeout(10000);
    http_->SetHeader("Authorization", authorization);
    http_->SetHeader("Content-Type", "application/json");
    http_->SetHeader("Accept", "application/json");
    http_->SetHeader("Connection", "keep-alive");
    http_->SetContent(std::move(body));

    ESP_LOGI(TAG, "DP-CF-002: solicitando TTS a Home Assistant/Piper");

    auto opened = http_->Open("POST", url);
    if (!opened) {
        ESP_LOGE(TAG, "DP-CF-002: error abriendo tts_get_url: %s", opened.error().ToString().c_str());
        return false;
    }

    auto status = http_->GetStatusCode();
    if (!status) {
        ESP_LOGE(TAG, "DP-CF-002: sin HTTP status: %s", status.error().ToString().c_str());
        return false;
    }

    const std::string response = http_->ReadAll();
    if (*status < 200 || *status >= 300) {
        ESP_LOGE(TAG, "DP-CF-002: Home Assistant TTS HTTP %d: %s", *status, response.c_str());
        return false;
    }

    cJSON* response_json = cJSON_Parse(response.c_str());
    if (response_json == nullptr) {
        ESP_LOGE(TAG, "DP-CF-002: respuesta TTS no es JSON");
        return false;
    }

    cJSON* url_json = cJSON_GetObjectItemCaseSensitive(response_json, "url");
    if (!cJSON_IsString(url_json) || url_json->valuestring == nullptr || url_json->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "DP-CF-002: respuesta TTS sin url");
        cJSON_Delete(response_json);
        return false;
    }

    audio_url = url_json->valuestring;
    cJSON_Delete(response_json);

    ESP_LOGI(TAG, "DP-CF-002: URL TTS obtenida (%u bytes)", static_cast<unsigned>(audio_url.size()));
    return true;
}


bool CareHomeAssistant::CallService(
    const std::string& domain,
    const std::string& service,
    const std::string& entity_id,
    std::string& response
) {
    response.clear();

    std::lock_guard<std::mutex> lock(http_mutex_);

    if (!ready_ || !network_ready_) {
        ESP_LOGW(TAG, "Home Assistant no esta listo");
        return false;
    }

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        ESP_LOGE(TAG, "Interfaz de red no disponible");
        return false;
    }

    if (!http_) {
        http_ = network->CreateHttp(3);
        if (!http_) {
            ESP_LOGE(TAG, "No se pudo crear cliente HTTP");
            return false;
        }
        ESP_LOGI(TAG, "Cliente HTTP persistente creado");
    }

    const std::string url =
        std::string(CARE_HA_URL) +
        "/api/services/" + domain + "/" + service;

    const std::string authorization =
        "Bearer " + std::string(CARE_HA_TOKEN);

    std::string body =
        std::string("{\"entity_id\":\"") +
        entity_id +
        "\"}";

    http_->SetTimeout(5000);
    http_->SetHeader("Authorization", authorization);
    http_->SetHeader("Content-Type", "application/json");
    http_->SetHeader("Accept", "application/json");
    http_->SetHeader("Connection", "keep-alive");
    http_->SetContent(std::move(body));

    ESP_LOGI(
        TAG,
        "HA service: %s.%s -> %s",
        domain.c_str(),
        service.c_str(),
        entity_id.c_str()
    );

    auto opened = http_->Open("POST", url);
    if (!opened) {
        ESP_LOGE(TAG, "Error llamando servicio HA: %s", opened.error().ToString().c_str());
        return false;
    }

    auto status = http_->GetStatusCode();
    if (!status) {
        ESP_LOGE(TAG, "No se pudo obtener HTTP status: %s", status.error().ToString().c_str());
        return false;
    }

    response = http_->ReadAll();

    if (*status < 200 || *status >= 300) {
        ESP_LOGE(TAG, "Servicio HA respondio HTTP %d", *status);
        return false;
    }

    ESP_LOGI(TAG, "Servicio HA ejecutado correctamente HTTP %d", *status);
    return true;
}

bool CareHomeAssistant::ControlAllowedDevice(
    const std::string& query,
    const std::string& action,
    CareHaDeviceControlResult& result
) {
    result = CareHaDeviceControlResult{};
    result.action = action;

    const std::string normalized_query = NormalizeDeviceName(query);

    if (normalized_query.empty()) {
        result.message = "No recibi un nombre de dispositivo valido.";
        return false;
    }

    if (action != "turn_on" && action != "turn_off") {
        result.message = "La accion solicitada no esta soportada.";
        return false;
    }

    std::vector<CareHaEntity> allowed;
    if (!GetAllowedEntityDetails(allowed)) {
        result.message = "No pude consultar los dispositivos permitidos.";
        return false;
    }

    std::vector<const CareHaEntity*> exact;
    std::vector<const CareHaEntity*> partial;

    for (const auto& entity : allowed) {
        if (!SupportsPowerAction(entity.domain)) {
            continue;
        }

        const std::string friendly = NormalizeDeviceName(entity.friendly_name);
        const std::string entity_name = NormalizeDeviceName(entity.entity_id);

        if (friendly == normalized_query || entity_name == normalized_query) {
            exact.push_back(&entity);
            continue;
        }

        if ((!friendly.empty() && friendly.find(normalized_query) != std::string::npos) ||
            (!normalized_query.empty() && normalized_query.find(friendly) != std::string::npos)) {
            partial.push_back(&entity);
        }
    }

    const CareHaEntity* selected = nullptr;

    if (exact.size() == 1) {
        selected = exact.front();
    } else if (exact.size() > 1) {
        result.ambiguous = true;
        result.message = "Hay mas de un dispositivo permitido con ese nombre.";
        return false;
    } else if (partial.size() == 1) {
        selected = partial.front();
    } else if (partial.size() > 1) {
        result.ambiguous = true;
        result.message = "El nombre coincide con varios dispositivos permitidos.";
        return false;
    }

    if (selected == nullptr) {
        result.message = "No encontre ese dispositivo entre los permitidos.";
        return false;
    }

    result.found = true;
    result.entity_id = selected->entity_id;
    result.friendly_name = selected->friendly_name;
    result.domain = selected->domain;

    std::string response;
    if (!CallService(selected->domain, action, selected->entity_id, response)) {
        result.message = "Home Assistant no pudo ejecutar la accion.";
        return false;
    }

    result.success = true;
    result.message =
        action == "turn_on"
            ? "Dispositivo encendido."
            : "Dispositivo apagado.";

    return true;
}



// DP-CF-001-r3 — buzón familiar compartido por todo el módulo.
// Evita depender de qué instancia de CareHomeAssistant atiende cada camino.
namespace {
std::mutex g_family_message_mutex;
std::string g_pending_family_sender;
std::string g_pending_family_message;
}

void CareHomeAssistant::SetPendingFamilyMessage(
    const std::string& sender,
    const std::string& message
) {
    std::lock_guard<std::mutex> lock(g_family_message_mutex);

    g_pending_family_sender = sender;
    g_pending_family_message = message;

    ESP_LOGI(
        TAG,
        "DP-CF-001-r3 SET mensaje familiar: remitente=%s sender_bytes=%u message_bytes=%u",
        g_pending_family_sender.c_str(),
        static_cast<unsigned>(g_pending_family_sender.size()),
        static_cast<unsigned>(g_pending_family_message.size())
    );
}

bool CareHomeAssistant::GetPendingFamilyMessage(
    std::string& sender,
    std::string& message
) {
    std::lock_guard<std::mutex> lock(g_family_message_mutex);

    ESP_LOGI(
        TAG,
        "DP-CF-001-r3 GET mensaje familiar: sender_bytes=%u message_bytes=%u",
        static_cast<unsigned>(g_pending_family_sender.size()),
        static_cast<unsigned>(g_pending_family_message.size())
    );

    if (g_pending_family_sender.empty() ||
        g_pending_family_message.empty()) {
        sender.clear();
        message.clear();
        return false;
    }

    sender = g_pending_family_sender;
    message = g_pending_family_message;
    return true;
}


const char* CareHomeAssistant::GetName() const {
    return "care-homeassistant";
}
