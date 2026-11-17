#ifndef __LAMP_CONTROLLER_H__
#define __LAMP_CONTROLLER_H__

#include "mcp_server.h"


class LampController {
private:
    bool power_ = false;
    gpio_num_t gpio_num_;

public:
    LampController(gpio_num_t gpio_num) : gpio_num_(gpio_num) {
        if (gpio_num_ == GPIO_NUM_NC) {
            return;
        }

        gpio_config_t config = {
            .pin_bit_mask = (1ULL << gpio_num_),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&config));
        gpio_set_level(gpio_num_, 0);

        // DP-HA-010C-r4
        // CARE usa Home Assistant para controlar luces y dispositivos del hogar.
        // No registramos los tools genéricos self.lamp.* porque el modelo los
        // confundía con entidades reales como "Lámpara de pie".
        //
        // El controlador GPIO local se conserva; solo se deshabilita su exposición
        // como herramienta MCP.
    }
};


#endif // __LAMP_CONTROLLER_H__
