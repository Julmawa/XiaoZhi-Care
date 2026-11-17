#include "care_telegram_pair.h"
#include "care_telegram_repository.h"
#include "care_manager.h"
#include "care_restore.h"
#include "esp_random.h"
#include "esp_timer.h"
#include <mutex>
#include <vector>
#include <algorithm>

namespace xiaozhi_care {
namespace {
std::mutex mutex;
struct Challenge {
    std::string person, code, account;
    int64_t deadline = 0;
} current;
bool Available() {
    if (current.deadline > esp_timer_get_time()) return true;
    current = {};
    return false;
}

constexpr int64_t kRequestLifetimeUs = 15LL * 60 * 1000000;
constexpr size_t kMaxRequests = 8;
struct PendingRequest {
    TelegramPairRequest view;
    int64_t deadline_us = 0;
};
std::vector<PendingRequest> s_requests;

void PruneRequests() {
    const int64_t now = esp_timer_get_time();
    s_requests.erase(std::remove_if(s_requests.begin(), s_requests.end(),
        [now](const PendingRequest& req) { return req.deadline_us <= now; }), s_requests.end());
}

bool ValidAccount(const std::string& account) {
    return !account.empty() && account.size() <= 20 && account.front() != '0' &&
        account.find_first_not_of("0123456789") == std::string::npos &&
        account.find_first_not_of('0') != std::string::npos;
}

std::string SafeTelegramLabel(const std::string& value, size_t max_bytes) {
    std::string result;
    result.reserve(std::min(value.size(), max_bytes));
    for (size_t i = 0; i < value.size();) {
        const unsigned char lead = static_cast<unsigned char>(value[i]);
        if (lead < 32 || lead == 127) { ++i; continue; }
        const size_t width = lead < 0x80 ? 1 :
            (lead & 0xE0) == 0xC0 ? 2 :
            (lead & 0xF0) == 0xE0 ? 3 :
            (lead & 0xF8) == 0xF0 ? 4 : 0;
        if (!width) { ++i; continue; }
        if (i + width > value.size() || result.size() + width > max_bytes) break;
        bool valid = true;
        for (size_t j = 1; j < width; ++j)
            valid = valid && (static_cast<unsigned char>(value[i+j]) & 0xC0) == 0x80;
        if (!valid) { ++i; continue; }
        result.append(value, i, width);
        i += width;
    }
    return result;
}

std::string RandomRequestId() {
    unsigned char bytes[12];
    esp_fill_random(bytes, sizeof(bytes));
    std::string id;
    id.reserve(sizeof(bytes) * 2);
    for (auto b : bytes) {
        id += "0123456789abcdef"[b >> 4];
        id += "0123456789abcdef"[b & 15];
    }
    return id;
}
}
std::string StartTelegramPair(const std::string& person) {
    if (person.empty()) return {};
    std::lock_guard<std::mutex> lock(mutex);
    unsigned char bytes[16];
    esp_fill_random(bytes,sizeof(bytes));
    std::string code;
    code.reserve(2*sizeof(bytes));
    for (auto b : bytes) {
        code += "0123456789abcdef"[b >> 4];
        code += "0123456789abcdef"[b & 15];
    }
    current = {person,code,{},esp_timer_get_time()+300000000};
    return code;
}
TelegramPairStatus TelegramPairState(const std::string& person) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!Available() || current.person != person) return TelegramPairStatus::None;
    return current.account.empty() ? TelegramPairStatus::Waiting : TelegramPairStatus::Received;
}
bool ReceiveTelegramPair(const std::string& code, const std::string& account) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!Available() || !current.account.empty() || code != current.code ||
        account.empty() || account.size()>20 || account.find_first_not_of("0123456789")!=std::string::npos)
        return false;
    current.account = account;
    current.code.clear();
    return true;
}
// DP-049 R4: /vincular sin codigo crea solamente una solicitud pendiente.
bool ReceiveTelegramRequest(const std::string& account,
                            const std::string& name,
                            const std::string& username) {
    if (!ValidAccount(account)) return false;
    std::lock_guard<std::mutex> lock(mutex);
    PruneRequests();
    CareTelegramLink linked;
    const auto lookup = CareTelegramRepository().FindByTelegramId(account, linked);
    // Fail closed: nunca aceptar solicitudes cuando el repositorio tiene errores.
    if (lookup != CareTelegramError::NOT_FOUND) return false;
    for (const auto& pending : s_requests) {
        if (pending.view.account == account) return true; // idempotente, sin extender caducidad
    }
    if (s_requests.size() >= kMaxRequests) return false;
    const std::string safe_name = SafeTelegramLabel(name, 80);
    const std::string safe_user = SafeTelegramLabel(username, 32);
    TelegramPairRequest request{RandomRequestId(), account,
        safe_name.empty() ? "Cuenta de Telegram" : safe_name, safe_user};
    s_requests.push_back({std::move(request), esp_timer_get_time() + kRequestLifetimeUs});
    return true;
}
std::vector<TelegramPairRequest> PendingTelegramRequests() {
    std::lock_guard<std::mutex> lock(mutex);
    PruneRequests();
    std::vector<TelegramPairRequest> result;
    result.reserve(s_requests.size());
    for (const auto& entry : s_requests) result.push_back(entry.view);
    return result;
}
bool ApproveTelegramRequest(const std::string& person_id, const std::string& request_id) {
    if (person_id.empty() || request_id.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex);
    PruneRequests();
    auto pending = std::find_if(s_requests.begin(), s_requests.end(),
        [&](const PendingRequest& r) { return r.view.request_id == request_id; });
    if (pending == s_requests.end()) return false;
    CarePerson person;
    if (CareManager::GetInstance().GetPerson(person_id, person) != CareError::OK || !person.enabled)
        return false;
    CareTelegramRepository repo;
    CareTelegramLink existing;
    const auto account_state = repo.FindByTelegramId(pending->view.account, existing);
    if (account_state != CareTelegramError::NOT_FOUND) return false;
    const auto person_state = repo.LoadLink(person_id, existing);
    if (person_state != CareTelegramError::NOT_FOUND &&
        (person_state != CareTelegramError::OK ||
         existing.status != CareTelegramLinkStatus::REVOKED))
        return false; // reemplazar una vinculacion existente requiere revocacion explicita
    CareTelegramLink link;
    link.person_id = person_id;
    link.telegram_id = pending->view.account;
    link.data_generation = restore::Active().data;
    link.enabled = true;
    link.status = CareTelegramLinkStatus::AUTHORIZED;
    if (repo.SaveLink(link) != CareTelegramError::OK) return false;
    s_requests.erase(pending); // solo tras persistencia exitosa
    return true;
}
bool ApproveTelegramPair(const std::string& person_id) {
    std::lock_guard<std::mutex> lock(mutex);
    if (!Available() || current.person != person_id || current.account.empty()) return false;
    CarePerson person;
    if (CareManager::GetInstance().GetPerson(person_id,person) != CareError::OK || !person.enabled) return false;
    CareTelegramLink link;
    link.person_id = person_id;
    link.telegram_id = current.account;
    link.data_generation = restore::Active().data;
    link.enabled = true;
    link.status = CareTelegramLinkStatus::AUTHORIZED;
    if (CareTelegramRepository().SaveLink(link) != CareTelegramError::OK) return false;
    current = {};
    return true;
}
void CancelTelegramPair(const std::string& person) {
    std::lock_guard<std::mutex> lock(mutex);
    if (current.person == person) current = {};
}
bool ResolveTelegramSender(const std::string& account, std::string& display_name, bool* unavailable) {
    if (unavailable) *unavailable=false;
    CareTelegramLink link;
    const auto lookup=CareTelegramRepository().FindByTelegramId(account,link);
    if (lookup!=CareTelegramError::OK) {
        if (unavailable) *unavailable=lookup!=CareTelegramError::NOT_FOUND && lookup!=CareTelegramError::INVALID_ARGUMENT;
        return false;
    }
    if (!link.enabled || link.status!=CareTelegramLinkStatus::AUTHORIZED ||
        link.data_generation!=restore::Active().data) return false;
    CarePerson person;
    const auto loaded=CareManager::GetInstance().GetPerson(link.person_id,person);
    if (loaded!=CareError::OK) {
        if (unavailable) *unavailable=loaded!=CareError::NOT_FOUND;
        return false;
    }
    if (!person.enabled) return false;
    // R4: anunciar el apodo familiar, sin modificar el nombre legal almacenado.
    const std::string spoken_name = person.nickname.empty() ? person.name : person.nickname;
    std::string relationship = person.relationship;
    if (!relationship.empty() && relationship != "Otro" &&
        relationship != "Usuario principal" && relationship != "Principal") {
        const unsigned char first = static_cast<unsigned char>(relationship[0]);
        if (first >= 'A' && first <= 'Z') relationship[0] = static_cast<char>(first + ('a' - 'A'));
        display_name = "tu " + relationship + " " + spoken_name;
    } else {
        display_name = spoken_name;
    }
    return true;
}
}
