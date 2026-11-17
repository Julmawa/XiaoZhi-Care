#include "care_restore.h"
#include "care_telegram_repository.h"
#include "care_manager.h"

#include "cJSON.h"
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <mutex>
#include <string>
#include <vector>

namespace xiaozhi_care {
namespace {
constexpr char kDirectory[] = "/care_data";
constexpr char kPrefix[] = "tg_link_";
constexpr char kSuffix[] = ".json";
constexpr size_t kMaxFileBytes = 2048;
std::mutex g_mutex;

bool ValidPersonId(const std::string& id) {
    if (id.size() != 7 || id[0] != 'p') return false;
    for (size_t i = 1; i < id.size(); ++i) {
        if (id[i] < '0' || id[i] > '9') return false;
    }
    return id != "p000000";
}

bool ValidTelegramId(const std::string& id) {
    if (id.empty() || id.size() > 20 || id[0] == '0') return false;
    for (char c : id) {
        if (c < '0' || c > '9') return false;
    }
    return true;
}

bool ValidLink(const CareTelegramLink& link) {
    if (link.version != kTelegramLinkRecordVersion || !ValidPersonId(link.person_id) ||
        !ValidTelegramId(link.telegram_id)) return false;
    if (link.enabled && link.status != CareTelegramLinkStatus::AUTHORIZED) return false;
    return true;
}

std::string PathFor(const std::string& person_id) {
    return std::string(kDirectory) + "/" + kPrefix + person_id + kSuffix;
}

CareTelegramError Decode(const std::string& json, CareTelegramLink& result) {
    cJSON* root = cJSON_ParseWithLength(json.data(), json.size());
    if (!root) return CareTelegramError::SERIALIZATION_ERROR;
    const cJSON* version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON* person = cJSON_GetObjectItemCaseSensitive(root, "person_id");
    const cJSON* telegram = cJSON_GetObjectItemCaseSensitive(root, "telegram_id");
    const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(root, "enabled");
    const cJSON* status = cJSON_GetObjectItemCaseSensitive(root, "status");
    const cJSON* generation = cJSON_GetObjectItemCaseSensitive(root, "generation");
    bool valid = (!generation || (cJSON_IsNumber(generation) && generation->valuedouble >= 0 &&
        generation->valuedouble <= UINT16_MAX && generation->valuedouble == generation->valueint)) &&
        cJSON_IsNumber(version) && version->valuedouble == kTelegramLinkRecordVersion &&
        cJSON_IsString(person) && person->valuestring &&
        cJSON_IsString(telegram) && telegram->valuestring &&
        cJSON_IsBool(enabled) && cJSON_IsNumber(status) &&
        status->valuedouble >= 0 && status->valuedouble <= 2 &&
        status->valuedouble == status->valueint;
    CareTelegramLink parsed;
    if (valid) {
        parsed.data_generation = generation ? generation->valueint : 0;
        parsed.person_id = person->valuestring;
        parsed.telegram_id = telegram->valuestring;
        parsed.enabled = cJSON_IsTrue(enabled);
        parsed.status = static_cast<CareTelegramLinkStatus>(status->valueint);
        valid = ValidLink(parsed);
    }
    cJSON_Delete(root);
    if (!valid) return CareTelegramError::SERIALIZATION_ERROR;
    result = std::move(parsed);
    return CareTelegramError::OK;
}

CareTelegramError ReadFile(const std::string& path, std::string& content) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return errno == ENOENT ? CareTelegramError::NOT_FOUND : CareTelegramError::STORAGE_ERROR;
    if (std::fseek(f, 0, SEEK_END) != 0) { std::fclose(f); return CareTelegramError::STORAGE_ERROR; }
    long size = std::ftell(f);
    if (size <= 0 || static_cast<size_t>(size) > kMaxFileBytes || std::fseek(f, 0, SEEK_SET) != 0) {
        std::fclose(f);
        return CareTelegramError::SERIALIZATION_ERROR;
    }
    content.resize(static_cast<size_t>(size));
    size_t read = std::fread(&content[0], 1, content.size(), f);
    int closed = std::fclose(f);
    return read == content.size() && closed == 0 ? CareTelegramError::OK : CareTelegramError::STORAGE_ERROR;
}

CareTelegramError LoadUnlocked(const std::string& id, CareTelegramLink& result) {
    if (!ValidPersonId(id)) return CareTelegramError::INVALID_ARGUMENT;
    std::string json;
    CareTelegramError err = ReadFile(PathFor(id), json);
    if (err != CareTelegramError::OK) return err;
    err = Decode(json, result);
    if (err != CareTelegramError::OK) return err;
    return result.person_id == id ? CareTelegramError::OK : CareTelegramError::SERIALIZATION_ERROR;
}

CareTelegramError ListUnlocked(std::vector<CareTelegramLink>& result) {
    result.clear();
    DIR* dir = opendir(kDirectory);
    if (!dir) return CareTelegramError::STORAGE_ERROR;
    CareTelegramError outcome = CareTelegramError::OK;
    errno = 0;
    while (dirent* entry = readdir(dir)) {
        const std::string name(entry->d_name);
        // Una transaccion de escritura incompleta bloquea todas las
        // autorizaciones hasta su diagnostico y recuperacion controlada.
        // Evita que una revocacion interrumpida habilite el registro anterior.
        if (name.compare(0, std::strlen(kPrefix), kPrefix) == 0 &&
            (name.size() >= 4 &&
             (name.compare(name.size() - 4, 4, ".tmp") == 0 ||
              name.compare(name.size() - 4, 4, ".bak") == 0))) {
            outcome = CareTelegramError::STORAGE_ERROR;
            break;
        }
        const size_t pre = std::strlen(kPrefix), suf = std::strlen(kSuffix);
        if (name.size() != pre + 7 + suf || name.compare(0, pre, kPrefix) != 0 ||
            name.compare(name.size() - suf, suf, kSuffix) != 0) continue;
        const std::string id = name.substr(pre, 7);
        if (!ValidPersonId(id)) { outcome = CareTelegramError::SERIALIZATION_ERROR; break; }
        CareTelegramLink link;
        outcome = LoadUnlocked(id, link);
        if (outcome != CareTelegramError::OK) break;
        result.push_back(std::move(link));
        errno = 0;
    }
    if (outcome == CareTelegramError::OK && errno != 0) outcome = CareTelegramError::STORAGE_ERROR;
    closedir(dir);
    if (outcome != CareTelegramError::OK) result.clear();
    return outcome;
}

CareTelegramError WriteUnlocked(const CareTelegramLink& link) {
    cJSON* root = cJSON_CreateObject();
    if (!root) return CareTelegramError::SERIALIZATION_ERROR;
    cJSON_AddNumberToObject(root, "version", link.version);
    cJSON_AddStringToObject(root, "person_id", link.person_id.c_str());
    cJSON_AddStringToObject(root, "telegram_id", link.telegram_id.c_str());
    cJSON_AddBoolToObject(root, "enabled", link.enabled);
    cJSON_AddNumberToObject(root, "generation", link.data_generation);
    cJSON_AddNumberToObject(root, "status", static_cast<int>(link.status));
    char* json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return CareTelegramError::SERIALIZATION_ERROR;
    std::string content(json);
    cJSON_free(json);
    if (content.empty() || content.size() > kMaxFileBytes)
        return CareTelegramError::SERIALIZATION_ERROR;

    const std::string path = PathFor(link.person_id);
    const std::string tmp = path + ".tmp";
    const std::string backup = path + ".bak";

    // Nunca sobreescribir vestigios de una operacion incompleta.
    // Una recuperacion debera ser explicita y verificada.
    for (const auto& candidate : {tmp, backup}) {
        FILE* existing = std::fopen(candidate.c_str(), "rb");
        if (existing) { std::fclose(existing); return CareTelegramError::STORAGE_ERROR; }
        if (errno != ENOENT) return CareTelegramError::STORAGE_ERROR;
    }

    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return CareTelegramError::STORAGE_ERROR;
    const size_t wrote = std::fwrite(content.data(), 1, content.size(), f);
    const int flushed = std::fflush(f);
    const int closed = std::fclose(f);
    if (wrote != content.size() || flushed != 0 || closed != 0) {
        // Mantener .tmp si hubo un fallo: bloquear autorizaciones hasta repararlo.
        return CareTelegramError::STORAGE_ERROR;
    }

    // Verificar integridad ANTES de tocar el registro vigente.
    CareTelegramLink staged;
    std::string staged_json;
    if (ReadFile(tmp, staged_json) != CareTelegramError::OK ||
        Decode(staged_json, staged) != CareTelegramError::OK ||
        staged.person_id != link.person_id ||
        staged.telegram_id != link.telegram_id ||
        staged.data_generation != link.data_generation || staged.enabled != link.enabled || staged.status != link.status) {
        return CareTelegramError::STORAGE_ERROR;
    }

    // SPIFFS no garantiza que rename() sustituya un destino existente.
    // Conservamos el registro anterior bajo .bak durante la sustitucion.
    bool old_exists = false;
    FILE* current = std::fopen(path.c_str(), "rb");
    if (current) { old_exists = true; std::fclose(current); }
    else if (errno != ENOENT) return CareTelegramError::STORAGE_ERROR;

    if (old_exists && std::rename(path.c_str(), backup.c_str()) != 0)
        return CareTelegramError::STORAGE_ERROR;
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        // El archivo .bak permanece: ListUnlocked denegara todas las cuentas.
        return CareTelegramError::STORAGE_ERROR;
    }

    // Confirmar lectura del registro nuevo, no solamente el rename().
    CareTelegramLink saved;
    if (LoadUnlocked(link.person_id, saved) != CareTelegramError::OK ||
        saved.telegram_id != link.telegram_id ||
        saved.data_generation != link.data_generation || saved.enabled != link.enabled || saved.status != link.status)
        return CareTelegramError::STORAGE_ERROR;

    if (old_exists && std::remove(backup.c_str()) != 0)
        return CareTelegramError::STORAGE_ERROR;
    return CareTelegramError::OK;
}
}  // namespace

CareTelegramError CareTelegramRepository::SaveLink(const CareTelegramLink& link) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ValidLink(link)) return CareTelegramError::INVALID_ARGUMENT;
    std::vector<CareTelegramLink> links;
    CareTelegramError err = ListUnlocked(links);
    if (err != CareTelegramError::OK) return err;
    for (const auto& existing : links) {
        if (existing.telegram_id == link.telegram_id && existing.person_id != link.person_id &&
            existing.status != CareTelegramLinkStatus::REVOKED && existing.data_generation == link.data_generation) return CareTelegramError::DUPLICATE_ACCOUNT;
    }
    return WriteUnlocked(link);
}

CareTelegramError CareTelegramRepository::LoadLink(const std::string& person_id, CareTelegramLink& result) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return LoadUnlocked(person_id, result);
}

CareTelegramError CareTelegramRepository::FindByTelegramId(const std::string& telegram_id, CareTelegramLink& result) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!ValidTelegramId(telegram_id)) return CareTelegramError::INVALID_ARGUMENT;
    std::vector<CareTelegramLink> links;
    CareTelegramError err = ListUnlocked(links);
    if (err != CareTelegramError::OK) return err;
    for (const auto& link : links) {
        if (link.telegram_id == telegram_id && link.status != CareTelegramLinkStatus::REVOKED && link.data_generation == restore::Active().data) {
            result = link;
            return CareTelegramError::OK;
        }
    }
    return CareTelegramError::NOT_FOUND;
}

CareTelegramError CareTelegramRepository::CheckAuthorization(
    const std::string& telegram_id,
    bool& authorized
) {
    authorized = false;

    CareTelegramLink link;
    CareTelegramError result = FindByTelegramId(telegram_id, link);

    if (result != CareTelegramError::OK) {
        return result;
    }

    authorized = false;

    if (link.status != CareTelegramLinkStatus::AUTHORIZED ||
        !link.enabled) {
        return CareTelegramError::OK;
    }

    CareManager& manager = CareManager::GetInstance();

    if (!manager.IsReady()) {
        return CareTelegramError::STORAGE_ERROR;
    }

    CarePerson person;

    if (manager.GetPerson(link.person_id, person) != CareError::OK) {
        return CareTelegramError::STORAGE_ERROR;
    }

    authorized = person.enabled;

    return CareTelegramError::OK;
}
CareTelegramError CareTelegramRepository::ListLinks(std::vector<CareTelegramLink>& result) {
    std::lock_guard<std::mutex> lock(g_mutex);
    return ListUnlocked(result);
}

CareTelegramError CareTelegramRepository::RevokeLink(const std::string& person_id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    CareTelegramLink link;
    CareTelegramError err = LoadUnlocked(person_id, link);
    if (err != CareTelegramError::OK) return err;
    link.enabled = false;
    link.status = CareTelegramLinkStatus::REVOKED;
    return WriteUnlocked(link);
}
}  // namespace xiaozhi_care
