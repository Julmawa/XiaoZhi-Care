#pragma once

#include "care_telegram_models.h"

#include <string>
#include <vector>

namespace xiaozhi_care {

enum class CareTelegramError {
    OK,
    NOT_FOUND,
    INVALID_ARGUMENT,
    DUPLICATE_ACCOUNT,
    STORAGE_ERROR,
    SERIALIZATION_ERROR
};

class CareTelegramRepository {
public:
    // Guarda una vinculacion validada.
    CareTelegramError SaveLink(const CareTelegramLink& link);

    // Recupera una vinculacion mediante el ID de persona.
    CareTelegramError LoadLink(
        const std::string& person_id,
        CareTelegramLink& result
    );

    // Busca una cuenta por su identificador Telegram.
    CareTelegramError FindByTelegramId(
        const std::string& telegram_id,
        CareTelegramLink& result
    );

    // Solo autoriza cuentas habilitadas y aprobadas.
    // Ante cualquier error, authorized permanece false.
    CareTelegramError CheckAuthorization(
        const std::string& telegram_id,
        bool& authorized
    );
    // Recupera todas las vinculaciones existentes.
    CareTelegramError ListLinks(
        std::vector<CareTelegramLink>& result
    );

    // Revoca el permiso, sin borrar a la persona de CARE.
    CareTelegramError RevokeLink(
        const std::string& person_id
    );
};

}  // namespace xiaozhi_care