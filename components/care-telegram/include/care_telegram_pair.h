#pragma once
#include <string>
#include <vector>

namespace xiaozhi_care {
enum class TelegramPairStatus { None, Waiting, Received };
// Solicitudes recibidas desde chats privados. Solo el administrador puede aprobarlas.
// Se mantienen en RAM (15 minutos) y nunca otorgan permiso automáticamente.
struct TelegramPairRequest {
    std::string request_id;  // nonce aleatorio; evita aprobar solicitudes ya reemplazadas
    std::string account;     // ID numerico de Telegram, no nombre visible
    std::string name;
    std::string username;
};
// Devuelve false si el ID es invalido, la cuenta ya esta vinculada,
// el almacenamiento no esta disponible o la cola esta llena.
bool ReceiveTelegramRequest(const std::string& account,
                            const std::string& name,
                            const std::string& username);
std::vector<TelegramPairRequest> PendingTelegramRequests();
// Autoriza EXCLUSIVAMENTE una solicitud pendiente desde una sesion web con CSRF.
bool ApproveTelegramRequest(const std::string& person_id,
                            const std::string& request_id);
std::string StartTelegramPair(const std::string& person);
TelegramPairStatus TelegramPairState(const std::string& person);
bool ReceiveTelegramPair(const std::string& code, const std::string& account);
bool ApproveTelegramPair(const std::string& person);
void CancelTelegramPair(const std::string& person);
bool ResolveTelegramSender(const std::string& account, std::string& display_name, bool* unavailable = nullptr);
}
