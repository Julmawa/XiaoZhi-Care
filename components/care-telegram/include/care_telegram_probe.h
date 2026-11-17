#pragma once
#include <functional>
#include <cstdint>
#include <string>
namespace xiaozhi_care {
// Usar exclusivamente eventos de red: true despues de obtener IP; false al desconectar.
void SetCareTelegramNetworkReady(bool ready);
void StartCareTelegramProbe();
// Invoked on the Telegram worker; false keeps the first uncompleted update on Telegram.
void SetCareTelegramProbeReceivedCallback(
    std::function<bool(int64_t, const std::string&, const std::string&)> callback);
}
