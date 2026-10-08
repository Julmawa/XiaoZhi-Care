#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"

#include "care_manager.h"
#include "care_mcp_service.h"
#include "care_web_server.h"
#include "care_mcp_xiaozhi.h"
#include "care_daily/care_alarm_runtime.h"
#include "care_voice/reminder_voice_runtime.h"
#include "care_homeassistant.h"

#define TAG "main"

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    using xiaozhi_care::CareError;
    using xiaozhi_care::CareErrorToString;
    using xiaozhi_care::CareManager;

    CareError care_err = CareManager::GetInstance().Init();

    if (care_err != CareError::OK) {
        ESP_LOGE(
            TAG,
            "XiaoZhi Care unavailable: %s",
            CareErrorToString(care_err)
        );
    } else {
        // DP034_R4_PRELOAD
        // CARE storage ya está listo; calentamos el cache antes de aceptar consultas.
        xiaozhi_care::CareMcpService::GetInstance().PreloadCache();

        xiaozhi_care::daily::CareAlarmRuntime::GetInstance().Start();
        xiaozhi_care::voice::ReminderVoiceRuntime::GetInstance().Start();
        xiaozhi_care::RegisterCareMcpTools();
    }

    // Initialize and run the application
auto& app = Application::GetInstance();
app.Initialize();

// DP-HA-001
// Inicializa el puente entre XiaoZhi Care y Home Assistant.
// Por ahora solo valida que el modulo esta integrado correctamente.
CareHomeAssistant::GetInstance().Init();

    // DP-HA-009
    xiaozhi_care::CareWebServer::GetInstance().SetHomeAssistantStatusCallback(
        [](xiaozhi_care::CareWebHomeAssistantStatus& status) -> bool {
            auto& ha = CareHomeAssistant::GetInstance();

            status.ready = ha.IsReady();
            status.network_ready = ha.IsNetworkReady();

            if (!status.ready || !status.network_ready) {
                status.connected = false;
                return true;
            }

            CareHaIndoorClimate indoor;
            status.indoor_available = ha.GetIndoorClimate(indoor);

            if (status.indoor_available) {
                status.indoor_temperature = indoor.temperature;
                status.indoor_humidity = indoor.humidity;
            }

            CareHaWeather weather;
            status.weather_available = ha.GetWeather(weather);

            if (status.weather_available) {
                status.outdoor_temperature = weather.temperature;
                status.outdoor_humidity = weather.humidity;
                status.outdoor_condition = weather.condition;
            }

            status.connected =
                status.indoor_available ||
                status.weather_available;

            return true;
        });

    // DP-HA-010A - descubrimiento de entidades Home Assistant
    xiaozhi_care::CareWebServer::GetInstance().SetHomeAssistantEntitiesCallback(
        [](std::vector<xiaozhi_care::CareWebHomeAssistantEntity>& out) -> bool {
            std::vector<CareHaEntity> entities;

            if (!CareHomeAssistant::GetInstance().GetEntities(entities)) {
                return false;
            }

            out.clear();
            out.reserve(entities.size());

            for (const auto& entity : entities) {
                xiaozhi_care::CareWebHomeAssistantEntity web_entity;
                web_entity.entity_id = entity.entity_id;
                web_entity.domain = entity.domain;
                web_entity.friendly_name = entity.friendly_name;
                web_entity.state = entity.state;
                out.push_back(std::move(web_entity));
            }

            return true;
        });

    // DP-HA-010B - lista permitida persistente
    xiaozhi_care::CareWebServer::GetInstance().SetHomeAssistantAllowlistCallbacks(
        [](std::vector<std::string>& out) -> bool {
            return CareHomeAssistant::GetInstance()
                .GetAllowedEntities(out);
        },
        [](const std::vector<std::string>& entities) -> bool {
            return CareHomeAssistant::GetInstance()
                .SetAllowedEntities(entities);
        });


        if (care_err == CareError::OK) {
        esp_err_t web_err =
            xiaozhi_care::CareWebServer::GetInstance().Start();

        if (web_err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "XiaoZhi Care web unavailable: %s",
                esp_err_to_name(web_err)
            );
        }
    }

    app.Run();  // This function runs the main event loop and never returns
}
