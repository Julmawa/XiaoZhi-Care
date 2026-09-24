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
