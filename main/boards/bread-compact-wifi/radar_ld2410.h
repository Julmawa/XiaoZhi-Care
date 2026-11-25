// Radar de presencia LD2410C para el equipo de mamá.
//
// Conexión (UART, 256000 baudios, 8N1):
//   TX del radar -> RADAR_UART_RX_PIN (GPIO17)
//   RX del radar -> RADAR_UART_TX_PIN (GPIO18)
//   VCC 5V, GND común.
//
// El radar manda unas 10 tramas por segundo con:
//   - si hay alguien en movimiento, y a qué distancia
//   - si hay alguien quieto (respirando), y a qué distancia
//
// Esta clase las lee en una tarea propia y resume el estado en:
//   Nadie / EnMovimiento / Quieta
// Un cambio de estado sólo se acepta si dura 2 segundos, para evitar saltos.
//
// Es un archivo de una sola pieza (sólo .h) para no tener que reconfigurar CMake.

#pragma once

#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdio>

#include "telegram_bot.h"

class RadarLd2410 {
public:
    enum class Estado : uint8_t { Desconocido = 0, Nadie, EnMovimiento, Quieta };

    static RadarLd2410& GetInstance() {
        static RadarLd2410 instancia;
        return instancia;
    }

    static const char* Nombre(Estado e) {
        switch (e) {
            case Estado::Nadie:        return "nadie";
            case Estado::EnMovimiento: return "en movimiento";
            case Estado::Quieta:       return "quieta";
            default:                   return "desconocido";
        }
    }

    // Inicia la lectura del radar. rx/tx son los pines del ESP32.
    void Start(gpio_num_t rx_pin, gpio_num_t tx_pin, uart_port_t puerto = UART_NUM_1) {
        if (iniciado_) {
            return;
        }
        puerto_ = puerto;

        uart_config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.baud_rate = 256000;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;

        esp_err_t err = uart_driver_install(puerto_, 2048, 0, 0, nullptr, 0);
        if (err == ESP_OK) err = uart_param_config(puerto_, &cfg);
        if (err == ESP_OK) err = uart_set_pin(puerto_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            ESP_LOGE(kTag, "No se pudo iniciar el UART del radar: %s", esp_err_to_name(err));
            return;
        }

        iniciado_ = true;
        xTaskCreate(&RadarLd2410::TareaEntrada, "radar", 4096, this, 2, nullptr);
        ESP_LOGI(kTag, "Radar LD2410C: leyendo en RX=GPIO%d TX=GPIO%d", (int)rx_pin, (int)tx_pin);
    }

    // Pide cambiar el alcance (en puertas de 75 cm, 1..8) y el tiempo para "nadie" (s).
    // Se aplica en la tarea del radar. Seguro desde cualquier tarea.
    void Reconfigurar(uint32_t puertas, uint32_t segundos_sin_nadie) {
        if (puertas < 1) puertas = 1;
        if (puertas > 8) puertas = 8;
        if (segundos_sin_nadie < 1) segundos_sin_nadie = 1;
        if (puertas == puertas_.load() && segundos_sin_nadie == segundos_sin_nadie_.load()) return;
        puertas_ = puertas;
        segundos_sin_nadie_ = segundos_sin_nadie;
        reconfigurar_ = true;
    }
    void SetAvisosTelegram(bool activo) { avisar_por_telegram_ = activo; }

    // Estado estable (filtrado 2 s)
    Estado GetEstado() const { return estado_.load(); }
    bool HayAlguien() const {
        Estado e = estado_.load();
        return e == Estado::EnMovimiento || e == Estado::Quieta;
    }
    // Distancia aproximada al objetivo, en cm
    int DistanciaCm() const { return distancia_cm_.load(); }
    // Momento (ms desde el arranque) del último movimiento y de la última presencia
    int64_t UltimoMovimientoMs() const { return ultimo_movimiento_ms_.load(); }
    int64_t UltimaPresenciaMs() const { return ultima_presencia_ms_.load(); }
    // true si llegaron tramas en los últimos 3 segundos
    bool Conectado() const {
        int64_t u = ultima_trama_ms_.load();
        return u > 0 && (AhoraMs() - u) < 3000;
    }

private:
    static constexpr const char* kTag = "Radar";
    static constexpr int64_t kConfirmarCambioMs = 2000;

    // Alcance: el radar mide en "puertas" de 75 cm.
    //   2 puertas = hasta 1,5 m (cama desde la mesita de luz)
    // Lo que esté más lejos (resto de la pieza, la puerta, una mascota) se ignora.
    // Valores por defecto; la página de configuración los puede cambiar (Ajustes -> Radar).
    std::atomic<uint32_t> puertas_{2};        // 2 puertas = hasta 1,5 m
    std::atomic<uint32_t> segundos_sin_nadie_{5};
    std::atomic<bool> reconfigurar_{false};
    // MODO PRUEBA: manda cada cambio de presencia por Telegram al administrador.
    std::atomic<bool> avisar_por_telegram_{false};
    // Configurar el alcance por UART al arrancar.
    // IMPORTANTE: el radar necesita la fuente de 5V conectada. Sólo con el USB de la PC
    // no le alcanza la energía y deja de responder a los comandos.
    static constexpr bool kConfigurarAlcance = true;

    uart_port_t puerto_ = UART_NUM_1;
    bool iniciado_ = false;

    std::atomic<Estado> estado_{Estado::Desconocido};
    std::atomic<int> distancia_cm_{0};
    std::atomic<int64_t> ultimo_movimiento_ms_{0};
    std::atomic<int64_t> ultima_presencia_ms_{0};
    std::atomic<int64_t> ultima_trama_ms_{0};
    const char* ultimo_diagnostico_ = "";

    // Filtro de cambios
    Estado candidato_ = Estado::Desconocido;
    int64_t candidato_desde_ms_ = 0;

    RadarLd2410() = default;

    static int64_t AhoraMs() { return esp_timer_get_time() / 1000; }

    // Envía un comando de configuración y espera el acuse (ACK) del radar.
    // Formato: FD FC FB FA | largo(2) | comando(2) | valor | 04 03 02 01
    bool EnviarComando(uint16_t comando, const uint8_t* valor, int largo_valor) {
        uint8_t buf[64];
        int n = 0;
        const uint8_t cab[4] = {0xFD, 0xFC, 0xFB, 0xFA};
        const uint8_t cola[4] = {0x04, 0x03, 0x02, 0x01};
        memcpy(buf + n, cab, 4); n += 4;
        uint16_t largo = 2 + largo_valor;
        buf[n++] = largo & 0xFF;
        buf[n++] = largo >> 8;
        buf[n++] = comando & 0xFF;
        buf[n++] = comando >> 8;
        if (largo_valor > 0) { memcpy(buf + n, valor, largo_valor); n += largo_valor; }
        memcpy(buf + n, cola, 4); n += 4;

        uart_flush_input(puerto_);
        uart_write_bytes(puerto_, buf, n);

        // El ACK trae comando | 0x0100 seguido de estado 00 00 (éxito)
        uint8_t esperado[4] = {(uint8_t)(comando & 0xFF), (uint8_t)((comando >> 8) | 0x01), 0x00, 0x00};
        uint8_t resp[512];
        int total = 0;
        int64_t limite = AhoraMs() + 1000;
        while (AhoraMs() < limite && total < (int)sizeof(resp)) {
            int r = uart_read_bytes(puerto_, resp + total, sizeof(resp) - total, pdMS_TO_TICKS(50));
            if (r > 0) total += r;
            for (int i = 0; i + 4 <= total; i++) {
                if (memcmp(resp + i, esperado, 4) == 0) {
                    return true;
                }
            }
        }
        // Diagnóstico: ¿llegó alguna respuesta de configuración (FD FC FB FA)?
        int respuestas = 0;
        for (int i = 0; i + 4 <= total; i++) {
            if (resp[i] == 0xFD && resp[i + 1] == 0xFC && resp[i + 2] == 0xFB && resp[i + 3] == 0xFA) {
                respuestas++;
                char hex[64];
                int k = 0;
                for (int j = i; j < total && j < i + 18 && k < 60; j++) k += snprintf(hex + k, sizeof(hex) - k, "%02X ", resp[j]);
                ESP_LOGW(kTag, "Respuesta inesperada al comando %04X: %s", comando, hex);
            }
        }
        ultimo_diagnostico_ = respuestas > 0 ? "el radar respondió, pero con otro formato"
                                             : "el radar no respondió (revisar cable GPIO18 -> RX del radar)";
        ESP_LOGW(kTag, "Comando %04X sin acuse: %d bytes recibidos, %d respuestas de configuración",
                 comando, total, respuestas);
        {
            char hex[100];
            int k = 0;
            for (int j = 0; j < total && j < 30 && k < 95; j++) k += snprintf(hex + k, sizeof(hex) - k, "%02X ", resp[j]);
            ESP_LOGW(kTag, "Primeros bytes recibidos: %s", total > 0 ? hex : "(ninguno)");
        }
        return false;
    }

    static void PonerParametro(uint8_t* p, uint16_t id, uint32_t valor) {
        p[0] = id & 0xFF; p[1] = id >> 8;
        p[2] = valor & 0xFF; p[3] = (valor >> 8) & 0xFF;
        p[4] = (valor >> 16) & 0xFF; p[5] = (valor >> 24) & 0xFF;
    }

    // Limita el alcance del radar a la cama y fija el tiempo para "nadie".
    bool Configurar() {
        const uint8_t habilitar[2] = {0x01, 0x00};
        bool ok = EnviarComando(0x00FF, habilitar, 2);          // entrar en modo configuración
        if (ok) {
            uint8_t v[18];
            PonerParametro(v + 0, 0x0000, puertas_.load());
            PonerParametro(v + 6, 0x0001, puertas_.load());
            PonerParametro(v + 12, 0x0002, segundos_sin_nadie_.load());
            ok = EnviarComando(0x0060, v, sizeof(v));            // alcance y tiempo
        }
        EnviarComando(0x00FE, nullptr, 0);                       // salir de configuración
        return ok;
    }

    void InformarConfiguracion(bool ok) {
        char texto[160];
        if (ok) {
            snprintf(texto, sizeof(texto), "Radar configurado: alcance hasta %u cm, 'nadie' tras %u s",
                     (unsigned)(puertas_.load() * 75), (unsigned)segundos_sin_nadie_.load());
            ESP_LOGI(kTag, "%s", texto);
        } else {
            snprintf(texto, sizeof(texto), "Radar: NO se pudo configurar el alcance: %s", ultimo_diagnostico_);
            ESP_LOGW(kTag, "%s", texto);
        }
        if (avisar_por_telegram_.load()) {
            TelegramBot::GetInstance().QueueMessage(texto);
        }
    }

    static void TareaEntrada(void* arg) {
        static_cast<RadarLd2410*>(arg)->Tarea();
        vTaskDelete(nullptr);
    }

    void Tarea() {
        // Máquina de estados para encontrar tramas:
        //   F4 F3 F2 F1 | largo (2 bytes) | datos | F8 F7 F6 F5
        static const uint8_t kCabecera[4] = {0xF4, 0xF3, 0xF2, 0xF1};
        static const uint8_t kCola[4] = {0xF8, 0xF7, 0xF6, 0xF5};
        uint8_t lectura[128];
        uint8_t trama[64];
        int pos = 0;            // bytes acumulados en "trama"
        int largo = 0;          // largo de los datos
        bool aviso_sin_datos = false;
        bool aviso_con_datos = false;

        // El radar tarda un poco en arrancar: esperar hasta 5 s a que mande algo
        for (int i = 0; kConfigurarAlcance && i < 25; i++) {
            size_t disponibles = 0;
            uart_get_buffered_data_len(puerto_, &disponibles);
            if (disponibles > 20) break;
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        bool configurado = false;
        reconfigurar_ = false;
        if (kConfigurarAlcance) vTaskDelay(pdMS_TO_TICKS(500));
        for (int intento = 1; kConfigurarAlcance && intento <= 3 && !configurado; intento++) {
            configurado = Configurar();
            if (!configurado) {
                ESP_LOGW(kTag, "Configuración del radar: intento %d sin respuesta", intento);
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
        }
        if (kConfigurarAlcance) InformarConfiguracion(configurado);
        int64_t inicio_ms = AhoraMs();

        while (true) {
            if (kConfigurarAlcance && reconfigurar_.exchange(false)) {
                bool ok = Configurar();
                InformarConfiguracion(ok);
                pos = 0;
            }
            int n = uart_read_bytes(puerto_, lectura, sizeof(lectura), pdMS_TO_TICKS(200));
            for (int i = 0; i < n; i++) {
                uint8_t b = lectura[i];
                if (pos < 4) {
                    // Buscando la cabecera
                    if (b == kCabecera[pos]) {
                        trama[pos++] = b;
                    } else {
                        pos = (b == kCabecera[0]) ? 1 : 0;
                        if (pos == 1) trama[0] = b;
                    }
                    continue;
                }
                trama[pos++] = b;
                if (pos == 6) {
                    largo = trama[4] | (trama[5] << 8);
                    if (largo <= 0 || 6 + largo + 4 > (int)sizeof(trama)) {
                        pos = 0;  // largo inválido: volver a buscar
                    }
                    continue;
                }
                if (pos == 6 + largo + 4) {
                    if (memcmp(&trama[6 + largo], kCola, 4) == 0) {
                        ProcesarDatos(&trama[6], largo);
                    }
                    pos = 0;
                }
            }

            // Avisos de diagnóstico del cableado
            int64_t ahora = AhoraMs();
            if (Conectado()) {
                if (!aviso_con_datos) {
                    ESP_LOGI(kTag, "Radar conectado: recibiendo datos");
                    aviso_con_datos = true;
                    aviso_sin_datos = false;
                }
            } else if (!aviso_sin_datos && ahora - inicio_ms > 5000) {
                ESP_LOGW(kTag, "No llegan datos del radar: revisar TX del radar -> GPIO17, "
                               "RX del radar -> GPIO18, 5V y GND");
                aviso_sin_datos = true;
                aviso_con_datos = false;
                estado_ = Estado::Desconocido;
            }
        }
    }

    // datos[0]: tipo (0x02 básico, 0x01 ingeniería), datos[1]: 0xAA
    // datos[2]: objetivo (0 nadie, 1 movimiento, 2 quieto, 3 ambos)
    // datos[3..4]: distancia en movimiento (cm), datos[5]: energía en movimiento
    // datos[6..7]: distancia quieto (cm), datos[8]: energía quieto
    void ProcesarDatos(const uint8_t* d, int largo) {
        if (largo < 11 || (d[0] != 0x01 && d[0] != 0x02) || d[1] != 0xAA) {
            return;
        }
        int64_t ahora = AhoraMs();
        ultima_trama_ms_ = ahora;

        uint8_t objetivo = d[2];
        int dist_mov = d[3] | (d[4] << 8);
        int energia_mov = d[5];
        int dist_quieto = d[6] | (d[7] << 8);
        int energia_quieto = d[8];

        Estado crudo;
        int distancia;
        if (objetivo == 1 || objetivo == 3) {
            crudo = Estado::EnMovimiento;
            distancia = dist_mov;
            ultimo_movimiento_ms_ = ahora;
            ultima_presencia_ms_ = ahora;
        } else if (objetivo == 2) {
            crudo = Estado::Quieta;
            distancia = dist_quieto;
            ultima_presencia_ms_ = ahora;
        } else {
            crudo = Estado::Nadie;
            distancia = 0;
        }

        if (crudo != candidato_) {
            candidato_ = crudo;
            candidato_desde_ms_ = ahora;
        }
        distancia_cm_ = distancia;

        // Se acepta el cambio cuando se mantiene 2 segundos
        if (crudo != estado_.load() && ahora - candidato_desde_ms_ >= kConfirmarCambioMs) {
            estado_ = crudo;
            char texto[96];
            if (crudo == Estado::Nadie) {
                snprintf(texto, sizeof(texto), "Radar: nadie");
            } else {
                snprintf(texto, sizeof(texto), "Radar: %s a %d cm (energía mov=%d quieto=%d)",
                         Nombre(crudo), distancia, energia_mov, energia_quieto);
            }
            ESP_LOGI(kTag, "%s", texto);
            if (avisar_por_telegram_.load()) {
                TelegramBot::GetInstance().QueueMessage(texto);
            }
        }
    }
};
