#include "madre.h"

#include <cJSON.h>
#include <esp_app_desc.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "board.h"
#include "display.h"
#include "http.h"
#include "network_interface.h"
#include "mcp_server.h"
#include "telegram_bot.h"

#define TAG "Madre"

// Página de configuración incluida en el firmware (main/madre/config.html)
extern const char config_html_start[] asm("_binary_config_html_start");
extern const char config_html_end[] asm("_binary_config_html_end");

namespace {

constexpr const char* kNamespace = "madre";
constexpr size_t kMaxSeccion = 16 * 1024;   // tope por sección (JSON)

const char* const kDias[] = {"domingo", "lunes", "martes", "miércoles", "jueves", "viernes", "sábado"};
const char* const kDiasCortos[] = {"dom", "lun", "mar", "mié", "jue", "vie", "sáb"};
const char* const kMeses[] = {"enero", "febrero", "marzo", "abril", "mayo", "junio", "julio",
                              "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
// En la página los días van 0=lunes ... 6=domingo
const char* const kDiasPagina[] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};

bool RelojEnHora() {
    time_t ahora = time(nullptr);
    struct tm t;
    localtime_r(&ahora, &t);
    return t.tm_year + 1900 >= 2025;
}

// Día de hoy con 0=lunes ... 6=domingo (como la página)
int DiaPaginaHoy() {
    time_t ahora = time(nullptr);
    struct tm t;
    localtime_r(&ahora, &t);
    return (t.tm_wday + 6) % 7;
}

const char* MomentoDelDia(int hora) {
    if (hora < 6) return "de madrugada";
    if (hora < 12) return "de mañana";
    if (hora < 14) return "del mediodía";
    if (hora < 20) return "de tarde";
    return "de noche";
}

std::string Str(const cJSON* obj, const char* campo) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, campo);
    return (cJSON_IsString(v) && v->valuestring) ? std::string(v->valuestring) : std::string();
}

bool Bool(const cJSON* obj, const char* campo, bool por_defecto) {
    const cJSON* v = cJSON_GetObjectItemCaseSensitive(obj, campo);
    if (cJSON_IsBool(v)) return cJSON_IsTrue(v);
    return por_defecto;
}

bool TieneDia(const cJSON* obj, int dia) {
    const cJSON* dias = cJSON_GetObjectItemCaseSensitive(obj, "dias");
    if (!cJSON_IsArray(dias)) return true;  // sin lista = todos los días
    const cJSON* d;
    cJSON_ArrayForEach(d, dias) {
        if (cJSON_IsNumber(d) && d->valueint == dia) return true;
    }
    return false;
}

std::string TextoDias(const cJSON* obj) {
    const cJSON* dias = cJSON_GetObjectItemCaseSensitive(obj, "dias");
    if (!cJSON_IsArray(dias)) return "todos los días";
    int n = cJSON_GetArraySize(dias);
    if (n >= 7) return "todos los días";
    if (n == 0) return "ningún día";
    std::string s;
    const cJSON* d;
    cJSON_ArrayForEach(d, dias) {
        if (!cJSON_IsNumber(d) || d->valueint < 0 || d->valueint > 6) continue;
        if (!s.empty()) s += ", ";
        s += kDiasPagina[d->valueint];
    }
    return s;
}

std::string IpActual() {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif == nullptr) return "";
    esp_netif_ip_info_t info;
    if (esp_netif_get_ip_info(netif, &info) != ESP_OK || info.ip.addr == 0) return "";
    char buf[20];
    snprintf(buf, sizeof(buf), IPSTR, IP2STR(&info.ip));
    return buf;
}

// Escapa texto para ponerlo dentro de un string JSON
std::string JsonEsc(const std::string& s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '"': r += "\\\""; break;
            case '\\': r += "\\\\"; break;
            case '\n': r += "\\n"; break;
            case '\r': break;
            case '\t': r += ' '; break;
            default: r += c;
        }
    }
    return r;
}

}  // namespace

// ----------------------------------------------------------------------------
// Arranque
// ----------------------------------------------------------------------------
void Madre::Start() {
    if (iniciado_) return;
    iniciado_ = true;

    IniciarAlmacenamiento();
    AplicarAjustes();
    IniciarReloj();
    RegistrarHerramientas();
    ultima_senal_ms_ = esp_timer_get_time() / 1000;
    // 12 KB: las noticias usan HTTPS (mbedtls) y necesitan bastante pila
    xTaskCreate(&Madre::TareaEntrada, "madre", 12288, this, 2, nullptr);
}

void Madre::TareaEntrada(void* arg) {
    static_cast<Madre*>(arg)->Tarea();
    vTaskDelete(nullptr);
}

void Madre::Tarea() {
    // Esperar a tener IP en el WiFi de la casa
    std::string ip;
    while ((ip = IpActual()).empty()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    IniciarServidor();
    ESP_LOGI(TAG, "Página de configuración: http://%s", ip.c_str());
    TelegramBot::GetInstance().QueueMessage("Página de configuración del XiaoZhi: http://" + ip);

    bool aviso_hora = false;
    std::string ip_anterior = ip;
    int vuelta = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        int64_t ahora = esp_timer_get_time() / 1000;
        vuelta++;

        if (!aviso_hora && RelojEnHora()) {
            aviso_hora = true;
            ESP_LOGI(TAG, "Reloj en hora: %s", FechaEnPalabras().c_str());
        }
        if (vuelta % 10 == 0) {
            // Si cambió la IP (otro WiFi o el router la renovó), avisar
            std::string nueva = IpActual();
            if (!nueva.empty() && nueva != ip_anterior) {
                ip_anterior = nueva;
                TelegramBot::GetInstance().QueueMessage("Nueva dirección de la página del XiaoZhi: http://" + nueva);
            }
        }

        ActualizarPresencia(ahora);
        RevisarBienestar(ahora);
        ActualizarNoticias(ahora);
        if (aviso_hora) {
            RevisarAgenda(ahora);
        }

        // Saludo al despertar (tiene prioridad sobre los recordatorios)
        if (saludo_pendiente_) {
            if (ahora - saludo_desde_ms_ > 10 * 60 * 1000) {
                saludo_pendiente_ = false;      // ya pasó el momento
            } else if (PuedeHablar(ahora, true)) {
                saludo_pendiente_ = false;
                ESP_LOGI(TAG, "Saludo al despertar");
                Hablar("Me desperté");
                continue;
            }
        }
        ProcesarAvisos(ahora);
    }
}

// ----------------------------------------------------------------------------
// Almacenamiento (partición NVS propia "madre"; si no existe, la principal)
// ----------------------------------------------------------------------------
void Madre::IniciarAlmacenamiento() {
    esp_err_t err = nvs_flash_init_partition("madre");
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Borrando la partición 'madre' para reiniciarla");
        nvs_flash_erase_partition("madre");
        err = nvs_flash_init_partition("madre");
    }
    if (err == ESP_OK) {
        particion_ = "madre";
        almacenamiento_ok_ = true;
    } else {
        // Tabla de particiones vieja: usar la NVS principal (más chica)
        ESP_LOGW(TAG, "No hay partición 'madre' (%s): se usa la NVS principal", esp_err_to_name(err));
        particion_ = "nvs";
        almacenamiento_ok_ = true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    familia_ = Leer("familia", "[]");
    recordatorios_ = Leer("recordat", "[]");
    pastillero_ = Leer("pastill", "{}");
    ajustes_ = Leer("ajustes", "{}");
    ESP_LOGI(TAG, "Datos cargados (%s): familia %u B, recordatorios %u B, pastillero %u B, ajustes %u B",
             particion_.c_str(), (unsigned)familia_.size(), (unsigned)recordatorios_.size(),
             (unsigned)pastillero_.size(), (unsigned)ajustes_.size());
}

std::string Madre::Leer(const char* clave, const char* por_defecto) {
    nvs_handle_t h;
    if (nvs_open_from_partition(particion_.c_str(), kNamespace, NVS_READONLY, &h) != ESP_OK) {
        return por_defecto;
    }
    size_t largo = 0;
    std::string valor = por_defecto;
    if (nvs_get_blob(h, clave, nullptr, &largo) == ESP_OK && largo > 0 && largo <= kMaxSeccion) {
        std::string buf(largo, '\0');
        if (nvs_get_blob(h, clave, buf.data(), &largo) == ESP_OK) {
            valor.assign(buf.data(), largo);
        }
    }
    nvs_close(h);
    return valor;
}

bool Madre::Guardar(const char* clave, const std::string& valor) {
    nvs_handle_t h;
    esp_err_t err = nvs_open_from_partition(particion_.c_str(), kNamespace, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_blob(h, clave, valor.data(), valor.size());
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo guardar '%s': %s", clave, esp_err_to_name(err));
        return false;
    }
    return true;
}

std::string* Madre::Campo(const std::string& seccion, const char** clave_nvs, bool* es_lista) {
    if (seccion == "familia")       { *clave_nvs = "familia";  *es_lista = true;  return &familia_; }
    if (seccion == "recordatorios") { *clave_nvs = "recordat"; *es_lista = true;  return &recordatorios_; }
    if (seccion == "pastillero")    { *clave_nvs = "pastill";  *es_lista = false; return &pastillero_; }
    if (seccion == "ajustes")       { *clave_nvs = "ajustes";  *es_lista = false; return &ajustes_; }
    return nullptr;
}

std::string Madre::GetSeccion(const std::string& seccion) {
    std::lock_guard<std::mutex> lock(mutex_);
    const char* clave;
    bool es_lista;
    std::string* campo = Campo(seccion, &clave, &es_lista);
    return campo ? *campo : std::string();
}

// ----------------------------------------------------------------------------
// Reloj: hora mundial por Internet + zona horaria de Argentina (UTC-3, sin horario de verano)
// ----------------------------------------------------------------------------
void Madre::IniciarReloj() {
    setenv("TZ", "ART3", 1);
    tzset();
    if (!esp_sntp_enabled()) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }
}

std::string Madre::FechaEnPalabras() {
    if (!RelojEnHora()) return "";
    time_t ahora = time(nullptr);
    struct tm t;
    localtime_r(&ahora, &t);
    char buf[120];
    snprintf(buf, sizeof(buf), "%s %d de %s de %d, %02d:%02d, %s",
             kDias[t.tm_wday], t.tm_mday, kMeses[t.tm_mon], t.tm_year + 1900,
             t.tm_hour, t.tm_min, MomentoDelDia(t.tm_hour));
    return buf;
}

std::string Madre::DireccionPagina() {
    std::string ip = IpActual();
    return ip.empty() ? "" : "http://" + ip;
}

// ----------------------------------------------------------------------------
// Servidor web
// ----------------------------------------------------------------------------
void Madre::IniciarServidor() {
    if (servidor_ != nullptr) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 80;
    cfg.ctrl_port = 32790;          // distinto del que usa la configuración de WiFi
    cfg.max_uri_handlers = 8;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;

    if (httpd_start(&servidor_, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo iniciar el servidor web");
        servidor_ = nullptr;
        return;
    }

    httpd_uri_t pagina = {};
    pagina.uri = "/";
    pagina.method = HTTP_GET;
    pagina.handler = &Madre::PaginaHandler;
    httpd_register_uri_handler(servidor_, &pagina);

    httpd_uri_t config = {};
    config.uri = "/api/config";
    config.method = HTTP_GET;
    config.handler = &Madre::ConfigHandler;
    httpd_register_uri_handler(servidor_, &config);

    static const char* const kSecciones[] = {"/api/familia", "/api/recordatorios", "/api/pastillero", "/api/ajustes"};
    for (const char* uri : kSecciones) {
        httpd_uri_t g = {};
        g.uri = uri;
        g.method = HTTP_POST;
        g.handler = &Madre::GuardarHandler;
        httpd_register_uri_handler(servidor_, &g);
    }
}

int Madre::PaginaHandler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    size_t largo = config_html_end - config_html_start;
    if (largo > 0 && config_html_start[largo - 1] == '\0') largo--;  // TEXT agrega un \0 al final
    return httpd_resp_send(req, config_html_start, largo);
}

int Madre::ConfigHandler(httpd_req_t* req) {
    auto& m = GetInstance();
    std::string json;
    {
        std::lock_guard<std::mutex> lock(m.mutex_);
        json = "{\"familia\":" + m.familia_ + ",\"recordatorios\":" + m.recordatorios_ +
               ",\"pastillero\":" + m.pastillero_ + ",\"ajustes\":" + m.ajustes_;
    }
    json += ",\"estado\":" + m.EstadoJson() + "}";
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, json.data(), json.size());
}

int Madre::GuardarHandler(httpd_req_t* req) {
    auto& m = GetInstance();
    std::string seccion = req->uri + strlen("/api/");
    size_t q = seccion.find('?');
    if (q != std::string::npos) seccion.resize(q);

    if (req->content_len == 0 || req->content_len > kMaxSeccion) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Datos vacíos o demasiado grandes");
        return ESP_FAIL;
    }
    std::string cuerpo(req->content_len, '\0');
    size_t recibido = 0;
    while (recibido < req->content_len) {
        int r = httpd_req_recv(req, cuerpo.data() + recibido, req->content_len - recibido);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return ESP_FAIL;
        recibido += r;
    }

    // Validar que sea JSON del tipo correcto
    const char* clave;
    bool es_lista;
    std::string* campo;
    {
        std::lock_guard<std::mutex> lock(m.mutex_);
        campo = m.Campo(seccion, &clave, &es_lista);
    }
    if (campo == nullptr) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Sección desconocida");
        return ESP_FAIL;
    }
    cJSON* raiz = cJSON_Parse(cuerpo.c_str());
    bool valido = raiz != nullptr && (es_lista ? cJSON_IsArray(raiz) : cJSON_IsObject(raiz));
    std::string limpio;
    if (valido) {
        char* txt = cJSON_PrintUnformatted(raiz);
        if (txt) { limpio = txt; cJSON_free(txt); }
    }
    cJSON_Delete(raiz);
    if (!valido || limpio.empty()) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON inválido");
        return ESP_FAIL;
    }

    bool ok = m.Guardar(clave, limpio);
    if (ok) {
        std::lock_guard<std::mutex> lock(m.mutex_);
        *campo = limpio;
    }
    ESP_LOGI(TAG, "Guardado %s (%u B): %s", seccion.c_str(), (unsigned)limpio.size(), ok ? "OK" : "ERROR");
    if (ok && seccion == "ajustes") {
        m.AplicarAjustes();
    }

    httpd_resp_set_type(req, "application/json");
    if (!ok) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_sendstr(req, "{\"ok\":false}");
    }
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

std::string Madre::EstadoJson() {
    std::string hora = "sin hora todavía";
    if (RelojEnHora()) {
        time_t ahora = time(nullptr);
        struct tm t;
        localtime_r(&ahora, &t);
        char buf[40];
        snprintf(buf, sizeof(buf), "%s %02d/%02d %02d:%02d", kDiasCortos[t.tm_wday], t.tm_mday,
                 t.tm_mon + 1, t.tm_hour, t.tm_min);
        hora = buf;
    }
    std::string wifi = "–";
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        const char* calidad = ap.rssi > -60 ? "buena" : (ap.rssi > -75 ? "regular" : "débil");
        wifi = std::string(reinterpret_cast<const char*>(ap.ssid)) + " (" + calidad + ")";
    }
    std::string presencia = presencia_ ? presencia_() : "sin radar";
    const esp_app_desc_t* app = esp_app_get_description();

    return "{\"hora\":\"" + JsonEsc(hora) + "\",\"ip\":\"" + JsonEsc(IpActual()) +
           "\",\"wifi\":\"" + JsonEsc(wifi) + "\",\"presencia\":\"" + JsonEsc(presencia) +
           "\",\"version\":\"" + JsonEsc(app ? app->version : "") +
           "\",\"pastillero\":\"conexión disponible en la próxima versión\"}";
}

// ----------------------------------------------------------------------------
// Ajustes
// ----------------------------------------------------------------------------
void Madre::AplicarAjustes() {
    Ajustes a;
    cJSON* raiz = cJSON_Parse(GetSeccion("ajustes").c_str());
    auto num = [](const cJSON* o, const char* campo, int def) {
        const cJSON* v = cJSON_GetObjectItemCaseSensitive(o, campo);
        return cJSON_IsNumber(v) ? v->valueint : def;
    };
    const cJSON* r = cJSON_GetObjectItemCaseSensitive(raiz, "radar");
    a.radar_activo = Bool(r, "activo", true);
    a.radar_puertas = num(r, "alcance", 2);
    a.radar_sin_nadie = num(r, "sinNadie", 5);
    a.radar_avisos = Bool(r, "avisosTelegram", false);

    const cJSON* s = cJSON_GetObjectItemCaseSensitive(raiz, "saludo");
    a.saludo_activo = Bool(s, "activo", true);
    a.saludo_horas_quieta = num(s, "horasQuieta", 2);
    a.saludo_alternar = Bool(s, "alternar", true);
    a.saludo_decir_fecha = Bool(s, "decirFecha", true);
    const cJSON* frases = cJSON_GetObjectItemCaseSensitive(s, "frases");
    const cJSON* f;
    cJSON_ArrayForEach(f, frases) {
        if (cJSON_IsString(f) && f->valuestring && f->valuestring[0]) a.saludo_frases.push_back(f->valuestring);
    }
    if (a.saludo_frases.empty()) {
        a.saludo_frases = {"Hola, ¿te despertaste? ¿Cómo te sentís?", "¿Estás despierta? ¿Necesitás algo?",
                           "Buen día, ¿cómo dormiste?"};
    }

    const cJSON* b = cJSON_GetObjectItemCaseSensitive(raiz, "bienestar");
    a.bienestar_activo = Bool(b, "activo", true);
    a.bienestar_horas = num(b, "horas", 14);
    a.volumen = num(raiz, "volumen", 70);
    const cJSON* n = cJSON_GetObjectItemCaseSensitive(raiz, "noticias");
    a.noticias_activo = Bool(n, "activo", true);
    a.noticias_en_saludo = Bool(n, "enSaludo", false);
    a.noticias_cantidad = num(n, "cantidad", 3);
    if (!Str(n, "fuente").empty()) a.noticias_fuente = Str(n, "fuente");
    if (a.noticias_cantidad < 1) a.noticias_cantidad = 1;
    if (a.noticias_cantidad > 5) a.noticias_cantidad = 5;
    const cJSON* m = cJSON_GetObjectItemCaseSensitive(raiz, "mama");
    a.mama_apodo = Str(m, "apodo");
    if (a.mama_apodo.empty()) {
        std::string nombre = Str(m, "nombre");
        a.mama_apodo = nombre.substr(0, nombre.find(' '));
    }
    cJSON_Delete(raiz);

    if (a.saludo_horas_quieta < 1) a.saludo_horas_quieta = 1;
    if (a.bienestar_horas < 1) a.bienestar_horas = 1;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        aj_ = a;
    }

    if (radar_config_) {
        radar_config_(a.radar_puertas, a.radar_sin_nadie, a.radar_avisos);
    }
    auto* codec = Board::GetInstance().GetAudioCodec();
    if (codec && a.volumen >= 30 && a.volumen <= 100 && codec->output_volume() != a.volumen) {
        codec->SetOutputVolume(a.volumen);
    }
    ESP_LOGI(TAG, "Ajustes: radar %s %d cm, saludo %s (%d h), bienestar %s (%d h), volumen %d",
             a.radar_activo ? "sí" : "no", a.radar_puertas * 75, a.saludo_activo ? "sí" : "no",
             a.saludo_horas_quieta, a.bienestar_activo ? "sí" : "no", a.bienestar_horas, a.volumen);
}

// ----------------------------------------------------------------------------
// Presencia: dormida, despierta, ausente
// ----------------------------------------------------------------------------
void Madre::ActualizarPresencia(int64_t ahora) {
    int64_t dt = ultimo_tick_ms_ ? ahora - ultimo_tick_ms_ : 0;
    ultimo_tick_ms_ = ahora;

    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) {
        ultima_conversacion_ms_ = ahora;
        ultima_senal_ms_ = ahora;
        sueno_ms_ = 0;              // está despierta, hablando con XiaoZhi
    }

    bool radar_activo;
    int horas;
    bool saludo_activo;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        radar_activo = aj_.radar_activo;
        horas = aj_.saludo_horas_quieta;
        saludo_activo = aj_.saludo_activo;
    }
    int e = (radar_activo && radar_) ? radar_() : 0;
    estado_radar_ = e;
    if (e == 0) return;                                    // sin radar: no se sabe

    if (e == 2 || e == 3) ultima_senal_ms_ = ahora;

    if (e == 1) {
        nadie_ms_ += dt;
        if (nadie_ms_ > 10 * 60 * 1000) sueno_ms_ = 0;     // se fue de la pieza
    } else {
        nadie_ms_ = 0;
    }
    if (e == 3) {
        sueno_ms_ += dt;
    }
    if (e == 2) {
        if (mov_inicio_ms_ == 0 || ahora - ultimo_movimiento_ms_ > 5000) {
            mov_inicio_ms_ = ahora;                        // empieza un movimiento nuevo
        }
        ultimo_movimiento_ms_ = ahora;
        // 20 s seguidos moviéndose = se levantó / está activa
        if (ahora - mov_inicio_ms_ >= 20000) {
            if (sueno_ms_ >= (int64_t)horas * 3600 * 1000 && saludo_activo) {
                ESP_LOGI(TAG, "Se despertó (estuvo quieta %d min)", (int)(sueno_ms_ / 60000));
                saludo_pendiente_ = true;
                saludo_desde_ms_ = ahora;
            }
            sueno_ms_ = 0;
        }
    }
}

bool Madre::Durmiendo(int64_t ahora) {
    return estado_radar_ == 3 && sueno_ms_ >= 30 * 60 * 1000;
}

bool Madre::PuedeHablar(int64_t ahora, bool es_saludo) {
    auto& app = Application::GetInstance();
    if (app.GetDeviceState() != kDeviceStateIdle) return false;
    if (TelegramBot::GetInstance().IsVoiceBusy()) return false;
    if (ahora - ultima_conversacion_ms_ < 5000) return false;   // recién terminó una charla
    if (estado_radar_ == 1) return false;                        // no hay nadie en la pieza
    if (!es_saludo && Durmiendo(ahora)) return false;           // no despertarla
    return true;
}

void Madre::Hablar(const char* frase) {
    auto& app = Application::GetInstance();
    app.PlaySound(Lang::Sounds::OGG_POPUP);
    vTaskDelay(pdMS_TO_TICKS(1500));
    if (app.GetDeviceState() != kDeviceStateIdle) return;
    ESP_LOGI(TAG, "Activando al asistente: %s", frase);
    app.WakeWordInvoke(frase);
    for (int i = 0; i < 20 && app.GetDeviceState() == kDeviceStateIdle; i++) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ultima_conversacion_ms_ = esp_timer_get_time() / 1000;
}

void Madre::NotificarBoton() {
    ultima_senal_ms_ = esp_timer_get_time() / 1000;
    ConfirmarAvisos("botón");
}

// ----------------------------------------------------------------------------
// Bienestar: aviso a la familia si no hay señales de mamá
// ----------------------------------------------------------------------------
void Madre::RevisarBienestar(int64_t ahora) {
    bool activo;
    int horas;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activo = aj_.bienestar_activo;
        horas = aj_.bienestar_horas;
    }
    auto hora_de = [](int64_t hace_ms) {
        time_t t = time(nullptr) - (time_t)(hace_ms / 1000);
        struct tm tm;
        localtime_r(&t, &tm);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
        return std::string(buf);
    };
    int64_t sin_senal = ahora - ultima_senal_ms_;
    if (!bienestar_alertado_) {
        if (activo && sin_senal >= (int64_t)horas * 3600 * 1000) {
            bienestar_alertado_ = true;
            std::string msg = "⚠️ No hay señales de mamá desde las " + hora_de(sin_senal) + " (hace " +
                              std::to_string(sin_senal / 3600000) + " h): el radar no la detecta y no usó "
                              "el botón ni habló con XiaoZhi.";
            ESP_LOGW(TAG, "%s", msg.c_str());
            TelegramBot::GetInstance().QueueMessage(msg);
        }
    } else if (sin_senal < 60 * 1000) {
        bienestar_alertado_ = false;
        TelegramBot::GetInstance().QueueMessage("✅ Volvió a haber señales de mamá (" + hora_de(0) + ").");
    }
}

// ----------------------------------------------------------------------------
// Recordatorios
// ----------------------------------------------------------------------------
void Madre::RevisarAgenda(int64_t ahora) {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    int minuto = tm.tm_hour * 60 + tm.tm_min;
    if (minuto == ultimo_minuto_) return;
    ultimo_minuto_ = minuto;
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tm.tm_hour, tm.tm_min);
    int hoy = DiaPaginaHoy();

    std::vector<Aviso> nuevos;
    // Recordatorios de la página
    cJSON* raiz = cJSON_Parse(GetSeccion("recordatorios").c_str());
    const cJSON* r;
    cJSON_ArrayForEach(r, raiz) {
        if (!Bool(r, "activo", true) || !TieneDia(r, hoy) || Str(r, "hora") != hhmm) continue;
        Aviso a;
        a.texto = Str(r, "texto");
        if (a.texto.empty()) continue;
        a.repetir = Bool(r, "repetir", false);
        a.avisar = Bool(r, "avisar", false);
        nuevos.push_back(a);
    }
    cJSON_Delete(raiz);

    // Remedios del pastillero a la hora de cada franja
    static const char* const kFranjas[][2] = {{"manana", "mañana"}, {"tarde", "tarde"}, {"noche", "noche"}};
    raiz = cJSON_Parse(GetSeccion("pastillero").c_str());
    const cJSON* franjas = cJSON_GetObjectItemCaseSensitive(raiz, "franjas");
    for (auto& fr : kFranjas) {
        const cJSON* f = cJSON_GetObjectItemCaseSensitive(franjas, fr[0]);
        if (!cJSON_IsObject(f) || Str(f, "hora") != hhmm) continue;
        std::string lista;
        const cJSON* m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(f, "meds")) {
            if (!TieneDia(m, hoy) || Str(m, "nombre").empty()) continue;
            lista += (lista.empty() ? "" : "; ") + Str(m, "nombre");
            std::string dosis = Str(m, "dosis"), aspecto = Str(m, "aspecto");
            if (!dosis.empty()) lista += ", " + dosis;
            if (!aspecto.empty()) lista += " (" + aspecto + ")";
        }
        if (lista.empty()) continue;
        Aviso a;
        a.texto = std::string("Es hora de tomar los remedios de la ") + fr[1] + ", del pastillero: " + lista;
        a.repetir = true;
        a.avisar = true;
        a.remedio = true;
        nuevos.push_back(a);
    }
    cJSON_Delete(raiz);

    if (nuevos.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& a : nuevos) {
        a.creado_ms = ahora;
        a.proximo_ms = ahora;
        ESP_LOGI(TAG, "Recordatorio de las %s: %s", hhmm, a.texto.c_str());
        avisos_.push_back(a);
    }
}

void Madre::ProcesarAvisos(int64_t ahora) {
    std::vector<std::string> para_telegram;
    std::vector<std::string> decir;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Vencidos
        for (auto it = avisos_.begin(); it != avisos_.end();) {
            if (it->intentos >= 3 && ahora >= it->proximo_ms) {
                if (it->avisar) para_telegram.push_back("⚠️ Mamá no confirmó este recordatorio (se lo dije 3 veces): " + it->texto);
                it = avisos_.erase(it);
            } else if (it->intentos == 0 && ahora - it->creado_ms > 2 * 3600 * 1000) {
                if (it->avisar) para_telegram.push_back("⚠️ No le pude decir a mamá este recordatorio (no estaba en la pieza o dormía): " + it->texto);
                it = avisos_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& a : avisos_) {
            if (a.intentos < 3 && ahora >= a.proximo_ms) decir.push_back(a.texto);
        }
    }
    for (auto& m : para_telegram) {
        ESP_LOGW(TAG, "%s", m.c_str());
        TelegramBot::GetInstance().QueueMessage(m);
    }
    if (decir.empty() || !PuedeHablar(ahora, false)) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        aviso_actual_.clear();
        for (auto it = avisos_.begin(); it != avisos_.end();) {
            if (it->intentos < 3 && ahora >= it->proximo_ms) {
                aviso_actual_.push_back(it->texto + (it->repetir ? " [pedir confirmación]" : ""));
                it->intentos++;
                if (!it->repetir) {
                    it = avisos_.erase(it);       // se dice una sola vez
                    continue;
                }
                it->proximo_ms = ahora + 10 * 60 * 1000;   // repetir en 10 minutos
            }
            ++it;
        }
    }
    Hablar("Tengo un aviso");
}

void Madre::ConfirmarAvisos(const char* como) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t antes = avisos_.size();
    for (auto it = avisos_.begin(); it != avisos_.end();) {
        if (it->intentos >= 1) {
            ESP_LOGI(TAG, "Recordatorio confirmado (%s): %s", como, it->texto.c_str());
            it = avisos_.erase(it);
        } else {
            ++it;
        }
    }
    if (avisos_.size() != antes) aviso_actual_.clear();
}

std::string Madre::TextoSaludo() {
    std::vector<std::string> frases;
    bool alternar, decir_fecha;
    std::string nombre;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        frases = aj_.saludo_frases;
        alternar = aj_.saludo_alternar;
        decir_fecha = aj_.saludo_decir_fecha;
        nombre = aj_.mama_apodo;
    }
    if (nombre.empty()) nombre = "ella";
    std::string frase = frases.empty() ? "Hola, ¿te despertaste?" : frases[0];
    if (alternar && !frases.empty()) {
        if (indice_frase_ == 0) indice_frase_ = esp_random() % frases.size();
        frase = frases[indice_frase_ % frases.size()];
        indice_frase_++;
    }
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    std::string fecha = FechaEnPalabras();
    if (RelojEnHora() && tm.tm_hour < 6) {
        return "Es de madrugada (" + fecha + "). Decile a " + nombre + " en voz suave, en una o dos frases "
               "cortas, que es de noche, qué hora es y que puede seguir descansando. No le hagas preguntas.";
    }
    std::string s = "Saludá a " + nombre + " con esta frase, tal cual: \"" + frase + "\".";
    if (decir_fecha && !fecha.empty()) {
        s += " Después contale, con calma, que hoy es " + fecha + " (decí la hora en palabras simples).";
    }
    s += " Si la frase termina en pregunta, esperá su respuesta y seguí la charla con frases cortas.";
    bool noticias;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        noticias = aj_.noticias_activo && aj_.noticias_en_saludo;
    }
    if (noticias && RelojEnHora() && tm.tm_hour >= 6 && tm.tm_hour < 13) {
        s += " Más adelante en la charla, preguntale si quiere escuchar las noticias de hoy; si dice que sí, "
             "usá self.madre.get_news.";
    }
    return s;
}

// ----------------------------------------------------------------------------
// Noticias (RSS)
// ----------------------------------------------------------------------------
namespace {

const char* UrlNoticias(const std::string& fuente) {
    if (fuente == "lanacion") return "https://www.lanacion.com.ar/arc/outboundfeeds/rss/?outputType=xml";
    if (fuente == "google") return "https://news.google.com/rss?hl=es-419&gl=AR&ceid=AR:es-419";
    return "https://www.infobae.com/arc/outboundfeeds/rss/";
}

void AgregarUtf8(std::string& s, unsigned cp) {
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

const char* EntidadConNombre(const std::string& e) {
    static const char* const kTabla[][2] = {
        {"aacute", "á"}, {"eacute", "é"}, {"iacute", "í"}, {"oacute", "ó"}, {"uacute", "ú"},
        {"Aacute", "Á"}, {"Eacute", "É"}, {"Iacute", "Í"}, {"Oacute", "Ó"}, {"Uacute", "Ú"},
        {"ntilde", "ñ"}, {"Ntilde", "Ñ"}, {"uuml", "ü"}, {"Uuml", "Ü"}, {"iexcl", "¡"}, {"iquest", "¿"},
        {"laquo", "«"}, {"raquo", "»"}, {"ldquo", "“"}, {"rdquo", "”"}, {"lsquo", "‘"}, {"rsquo", "’"},
        {"hellip", "…"}, {"ndash", "–"}, {"mdash", "—"}, {"deg", "°"},
    };
    for (auto& par : kTabla) {
        if (e == par[0]) return par[1];
    }
    return nullptr;
}

// Quita CDATA y etiquetas, y traduce &amp; &quot; &#233; etc.
std::string LimpiarTitulo(std::string s) {
    size_t c = s.find("<![CDATA[");
    if (c != std::string::npos) {
        size_t f = s.find("]]>", c);
        s = s.substr(c + 9, f == std::string::npos ? std::string::npos : f - c - 9);
    }
    std::string r;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '<') {                      // etiquetas internas
            size_t f = s.find('>', i);
            if (f == std::string::npos) break;
            i = f;
            continue;
        }
        if (s[i] == '&') {
            size_t f = s.find(';', i);
            if (f != std::string::npos && f - i <= 8) {
                std::string ent = s.substr(i + 1, f - i - 1);
                if (ent == "amp") r += '&';
                else if (ent == "quot") r += '"';
                else if (ent == "apos" || ent == "#39") r += '\'';
                else if (ent == "lt") r += '<';
                else if (ent == "gt") r += '>';
                else if (ent == "nbsp") r += ' ';
                else if (const char* u = EntidadConNombre(ent)) r += u;
                else if (!ent.empty() && ent[0] == '#') {
                    unsigned cp = (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X'))
                                      ? strtoul(ent.c_str() + 2, nullptr, 16) : strtoul(ent.c_str() + 1, nullptr, 10);
                    if (cp) AgregarUtf8(r, cp);
                } else {
                    r += s.substr(i, f - i + 1);
                }
                i = f;
                continue;
            }
        }
        r += s[i];
    }
    // espacios de más
    std::string limpio;
    bool esp = true;
    for (char ch : r) {
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
        if (ch == ' ' && esp) continue;
        esp = (ch == ' ');
        limpio += ch;
    }
    while (!limpio.empty() && limpio.back() == ' ') limpio.pop_back();
    return limpio;
}

// Baja el RSS y devuelve hasta "maximo" títulos de noticias (lee sólo el principio del archivo).
bool BajarTitulos(const std::string& fuente, int maximo, std::vector<std::string>& titulos) {
    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) return false;
    std::string url = UrlNoticias(fuente);
    for (int redirecciones = 0; redirecciones < 3; redirecciones++) {
        auto http = network->CreateHttp(4);
        if (!http) return false;
        http->SetTimeout(10000);
        http->SetHeader("User-Agent", "Mozilla/5.0 (XiaoZhi)");
        if (auto abierto = http->Open("GET", url); !abierto) {
            ESP_LOGW(TAG, "Noticias: no se pudo conectar (%s)", abierto.error().ToString().c_str());
            return false;
        }
        auto estado = http->GetStatusCode();
        int codigo = estado ? *estado : 0;
        if (codigo == 301 || codigo == 302 || codigo == 307 || codigo == 308) {
            std::string destino = http->GetResponseHeader("Location");
            http->Close();
            if (destino.empty()) return false;
            url = destino;
            continue;
        }
        if (codigo != 200) {
            ESP_LOGW(TAG, "Noticias: HTTP %d", codigo);
            http->Close();
            return false;
        }

        std::string buf;
        std::vector<char> trozo_mem(2048);        // en memoria general, no en la pila
        char* trozo = trozo_mem.data();
        size_t leido = 0;
        bool en_item = false;
        while ((int)titulos.size() < maximo && leido < 400 * 1024) {
            auto r = http->Read(trozo, trozo_mem.size());
            if (!r || *r <= 0) break;
            leido += *r;
            buf.append(trozo, *r);
            while (true) {
                if (!en_item) {
                    size_t i = buf.find("<item");
                    if (i == std::string::npos) {
                        if (buf.size() > 16) buf.erase(0, buf.size() - 16);
                        break;
                    }
                    buf.erase(0, i + 5);
                    en_item = true;
                }
                size_t a = buf.find("<title");
                size_t fin_item = buf.find("</item>");
                if (fin_item != std::string::npos && (a == std::string::npos || fin_item < a)) {
                    buf.erase(0, fin_item + 7);       // item sin título
                    en_item = false;
                    continue;
                }
                if (a == std::string::npos) break;
                size_t b = buf.find('>', a);
                size_t c = buf.find("</title>", a);
                if (b == std::string::npos || c == std::string::npos) break;   // falta leer más
                std::string titulo = LimpiarTitulo(buf.substr(b + 1, c - b - 1));
                if (fuente == "google") {                // "Título - Diario"
                    size_t g = titulo.rfind(" - ");
                    if (g != std::string::npos && g > 20) titulo.resize(g);
                }
                if (!titulo.empty()) titulos.push_back(titulo);
                buf.erase(0, c + 8);
                en_item = false;
                if ((int)titulos.size() >= maximo) break;
            }
        }
        http->Close();
        return !titulos.empty();
    }
    return false;
}

}  // namespace

void Madre::ActualizarNoticias(int64_t ahora) {
    bool activo;
    std::string fuente;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activo = aj_.noticias_activo;
        fuente = aj_.noticias_fuente;
    }
    if (!activo) return;
    bool vencidas = noticias_ms_ == 0 || ahora - noticias_ms_ > 30 * 60 * 1000 || fuente != noticias_de_;
    if (!vencidas) return;
    if (noticias_intento_ms_ != 0 && ahora - noticias_intento_ms_ < 5 * 60 * 1000) return;   // reintento cada 5 min
    // Sólo con el equipo libre, para no cortar la voz de XiaoZhi
    if (Application::GetInstance().GetDeviceState() != kDeviceStateIdle) return;
    if (TelegramBot::GetInstance().IsVoiceBusy()) return;
    noticias_intento_ms_ = ahora;

    std::vector<std::string> titulos;
    if (BajarTitulos(fuente, 15, titulos)) {
        std::lock_guard<std::mutex> lock(mutex_);
        noticias_ = titulos;
        noticias_de_ = fuente;
        noticias_ms_ = ahora;
        ESP_LOGI(TAG, "Noticias (%s): %u títulos. Primero: %s", fuente.c_str(), (unsigned)titulos.size(),
                 titulos[0].c_str());
    }
}

std::string Madre::TextoNoticias() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!aj_.noticias_activo) return "Las noticias están desactivadas en la configuración.";
    if (noticias_.empty()) {
        return "Todavía no pude conseguir las noticias (sin conexión). Decile que lo intentás más tarde.";
    }
    int64_t minutos = (esp_timer_get_time() / 1000 - noticias_ms_) / 60000;
    std::string s = "Titulares recientes (de hace " + std::to_string(minutos) + " minutos):\n";
    for (auto& n : noticias_) s += "- " + n + "\n";
    s += "\nElegí las " + std::to_string(aj_.noticias_cantidad) +
         " noticias más importantes de Argentina (si no hay suficientes, completá con las del mundo más "
         "relevantes). Evitá crímenes, accidentes, tragedias y temas que puedan angustiarla. Contalas una "
         "por una, en frases cortas y palabras simples, sin leer los títulos tal cual. Al final preguntale si "
         "quiere saber más de alguna.";
    return s;
}

// ----------------------------------------------------------------------------
// Textos para XiaoZhi
// ----------------------------------------------------------------------------
std::string Madre::TextoFamilia() {
    cJSON* raiz = cJSON_Parse(GetSeccion("familia").c_str());
    std::string s;
    const cJSON* p;
    cJSON_ArrayForEach(p, raiz) {
        std::string nombre = Str(p, "nombre");
        if (nombre.empty()) continue;
        s += "- " + nombre;
        std::string apodo = Str(p, "apodo"), par = Str(p, "parentesco"), tel = Str(p, "telefono"),
                    dir = Str(p, "direccion"), cumple = Str(p, "cumple"), notas = Str(p, "notas");
        if (!apodo.empty()) s += " (mamá le dice \"" + apodo + "\")";
        if (!par.empty()) s += ", " + par;
        if (!tel.empty()) s += ". Teléfono: " + tel;
        if (!dir.empty()) s += ". Dirección: " + dir;
        if (!cumple.empty()) s += ". Cumpleaños: " + cumple;
        if (!notas.empty()) s += ". Nota: " + notas;
        s += "\n";
    }
    cJSON_Delete(raiz);
    return s.empty() ? "Todavía no hay familiares cargados." : s;
}

// "La usuaria se llama Ema Yolanda; le gusta que la llamen Yoli. Cumpleaños: 05/05/1943.\n"
std::string Madre::TextoMama() {
    cJSON* raiz = cJSON_Parse(GetSeccion("ajustes").c_str());
    const cJSON* m = cJSON_GetObjectItemCaseSensitive(raiz, "mama");
    std::string nombre = Str(m, "nombre"), apodo = Str(m, "apodo"), cumple = Str(m, "cumple");
    cJSON_Delete(raiz);
    if (nombre.empty() && apodo.empty()) return "";
    std::string s = "La usuaria";
    if (!nombre.empty()) s += " se llama " + nombre;
    if (!apodo.empty()) s += (nombre.empty() ? " " : "; ") + std::string("le gusta que la llamen ") + apodo;
    if (!cumple.empty()) s += ". Cumpleaños: " + cumple;
    return s + ".\n";
}

std::string Madre::TextoPastillero(bool solo_hoy) {
    static const char* const kFranjas[][2] = {{"manana", "Mañana"}, {"tarde", "Tarde"}, {"noche", "Noche"}};
    cJSON* raiz = cJSON_Parse(GetSeccion("pastillero").c_str());
    const cJSON* franjas = cJSON_GetObjectItemCaseSensitive(raiz, "franjas");
    int hoy = DiaPaginaHoy();
    std::string s;
    if (solo_hoy) s = std::string("Hoy es ") + kDiasPagina[hoy] + ".\n";
    for (auto& f : kFranjas) {
        const cJSON* fr = cJSON_GetObjectItemCaseSensitive(franjas, f[0]);
        if (!cJSON_IsObject(fr)) continue;
        std::string hora = Str(fr, "hora");
        std::string linea;
        const cJSON* meds = cJSON_GetObjectItemCaseSensitive(fr, "meds");
        const cJSON* m;
        cJSON_ArrayForEach(m, meds) {
            if (solo_hoy && !TieneDia(m, hoy)) continue;
            std::string nombre = Str(m, "nombre"), dosis = Str(m, "dosis"), aspecto = Str(m, "aspecto");
            if (nombre.empty()) continue;
            linea += "  * " + nombre;
            if (!dosis.empty()) linea += ", " + dosis;
            if (!aspecto.empty()) linea += " (es " + aspecto + ")";
            if (!solo_hoy) linea += "; " + TextoDias(m);
            linea += "\n";
        }
        if (linea.empty()) continue;
        s += std::string("- ") + f[1] + (hora.empty() ? "" : " (a las " + hora + ")") + ":\n" + linea;
    }
    cJSON_Delete(raiz);
    if (s.empty() || (solo_hoy && s.find("- ") == std::string::npos)) {
        return solo_hoy ? s + "Hoy no hay remedios cargados en el pastillero."
                        : "Todavía no hay remedios cargados en el pastillero.";
    }
    return s;
}

std::string Madre::TextoRecordatorios(bool solo_hoy) {
    cJSON* raiz = cJSON_Parse(GetSeccion("recordatorios").c_str());
    int hoy = DiaPaginaHoy();
    std::string s;
    const cJSON* r;
    cJSON_ArrayForEach(r, raiz) {
        if (!Bool(r, "activo", true)) continue;
        if (solo_hoy && !TieneDia(r, hoy)) continue;
        std::string texto = Str(r, "texto"), hora = Str(r, "hora");
        if (texto.empty()) continue;
        s += "- " + hora + ": " + texto;
        if (!solo_hoy) s += " (" + TextoDias(r) + ")";
        s += "\n";
    }
    cJSON_Delete(raiz);
    if (s.empty()) return solo_hoy ? "Hoy no hay recordatorios." : "No hay recordatorios cargados.";
    return s;
}

// ----------------------------------------------------------------------------
// Herramientas MCP
// ----------------------------------------------------------------------------
void Madre::RegistrarHerramientas() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.madre.get_today",
        "Get today's date, the current local time in Argentina and what is planned for today "
        "(reminders and pillbox times). Use it whenever the user asks what day or time it is, "
        "what day of the week it is, or what she has to do today. The user is an elderly woman "
        "who often loses track of time: answer warmly, in Spanish, saying the full day (weekday, "
        "day and month) and the time in simple words.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::string fecha = FechaEnPalabras();
            if (fecha.empty()) {
                return std::string("El reloj todavía no está en hora (sin conexión a Internet). "
                                   "Decile que en un momento vas a saber la hora.");
            }
            return TextoMama() + "Ahora: " + fecha + ".\nRecordatorios de hoy:\n" + TextoRecordatorios(true) +
                   "\nPastillero de hoy:\n" + TextoPastillero(true);
        });

    mcp.AddTool("self.madre.get_family",
        "Get the list of the user's family members and close people: name, the nickname she uses, "
        "relationship, phone number, address, birthday and notes. Use it when she asks about a "
        "relative or friend: who someone is, their phone number, where they live, when their "
        "birthday is. Match nicknames too. Read phone numbers slowly, digit by digit in groups.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue { return TextoFamilia(); });

    mcp.AddTool("self.madre.get_pillbox",
        "Get what medicine goes in each compartment of the user's pillbox and at what time she "
        "takes it (Mañana / Tarde / Noche). Use it when she asks which pills to take, what a pill "
        "is for, or what is in the pillbox. 'today_only' = true to get only today's medicines.",
        PropertyList({Property("today_only", kPropertyTypeBoolean, true)}),
        [this](const PropertyList& props) -> ReturnValue {
            return TextoPastillero(props["today_only"].value<bool>());
        });

    mcp.AddTool("self.madre.get_reminders",
        "Get the user's daily reminders (what XiaoZhi reminds her and when). Use it when she asks "
        "what she has to do, or about her reminders. 'today_only' = true for today only.",
        PropertyList({Property("today_only", kPropertyTypeBoolean, true)}),
        [this](const PropertyList& props) -> ReturnValue {
            return TextoRecordatorios(props["today_only"].value<bool>());
        });

    mcp.AddTool("self.madre.get_notice",
        "Call it ONLY when the user says exactly 'Tengo un aviso' (the device says this phrase by itself "
        "when it is time for a reminder). It returns the reminders you must tell the user now. Tell "
        "them warmly, in few words, in Spanish, addressing her by her name. If an item says "
        "'[pedir confirmación]', at the end ask her to touch the button or say 'listo' when she has done it "
        "(do not read the brackets aloud).",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            if (aviso_actual_.empty()) return std::string("No hay avisos pendientes.");
            std::string s = "Recordatorios para " + (aj_.mama_apodo.empty() ? std::string("la usuaria") : aj_.mama_apodo) + ":\n";
            for (auto& a : aviso_actual_) s += "- " + a + "\n";
            return s;
        });

    mcp.AddTool("self.madre.confirm_notice",
        "Call it when, after a reminder, the user says she already did it or understood ('listo', "
        "'ya está', 'ya lo tomé', 'ya los tomé', 'entendido', 'ya lo hice'). It stops the reminder from "
        "being repeated. Then answer briefly and kindly.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            ConfirmarAvisos("voz");
            return std::string("Listo, quedó confirmado.");
        });

    mcp.AddTool("self.madre.get_greeting",
        "Call it ONLY when the user says exactly 'Me desperté' (the device says this phrase by itself "
        "when it detects that she just woke up). It returns how to greet her. Follow it exactly.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue { return TextoSaludo(); });

    mcp.AddTool("self.madre.get_news",
        "Get today's news headlines from Argentina. Use it when the user asks for the news ('¿qué "
        "noticias hay?', '¿qué pasa en el país?', 'contame las noticias'). Follow the instructions "
        "included in the result.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue { return TextoNoticias(); });

    mcp.AddTool("self.madre.get_config_address",
        "Get the web address of this device's configuration page and show it on the screen. Use it "
        "when someone asks where to connect to configure the device, its IP address or its web "
        "page ('¿dónde me conecto?', '¿cuál es tu dirección?'). Say the address slowly, "
        "number by number, for example: 'ciento noventa y dos punto ciento sesenta y ocho...'.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::string dir = DireccionPagina();
            if (dir.empty()) {
                return std::string("No estoy conectado a ningún WiFi en este momento.");
            }
            auto display = Board::GetInstance().GetDisplay();
            if (display) {
                display->ShowNotification(dir, 30000);
            }
            return "La página de configuración está en " + dir +
                   " (hay que estar conectado al mismo WiFi). La dirección queda 30 segundos en la pantalla.";
        });
}
