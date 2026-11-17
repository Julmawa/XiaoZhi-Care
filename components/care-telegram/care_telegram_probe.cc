#include "care_telegram_pair.h"
#include <atomic>
#include <ctime>
#include "nvs.h"
#include "care_telegram_probe.h"
#include "care_telegram_probe_config.h"
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <string>
#include <cstdint>
#include <functional>
#include <utility>

namespace xiaozhi_care {
namespace {
constexpr char kTag[]="CARE_TG_PROBE";
constexpr size_t kMaxResponse=12000;
constexpr int kMaxText=200;
static std::atomic<bool> s_started{false};
// R4.1: el evento de red de Application se emite al obtener IP.
// Nunca iniciar HTTPS antes del evento de conexion.
static std::atomic<bool> s_network_ready{false};
// Se registra durante Application::Start antes de que se inicie la sonda.
static std::function<bool(int64_t, const std::string&, const std::string&)> s_received_callback;

esp_err_t OnHttp(esp_http_client_event_t* evt) {
    if(evt->event_id!=HTTP_EVENT_ON_DATA || !evt->user_data) return ESP_OK;
    auto* out=static_cast<std::string*>(evt->user_data);
    if(evt->data_len<0 || out->size()+static_cast<size_t>(evt->data_len)>kMaxResponse) {
        out->clear();
        out->assign(kMaxResponse+1, 'X');  // marca respuesta demasiado larga
        return ESP_FAIL;
    }
    out->append(static_cast<const char*>(evt->data),static_cast<size_t>(evt->data_len));
    return ESP_OK;
}

bool RememberDelivery(int64_t id) {
    // Only a receipt and wall-clock seconds, never message text or account.
    const auto now = std::time(nullptr);
    if (now < 1704067200 || id < 0 || id >= UINT32_MAX) return false;
    const uint64_t stamp=(uint64_t(now)<<32)|uint32_t(id);
    nvs_handle_t h;
    if (nvs_open("care_tg_pos",NVS_READWRITE,&h)!=ESP_OK) return false;
    const bool ok=nvs_set_u64(h,"done",stamp)==ESP_OK && nvs_commit(h)==ESP_OK;
    nvs_close(h);
    return ok;
}
int64_t ReadDelivery() {
    const auto now=std::time(nullptr);
    if (now<1704067200) return -1;
    nvs_handle_t h;
    if (nvs_open("care_tg_pos",NVS_READONLY,&h)!=ESP_OK) return -1;
    uint64_t packed=0;
    const auto err=nvs_get_u64(h,"done",&packed);
    nvs_close(h);
    if (err!=ESP_OK || uint64_t(now)<(packed>>32) || uint64_t(now)-(packed>>32)>86400) return -1;
    return uint32_t(packed);
}

struct PollState {
    int64_t offset = 0;
    int64_t played_uncommitted = -1;

    bool CommitPlayed() {
        if (played_uncommitted < 0) return true;
        if (!RememberDelivery(played_uncommitted)) return false;
        offset = played_uncommitted + 1;
        played_uncommitted = -1;
        return true;
    }
};

void Process(const std::string& body, PollState& state) {
    if (!state.CommitPlayed()) return;
    cJSON* root=cJSON_ParseWithLengthOpts(body.c_str(),body.size()+1,nullptr,true);
    if(!root)return;
    const cJSON* results=cJSON_GetObjectItemCaseSensitive(root,"result");
    if(!cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root,"ok")) || !cJSON_IsArray(results)) {
        cJSON_Delete(root);
        return;
    }
    const cJSON* entry=nullptr;
    cJSON_ArrayForEach(entry,results) {
        const auto* update=cJSON_GetObjectItemCaseSensitive(entry,"update_id");
        if (!cJSON_IsNumber(update) || update->valuedouble<0 || update->valuedouble>=9007199254740991.0) break;
        const auto id=int64_t(update->valuedouble);
        if (double(id)!=update->valuedouble) break;
        if (id<state.offset) continue;
        const auto* msg=cJSON_GetObjectItemCaseSensitive(entry,"message");
        const auto* from=cJSON_GetObjectItemCaseSensitive(msg,"from");
        const auto* chat=cJSON_GetObjectItemCaseSensitive(msg,"chat");
        const auto* sender=cJSON_GetObjectItemCaseSensitive(from,"id");
        const auto* chatid=cJSON_GetObjectItemCaseSensitive(chat,"id");
        const auto* type=cJSON_GetObjectItemCaseSensitive(chat,"type");
        const auto* text=cJSON_GetObjectItemCaseSensitive(msg,"text");
        bool consumed=true;
        if (cJSON_IsNumber(sender) && sender->valuedouble>0 && sender->valuedouble<9007199254740991.0 &&
            double(int64_t(sender->valuedouble))==sender->valuedouble && cJSON_IsNumber(chatid) &&
            sender->valuedouble==chatid->valuedouble && cJSON_IsString(type) &&
            std::strcmp(type->valuestring,"private")==0 && cJSON_IsString(text)) {
            const auto account=std::to_string(int64_t(sender->valuedouble));
            const std::string message=text->valuestring;
            if (message == "/vincular") {
                // Sin codigo: el familiar solo solicita. Requiere aprobacion web del admin.
                const auto* first = cJSON_GetObjectItemCaseSensitive(from, "first_name");
                const auto* last = cJSON_GetObjectItemCaseSensitive(from, "last_name");
                const auto* username = cJSON_GetObjectItemCaseSensitive(from, "username");
                std::string name;
                if (cJSON_IsString(first) && first->valuestring) name = first->valuestring;
                if (cJSON_IsString(last) && last->valuestring) {
                    if (!name.empty()) name += " ";
                    name += last->valuestring;
                }
                const std::string user = cJSON_IsString(username) && username->valuestring
                    ? username->valuestring : "";
                if (!ReceiveTelegramRequest(account, name, user))
                    ESP_LOGW(kTag, "Solicitud Telegram no admitida (vinculada, repetida, llena o almacenamiento no disponible)");
            } else if (message.rfind("/vincular ",0)==0) {
                // Compatibilidad: conservar el codigo R3 por si se requiere recuperar una cuenta.
                ReceiveTelegramPair(message.substr(10),account);
            } else if (!message.empty() && message.size()<=kMaxText) {
                std::string name;
                bool unavailable=false;
                if (ResolveTelegramSender(account,name,&unavailable) && id != ReadDelivery()) {
                    consumed=s_received_callback && s_received_callback(id,account,message);
                    if (consumed) {
                        state.played_uncommitted = id;
                        if (!state.CommitPlayed()) {
                            ESP_LOGW(kTag,"Telegram receipt not committed; holding queue");
                            break;
                        }
                    }
                } else if (unavailable) consumed=false;
            } else {
                ESP_LOGW(kTag,"Unsupported message length");
            }
        }
        if (!consumed) { ESP_LOGW(kTag,"Audio pending; update retained at Telegram"); break; }
        state.offset=id+1;
    }
    cJSON_Delete(root);
}

void Task(void*) {
    ESP_LOGI(kTag,"Telegram inicializado; esperando red conectada (IP)");
    PollState state;
    unsigned failures=0;
    bool was_online=false;
    while(true) {
        // R4.1: no consultar DNS/TLS hasta tener IP; pausar si cae la red.
        // Se reanuda sin perder offset, vinculos ni mensajes pendientes.
        if (!s_network_ready.load(std::memory_order_acquire)) {
            if (was_online) {
                ESP_LOGW(kTag,"Red desconectada; pausando consultas Telegram");
                failures=0;
                was_online=false;
            }
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (!was_online) {
            ESP_LOGI(kTag,"Red conectada; iniciando consultas Telegram");
            was_online=true;
            failures=0;
        }
        if (!state.CommitPlayed()) {
            vTaskDelay(pdMS_TO_TICKS(8000));
            continue;
        }
        std::string body;
        const std::string url=std::string("https://api.telegram.org/bot")+
             CARE_TG_PROBE_BOT_TOKEN+"/getUpdates?timeout=0&limit=1&allowed_updates=%5B%22message%22%5D&offset="+std::to_string(state.offset);
        esp_http_client_config_t config={};
        config.url=url.c_str();
        config.method=HTTP_METHOD_GET;
        config.event_handler=&OnHttp;
        config.user_data=&body;
        config.crt_bundle_attach=esp_crt_bundle_attach;
        config.timeout_ms=7500;
        config.disable_auto_redirect=true;
        config.buffer_size=1024;
        esp_http_client_handle_t client=esp_http_client_init(&config);
        if(client) {
            esp_err_t err=esp_http_client_perform(client);
            const int code=esp_http_client_get_status_code(client);
            esp_http_client_cleanup(client);
            if(err==ESP_OK && code==200 && body.size()<=kMaxResponse) { failures=0; Process(body,state); }
            else if(code==409) ESP_LOGW(kTag,"Conflicto 409: detener programa Telegram V1 en Windows");
            else ESP_LOGW(kTag,"Consulta Telegram fallo: %s HTTP %d",esp_err_to_name(err),code);
            if ((err!=ESP_OK || code!=200 || body.size()>kMaxResponse) && failures<4) ++failures;
        } else {
            ESP_LOGW(kTag,"No se pudo crear cliente HTTPS");
            if (failures<4) ++failures;
        }
        vTaskDelay(pdMS_TO_TICKS((failures ? failures : 1) * 8000));
    }
}
} // namespace
void SetCareTelegramProbeReceivedCallback(
    std::function<bool(int64_t, const std::string&, const std::string&)> callback) {
    // Llamar una sola vez desde la inicializacion de Application.
    s_received_callback = std::move(callback);
}
void SetCareTelegramNetworkReady(bool ready) {
    // Publicacion thread-safe desde el hilo principal de Application.
    s_network_ready.store(ready, std::memory_order_release);
}

void StartCareTelegramProbe() {

    if(std::strlen(CARE_TG_PROBE_BOT_TOKEN)<20) {
        ESP_LOGW(kTag,"Prueba deshabilitada: configurar token del bot localmente");
        return;
    }
    if (s_started.exchange(true)) return;
    if (xTaskCreate(&Task,"tg_probe",10240,nullptr,3,nullptr)!=pdPASS) {
        s_started.store(false);
        ESP_LOGE(kTag,"No se pudo iniciar Telegram");
    }
}
} // namespace xiaozhi_care
