// telegram_bot.h
// Integración de Telegram (bot creado con @BotFather) para xiaozhi-esp32.
//
// Texto:
//  - Recibe mensajes, los muestra en pantalla y el asistente los lee en voz alta.
//  - El asistente envía mensajes con la herramienta MCP "self.telegram.send_message".
// Voz:
//  - Las notas de voz que llegan se reproducen por el parlante.
//  - "Mandá un audio por Telegram": el asistente llama a
//    "self.telegram.send_voice_message", se cierra la conversación, suena un pitido,
//    se graba del micrófono y se envía como nota de voz.
//
// Todo se configura en menuconfig: Xiaozhi Assistant -> Telegram Bot.

#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <functional>

struct cJSON;

struct TelegramMessage {
    std::string chat_id;
    std::string from;
    std::string text;
    bool unread = true;
};

// Familiar autorizado (chat privado con el bot)
struct TelegramContact {
    std::string chat_id;
    std::string name;  // nombre con el que la usuaria lo llama ("Julio", "la nena"...)
    bool from_file = false;  // viene de familia.txt (no se guarda en la memoria del equipo)
};

class TelegramBot {
public:
    static TelegramBot& GetInstance() {
        static TelegramBot instance;
        return instance;
    }

    TelegramBot(const TelegramBot&) = delete;
    TelegramBot& operator=(const TelegramBot&) = delete;

    // Llamar una sola vez, después de Application::Initialize() y antes de Run().
    void Start();

    // Envía un texto. Si chat_id está vacío usa el primer chat autorizado.
    bool SendMessage(const std::string& text, const std::string& chat_id = "",
                     const std::string& reply_markup_json = "");

    // Mensaje de voz en curso (esperando el pitido o grabando).
    // Mientras sea true, el botón táctil no abre una conversación.
    bool IsVoiceBusy() const;
    // Llamar al tocar el botón mientras IsVoiceBusy(): termina la grabación y la envía.
    void HandleButtonDuringVoice();

    // Encola un texto para enviarlo cuando el equipo esté libre (no bloquea).
    // Si chat_id está vacío va al administrador. Se puede llamar desde cualquier tarea.
    void QueueMessage(const std::string& text, const std::string& chat_id = "");

    // Extensiones (por ejemplo, la lista de compras del equipo de mamá).
    // Comando extra del administrador: devuelve true si lo manejó y completa el texto de respuesta
    // y, opcionalmente, botones (reply_markup en JSON).
    using ComandoExtra = std::function<bool(const std::string& cmd, const std::string& arg,
                                            std::string& texto, std::string& botones)>;
    // Botón extra (callback_data): devuelve true si lo manejó.
    using BotonExtra = std::function<bool(const std::string& data, std::string& texto, std::string& botones)>;
    void AddAdminCommand(ComandoExtra f, const std::string& ayuda);
    void AddCallbackHandler(BotonExtra f);
    // Traduce cómo la usuaria nombra a alguien ("mi hijo", "Julito") al nombre del contacto ("Julio").
    // Devuelve vacío si no sabe.
    void SetNameResolver(std::function<std::string(const std::string&)> f) { name_resolver_ = std::move(f); }

    // Emergencias: mensaje urgente que se manda enseguida, aunque XiaoZhi esté hablando.
    // chat_id vacío = administrador. reply_markup en JSON (botones), opcional.
    void QueueUrgent(const std::string& text, const std::string& chat_id = "",
                     const std::string& reply_markup_json = "");
    // Todos los familiares autorizados (incluye al administrador): (chat_id, nombre)
    std::vector<std::pair<std::string, std::string>> GetContacts();
    // Botón que puede tocar cualquier familiar (no sólo el administrador), por ejemplo "Voy yo".
    // Devuelve true si lo manejó; "respuesta" se le manda a quien lo tocó.
    using BotonFamilia = std::function<bool(const std::string& data, const std::string& chat_id,
                                            const std::string& nombre, std::string& respuesta)>;
    void AddFamilyCallbackHandler(BotonFamilia f);
    // Nombre del administrador en la lista de familiares (vacío si no figura)
    std::string AdminName() { return ContactName(default_chat_id_); }

private:
    TelegramBot() = default;

    static void TaskEntry(void* arg);
    void PollLoop();
    void RegisterMcpTools();
    void SkipOldUpdates();
    void PollOnce();
    void HandleUpdate(cJSON* update);
    void Deliver(const TelegramMessage& msg);
    void ProcessPendingVoice();
    bool IsAllowed(const std::string& chat_id);
    std::string TakeMessagesJson();

    // Familia (contactos autorizados por el administrador desde Telegram)
    void LoadContacts();
    void LoadFamilyFile();
    void SaveContacts();          // llamar con data_mutex_ tomado
    bool IsContact(const std::string& chat_id);
    std::string ContactName(const std::string& chat_id);
    std::string ContactNamesList();
    bool ResolveRecipients(const std::string& to, std::vector<std::string>& ids,
                           std::string& names, std::string& error);
    bool HandleAdminCommand(const std::string& text);
    void HandleCallbackQuery(cJSON* callback);
    void RequestAuthorization(const std::string& chat_id, const std::string& name, bool tell_person = true);
    void ApproveContact(const std::string& chat_id, std::string name);
    std::string FetchChatName(const std::string& chat_id);
    void SendToAdmin(const std::string& text);
    std::string BotUsername();
    void SaveInvites();           // llamar con data_mutex_ tomado
    bool UseInvite(const std::string& code, const std::string& chat_id, const std::string& tg_name);
    void FlushOutbox();

    // HTTP
    bool HttpRequest(const std::string& method, const std::string& url,
                     const std::string& content_type, const std::string& body,
                     std::string& response, int timeout_ms = 15000);
    bool ApiCall(const std::string& method, const std::string& json_body, std::string& response);

    // Voz
    void HandleIncomingVoice(const std::string& chat_id, const std::string& from,
                             const std::string& file_id, int duration_s);
    bool DownloadFile(const std::string& file_id, std::string& data);
    void HandlePendingRecording();
    void RecordAndSendVoice(int max_seconds);
    bool SendVoice(const std::string& ogg, const std::string& chat_id = "");
    bool SendFile(const std::string& method, const std::string& field, const std::string& filename,
                  const std::string& content_type, const std::string& data, const std::string& chat_id,
                  const std::string& caption = "");
    std::string BuildFamilyFile();
    void SendFamilyFile(const std::string& caption);

    bool started_ = false;
    long long offset_ = 0;
    std::vector<std::string> allowed_chat_ids_;
    std::string default_chat_id_;
    std::vector<ComandoExtra> comandos_extra_;
    std::vector<std::string> ayuda_extra_;
    std::vector<BotonExtra> botones_extra_;
    std::function<std::string(const std::string&)> name_resolver_;
    std::vector<BotonFamilia> botones_familia_;
    struct Urgente {
        std::string chat_id, text, markup;
        int intentos = 0;
    };
    std::deque<Urgente> urgent_;   // protegido por data_mutex_
    void FlushUrgent();

    std::mutex http_mutex_;   // una sola conexión TLS a la vez (ahorra RAM)
    std::mutex data_mutex_;   // protege recent_ y announce_pending_
    std::deque<TelegramMessage> recent_;
    bool announce_pending_ = false;  // hay mensajes nuevos para que el asistente lea
    std::deque<std::pair<std::string, std::string>> outbox_;  // (chat_id, texto) pendientes
    std::vector<TelegramContact> contacts_;
    std::map<std::string, std::string> pending_requests_;  // chat_id -> nombre
    std::set<std::string> rejected_;
    std::map<std::string, std::string> invites_;  // código de invitación -> nombre (/vincular)
    std::string bot_username_;
    std::vector<std::string> record_targets_;  // destinatarios del audio en curso
    std::string record_target_names_;

    // Grabación pedida por el asistente
    std::atomic<bool> record_pending_{false};
    std::atomic<int> record_seconds_{10};
    std::atomic<bool> recording_{false};
    std::atomic<bool> stop_recording_{false};
    int64_t recording_started_us_ = 0;
    int64_t record_requested_us_ = 0;
};
