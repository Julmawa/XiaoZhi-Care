// telegram_bot.cc
// Ver telegram_bot.h para la descripción general.

#include "telegram_bot.h"

#include <sdkconfig.h>

#if CONFIG_TELEGRAM_ENABLE

#include <algorithm>
#include <cstring>

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cJSON.h>
#include <http.h>
#include <network_interface.h>
#include "esp_audio_types.h"
#include "esp_audio_enc.h"
#include "esp_opus_enc.h"

#include "application.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "mcp_server.h"
#include "settings.h"
#include "assets/lang_config.h"

#define TAG "Telegram"

// familia.txt se incluye en el firmware al compilar (ver main/CMakeLists.txt)
extern const char familia_txt_start[] asm("_binary_familia_txt_start");
extern const char familia_txt_end[] asm("_binary_familia_txt_end");

// Valores por defecto, por si la configuración (sdkconfig) todavía no tiene estas opciones
#ifndef CONFIG_TELEGRAM_WAKE_TEXT
#define CONFIG_TELEGRAM_WAKE_TEXT "Leé Telegram"
#endif
#ifndef CONFIG_TELEGRAM_RECORD_MAX_SECONDS
#define CONFIG_TELEGRAM_RECORD_MAX_SECONDS 60
#endif
#ifndef CONFIG_TELEGRAM_VOICE_MAX_SECONDS
#define CONFIG_TELEGRAM_VOICE_MAX_SECONDS 60
#endif

namespace {

constexpr size_t kMaxRecent = 10;        // mensajes recientes que se guardan
constexpr int kHttpConnectId = 3;        // sólo importa en placas 4G (ML307); en WiFi se ignora
constexpr int kOpusFrameMs = 60;         // duración de cada paquete Opus grabado
constexpr int kRecordSampleRate = 16000;
constexpr int kBeepVolume = 100;

// Encabezado del familia.txt que se exporta por Telegram
const char kFamilyFileHeader[] =
    "# ============================================================\n"
    "#  FAMILIA DEL XIAOZHI\n"
    "# ============================================================\n"
    "#  Las personas de esta lista pueden escribirle y mandarle\n"
    "#  audios a mamá por Telegram, y mamá les puede mandar\n"
    "#  mensajes a ellas.\n"
    "#\n"
    "#  Una persona por línea, así:\n"
    "#\n"
    "#      Nombre = chat_id\n"
    "#\n"
    "#  - Nombre: como lo dice mamá (\"Julio\", \"Ana\", \"la nena\",\n"
    "#    \"el doctor\"). Es el nombre que ella escucha cuando le\n"
    "#    llega un mensaje y el que usa para mandarle uno.\n"
    "#  - chat_id: el número de Telegram de esa persona.\n"
    "#\n"
    "#  Para sumar a alguien, lo más fácil: la persona abre el bot,\n"
    "#  toca \"Iniciar\" y escribe /vincular. Vos tocás \"Aprobar\" y\n"
    "#  el bot te manda este archivo actualizado por Telegram.\n"
    "#  Guardalo acá reemplazando el anterior. (Si no lo hacés,\n"
    "#  igual funciona: el XiaoZhi lo tiene guardado.)\n"
    "#  Con /exportar el bot te lo manda cuando quieras.\n"
    "#\n"
    "#  Las líneas que empiezan con # no se tienen en cuenta.\n"
    "#  Si cambiás este archivo a mano, grabá el equipo de nuevo\n"
    "#  (la llama de ESP-IDF).\n"
    "# ============================================================\n";         // volumen de los pitidos de grabación (0-100)

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::vector<std::string> SplitCsv(const std::string& csv) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= csv.size()) {
        size_t comma = csv.find(',', start);
        if (comma == std::string::npos) comma = csv.size();
        auto item = Trim(csv.substr(start, comma - start));
        if (!item.empty()) out.push_back(item);
        start = comma + 1;
    }
    return out;
}

// Minúsculas y sin tildes, para comparar nombres ("Julián" == "julian")
std::string Normalize(const std::string& in) {
    static const std::pair<const char*, char> kMap[] = {
        {"á", 'a'}, {"é", 'e'}, {"í", 'i'}, {"ó", 'o'}, {"ú", 'u'}, {"ü", 'u'}, {"ñ", 'n'},
        {"Á", 'a'}, {"É", 'e'}, {"Í", 'i'}, {"Ó", 'o'}, {"Ú", 'u'}, {"Ü", 'u'}, {"Ñ", 'n'}};
    std::string out;
    for (size_t i = 0; i < in.size();) {
        bool mapped = false;
        for (auto& m : kMap) {
            size_t len = strlen(m.first);
            if (in.compare(i, len, m.first) == 0) {
                out.push_back(m.second);
                i += len;
                mapped = true;
                break;
            }
        }
        if (!mapped) {
            out.push_back((char)tolower((unsigned char)in[i]));
            i++;
        }
    }
    return Trim(out);
}

std::string JsonIdToString(cJSON* item) {
    if (cJSON_IsNumber(item)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%lld", (long long)item->valuedouble);
        return buf;
    }
    if (cJSON_IsString(item)) return item->valuestring;
    return "";
}

void ShowAlert(const std::string& text, bool with_sound) {
    Application::GetInstance().Schedule([text, with_sound]() {
        Application::GetInstance().Alert("Telegram", text.c_str(), "neutral",
                                         with_sound ? Lang::Sounds::OGG_POPUP : std::string_view());
    });
}

// Vuelve la pantalla a "En espera" (sólo si el equipo sigue en reposo)
void DismissAlertLater(int delay_ms) {
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
    Application::GetInstance().Schedule([]() { Application::GetInstance().DismissAlert(); });
}

void WaitPlaybackIdle(int timeout_ms) {
    auto& audio = Application::GetInstance().GetAudioService();
    for (int t = 0; t < timeout_ms && !audio.IsPlaybackIdle(); t += 100) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Pitido de grabación a volumen máximo; después vuelve al volumen que tenía
void LoudBeep() {
    auto& audio = Application::GetInstance().GetAudioService();
    auto* codec = Board::GetInstance().GetAudioCodec();
    const int old_volume = codec->output_volume();
    if (old_volume < kBeepVolume) codec->SetOutputVolume(kBeepVolume);
    audio.PlaySound(Lang::Sounds::OGG_POPUP);
    WaitPlaybackIdle(3000);
    vTaskDelay(pdMS_TO_TICKS(150));  // que termine de salir por el parlante
    if (old_volume < kBeepVolume) codec->SetOutputVolume(old_volume);
}

// ---------------------------------------------------------------------------
// Contenedor OGG para Opus (RFC 7845), necesario para mandar notas de voz
// ---------------------------------------------------------------------------

uint32_t OggCrc(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t r = i << 24;
            for (int k = 0; k < 8; k++) {
                r = (r & 0x80000000u) ? (r << 1) ^ 0x04C11DB7u : (r << 1);
            }
            table[i] = r;
        }
        ready = true;
    }
    uint32_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc = (crc << 8) ^ table[((crc >> 24) ^ data[i]) & 0xFF];
    }
    return crc;
}

void PutLE(std::string& s, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; i++) s.push_back((char)((v >> (8 * i)) & 0xFF));
}

// Agrega una página OGG con los paquetes indicados (cada uno: puntero + largo)
void AppendOggPage(std::string& out, uint8_t header_type, uint64_t granule, uint32_t serial,
                   uint32_t seq, const std::vector<std::pair<const uint8_t*, size_t>>& packets) {
    std::string page;
    page.append("OggS", 4);
    page.push_back(0);  // versión
    page.push_back((char)header_type);
    PutLE(page, granule, 8);
    PutLE(page, serial, 4);
    PutLE(page, seq, 4);
    PutLE(page, 0, 4);  // CRC, se completa al final
    std::string segments;
    for (auto& p : packets) {
        size_t len = p.second;
        while (len >= 255) {
            segments.push_back((char)255);
            len -= 255;
        }
        segments.push_back((char)len);
    }
    page.push_back((char)segments.size());
    page += segments;
    for (auto& p : packets) page.append((const char*)p.first, p.second);

    uint32_t crc = OggCrc((const uint8_t*)page.data(), page.size());
    for (int i = 0; i < 4; i++) page[22 + i] = (char)((crc >> (8 * i)) & 0xFF);
    out += page;
}

size_t SegmentsFor(size_t len) { return len / 255 + 1; }

// packets: todos los paquetes Opus seguidos en "data"; "lens" tiene el largo de cada uno
std::string BuildOggOpus(const std::string& data, const std::vector<uint16_t>& lens,
                         int input_sample_rate, int frame_ms) {
    std::string out;
    const uint32_t serial = (uint32_t)esp_timer_get_time();
    uint32_t seq = 0;
    const uint16_t pre_skip = 312;

    // Página 1: OpusHead
    std::string head = "OpusHead";
    head.push_back(1);  // versión
    head.push_back(1);  // canales (mono)
    PutLE(head, pre_skip, 2);
    PutLE(head, input_sample_rate, 4);
    PutLE(head, 0, 2);  // ganancia
    head.push_back(0);  // mapping family
    AppendOggPage(out, 0x02, 0, serial, seq++, {{(const uint8_t*)head.data(), head.size()}});

    // Página 2: OpusTags
    std::string tags = "OpusTags";
    const char* vendor = "xiaozhi-telegram";
    PutLE(tags, strlen(vendor), 4);
    tags += vendor;
    PutLE(tags, 0, 4);  // sin comentarios
    AppendOggPage(out, 0x00, 0, serial, seq++, {{(const uint8_t*)tags.data(), tags.size()}});

    // Páginas de audio
    const uint64_t samples_per_packet = 48 * frame_ms;  // la posición se cuenta a 48 kHz
    uint64_t granule = 0;
    size_t offset = 0;
    size_t i = 0;
    while (i < lens.size()) {
        std::vector<std::pair<const uint8_t*, size_t>> page_packets;
        size_t segs = 0;
        while (i < lens.size() && page_packets.size() < 50 && segs + SegmentsFor(lens[i]) <= 255) {
            page_packets.push_back({(const uint8_t*)data.data() + offset, lens[i]});
            segs += SegmentsFor(lens[i]);
            offset += lens[i];
            granule += samples_per_packet;
            i++;
        }
        uint8_t type = (i >= lens.size()) ? 0x04 : 0x00;  // última página: fin de stream
        AppendOggPage(out, type, granule, serial, seq++, page_packets);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Grabación (en una tarea aparte: el codificador Opus necesita una pila grande)
// ---------------------------------------------------------------------------

struct RecordJob {
    int seconds = 10;                          // máximo
    const std::atomic<bool>* stop = nullptr;   // se pone en true al tocar el botón
    TaskHandle_t waiter = nullptr;
    std::string data;            // paquetes Opus concatenados
    std::vector<uint16_t> lens;  // largo de cada paquete
};

void RecordTask(void* arg) {
    auto* job = static_cast<RecordJob*>(arg);

    esp_opus_enc_config_t cfg = {};
    cfg.sample_rate = ESP_AUDIO_SAMPLE_RATE_16K;
    cfg.channel = ESP_AUDIO_MONO;
    cfg.bits_per_sample = ESP_AUDIO_BIT16;
    cfg.bitrate = ESP_OPUS_BITRATE_AUTO;
    cfg.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS;
    cfg.application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO;
    cfg.complexity = 0;
    cfg.enable_fec = false;
    cfg.enable_dtx = false;
    cfg.enable_vbr = true;

    void* enc = nullptr;
    if (esp_opus_enc_open(&cfg, sizeof(cfg), &enc) != ESP_AUDIO_ERR_OK || enc == nullptr) {
        ESP_LOGE(TAG, "No se pudo crear el codificador Opus");
    } else {
        int in_size = 0, out_size = 0;
        esp_opus_enc_get_frame_size(enc, &in_size, &out_size);
        const size_t frame_samples = in_size / sizeof(int16_t);
        const int channels = Board::GetInstance().GetAudioCodec()->input_channels();
        auto& audio = Application::GetInstance().GetAudioService();

        std::vector<int16_t> chunk;
        std::vector<int16_t> acc;
        std::vector<uint8_t> out(out_size);
        const size_t total_packets = job->seconds * 1000 / kOpusFrameMs;

        while (job->lens.size() < total_packets && !(job->stop && job->stop->load())) {
            if (!audio.ReadAudioData(chunk, kRecordSampleRate, frame_samples)) {
                ESP_LOGW(TAG, "Fallo al leer el micrófono");
                break;
            }
            if (channels == 2) {
                for (size_t k = 0; k + 1 < chunk.size(); k += 2) acc.push_back(chunk[k]);
            } else {
                acc.insert(acc.end(), chunk.begin(), chunk.end());
            }
            while (acc.size() >= frame_samples && job->lens.size() < total_packets) {
                esp_audio_enc_in_frame_t in = {};
                in.buffer = (uint8_t*)acc.data();
                in.len = (uint32_t)in_size;
                esp_audio_enc_out_frame_t o = {};
                o.buffer = out.data();
                o.len = (uint32_t)out_size;
                if (esp_opus_enc_process(enc, &in, &o) == ESP_AUDIO_ERR_OK && o.encoded_bytes > 0) {
                    job->data.append((const char*)out.data(), o.encoded_bytes);
                    job->lens.push_back((uint16_t)o.encoded_bytes);
                }
                acc.erase(acc.begin(), acc.begin() + frame_samples);
            }
        }
        esp_opus_enc_close(enc);
    }

    xTaskNotifyGive(job->waiter);
    vTaskDelete(nullptr);
}

}  // namespace

// ---------------------------------------------------------------------------
// Arranque y bucle principal
// ---------------------------------------------------------------------------

void TelegramBot::Start() {
    if (started_) return;
    started_ = true;

    std::string token = CONFIG_TELEGRAM_BOT_TOKEN;
    if (token.empty()) {
        ESP_LOGE(TAG, "Falta el token del bot (menuconfig -> Xiaozhi Assistant -> Telegram Bot)");
        return;
    }

    allowed_chat_ids_ = SplitCsv(CONFIG_TELEGRAM_ALLOWED_CHAT_IDS);
    if (!allowed_chat_ids_.empty()) {
        default_chat_id_ = allowed_chat_ids_.front();  // el primero es el administrador
    } else {
        ESP_LOGW(TAG, "No hay chat_id autorizado. Escribile /id al bot y mirá el monitor serie.");
    }
    LoadContacts();
    LoadFamilyFile();

    RegisterMcpTools();

    // La pila va en RAM interna: TLS + cJSON necesitan ~8 KB.
    xTaskCreate(TaskEntry, "telegram", 8192, this, 1, nullptr);  // prioridad baja: el audio siempre primero
}

void TelegramBot::TaskEntry(void* arg) {
    static_cast<TelegramBot*>(arg)->PollLoop();
    vTaskDelete(nullptr);
}

void TelegramBot::PollLoop() {
    auto& app = Application::GetInstance();

    // Esperar a que el equipo esté activado y en reposo (no en una conversación)
    auto wait_idle = [&app]() {
        while (app.GetDeviceState() != kDeviceStateIdle) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    };
    wait_idle();
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "Iniciando bot de Telegram");

    wait_idle();
    SkipOldUpdates();

    // El administrador también es un contacto (para que la usuaria pueda escribirle)
    if (!default_chat_id_.empty() && !IsContact(default_chat_id_)) {
        ApproveContact(default_chat_id_, "");
    }
    if (!default_chat_id_.empty() && ContactName(default_chat_id_).empty()) {
        std::string admin_name = FetchChatName(default_chat_id_);
        if (!admin_name.empty()) {
            std::lock_guard<std::mutex> lock(data_mutex_);
            for (auto& c : contacts_) {
                if (c.chat_id == default_chat_id_) c.name = admin_name;
            }
            SaveContacts();
        }
    }

#if CONFIG_TELEGRAM_NOTIFY_ON_BOOT
    if (!default_chat_id_.empty()) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        outbox_.push_back({default_chat_id_, "XiaoZhi está en línea."});
    }
#endif

    int64_t next_poll_us = 0;
    while (true) {
        // Grabación de audio pedida por el asistente
        if (record_pending_) {
            HandlePendingRecording();
            vTaskDelay(pdMS_TO_TICKS(300));
            continue;
        }

        auto state = app.GetDeviceState();

        // Mensajes pendientes de enviar. Se mandan con el equipo en reposo o
        // escuchando, nunca mientras habla (la conexión cortaría su voz).
        if (state == kDeviceStateIdle || state == kDeviceStateListening) {
            FlushOutbox();
        }

        // Sólo se consulta Telegram con el equipo en reposo: cada conexión HTTPS
        // usa mucho CPU y, durante una conversación, satura el audio.
        const int64_t now = esp_timer_get_time();
        if (state == kDeviceStateIdle && now >= next_poll_us) {
            PollOnce();
            ProcessPendingVoice();
            next_poll_us = esp_timer_get_time() + (int64_t)CONFIG_TELEGRAM_POLL_INTERVAL_MS * 1000;
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

void TelegramBot::QueueMessage(const std::string& text, const std::string& chat_id) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (outbox_.size() >= 30) {
        outbox_.pop_front();  // no acumular sin límite si no hay conexión
    }
    outbox_.push_back({chat_id.empty() ? default_chat_id_ : chat_id, text});
}

void TelegramBot::FlushOutbox() {
    while (true) {
        std::string text, target;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (outbox_.empty()) return;
            target = outbox_.front().first;
            text = outbox_.front().second;
        }
        bool ok = false;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            ok = SendMessage(text, target);
            if (!ok) vTaskDelay(pdMS_TO_TICKS(2000));
        }
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            if (!outbox_.empty()) outbox_.pop_front();
        }
        if (!ok) {
            ESP_LOGE(TAG, "No se pudo enviar el mensaje después de 3 intentos");
            ShowAlert("No se pudo enviar el mensaje por Telegram", false);
        }
        auto state = Application::GetInstance().GetDeviceState();
        if (state != kDeviceStateIdle && state != kDeviceStateListening) return;
    }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

bool TelegramBot::HttpRequest(const std::string& method, const std::string& url,
                              const std::string& content_type, const std::string& body,
                              std::string& response, int timeout_ms) {
    std::lock_guard<std::mutex> lock(http_mutex_);

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) return false;

    auto http = network->CreateHttp(kHttpConnectId);
    if (!http) {
        ESP_LOGW(TAG, "No se pudo crear el cliente HTTP");
        return false;
    }

    http->SetTimeout(timeout_ms);
    if (!content_type.empty()) {
        http->SetHeader("Content-Type", content_type);
    }
    if (!body.empty()) {
        http->SetContent(std::string(body));
    }

    if (auto opened = http->Open(method, url); !opened) {
        ESP_LOGW(TAG, "No se pudo conectar con api.telegram.org: %s",
                 opened.error().ToString().c_str());
        return false;
    }
    auto status_result = http->GetStatusCode();
    if (!status_result) {
        ESP_LOGW(TAG, "No se pudo leer la respuesta: %s", status_result.error().ToString().c_str());
        http->Close();
        return false;
    }
    int status = *status_result;
    response = http->ReadAll();
    http->Close();

    if (status != 200) {
        // No se loguea la URL para no exponer el token
        ESP_LOGW(TAG, "HTTP %d %s", status, response.size() < 300 ? response.c_str() : "");
        return false;
    }
    return true;
}

bool TelegramBot::ApiCall(const std::string& method, const std::string& json_body, std::string& response) {
    std::string url = std::string("https://api.telegram.org/bot") + CONFIG_TELEGRAM_BOT_TOKEN + "/" + method;
    bool ok = HttpRequest("POST", url, "application/json", json_body, response);
    if (!ok) ESP_LOGW(TAG, "Falló %s", method.c_str());
    return ok;
}

bool TelegramBot::SendMessage(const std::string& text, const std::string& chat_id,
                              const std::string& reply_markup_json) {
    std::string target = chat_id.empty() ? default_chat_id_ : chat_id;
    if (target.empty()) {
        ESP_LOGW(TAG, "SendMessage: no hay chat_id de destino");
        return false;
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "chat_id", target.c_str());
    cJSON_AddStringToObject(root, "text", text.c_str());
    if (!reply_markup_json.empty()) {
        cJSON* markup = cJSON_Parse(reply_markup_json.c_str());
        if (markup) cJSON_AddItemToObject(root, "reply_markup", markup);
    }
    char* body = cJSON_PrintUnformatted(root);
    std::string json_body = body ? body : "";
    if (body) cJSON_free(body);
    cJSON_Delete(root);

    std::string response;
    bool ok = ApiCall("sendMessage", json_body, response);
    ESP_LOGI(TAG, "sendMessage -> %s: %s", target.c_str(), ok ? "OK" : "ERROR");
    return ok;
}

// ---------------------------------------------------------------------------
// Recepción
// ---------------------------------------------------------------------------

// Al arrancar descartamos mensajes viejos para no repetirlos en cada reinicio.
void TelegramBot::SkipOldUpdates() {
    std::string response;
    if (!ApiCall("getUpdates", "{\"offset\":-1,\"timeout\":0}", response)) return;

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) return;
    cJSON* result = cJSON_GetObjectItem(root, "result");
    cJSON* update = nullptr;
    cJSON_ArrayForEach(update, result) {
        cJSON* id = cJSON_GetObjectItem(update, "update_id");
        if (cJSON_IsNumber(id)) {
            offset_ = (long long)id->valuedouble + 1;
        }
    }
    cJSON_Delete(root);
    ESP_LOGI(TAG, "Mensajes anteriores descartados (offset=%lld)", offset_);
}

void TelegramBot::PollOnce() {
    char body[128];
    snprintf(body, sizeof(body),
             "{\"offset\":%lld,\"timeout\":0,\"limit\":5,\"allowed_updates\":[\"message\",\"callback_query\"]}", offset_);

    std::string response;
    if (!ApiCall("getUpdates", body, response)) return;

    cJSON* root = cJSON_Parse(response.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Respuesta JSON inválida");
        return;
    }
    cJSON* result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsArray(result)) {
        cJSON* update = nullptr;
        cJSON_ArrayForEach(update, result) {
            cJSON* id = cJSON_GetObjectItem(update, "update_id");
            if (cJSON_IsNumber(id)) {
                offset_ = (long long)id->valuedouble + 1;
            }
            HandleUpdate(update);
        }
    }
    cJSON_Delete(root);
}

void TelegramBot::HandleUpdate(cJSON* update) {
    cJSON* callback = cJSON_GetObjectItem(update, "callback_query");
    if (cJSON_IsObject(callback)) {
        HandleCallbackQuery(callback);
        return;
    }
    cJSON* message = cJSON_GetObjectItem(update, "message");
    if (!cJSON_IsObject(message)) return;

    cJSON* chat = cJSON_GetObjectItem(message, "chat");
    std::string chat_id = JsonIdToString(cJSON_GetObjectItem(chat, "id"));
    if (chat_id.empty()) return;

    cJSON* chat_type = cJSON_GetObjectItem(chat, "type");
    const bool is_group = cJSON_IsString(chat_type) && strcmp(chat_type->valuestring, "private") != 0;

    // Un grupo que pasa a "supergrupo" cambia de chat_id: se sigue aceptando el nuevo
    cJSON* migrate_to = cJSON_GetObjectItem(message, "migrate_to_chat_id");
    if (migrate_to != nullptr) {
        std::string new_id = JsonIdToString(migrate_to);
        if (IsAllowed(chat_id) && !new_id.empty() && !IsAllowed(new_id)) {
            ESP_LOGW(TAG, "El grupo %s ahora es %s. Actualizá 'Allowed chat IDs' en menuconfig.",
                     chat_id.c_str(), new_id.c_str());
            allowed_chat_ids_.push_back(new_id);
            if (default_chat_id_ == chat_id) default_chat_id_ = new_id;
        }
        return;
    }

    std::string from = "alguien";
    cJSON* from_obj = cJSON_GetObjectItem(message, "from");
    cJSON* first_name = cJSON_GetObjectItem(from_obj, "first_name");
    if (cJSON_IsString(first_name)) from = first_name->valuestring;
    if (cJSON_IsTrue(cJSON_GetObjectItem(from_obj, "is_bot"))) return;  // ignorar otros bots

    cJSON* text_item = cJSON_GetObjectItem(message, "text");
    std::string text = cJSON_IsString(text_item) ? text_item->valuestring : "";

    ESP_LOGI(TAG, "Mensaje de %s (chat_id=%s%s): %s", from.c_str(), chat_id.c_str(), is_group ? ", grupo" : "",
             text.empty() ? "(sin texto)" : text.c_str());

    // Comandos: en grupos llegan como "/id@NombreDelBot"
    std::string command;
    if (!text.empty() && text[0] == '/') {
        command = text.substr(0, text.find_first_of(" @"));
    }

    // /vincular escrito por un familiar: ve su número y se le avisa al administrador
    if (command == "/vincular" && !is_group && chat_id != default_chat_id_) {
        if (IsAllowed(chat_id)) {
            SendMessage("Hola " + from + ". Tu número es: " + chat_id + "\nYa formás parte de la familia "
                        "del XiaoZhi.", chat_id);
        } else {
            bool already_pending = false;
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                already_pending = pending_requests_.count(chat_id) > 0 || rejected_.count(chat_id) > 0;
            }
            SendMessage("Hola " + from + ". Tu número es: " + chat_id + "\n" +
                        (already_pending ? std::string("Ya le avisé al administrador, esperá su respuesta.")
                                         : std::string("Le avisé al administrador para que te sume a la familia. "
                                                       "Te aviso apenas esté listo.")), chat_id);
            RequestAuthorization(chat_id, from, false);
        }
        return;
    }

    // /start CODIGO: llegó con un link de /vincular
    if (command == "/start" && !is_group) {
        size_t sp = text.find(' ');
        std::string code = sp == std::string::npos ? "" : Trim(text.substr(sp + 1));
        if (!code.empty() && UseInvite(code, chat_id, from)) {
            return;
        }
    }
    // /start: un familiar nuevo pide permiso; uno autorizado recibe un saludo
    if (command == "/start" && !is_group) {
        if (IsAllowed(chat_id)) {
            SendMessage("Hola " + from + ". Ya tenés autorización: lo que escribas o mandes en audio le "
                        "llega al XiaoZhi.", chat_id);
        } else {
            RequestAuthorization(chat_id, from);
        }
        return;
    }
    // /id: muestra el chat_id (útil para configurar)
    if (command == "/id" || command == "/start") {
        std::string reply = is_group ? "El chat_id de este grupo es: " + chat_id
                                     : "Hola " + from + ". Tu chat_id es: " + chat_id;
        reply += IsAllowed(chat_id) ? "\nEstá autorizado." : "";
        SendMessage(reply, chat_id);
        return;
    }

    // Comandos del administrador (en su chat privado)
    if (!command.empty()) {
        if (chat_id == default_chat_id_ && !is_group) {
            HandleAdminCommand(text);
        }
        return;
    }

    if (!IsAllowed(chat_id)) {
        if (!is_group) {
            RequestAuthorization(chat_id, from);
        } else {
            ESP_LOGW(TAG, "Grupo %s no autorizado, mensaje ignorado", chat_id.c_str());
        }
        return;
    }

    // Para la usuaria, cada familiar se llama como figura en la lista
    if (!is_group) {
        std::string alias = ContactName(chat_id);
        if (!alias.empty()) {
            from = alias;
        } else if (IsContact(chat_id)) {
            // Contacto sin nombre todavía (el administrador al principio): usar el de Telegram
            std::lock_guard<std::mutex> lock(data_mutex_);
            for (auto& c : contacts_) {
                if (c.chat_id == chat_id) c.name = from;
            }
            SaveContacts();
        }
    }

    // Nota de voz
    cJSON* voice = cJSON_GetObjectItem(message, "voice");
    if (cJSON_IsObject(voice)) {
        cJSON* file_id = cJSON_GetObjectItem(voice, "file_id");
        cJSON* duration = cJSON_GetObjectItem(voice, "duration");
        if (cJSON_IsString(file_id)) {
            HandleIncomingVoice(chat_id, from, file_id->valuestring,
                                cJSON_IsNumber(duration) ? duration->valueint : 0);
        }
        return;
    }

    if (text.empty()) {
        // En el grupo hay avisos (alguien entró, fotos, stickers...): se ignoran en silencio
        if (!is_group) {
            SendMessage("Por ahora puedo recibir mensajes de texto y notas de voz.", chat_id);
        }
        return;
    }

    Deliver(TelegramMessage{chat_id, from, text});
}

bool TelegramBot::IsAllowed(const std::string& chat_id) {
    for (const auto& id : allowed_chat_ids_) {
        if (id == chat_id) return true;
    }
    return IsContact(chat_id);
}

void TelegramBot::Deliver(const TelegramMessage& msg) {
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        recent_.push_back(msg);
        while (recent_.size() > kMaxRecent) recent_.pop_front();
#if CONFIG_TELEGRAM_SPEAK_INCOMING
        announce_pending_ = true;
#endif
    }

    if (Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
        ShowAlert(msg.from + ": " + msg.text, true);
    }
}

// Cuando el equipo está libre, "despierta" al asistente con una frase corta
// (el servidor sólo acepta textos cortos como palabra de activación).
// El asistente entonces llama a self.telegram.get_messages y lee los nuevos.
void TelegramBot::ProcessPendingVoice() {
#if CONFIG_TELEGRAM_SPEAK_INCOMING
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle) return;

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (!announce_pending_) return;
        announce_pending_ = false;
    }

    // Dejar que se escuche el sonido de aviso antes de abrir la conversación
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (app.GetDeviceState() != kDeviceStateIdle) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        announce_pending_ = true;  // reintentar más tarde
        return;
    }

    ESP_LOGI(TAG, "Avisando al asistente: %s", CONFIG_TELEGRAM_WAKE_TEXT);
    app.WakeWordInvoke(CONFIG_TELEGRAM_WAKE_TEXT);

    // Esperar a que arranque la conversación antes de volver a intentar
    for (int i = 0; i < 20 && app.GetDeviceState() == kDeviceStateIdle; i++) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
#endif
}

// Devuelve los mensajes nuevos (y los ya leídos aparte) y marca los nuevos como leídos.
std::string TelegramBot::TakeMessagesJson() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    cJSON* root = cJSON_CreateObject();
    cJSON* fresh = cJSON_AddArrayToObject(root, "new_messages");
    cJSON* old = cJSON_AddArrayToObject(root, "already_read");
    for (auto& m : recent_) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "from", m.from.c_str());
        cJSON_AddStringToObject(item, "text", m.text.c_str());
        cJSON_AddItemToArray(m.unread ? fresh : old, item);
        m.unread = false;
    }
    char* s = cJSON_PrintUnformatted(root);
    std::string out = s ? s : "{}";
    if (s) cJSON_free(s);
    cJSON_Delete(root);
    return out;
}

// ---------------------------------------------------------------------------
// Familia: contactos autorizados por el administrador desde Telegram
// ---------------------------------------------------------------------------

void TelegramBot::LoadContacts() {
    Settings settings("telegram", false);
    std::string json = settings.GetString("contacts", "[]");
    cJSON* arr = cJSON_Parse(json.c_str());
    std::lock_guard<std::mutex> lock(data_mutex_);
    contacts_.clear();
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, arr) {
        cJSON* id = cJSON_GetObjectItem(item, "id");
        cJSON* name = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsString(id)) {
            contacts_.push_back({id->valuestring, cJSON_IsString(name) ? name->valuestring : ""});
        }
    }
    if (arr) cJSON_Delete(arr);

    std::string inv = settings.GetString("invites", "{}");
    cJSON* obj = cJSON_Parse(inv.c_str());
    invites_.clear();
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, obj) {
        if (cJSON_IsString(it) && it->string) invites_[it->string] = it->valuestring;
    }
    if (obj) cJSON_Delete(obj);
    ESP_LOGI(TAG, "Familia: %u contactos, %u invitaciones pendientes", (unsigned)contacts_.size(),
             (unsigned)invites_.size());
}

// Lee familia.txt: una persona por línea, "Nombre = chat_id". Las líneas con # se ignoran.
void TelegramBot::LoadFamilyFile() {
    std::string text(familia_txt_start, familia_txt_end - familia_txt_start);
    std::lock_guard<std::mutex> lock(data_mutex_);
    int count = 0;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = Trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (!line.empty() && line.back() == '\0') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = Trim(line.substr(0, eq));
        std::string id = Trim(line.substr(eq + 1));
        if (name.empty() || id.empty()) continue;
        bool found = false;
        for (auto& c : contacts_) {
            if (c.chat_id == id) {
                c.name = name;  // el nombre del archivo manda
                c.from_file = true;
                found = true;
            }
        }
        if (!found) contacts_.push_back({id, name, true});
        count++;
    }
    ESP_LOGI(TAG, "familia.txt: %d personas", count);
}

void TelegramBot::SaveContacts() {
    cJSON* arr = cJSON_CreateArray();
    for (auto& c : contacts_) {
        if (c.from_file) continue;  // esos viven en familia.txt
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "id", c.chat_id.c_str());
        cJSON_AddStringToObject(item, "name", c.name.c_str());
        cJSON_AddItemToArray(arr, item);
    }
    char* json = cJSON_PrintUnformatted(arr);
    Settings settings("telegram", true);
    settings.SetString("contacts", json ? json : "[]");
    if (json) cJSON_free(json);
    cJSON_Delete(arr);
}

bool TelegramBot::IsContact(const std::string& chat_id) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (auto& c : contacts_) {
        if (c.chat_id == chat_id) return true;
    }
    return false;
}

std::string TelegramBot::ContactName(const std::string& chat_id) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (auto& c : contacts_) {
        if (c.chat_id == chat_id) return c.name;
    }
    return "";
}

std::string TelegramBot::ContactNamesList() {
    std::lock_guard<std::mutex> lock(data_mutex_);
    std::string out;
    for (auto& c : contacts_) {
        if (c.name.empty()) continue;
        if (!out.empty()) out += ", ";
        out += c.name;
    }
    return out;
}

// "to" puede ser un nombre ("Julio", "la nena"), "todos"/"la familia", o vacío.
bool TelegramBot::ResolveRecipients(const std::string& to, std::vector<std::string>& ids,
                                    std::string& names, std::string& error) {
    ids.clear();
    names.clear();
    std::string q = Normalize(to);
    std::lock_guard<std::mutex> lock(data_mutex_);
    if (contacts_.empty()) {
        error = "There are no family members authorized yet.";
        return false;
    }
    auto all = [&]() {
        for (auto& c : contacts_) {
            ids.push_back(c.chat_id);
        }
        names = "toda la familia";
        return true;
    };
    if (q.empty()) {
        if (contacts_.size() == 1) {
            ids.push_back(contacts_[0].chat_id);
            names = contacts_[0].name;
            return true;
        }
    } else if ((" " + q + " ").find(" todos ") != std::string::npos ||
               (" " + q + " ").find(" todas ") != std::string::npos ||
               q.find("familia") != std::string::npos || q.find("todo el mundo") != std::string::npos ||
               q == "all" || q == "everyone") {
        return all();
    } else {
        for (auto& c : contacts_) {
            std::string n = Normalize(c.name);
            if (n.empty()) continue;
            std::string first = n.substr(0, n.find(' '));
            if (n == q || first == q || q.find(n) != std::string::npos ||
                (q.size() >= 3 && n.rfind(q, 0) == 0)) {
                ids.push_back(c.chat_id);
                if (!names.empty()) names += ", ";
                names += c.name;
            }
        }
        if (!ids.empty()) return true;
    }
    std::string list;
    for (auto& c : contacts_) {
        if (c.name.empty()) continue;
        if (!list.empty()) list += ", ";
        list += c.name;
    }
    error = (q.empty() ? std::string("The user did not say who the message is for. ")
                       : "Nobody called '" + to + "' in the family list. ") +
            "Ask the user, in Spanish, who it is for. Family members: " + list +
            ". They can also say 'a todos'.";
    return false;
}

void TelegramBot::SaveInvites() {
    cJSON* obj = cJSON_CreateObject();
    for (auto& kv : invites_) cJSON_AddStringToObject(obj, kv.first.c_str(), kv.second.c_str());
    char* json = cJSON_PrintUnformatted(obj);
    Settings settings("telegram", true);
    settings.SetString("invites", json ? json : "{}");
    if (json) cJSON_free(json);
    cJSON_Delete(obj);
}

std::string TelegramBot::BotUsername() {
    if (!bot_username_.empty()) return bot_username_;
    std::string response;
    if (ApiCall("getMe", "{}", response)) {
        cJSON* root = cJSON_Parse(response.c_str());
        if (root) {
            cJSON* u = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "result"), "username");
            if (cJSON_IsString(u)) bot_username_ = u->valuestring;
            cJSON_Delete(root);
        }
    }
    return bot_username_;
}

// Un familiar abrió el link de /vincular: queda en la familia con el nombre elegido
bool TelegramBot::UseInvite(const std::string& code, const std::string& chat_id, const std::string& tg_name) {
    std::string name;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto it = invites_.find(code);
        if (it == invites_.end()) return false;
        name = it->second;
        invites_.erase(it);
        SaveInvites();
    }
    bool already = IsContact(chat_id);
    if (!already) {
        ApproveContact(chat_id, name);
    }
    ESP_LOGI(TAG, "Invitación usada: %s (%s) como %s", tg_name.c_str(), chat_id.c_str(), name.c_str());
    SendMessage("¡Hola " + tg_name + "! Ya formás parte de la familia del XiaoZhi. Lo que escribas o "
                "mandes en audio le llega, y también vas a recibir sus mensajes.", chat_id);
    SendToAdmin(already ? tg_name + " abrió la invitación de \"" + name + "\", pero ya estaba en la familia."
                        : "Vinculado: " + tg_name + " ya está en la familia como \"" + name + "\".");
    if (!already) SendFamilyFile("familia.txt actualizado. Si querés tenerlo también en la PC, guardalo en C:\\xiaozhi-Telegram\\main\\telegram reemplazando el anterior. (No es obligatorio: el XiaoZhi ya lo tiene guardado.)");
    return true;
}

void TelegramBot::SendToAdmin(const std::string& text) {
    if (!default_chat_id_.empty()) SendMessage(text, default_chat_id_);
}

std::string TelegramBot::FetchChatName(const std::string& chat_id) {
    std::string response;
    if (!ApiCall("getChat", "{\"chat_id\":\"" + chat_id + "\"}", response)) return "";
    std::string name;
    cJSON* root = cJSON_Parse(response.c_str());
    if (root) {
        cJSON* fn = cJSON_GetObjectItem(cJSON_GetObjectItem(root, "result"), "first_name");
        if (cJSON_IsString(fn)) name = fn->valuestring;
        cJSON_Delete(root);
    }
    return name;
}

void TelegramBot::RequestAuthorization(const std::string& chat_id, const std::string& name, bool tell_person) {
    if (chat_id == default_chat_id_) return;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (rejected_.count(chat_id) || pending_requests_.count(chat_id)) return;
        pending_requests_[chat_id] = name;
    }
    ESP_LOGI(TAG, "Pedido de autorización de %s (%s)", name.c_str(), chat_id.c_str());
    if (tell_person) {
        SendMessage("Hola " + name + ". Tu número es: " + chat_id + "\nLe pedí al administrador que te "
                    "autorice. Te aviso apenas esté listo.", chat_id);
    }
    std::string markup = "{\"inline_keyboard\":[[{\"text\":\"Aprobar\",\"callback_data\":\"ok:" + chat_id +
                         "\"},{\"text\":\"Rechazar\",\"callback_data\":\"no:" + chat_id + "\"}]]}";
    SendMessage(name + " quiere sumarse a la familia del XiaoZhi (número " + chat_id + ").\n"
                "¿Le das autorización?", default_chat_id_, markup);
}

void TelegramBot::ApproveContact(const std::string& chat_id, std::string name) {
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (auto& c : contacts_) {
        if (c.chat_id == chat_id) return;
    }
    contacts_.push_back({chat_id, Trim(name)});
    pending_requests_.erase(chat_id);
    rejected_.erase(chat_id);
    SaveContacts();
    ESP_LOGI(TAG, "Familia: agregado %s (%s)", name.c_str(), chat_id.c_str());
}

void TelegramBot::HandleCallbackQuery(cJSON* callback) {
    cJSON* id = cJSON_GetObjectItem(callback, "id");
    cJSON* data = cJSON_GetObjectItem(callback, "data");
    std::string from_id = JsonIdToString(cJSON_GetObjectItem(cJSON_GetObjectItem(callback, "from"), "id"));

    // Cerrar el "relojito" del botón en Telegram
    if (cJSON_IsString(id)) {
        std::string resp;
        ApiCall("answerCallbackQuery", std::string("{\"callback_query_id\":\"") + id->valuestring + "\"}", resp);
    }
    if (from_id != default_chat_id_ || !cJSON_IsString(data)) return;  // sólo el administrador

    std::string d = data->valuestring;
    if (d.size() < 4 || d[2] != ':') return;
    std::string action = d.substr(0, 2);
    std::string target = d.substr(3);

    std::string name;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        auto it = pending_requests_.find(target);
        if (it != pending_requests_.end()) name = it->second;
    }
    if (name.empty()) name = FetchChatName(target);
    if (name.empty()) name = "Familiar";

    if (action == "ok") {
        if (IsContact(target)) {
            SendToAdmin(name + " ya estaba en la familia.");
            return;
        }
        ApproveContact(target, name);
        SendToAdmin("Listo: " + name + " ya está en la familia.\n"
                    "Para cambiar el nombre que usa el XiaoZhi: /nombre " + name + " = Nuevo nombre");
        SendMessage("¡Listo, " + name + "! Ya tenés autorización. Lo que escribas o mandes en audio "
                    "le llega al XiaoZhi.", target);
        SendFamilyFile("familia.txt actualizado. Si querés tenerlo también en la PC, guardalo en C:\\xiaozhi-Telegram\\main\\telegram reemplazando el anterior. (No es obligatorio: el XiaoZhi ya lo tiene guardado.)");
    } else if (action == "no") {
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            pending_requests_.erase(target);
            rejected_.insert(target);
        }
        SendToAdmin("Rechazado: " + name + ".");
    }
}

// Comandos del administrador:
//   /familia                      -> lista
//   /quitar Nombre                -> saca a alguien
//   /nombre Nombre = Nuevo nombre -> cambia cómo lo nombra el XiaoZhi
//   /ayuda
bool TelegramBot::HandleAdminCommand(const std::string& text) {
    size_t sp = text.find(' ');
    std::string cmd = text.substr(0, std::min(sp, text.find('@')));
    std::string arg = sp == std::string::npos ? "" : Trim(text.substr(sp + 1));

    auto find = [this](const std::string& who) -> int {
        std::string q = Normalize(who);
        for (size_t i = 0; i < contacts_.size(); i++) {
            if (contacts_[i].chat_id == who || Normalize(contacts_[i].name) == q) return (int)i;
        }
        return -1;
    };

    if (cmd == "/vincular") {
        if (arg.empty()) {
            SendToAdmin("Usalo así: /vincular Ana\n(con el nombre que usa mamá para esa persona)");
            return true;
        }
        static const char kChars[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
        std::string code;
        for (int i = 0; i < 10; i++) code.push_back(kChars[esp_random() % (sizeof(kChars) - 1)]);
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            invites_[code] = arg;
            SaveInvites();
        }
        std::string user = BotUsername();
        if (user.empty()) {
            SendToAdmin("No pude armar el link (no sé el nombre del bot). Probá de nuevo en un rato.");
            return true;
        }
        SendToAdmin("Invitación para " + arg + ":\n\nhttps://t.me/" + user + "?start=" + code +
                    "\n\nMandale este link a " + arg + ". Cuando lo abra y toque \"Iniciar\", queda "
                    "en la familia como \"" + arg + "\". El link sirve una sola vez.");
        return true;
    }
    if (cmd == "/invitaciones") {
        std::string out;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            for (auto& kv : invites_) out += "\n- " + kv.second;
        }
        SendToAdmin(out.empty() ? "No hay invitaciones pendientes." : "Invitaciones sin usar:" + out +
                    "\n\nPara anularlas todas: /anular");
        return true;
    }
    if (cmd == "/anular") {
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            invites_.clear();
            SaveInvites();
        }
        SendToAdmin("Listo, anulé todas las invitaciones sin usar.");
        return true;
    }
    if (cmd == "/exportar") {
        SendFamilyFile("familia.txt con toda la familia. Guardalo en "
                       "C:\\xiaozhi-Telegram\\main\\telegram reemplazando el anterior.");
        return true;
    }
    if (cmd == "/familia" || cmd == "/lista") {
        std::string out = "Familia autorizada:";
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            for (auto& c : contacts_) {
                out += "\n- " + (c.name.empty() ? std::string("(sin nombre)") : c.name) +
                       (c.chat_id == default_chat_id_ ? " (administrador)" : "") +
                       (c.from_file ? " [familia.txt]" : "");
            }
        }
        SendToAdmin(out);
        return true;
    }
    if (cmd == "/quitar") {
        std::string removed;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            int i = find(arg);
            if (i >= 0 && contacts_[i].from_file) {
                removed = "#archivo";
            } else if (i >= 0 && contacts_[i].chat_id != default_chat_id_) {
                removed = contacts_[i].name;
                contacts_.erase(contacts_.begin() + i);
                SaveContacts();
            }
        }
        if (removed == "#archivo") {
            SendToAdmin("\"" + arg + "\" está en familia.txt: borralo de ese archivo y volvé a grabar el equipo.");
        } else {
            SendToAdmin(removed.empty() ? "No encontré a \"" + arg + "\" (o es el administrador). Mirá /familia"
                                        : "Listo, " + removed + " ya no está en la familia.");
            if (!removed.empty()) SendFamilyFile("familia.txt actualizado. Si querés tenerlo también en la PC, guardalo en C:\\xiaozhi-Telegram\\main\\telegram reemplazando el anterior. (No es obligatorio: el XiaoZhi ya lo tiene guardado.)");
        }
        return true;
    }
    if (cmd == "/nombre") {
        size_t eq = arg.find('=');
        if (eq == std::string::npos) {
            SendToAdmin("Usalo así: /nombre Ana = La nena");
            return true;
        }
        std::string who = Trim(arg.substr(0, eq));
        std::string new_name = Trim(arg.substr(eq + 1));
        bool ok = false;
        {
            std::lock_guard<std::mutex> lock(data_mutex_);
            int i = find(who);
            if (i >= 0 && contacts_[i].from_file) {
                i = -2;
            } else if (i >= 0 && !new_name.empty()) {
                contacts_[i].name = new_name;
                SaveContacts();
                ok = true;
            }
        }
        if (ok) SendFamilyFile("familia.txt actualizado. Si querés tenerlo también en la PC, guardalo en C:\\xiaozhi-Telegram\\main\\telegram reemplazando el anterior. (No es obligatorio: el XiaoZhi ya lo tiene guardado.)");
        SendToAdmin(ok ? "Listo: ahora el XiaoZhi usa el nombre \"" + new_name + "\"."
                       : "No encontré a \"" + who + "\" en la lista, o está en familia.txt "
                         "(ahí se cambia el nombre editando el archivo). Mirá /familia");
        return true;
    }
    SendToAdmin("Comandos:\n/vincular Nombre - link de invitación para sumar a alguien\n"
                "/invitaciones - ver invitaciones sin usar\n/anular - anular invitaciones sin usar\n"
                "/familia - ver quiénes están\n/exportar - recibir familia.txt actualizado\n"
                "/quitar Nombre - sacar a alguien\n"
                "/nombre Nombre = Nuevo nombre - cómo lo llama el XiaoZhi\n"
                "También: si alguien le escribe al bot sin invitación, te llega un aviso para aprobarlo.");
    return true;
}

// ---------------------------------------------------------------------------
// Notas de voz: recepción
// ---------------------------------------------------------------------------

bool TelegramBot::DownloadFile(const std::string& file_id, std::string& data) {
    // 1) Pedir la ruta del archivo
    cJSON* req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "file_id", file_id.c_str());
    char* body = cJSON_PrintUnformatted(req);
    std::string json_body = body ? body : "";
    if (body) cJSON_free(body);
    cJSON_Delete(req);

    std::string response;
    if (!ApiCall("getFile", json_body, response)) return false;

    std::string file_path;
    cJSON* root = cJSON_Parse(response.c_str());
    if (root != nullptr) {
        cJSON* result = cJSON_GetObjectItem(root, "result");
        cJSON* path = cJSON_GetObjectItem(result, "file_path");
        if (cJSON_IsString(path)) file_path = path->valuestring;
        cJSON_Delete(root);
    }
    if (file_path.empty()) {
        ESP_LOGW(TAG, "getFile sin file_path");
        return false;
    }

    // 2) Descargarlo
    std::string url = std::string("https://api.telegram.org/file/bot") + CONFIG_TELEGRAM_BOT_TOKEN + "/" + file_path;
    if (!HttpRequest("GET", url, "", "", data, 30000)) return false;
    ESP_LOGI(TAG, "Audio descargado: %u bytes", (unsigned)data.size());
    return !data.empty();
}

void TelegramBot::HandleIncomingVoice(const std::string& chat_id, const std::string& from,
                                      const std::string& file_id, int duration_s) {
    if (duration_s > CONFIG_TELEGRAM_VOICE_MAX_SECONDS) {
        SendMessage("El audio es muy largo. Máximo " + std::to_string(CONFIG_TELEGRAM_VOICE_MAX_SECONDS) +
                    " segundos.", chat_id);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        TelegramMessage m{chat_id, from, "(mensaje de voz de " + std::to_string(duration_s) + " segundos, ya reproducido)"};
        m.unread = false;  // se reproduce ahora, el asistente no tiene que leerlo
        recent_.push_back(m);
        while (recent_.size() > kMaxRecent) recent_.pop_front();
    }

    std::string ogg;
    if (!DownloadFile(file_id, ogg)) {
        SendMessage("No pude descargar el audio.", chat_id);
        return;
    }

    auto& app = Application::GetInstance();
    ShowAlert(from + " te mandó un audio", true);
    vTaskDelay(pdMS_TO_TICKS(1500));  // dejar sonar el aviso

    ESP_LOGI(TAG, "Reproduciendo audio de %s (%d s)", from.c_str(), duration_s);
    app.GetAudioService().PlaySound(ogg);  // se bloquea mientras carga el audio
    WaitPlaybackIdle((duration_s + 5) * 1000);
    DismissAlertLater(1000);
}

// ---------------------------------------------------------------------------
// Notas de voz: envío
// ---------------------------------------------------------------------------

// El asistente pidió grabar: esperamos a que termine de hablar, cerramos la
// conversación y, con el equipo en reposo, grabamos.
void TelegramBot::HandlePendingRecording() {
    static int64_t listening_since = 0;
    auto& app = Application::GetInstance();
    const int64_t now = esp_timer_get_time();

    if (now - record_requested_us_ > 60LL * 1000 * 1000) {
        ESP_LOGW(TAG, "Grabación cancelada: la conversación no terminó a tiempo");
        record_pending_ = false;
        listening_since = 0;
        return;
    }

    auto state = app.GetDeviceState();
    if (state == kDeviceStateIdle) {
        record_pending_ = false;
        listening_since = 0;
        RecordAndSendVoice(CONFIG_TELEGRAM_RECORD_MAX_SECONDS);
        return;
    }

    if (state == kDeviceStateListening && app.GetAudioService().IsPlaybackIdle()) {
        if (listening_since == 0) listening_since = now;
        if (now - listening_since > 1200 * 1000 && now - record_requested_us_ > 2000 * 1000) {
            ESP_LOGI(TAG, "Cerrando la conversación para grabar el audio");
            app.ToggleChatState();
            listening_since = 0;
        }
    } else {
        listening_since = 0;
    }
}

void TelegramBot::RecordAndSendVoice(int max_seconds) {
    if (default_chat_id_.empty()) return;
    auto& app = Application::GetInstance();
    auto& audio = app.GetAudioService();

    ShowAlert("Al escuchar el pitido, comenzá a hablar", false);

    // Pausar la detección de la palabra de activación para usar el micrófono
    audio.EnableWakeWordDetection(false);
    vTaskDelay(pdMS_TO_TICKS(250));
    LoudBeep();
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "Grabando (máximo %d s, se termina con el botón)...", max_seconds);
    stop_recording_ = false;
    recording_started_us_ = esp_timer_get_time();
    recording_ = true;

    RecordJob job;
    job.seconds = max_seconds;
    job.stop = &stop_recording_;
    job.waiter = xTaskGetCurrentTaskHandle();
    if (xTaskCreate(RecordTask, "tg_record", 2048 * 12, &job, 4, nullptr) == pdPASS) {
        int elapsed = 0;
        auto show = [&app](const std::string& text) {
            app.Schedule([text]() {
                auto display = Board::GetInstance().GetDisplay();
                if (display) display->SetChatMessage("system", text.c_str());
            });
        };
        show("Grabando... Presioná el botón para enviar");
        while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
            elapsed++;
            int remaining = max_seconds - elapsed;
            if (remaining <= 10 && remaining > 0) {
                show("Presioná el botón para enviar. Quedan " + std::to_string(remaining) + " s");
            }
        }
    } else {
        ESP_LOGE(TAG, "No hay memoria para la tarea de grabación");
    }
    recording_ = false;

    if (app.GetDeviceState() == kDeviceStateIdle) {
        audio.EnableWakeWordDetection(true);
    }
    LoudBeep();  // pitido de fin

    const float seconds_recorded = job.lens.size() * kOpusFrameMs / 1000.0f;
    ESP_LOGI(TAG, "Grabación terminada (%s): %.1f s",
             stop_recording_ ? "botón" : "tiempo máximo", seconds_recorded);

    if (seconds_recorded < 1.0f) {
        ShowAlert("Mensaje cancelado", false);
        DismissAlertLater(3000);
        return;
    }

    std::string ogg = BuildOggOpus(job.data, job.lens, kRecordSampleRate, kOpusFrameMs);
    ESP_LOGI(TAG, "Audio grabado: %u paquetes, %u bytes", (unsigned)job.lens.size(), (unsigned)ogg.size());

    std::vector<std::string> targets;
    std::string target_names;
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        targets = record_targets_;
        target_names = record_target_names_;
    }
    if (targets.empty() && !default_chat_id_.empty()) targets.push_back(default_chat_id_);

    ShowAlert("Enviando mensaje...", false);
    int sent = 0;
    for (auto& t : targets) {
        if (SendVoice(ogg, t)) sent++;
    }
    bool ok = sent == (int)targets.size();
    ShowAlert(ok ? "Mensaje enviado a " + target_names
                 : (sent > 0 ? "Se envió sólo a algunos" : "No se pudo enviar el mensaje"), false);
    DismissAlertLater(4000);
}

bool TelegramBot::IsVoiceBusy() const {
    return record_pending_ || recording_;
}

void TelegramBot::HandleButtonDuringVoice() {
    if (!recording_) {
        // Todavía está hablando el asistente (dando la instrucción): no cortarlo
        ESP_LOGI(TAG, "Botón ignorado: esperando el pitido para grabar");
        return;
    }
    // Evitar que un toque justo al empezar corte la grabación
    if (esp_timer_get_time() - recording_started_us_ < 1000 * 1000) {
        ESP_LOGI(TAG, "Botón ignorado: la grabación recién empieza");
        return;
    }
    ESP_LOGI(TAG, "Botón: terminar grabación y enviar");
    stop_recording_ = true;
}

bool TelegramBot::SendFile(const std::string& method, const std::string& field, const std::string& filename,
                           const std::string& content_type, const std::string& data, const std::string& chat_id,
                           const std::string& caption) {
    std::string target = chat_id.empty() ? default_chat_id_ : chat_id;
    if (target.empty()) return false;

    const std::string boundary = "----XiaozhiTelegramBoundary7d2f";
    std::string body;
    body.reserve(data.size() + 512 + caption.size());
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n";
    body += target + "\r\n";
    if (!caption.empty()) {
        body += "--" + boundary + "\r\n";
        body += "Content-Disposition: form-data; name=\"caption\"\r\n\r\n";
        body += caption + "\r\n";
    }
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + field + "\"; filename=\"" + filename + "\"\r\n";
    body += "Content-Type: " + content_type + "\r\n\r\n";
    body += data;
    body += "\r\n--" + boundary + "--\r\n";

    std::string url = std::string("https://api.telegram.org/bot") + CONFIG_TELEGRAM_BOT_TOKEN + "/" + method;
    std::string response;
    bool ok = HttpRequest("POST", url, "multipart/form-data; boundary=" + boundary, body, response, 30000);
    ESP_LOGI(TAG, "%s -> %s: %s", method.c_str(), target.c_str(), ok ? "OK" : "ERROR");
    return ok;
}

bool TelegramBot::SendVoice(const std::string& ogg, const std::string& chat_id) {
    return SendFile("sendVoice", "voice", "voice.ogg", "audio/ogg", ogg, chat_id);
}

// familia.txt con todos: los del archivo y los aprobados desde Telegram
std::string TelegramBot::BuildFamilyFile() {
    std::string out = kFamilyFileHeader;
    out += "\n";
    std::lock_guard<std::mutex> lock(data_mutex_);
    for (auto& c : contacts_) {
        if (c.name.empty()) continue;
        out += c.name + " = " + c.chat_id + "\n";
    }
    return out;
}

void TelegramBot::SendFamilyFile(const std::string& caption) {
    SendFile("sendDocument", "document", "familia.txt", "text/plain", BuildFamilyFile(), default_chat_id_,
             caption);
}

// ---------------------------------------------------------------------------
// Herramientas para el asistente
// ---------------------------------------------------------------------------

void TelegramBot::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.telegram.send_voice_message",
        "Send a message by Telegram to a family member (or to all of them) as a VOICE recording of "
        "the user. This is the DEFAULT way to send any message: use it whenever the user wants to "
        "send, tell or write something to someone (e.g. 'mandale un mensaje a Julio', 'decile a Ana "
        "que...', 'mandale un audio a todos'). 'to' = the name as the user said it ('Julio', "
        "'la nena'), or 'todos' for everybody; leave it empty if the user did not say who. Call it "
        "right away, without asking what the message says. If it returns an error, ask the user "
        "who it is for, as the error explains. If it returns OK, say EXACTLY the Spanish sentence "
        "it returns and nothing else. The device then beeps, records the user's voice until they "
        "press the button, and sends it.",
        PropertyList({Property("to", kPropertyTypeString, std::string(""))}),
        [this](const PropertyList& properties) -> ReturnValue {
            if (record_pending_ || recording_) {
                return std::string("A recording is already in progress");
            }
            std::vector<std::string> ids;
            std::string names, error;
            if (!ResolveRecipients(properties["to"].value<std::string>(), ids, names, error)) {
                return std::string("Error: ") + error;
            }
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                record_targets_ = ids;
                record_target_names_ = names;
            }
            record_requested_us_ = esp_timer_get_time();
            record_pending_ = true;
            return std::string("OK, the message is for ") + names +
                   ". Say exactly, in Spanish, and nothing else: 'Al escuchar el pitido, "
                   "comenzá a hablar. Cuando termines el mensaje, presioná el botón para enviarlo.'";
        });

    mcp.AddTool("self.telegram.send_message",
        "Send a WRITTEN text message by Telegram to a family member or to all ('todos'). Use it "
        "ONLY when the user explicitly asks for a written / text message ('mensaje escrito', "
        "'mensaje de texto', 'escribile'). For any other request to send a message use "
        "self.telegram.send_voice_message. 'to' = name as the user said it, 'todos', or empty if "
        "not said. Write the text in Spanish, in first person as the user said it.",
        PropertyList({Property("to", kPropertyTypeString, std::string("")),
                      Property("text", kPropertyTypeString)}),
        [this](const PropertyList& properties) -> ReturnValue {
            auto text = properties["text"].value<std::string>();
            if (Trim(text).empty()) {
                return std::string("Error: empty message");
            }
            std::vector<std::string> ids;
            std::string names, error;
            if (!ResolveRecipients(properties["to"].value<std::string>(), ids, names, error)) {
                return std::string("Error: ") + error;
            }
            {
                std::lock_guard<std::mutex> lock(data_mutex_);
                for (auto& id : ids) outbox_.push_back({id, text});
            }
            return std::string("OK, the message is being sent to ") + names + ". Confirm it briefly.";
        });

    mcp.AddTool("self.telegram.list_family",
        "List the family members the user can send Telegram messages to.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::string list = ContactNamesList();
            return list.empty() ? std::string("No family members yet") : list;
        });

    mcp.AddTool("self.telegram.get_messages",
        "Get the Telegram messages received by the device. Call it whenever the user says '"
        CONFIG_TELEGRAM_WAKE_TEXT "' or asks about Telegram messages. "
        "Messages come from different family members. Read every item of new_messages aloud word "
        "for word in Spanish, always saying who sent it (e.g. 'Ana te escribió: ...'). "
        "If new_messages is empty, say there are no new messages; "
        "only read already_read if the user asks for previous messages. Voice messages were "
        "already played on the speaker, just mention them.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return TakeMessagesJson();
        });
}

#else  // !CONFIG_TELEGRAM_ENABLE

void TelegramBot::Start() {}
bool TelegramBot::SendMessage(const std::string&, const std::string&, const std::string&) { return false; }
bool TelegramBot::IsVoiceBusy() const { return false; }
void TelegramBot::HandleButtonDuringVoice() {}

#endif  // CONFIG_TELEGRAM_ENABLE
