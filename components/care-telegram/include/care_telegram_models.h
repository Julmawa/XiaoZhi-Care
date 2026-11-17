#pragma once

#include <cstdint>
#include <string>

namespace xiaozhi_care {

constexpr uint32_t kTelegramLinkRecordVersion = 1;

enum class CareTelegramLinkStatus {
    PENDING,
    AUTHORIZED,
    REVOKED
};

struct CareTelegramLink {
    uint32_t version = kTelegramLinkRecordVersion;

    // Identificador interno de la persona de CARE.
    std::string person_id;

    // Identificador de cuenta recibido desde Telegram.
    std::string telegram_id;

    // Permiso explícito para enviar mensajes a CARE.
    bool enabled = false;
    uint16_t data_generation = 0;

    CareTelegramLinkStatus status = CareTelegramLinkStatus::PENDING;
};

}  // namespace xiaozhi_care