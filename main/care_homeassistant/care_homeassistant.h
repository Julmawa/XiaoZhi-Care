#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "http.h"

struct CareHaWeather {
    bool valid = false;
    float temperature = 0.0f;
    int humidity = 0;
    std::string condition;
};

struct CareHaIndoorClimate {
    bool valid = false;
    float temperature = 0.0f;
    float humidity = 0.0f;
};

struct CareHaEntity {
    std::string entity_id;
    std::string domain;
    std::string friendly_name;
    std::string state;
};

struct CareHaDeviceControlResult {
    bool success = false;
    bool found = false;
    bool ambiguous = false;
    std::string entity_id;
    std::string friendly_name;
    std::string domain;
    std::string action;
    std::string message;
};

class CareHomeAssistant {
public:
    static CareHomeAssistant& GetInstance();

    bool Init();
    bool IsReady() const;
    bool IsNetworkReady() const;

    void OnNetworkConnected();

    bool GetEntityState(
        const std::string& entity_id,
        std::string& response
    );

    bool GetWeather(CareHaWeather& weather);
    bool GetIndoorClimate(CareHaIndoorClimate& climate);
    bool GetTtsUrl(
        const std::string& message,
        std::string& audio_url);
    bool GetEntities(std::vector<CareHaEntity>& entities);
    bool GetAllowedEntities(std::vector<std::string>& entities);
    bool SetAllowedEntities(const std::vector<std::string>& entities);
    bool IsEntityAllowed(const std::string& entity_id);
    void SetPendingFamilyMessage(
        const std::string& sender,
        const std::string& message);
    bool GetPendingFamilyMessage(
        std::string& sender,
        std::string& message);
    bool GetAllowedEntityDetails(std::vector<CareHaEntity>& entities);
    bool ControlAllowedDevice(
        const std::string& query,
        const std::string& action,
        CareHaDeviceControlResult& result);

    const char* GetName() const;

private:
    bool CallService(
        const std::string& domain,
        const std::string& service,
        const std::string& entity_id,
        std::string& response);

    CareHomeAssistant() = default;

    CareHomeAssistant(const CareHomeAssistant&) = delete;
    CareHomeAssistant& operator=(const CareHomeAssistant&) = delete;

    bool ready_ = false;
    bool network_ready_ = false;

    // Cliente persistente: evita destruir HttpClient mientras la tarea TCP
    // todavía puede estar procesando una desconexión.
    std::unique_ptr<Http> http_;
    std::mutex http_mutex_;

    std::mutex config_mutex_;
    std::vector<std::string> allowed_entities_;
    std::mutex family_message_mutex_;
    std::string pending_family_sender_;
    std::string pending_family_message_;
};
