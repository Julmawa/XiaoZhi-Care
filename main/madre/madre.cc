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
#include <algorithm>
#include <cctype>
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

// "2026-11-28" -> "viernes 28/11"
std::string FechaLinda(const std::string& iso) {
    int y = 0, m = 0, d = 0;
    if (sscanf(iso.c_str(), "%d-%d-%d", &y, &m, &d) != 3) return iso;
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = m - 1;
    t.tm_mday = d;
    t.tm_hour = 12;
    t.tm_isdst = -1;
    mktime(&t);
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %d/%d", kDias[t.tm_wday], d, m);
    return buf;
}

std::string ClaveCompra(const std::string& s);   // más abajo (minúsculas, sin acentos)

// Cuántas pastillas hay en el casillero: "2,5 mg, 1 pastilla" -> 1 ; "media pastilla" -> 1 ; sin dato -> 1
double ContarPastillas(const std::string& dosis) {
    std::string d = " " + ClaveCompra(dosis) + " ";
    for (char& c : d) {
        if (c == ',' || c == ';' || c == '(' || c == ')') c = ' ';
    }
    for (const char* unidad : {"pastilla", "comprimido", "capsula", "tableta"}) {
        size_t p = d.find(unidad);
        if (p == std::string::npos) continue;
        // palabra anterior
        size_t fin = d.find_last_not_of(' ', p - 1);
        if (fin == std::string::npos) return 1;
        size_t ini = d.find_last_of(' ', fin);
        std::string w = d.substr(ini + 1, fin - ini);
        if (w == "media" || w == "medio") return 1;    // media pastilla: igual es una en el casillero
        if (w == "un" || w == "una") return 1;
        if (w == "dos") return 2;
        if (w == "tres") return 3;
        if (w == "cuatro") return 4;
        std::string n = w;
        for (char& c : n) {
            if (c == ',') c = '.';
        }
        double v = atof(n.c_str());
        if (v > 0 && v < 20) return (double)(int)(v + 0.99);   // 1,5 -> 2 pedazos en el casillero
        return 1;
    }
    return 1;
}

// 2 -> "2 pastillas", 1 -> "1 pastilla", 0.5 -> "media pastilla", 1.5 -> "1 pastilla y media"
std::string TextoPastillas(double n) {
    int enteras = (int)n;
    bool media = (n - enteras) > 0.25;
    if (enteras == 0) return media ? "media pastilla" : "las pastillas";
    std::string s = std::to_string(enteras) + (enteras == 1 ? " pastilla" : " pastillas");
    if (media) s += " y media";
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
    RegistrarComprasTelegram();
    RegistrarRecordatoriosTelegram();
    RegistrarResumenTelegram();
    RegistrarEmergenciaTelegram();
    RegistrarAjustesTelegram();
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
        ProcesarEmergencia(ahora);
        AplicarEstados();
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
                if (RelojEnHora()) {
                    time_t t = time(nullptr);
                    struct tm tm;
                    localtime_r(&t, &tm);
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
                    Anotar(std::string("☀️ ") + buf + " la saludé cuando se levantó");
                }
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
    compras_ = Leer("compras", "[]");
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
    if (seccion == "compras")       { *clave_nvs = "compras";  *es_lista = true;  return &compras_; }
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

    static const char* const kSecciones[] = {"/api/familia", "/api/recordatorios", "/api/pastillero",
                                             "/api/ajustes", "/api/compras"};
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
               ",\"pastillero\":" + m.pastillero_ + ",\"ajustes\":" + m.ajustes_ +
               ",\"compras\":" + m.compras_;
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
    const cJSON* res = cJSON_GetObjectItemCaseSensitive(raiz, "resumen");
    a.resumen_activo = Bool(res, "activo", true);
    if (Str(res, "hora").size() == 5) a.resumen_hora = Str(res, "hora");
    const cJSON* rem = cJSON_GetObjectItemCaseSensitive(raiz, "remedios");
    a.remedios_avisos = num(rem, "avisos", 3);
    a.remedios_minutos = num(rem, "minutos", 10);
    if (a.remedios_avisos < 1) a.remedios_avisos = 1;
    if (a.remedios_avisos > 10) a.remedios_avisos = 10;
    if (a.remedios_minutos < 1) a.remedios_minutos = 1;
    if (a.remedios_minutos > 120) a.remedios_minutos = 120;
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
// Ajustes por Telegram (/ajustes, /estado): lo mismo que la pestaña Ajustes de la página
// ----------------------------------------------------------------------------
namespace {
struct Interruptor {
    const char* id;        // callback "aj:<id>"
    const char* seccion;   // objeto dentro de "ajustes"
    const char* campo;
    bool por_defecto;
    const char* nombre;
};
const Interruptor kInterruptores[] = {
    {"radar", "radar", "activo", true, "📡 Radar (saber si está en la pieza)"},
    {"aviradar", "radar", "avisosTelegram", false, "🔔 Avisos del radar por Telegram"},
    {"saludo", "saludo", "activo", true, "🌅 Saludo cuando se levanta"},
    {"bienestar", "bienestar", "activo", true, "💛 Aviso si no hay señales"},
    {"noticias", "noticias", "activo", true, "📰 Noticias"},
    {"resumen", "resumen", "activo", true, "📋 Resumen del día"},
};
}  // namespace

// Cambia un valor de "ajustes" (seccion vacía = en la raíz), lo guarda y lo aplica.
bool Madre::CambiarAjuste(const char* seccion, const char* campo, cJSON* valor) {
    cJSON* raiz = cJSON_Parse(GetSeccion("ajustes").c_str());
    if (!cJSON_IsObject(raiz)) {
        cJSON_Delete(raiz);
        raiz = cJSON_CreateObject();
    }
    cJSON* destino = raiz;
    if (seccion && seccion[0]) {
        destino = cJSON_GetObjectItemCaseSensitive(raiz, seccion);
        if (!cJSON_IsObject(destino)) {
            cJSON_DeleteItemFromObjectCaseSensitive(raiz, seccion);
            destino = cJSON_AddObjectToObject(raiz, seccion);
        }
    }
    if (cJSON_GetObjectItemCaseSensitive(destino, campo)) {
        cJSON_ReplaceItemInObjectCaseSensitive(destino, campo, valor);
    } else {
        cJSON_AddItemToObject(destino, campo, valor);
    }
    char* txt = cJSON_PrintUnformatted(raiz);
    std::string nuevo = txt ? txt : "";
    if (txt) cJSON_free(txt);
    cJSON_Delete(raiz);
    if (nuevo.empty() || nuevo.size() > kMaxSeccion || !Guardar("ajustes", nuevo)) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ajustes_ = nuevo;
    }
    AplicarAjustes();
    return true;
}

std::string Madre::TextoAjustes(std::string& botones) {
    cJSON* raiz = cJSON_Parse(GetSeccion("ajustes").c_str());
    Ajustes a;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        a = aj_;
    }
    std::string t = "⚙️ Ajustes del equipo de mamá\n(tocá un botón para prender o apagar)\n\n";
    botones = "{\"inline_keyboard\":[";
    for (const auto& i : kInterruptores) {
        bool v = Bool(cJSON_GetObjectItemCaseSensitive(raiz, i.seccion), i.campo, i.por_defecto);
        std::string fila = std::string(v ? "✅ " : "⬜ ") + i.nombre;
        botones += "[{\"text\":\"" + fila + "\",\"callback_data\":\"aj:" + i.id + "\"}],";
    }
    cJSON_Delete(raiz);
    botones += "[{\"text\":\"🔉 Volumen -10\",\"callback_data\":\"aj:vol-\"},"
               "{\"text\":\"🔊 Volumen +10\",\"callback_data\":\"aj:vol+\"}],"
               "[{\"text\":\"💊 Avisos -1\",\"callback_data\":\"aj:av-\"},"
               "{\"text\":\"💊 Avisos +1\",\"callback_data\":\"aj:av+\"}],"
               "[{\"text\":\"⏱ Minutos -5\",\"callback_data\":\"aj:min-\"},"
               "{\"text\":\"⏱ Minutos +5\",\"callback_data\":\"aj:min+\"}]]}";
    t += "🔊 Volumen: " + std::to_string(a.volumen) + "\n";
    t += "📋 Resumen del día a las " + a.resumen_hora + "\n";
    t += "💛 Aviso si no hay señales en " + std::to_string(a.bienestar_horas) + " horas\n";
    t += "🌅 Saludo después de " + std::to_string(a.saludo_horas_quieta) + " horas quieta\n";
    t += "📰 Noticias: " + std::to_string(a.noticias_cantidad) + " por vez\n";
    t += "💊 Remedios: " + std::to_string(a.remedios_avisos) +
         (a.remedios_avisos == 1 ? " aviso" : " avisos, cada " + std::to_string(a.remedios_minutos) + " minutos") +
         "\n\nTambién podés escribir:\n"
         "/ajustes avisos 4  (avisos de remedios, de 1 a 10)\n"
         "/ajustes minutos 15  (entre aviso y aviso, de 1 a 120)\n"
         "/ajustes volumen 60\n"
         "/ajustes resumen 21:30\n"
         "/ajustes bienestar 12  (horas)\n"
         "/ajustes saludo 3  (horas quieta)\n"
         "/ajustes noticias 4  (cuántas, de 1 a 5)";
    return t;
}

void Madre::RegistrarAjustesTelegram() {
    auto& tg = TelegramBot::GetInstance();
    tg.AddAdminCommand(
        [this](const std::string& cmd, const std::string& arg, std::string& texto, std::string& b) {
            if (cmd == "/estado") {
                cJSON* e = cJSON_Parse(EstadoJson().c_str());
                texto = "📟 Estado del equipo de mamá\n"
                        "Hora: " + Str(e, "hora") + "\n"
                        "WiFi: " + Str(e, "wifi") + "\n"
                        "Dirección de la página: http://" + Str(e, "ip") + "\n"
                        "Presencia: " + Str(e, "presencia");
                cJSON_Delete(e);
                return true;
            }
            if (cmd != "/ajustes") return false;
            if (arg.empty()) {
                texto = TextoAjustes(b);
                return true;
            }
            std::string que = arg.substr(0, arg.find(' '));
            std::string valor = arg.find(' ') == std::string::npos ? "" : arg.substr(arg.find(' ') + 1);
            while (!valor.empty() && valor.back() == ' ') valor.pop_back();
            for (auto& c : que) c = (char)tolower((unsigned char)c);
            char* fin = nullptr;
            long n = strtol(valor.c_str(), &fin, 10);
            bool es_numero = !valor.empty() && fin && *fin == '\0';
            bool ok = false;
            if (que == "volumen" && es_numero && n >= 30 && n <= 100) {
                ok = CambiarAjuste("", "volumen", cJSON_CreateNumber(n));
            } else if ((que == "avisos" || que == "remedios") && es_numero && n >= 1 && n <= 10) {
                ok = CambiarAjuste("remedios", "avisos", cJSON_CreateNumber(n));
            } else if (que == "minutos" && es_numero && n >= 1 && n <= 120) {
                ok = CambiarAjuste("remedios", "minutos", cJSON_CreateNumber(n));
            } else if (que == "bienestar" && es_numero && n >= 1 && n <= 48) {
                ok = CambiarAjuste("bienestar", "horas", cJSON_CreateNumber(n));
            } else if (que == "saludo" && es_numero && n >= 1 && n <= 12) {
                ok = CambiarAjuste("saludo", "horasQuieta", cJSON_CreateNumber(n));
            } else if (que == "noticias" && es_numero && n >= 1 && n <= 5) {
                ok = CambiarAjuste("noticias", "cantidad", cJSON_CreateNumber(n));
            } else if (que == "resumen") {
                int h = -1, m = -1;
                if (sscanf(valor.c_str(), "%d:%d", &h, &m) == 2 && h >= 0 && h <= 23 && m >= 0 && m <= 59) {
                    char hora[8];
                    snprintf(hora, sizeof(hora), "%02d:%02d", h, m);
                    ok = CambiarAjuste("resumen", "hora", cJSON_CreateString(hora));
                }
            } else {
                texto = "No conozco ese ajuste. Escribí /ajustes para ver la lista.";
                return true;
            }
            if (!ok) {
                texto = "No lo pude cambiar. Revisá el valor (por ejemplo: /ajustes volumen 60, de 30 a 100).";
                return true;
            }
            texto = "✅ Listo, cambiado.\n\n" + TextoAjustes(b);
            return true;
        },
        "/ajustes - ver y cambiar los ajustes del equipo (volumen, radar, saludo, noticias...)\n"
        "/estado - hora, WiFi y dirección de la página del equipo");

    tg.AddCallbackHandler([this](const std::string& data, std::string& texto, std::string& b) {
        if (data.rfind("aj:", 0) != 0) return false;
        std::string id = data.substr(3);
        bool ok = false;
        if (id == "vol+" || id == "vol-") {
            int v;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                v = aj_.volumen;
            }
            v += id == "vol+" ? 10 : -10;
            if (v > 100) v = 100;
            if (v < 30) v = 30;
            ok = CambiarAjuste("", "volumen", cJSON_CreateNumber(v));
        } else if (id == "av+" || id == "av-" || id == "min+" || id == "min-") {
            int avisos, minutos;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                avisos = aj_.remedios_avisos;
                minutos = aj_.remedios_minutos;
            }
            if (id[0] == 'a') {
                avisos = std::max(1, std::min(10, avisos + (id == "av+" ? 1 : -1)));
                ok = CambiarAjuste("remedios", "avisos", cJSON_CreateNumber(avisos));
            } else {
                minutos = id == "min+" ? minutos + 5 : minutos - 5;
                minutos = std::max(1, std::min(120, minutos));
                ok = CambiarAjuste("remedios", "minutos", cJSON_CreateNumber(minutos));
            }
        } else {
            for (const auto& i : kInterruptores) {
                if (id != i.id) continue;
                cJSON* raiz = cJSON_Parse(GetSeccion("ajustes").c_str());
                bool v = Bool(cJSON_GetObjectItemCaseSensitive(raiz, i.seccion), i.campo, i.por_defecto);
                cJSON_Delete(raiz);
                ok = CambiarAjuste(i.seccion, i.campo, cJSON_CreateBool(!v));
            }
        }
        texto = (ok ? "✅ Listo, cambiado.\n\n" : "No lo pude cambiar.\n\n") + TextoAjustes(b);
        return true;
    });
}

// ----------------------------------------------------------------------------
// Presencia: dormida, despierta, ausente
// ----------------------------------------------------------------------------
void Madre::ActualizarPresencia(int64_t ahora) {
    int64_t dt = ultimo_tick_ms_ ? ahora - ultimo_tick_ms_ : 0;
    ultimo_tick_ms_ = ahora;

    bool libre = Application::GetInstance().GetDeviceState() == kDeviceStateIdle;
    if (estaba_libre_ && !libre && ahora - activacion_propia_ms_ > 15000) {
        // Empezó una charla (ella tocó el botón o le habló): para el resumen
        std::lock_guard<std::mutex> lock(diario_mutex_);
        diario_.charlas++;
        diario_.ultima_charla = time(nullptr);
    }
    estaba_libre_ = libre;
    if (!libre) {
        ultima_conversacion_ms_ = ahora;
        ultima_senal_ms_ = ahora;
        CortarSueno(ahora, 0);      // está despierta, hablando con XiaoZhi
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
        if (nadie_ms_ > 10 * 60 * 1000) CortarSueno(ahora, nadie_ms_);   // se fue de la pieza
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
            CortarSueno(ahora, 20000);
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

// Campana de notificaciones, incluida en el firmware (CMakeLists: madre/campana.ogg)
extern "C" {
extern const char campana_ogg_start[] asm("_binary_campana_ogg_start");
extern const char campana_ogg_end[] asm("_binary_campana_ogg_end");
}

std::string_view Madre::SonidoCampana() {
    return std::string_view(campana_ogg_start, campana_ogg_end - campana_ogg_start);
}

void Madre::Hablar(const char* frase) {
    auto& app = Application::GetInstance();
    app.PlaySound(SonidoCampana());
    vTaskDelay(pdMS_TO_TICKS(2300));      // que termine la campana (2,1 s)
    if (app.GetDeviceState() != kDeviceStateIdle) return;
    ESP_LOGI(TAG, "Activando al asistente: %s", frase);
    activacion_propia_ms_ = esp_timer_get_time() / 1000;
    app.WakeWordInvoke(frase);
    for (int i = 0; i < 20 && app.GetDeviceState() == kDeviceStateIdle; i++) {
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    ultima_conversacion_ms_ = esp_timer_get_time() / 1000;
    activacion_propia_ms_ = ultima_conversacion_ms_;
}

void Madre::NotificarBoton() {
    ultima_senal_ms_ = esp_timer_get_time() / 1000;
    {
        std::lock_guard<std::mutex> lock(diario_mutex_);
        diario_.botones++;
    }
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
        char buf[32];
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
            Anotar("⚠️ " + hora_de(0) + " te avisé que no había señales de mamá");
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
    char hhmm[32];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", tm.tm_hour, tm.tm_min);
    int hoy = DiaPaginaHoy();
    char hoy_iso[32];
    snprintf(hoy_iso, sizeof(hoy_iso), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    RevisarResumen(hhmm, minuto);
    static bool limpiado = false;
    if (!limpiado || strcmp(hhmm, "00:01") == 0) {      // borrar los de fechas que ya pasaron
        limpiado = true;
        LimpiarRecordatoriosViejos();
    }

    std::vector<Aviso> nuevos;
    // Recordatorios de la página
    cJSON* raiz = cJSON_Parse(GetSeccion("recordatorios").c_str());
    const cJSON* r;
    cJSON_ArrayForEach(r, raiz) {
        if (!Bool(r, "activo", true) || Str(r, "hora") != hhmm) continue;
        std::string fecha = Str(r, "fecha");          // "2026-11-28": una sola vez
        if (fecha.empty() ? !TieneDia(r, hoy) : fecha != hoy_iso) continue;
        Aviso a;
        a.texto = Str(r, "texto");
        if (a.texto.empty()) continue;
        a.repetir = Bool(r, "repetir", false);
        a.avisar = Bool(r, "avisar", false);
        a.corto = a.texto;
        a.rec_id = Str(r, "id");
        std::string de = Str(r, "de");
        if (Str(r, "origen") == "telegram" && !de.empty()) a.texto = de + " te manda a decir: " + a.texto;
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
        // Simple para ella: qué casillero abrir y cuántas pastillas tiene.
        // Los colores van aparte, por si pregunta; los nombres de los remedios no se dicen.
        std::string lista, colores;
        double pastillas = 0;
        const cJSON* m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(f, "meds")) {
            if (!TieneDia(m, hoy) || Str(m, "nombre").empty()) continue;
            lista += (lista.empty() ? "" : "; ") + Str(m, "nombre");
            pastillas += ContarPastillas(Str(m, "dosis"));
            std::string aspecto = Str(m, "aspecto");
            colores += (colores.empty() ? "" : ", ") + (aspecto.empty() ? std::string("una pastilla") : aspecto);
        }
        if (lista.empty()) continue;
        Aviso a;
        a.texto = std::string("Es hora de los remedios de la ") + fr[1] + ": abrí el casillero del " +
                  kDias[tm.tm_wday] + " a la " + fr[1] + ", que tiene " + TextoPastillas(pastillas) + ".";
        a.privado = "Si pregunta cuáles son, decile sólo cómo son: " + colores +
                    ". No nombres los remedios ni las dosis.";
        a.repetir = true;
        a.avisar = true;
        a.remedio = true;
        a.corto = std::string("remedios de la ") + fr[1];
        nuevos.push_back(a);
    }
    cJSON_Delete(raiz);

    if (nuevos.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& a : nuevos) {
        a.hora = hhmm;
        a.creado_ms = ahora;
        a.proximo_ms = ahora;
        ESP_LOGI(TAG, "Recordatorio de las %s: %s", hhmm, a.texto.c_str());
        avisos_.push_back(a);
    }
}

int Madre::VecesAviso(const Aviso& a) const { return a.remedio ? aj_.remedios_avisos : 3; }
int Madre::MinutosAviso(const Aviso& a) const { return a.remedio ? aj_.remedios_minutos : 10; }

void Madre::ProcesarAvisos(int64_t ahora) {
    std::vector<std::string> para_telegram;
    std::vector<std::string> para_diario;
    std::vector<std::string> decir;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // Vencidos
        for (auto it = avisos_.begin(); it != avisos_.end();) {
            if (it->intentos >= VecesAviso(*it) && ahora >= it->proximo_ms) {
                const std::string veces = it->intentos == 1 ? "1 vez" : std::to_string(it->intentos) + " veces";
                para_diario.push_back("⚠️ " + it->hora + " " + it->corto + ": no confirmó (se lo dije " + veces + ")");
                MarcarEstado(it->rec_id, "no_confirmo");
                for (auto& e : escuchados_) {
                    if (e.creado_ms == it->creado_ms && e.texto == it->texto) e.estado = "no_confirmo";
                }
                if (it->avisar) para_telegram.push_back("⚠️ Mamá no confirmó este recordatorio (se lo dije " + veces + "): " + it->texto);
                it = avisos_.erase(it);
            } else if (it->intentos == 0 && ahora - it->creado_ms > 2 * 3600 * 1000) {
                para_diario.push_back("⚠️ " + it->hora + " " + it->corto + ": no se lo pude decir");
                MarcarEstado(it->rec_id, "no_dicho");
                if (it->avisar) para_telegram.push_back("⚠️ No le pude decir a mamá este recordatorio (no estaba en la pieza o dormía): " + it->texto);
                it = avisos_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& a : avisos_) {
            if (a.intentos < VecesAviso(a) && ahora >= a.proximo_ms) decir.push_back(a.texto);
        }
    }
    for (auto& m : para_telegram) {
        ESP_LOGW(TAG, "%s", m.c_str());
        TelegramBot::GetInstance().QueueMessage(m);
    }
    for (auto& m : para_diario) Anotar(m);
    if (decir.empty() || !PuedeHablar(ahora, false)) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        aviso_actual_.clear();
        for (auto it = avisos_.begin(); it != avisos_.end();) {
            if (it->intentos < VecesAviso(*it) && ahora >= it->proximo_ms) {
                aviso_actual_.push_back(it->texto + (it->repetir ? " [pedir confirmación]" : "") +
                                        (it->privado.empty() ? "" : " [" + it->privado + "]"));
                it->intentos++;
                MarcarEstado(it->rec_id, it->repetir ? "esperando" : "dicho");
                {
                    // Guardarlo para poder repetírselo ("no lo escuché")
                    bool ya = false;
                    for (auto& e : escuchados_) {
                        if (e.creado_ms == it->creado_ms && e.texto == it->texto) {
                            e.cuando = time(nullptr);
                            ya = true;
                        }
                    }
                    if (!ya) {
                        Escuchado e;
                        e.cuando = time(nullptr);
                        e.creado_ms = it->creado_ms;
                        e.texto = it->texto;
                        e.pide_confirmar = it->repetir;
                        escuchados_.push_back(e);
                        if (escuchados_.size() > 10) escuchados_.erase(escuchados_.begin());
                    }
                }
                if (!it->repetir) {
                    para_diario.push_back("🔔 " + it->hora + " " + it->corto + ": se lo dije");
                    it = avisos_.erase(it);       // se dice una sola vez
                    continue;
                }
                it->proximo_ms = ahora + (int64_t)MinutosAviso(*it) * 60 * 1000;   // repetir
            }
            ++it;
        }
    }
    for (size_t i = 0; i < para_diario.size(); i++) {
        if (para_diario[i].rfind("🔔", 0) == 0) Anotar(para_diario[i]);
    }
    Hablar("Tengo un aviso");
}

void Madre::ConfirmarAvisos(const char* como) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t antes = avisos_.size();
    for (auto it = avisos_.begin(); it != avisos_.end();) {
        if (it->intentos >= 1) {
            ESP_LOGI(TAG, "Recordatorio confirmado (%s): %s", como, it->texto.c_str());
            Anotar("✅ " + it->hora + " " + it->corto + ": confirmó " +
                   (strcmp(como, "botón") == 0 ? "con el botón" : "con la voz"));
            MarcarEstado(it->rec_id, strcmp(como, "botón") == 0 ? "confirmo_boton" : "confirmo_voz");
            for (auto& e : escuchados_) {
                if (e.creado_ms == it->creado_ms && e.texto == it->texto) e.estado = "confirmo";
            }
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
// Lista de compras
// ----------------------------------------------------------------------------
namespace {

// "Harina " -> "harina" ; quita acentos para comparar
std::string ClaveCompra(const std::string& s) {
    static const char* const kAcentos[][2] = {{"á", "a"}, {"é", "e"}, {"í", "i"}, {"ó", "o"}, {"ú", "u"},
                                              {"Á", "a"}, {"É", "e"}, {"Í", "i"}, {"Ó", "o"}, {"Ú", "u"},
                                              {"ü", "u"}, {"Ü", "u"}, {"ñ", "n"}, {"Ñ", "n"}};
    std::string r = s;
    for (auto& a : kAcentos) {
        size_t p;
        while ((p = r.find(a[0])) != std::string::npos) r.replace(p, strlen(a[0]), a[1]);
    }
    std::string limpio;
    for (char c : r) {
        if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        if (c == ' ' && (limpio.empty() || limpio.back() == ' ')) continue;
        limpio += c;
    }
    while (!limpio.empty() && limpio.back() == ' ') limpio.pop_back();
    return limpio;
}

std::string Recortar(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n.");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n.");
    return s.substr(a, b - a + 1);
}

}  // namespace

std::string Madre::AgregarCompras(const std::string& items, bool de_mama) {
    // Separar por comas y por " y "
    std::vector<std::string> nuevos;
    std::string resto = items;
    size_t p;
    while ((p = resto.find(" y ")) != std::string::npos) resto.replace(p, 3, ",");
    size_t inicio = 0;
    while (inicio <= resto.size()) {
        size_t coma = resto.find(',', inicio);
        std::string it = Recortar(resto.substr(inicio, coma == std::string::npos ? std::string::npos : coma - inicio));
        if (!it.empty()) nuevos.push_back(it);
        if (coma == std::string::npos) break;
        inicio = coma + 1;
    }
    if (nuevos.empty()) return "No entendí qué hay que anotar.";

    char fecha[32] = "";
    if (RelojEnHora()) {
        time_t t = time(nullptr);
        struct tm tm;
        localtime_r(&t, &tm);
        snprintf(fecha, sizeof(fecha), "%02d/%02d", tm.tm_mday, tm.tm_mon + 1);
    }

    std::string agregados, ya_estaban, lista;
    int total = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cJSON* raiz = cJSON_Parse(compras_.c_str());
        if (!cJSON_IsArray(raiz)) { cJSON_Delete(raiz); raiz = cJSON_CreateArray(); }
        for (auto& n : nuevos) {
            bool existe = false;
            const cJSON* e;
            cJSON_ArrayForEach(e, raiz) {
                if (ClaveCompra(Str(e, "item")) == ClaveCompra(n)) { existe = true; break; }
            }
            if (existe) { ya_estaban += (ya_estaban.empty() ? "" : ", ") + n; continue; }
            if (cJSON_GetArraySize(raiz) >= 60) break;
            cJSON* o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "item", n.c_str());
            cJSON_AddStringToObject(o, "fecha", fecha);
            cJSON_AddItemToArray(raiz, o);
            agregados += (agregados.empty() ? "" : ", ") + n;
        }
        total = cJSON_GetArraySize(raiz);
        char* txt = cJSON_PrintUnformatted(raiz);
        if (txt) {
            std::string nuevo = txt;
            cJSON_free(txt);
            if (!agregados.empty()) {
                compras_ = nuevo;
                Guardar("compras", nuevo);
            }
        }
        cJSON_Delete(raiz);
    }
    ESP_LOGI(TAG, "Compras: agregado [%s] ya estaban [%s]", agregados.c_str(), ya_estaban.c_str());
    if (de_mama && !agregados.empty()) {
        std::lock_guard<std::mutex> lock(diario_mutex_);
        diario_.compras.push_back(agregados);
    }
    std::string r;
    if (!agregados.empty()) r += "Anotado en la lista de compras: " + agregados + ". ";
    if (!ya_estaban.empty()) r += "Ya estaba en la lista: " + ya_estaban + ". ";
    r += "La lista tiene " + std::to_string(total) + (total == 1 ? " cosa." : " cosas.");
    r += " (Julio la ve por Telegram cuando va a hacer las compras.)";
    return r;
}

std::string Madre::QuitarCompra(const std::string& item) {
    std::string buscado = ClaveCompra(Recortar(item));
    if (buscado.empty()) return "No entendí qué hay que sacar.";
    std::string quitados;
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* raiz = cJSON_Parse(compras_.c_str());
    if (!cJSON_IsArray(raiz)) { cJSON_Delete(raiz); return "La lista de compras está vacía."; }
    for (int i = cJSON_GetArraySize(raiz) - 1; i >= 0; i--) {
        std::string actual = Str(cJSON_GetArrayItem(raiz, i), "item");
        std::string clave = ClaveCompra(actual);
        if (clave == buscado || clave.find(buscado) != std::string::npos || buscado.find(clave) != std::string::npos) {
            quitados += (quitados.empty() ? "" : ", ") + actual;
            cJSON_DeleteItemFromArray(raiz, i);
        }
    }
    if (!quitados.empty()) {
        char* txt = cJSON_PrintUnformatted(raiz);
        if (txt) {
            std::string nuevo = txt;
            cJSON_free(txt);
            compras_ = nuevo;
            Guardar("compras", nuevo);
        }
    }
    cJSON_Delete(raiz);
    return quitados.empty() ? "Eso no estaba en la lista de compras." : "Saqué de la lista: " + quitados + ".";
}

std::string Madre::TextoCompras(bool para_telegram) {
    cJSON* raiz = cJSON_Parse(GetSeccion("compras").c_str());
    std::string s;
    int n = 0;
    const cJSON* e;
    cJSON_ArrayForEach(e, raiz) {
        std::string item = Str(e, "item"), fecha = Str(e, "fecha");
        if (item.empty()) continue;
        n++;
        s += (para_telegram ? "• " : "- ") + item;
        if (para_telegram && !fecha.empty()) s += "  (" + fecha + ")";
        s += "\n";
    }
    cJSON_Delete(raiz);
    if (n == 0) return para_telegram ? "🛒 La lista de compras de mamá está vacía." : "La lista de compras está vacía.";
    return (para_telegram ? "🛒 Lista de compras de mamá (" + std::to_string(n) + "):\n" : std::string("Lista de compras:\n")) + s;
}

void Madre::VaciarCompras() {
    std::lock_guard<std::mutex> lock(mutex_);
    compras_ = "[]";
    Guardar("compras", "[]");
}

void Madre::RegistrarComprasTelegram() {
    auto& tg = TelegramBot::GetInstance();
    auto botones = [this](std::string& b) {
        bool vacia = GetSeccion("compras").find("\"item\"") == std::string::npos;
        b = vacia ? "" : "{\"inline_keyboard\":[[{\"text\":\"✅ Ya compré todo\",\"callback_data\":\"compras:vaciar\"}],"
                         "[{\"text\":\"🔄 Actualizar\",\"callback_data\":\"compras:ver\"}]]}";
    };
    tg.AddAdminCommand(
        [this, botones](const std::string& cmd, const std::string& arg, std::string& texto, std::string& b) {
            if (cmd != "/compras") return false;
            if (!arg.empty()) {                              // "/compras leche, pan" agrega desde Telegram
                AgregarCompras(arg);
            }
            texto = TextoCompras(true);
            botones(b);
            return true;
        },
        "/compras - ver la lista de compras de mamá (o /compras leche, pan para agregar)");
    tg.AddCallbackHandler([this, botones](const std::string& data, std::string& texto, std::string& b) {
        if (data == "compras:vaciar") {
            VaciarCompras();
            texto = "✅ Listo, vacié la lista de compras.";
            return true;
        }
        if (data == "compras:ver") {
            texto = TextoCompras(true);
            botones(b);
            return true;
        }
        return false;
    });
}

// ----------------------------------------------------------------------------
// Recordatorios desde Telegram
//   /recordar 28/11 10:30 Turno con el traumatólogo
//   /recordar mañana a las 9 Viene Gladys
//   /recordar el viernes 18hs Llamar a la gringa
//   /recordar todos los jueves 9:00 Viene Gladys a limpiar
//   /recordatorios  -> lista con botones para borrar
// ----------------------------------------------------------------------------
namespace {

struct PedidoRecordatorio {
    std::string error;
    bool semanal = false;
    std::vector<int> dias;        // 0 = lunes ... 6 = domingo (como en la página)
    struct tm fecha = {};         // si no es semanal
    int hora = -1, minuto = 0;
    std::string texto;
};

// "jueves" -> 3 (lunes = 0); -1 si no es un día
int DiaDeLaSemana(const std::string& k) {
    static const char* const kNombres[] = {"lunes", "martes", "miercoles", "jueves", "viernes", "sabado", "domingo"};
    for (int i = 0; i < 7; i++) {
        if (k == kNombres[i]) return i;
    }
    return -1;
}

// "10", "10:30", "10.30", "10hs", "10:30hs", "9h" -> hora y minuto
bool LeerHora(const std::string& k, int& h, int& m) {
    std::string t = k;
    for (const char* suf : {"hs", "hrs", "h"}) {
        size_t n = strlen(suf);
        if (t.size() > n && t.compare(t.size() - n, n, suf) == 0) {
            t.erase(t.size() - n);
            break;
        }
    }
    int hh = -1, mm = 0;
    char sep = 0;
    int leidos = 0;
    if (sscanf(t.c_str(), "%d%c%d%n", &hh, &sep, &mm, &leidos) == 3 && (sep == ':' || sep == '.') &&
        leidos == (int)t.size()) {
    } else if (sscanf(t.c_str(), "%d%n", &hh, &leidos) == 1 && leidos == (int)t.size()) {
        mm = 0;
    } else {
        return false;
    }
    if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
    h = hh;
    m = mm;
    return true;
}

// "28/11", "28/11/26", "28-11-2026"
bool LeerFecha(const std::string& k, int& d, int& m, int& y) {
    int a = 0, b = 0, c = -1, leidos = 0;
    char s1 = 0, s2 = 0;
    if (sscanf(k.c_str(), "%d%c%d%c%d%n", &a, &s1, &b, &s2, &c, &leidos) == 5 && leidos == (int)k.size() &&
        (s1 == '/' || s1 == '-') && s2 == s1) {
    } else if (sscanf(k.c_str(), "%d%c%d%n", &a, &s1, &b, &leidos) == 3 && leidos == (int)k.size() &&
               (s1 == '/' || s1 == '-')) {
        c = -1;
    } else {
        return false;
    }
    if (a < 1 || a > 31 || b < 1 || b > 12) return false;
    if (c >= 0 && c < 100) c += 2000;
    d = a;
    m = b;
    y = c;
    return true;
}

PedidoRecordatorio InterpretarRecordar(const std::string& pedido, time_t ahora) {
    PedidoRecordatorio p;
    struct tm hoy;
    localtime_r(&ahora, &hoy);

    // Palabras originales (para el texto) y normalizadas (para entender)
    std::vector<std::string> orig, k;
    {
        std::string w;
        for (size_t i = 0; i <= pedido.size(); i++) {
            char c = i < pedido.size() ? pedido[i] : ' ';
            if (c == ' ' || c == '\n' || c == '\t') {
                if (!w.empty()) orig.push_back(w);
                w.clear();
            } else {
                w += c;
            }
        }
    }
    for (auto& w : orig) {
        std::string n = ClaveCompra(w);
        while (!n.empty() && (n.back() == ',' || n.back() == ';' || (n.back() == '.' && n.size() > 1 &&
                                                                       !isdigit((unsigned char)n[n.size() - 2]))))
            n.pop_back();
        k.push_back(n);
    }

    int dia_semana = -1;            // "el viernes"
    int suma_dias = -1;             // hoy = 0, mañana = 1...
    int fd = 0, fm = 0, fy = -1;    // fecha escrita
    bool hay_fecha = false;
    size_t i = 0;
    auto es_fecha_u_hora = [&](size_t j) {
        if (j >= k.size()) return false;
        int a, b, c;
        return LeerHora(k[j], a, b) || LeerFecha(k[j], a, b, c) || DiaDeLaSemana(k[j]) >= 0 || k[j] == "hoy" ||
               k[j] == "manana" || k[j] == "pasado" || k[j] == "todos" || k[j] == "cada";
    };
    while (i < k.size()) {
        const std::string& w = k[i];
        int a, b, c;
        // relleno: "el viernes", "a las 10", "para mañana", "para el lunes a las 9"
        auto es_relleno = [](const std::string& x) {
            return x == "el" || x == "a" || x == "las" || x == "la" || x == "para" || x == "alas" || x == "del";
        };
        if (es_relleno(w)) {
            size_t j = i;
            while (j < k.size() && es_relleno(k[j])) j++;
            if (es_fecha_u_hora(j)) {
                i = j;
                continue;
            }
        }
        if (!hay_fecha && w == "hoy") { suma_dias = 0; hay_fecha = true; i++; continue; }
        if (!hay_fecha && w == "manana") { suma_dias = 1; hay_fecha = true; i++; continue; }
        if (!hay_fecha && w == "pasado" && i + 1 < k.size() && k[i + 1] == "manana") {
            suma_dias = 2; hay_fecha = true; i += 2; continue;
        }
        if (!hay_fecha && (w == "todos" || w == "cada")) {
            size_t j = i + 1;
            if (j < k.size() && (k[j] == "los" || k[j] == "las")) j++;
            if (j < k.size() && (k[j] == "dias" || k[j] == "dia")) {
                p.semanal = true;
                for (int d = 0; d < 7; d++) p.dias.push_back(d);
                hay_fecha = true;
                i = j + 1;
                continue;
            }
            std::vector<int> ds;
            while (j < k.size()) {
                int d = DiaDeLaSemana(k[j]);
                if (d >= 0) { ds.push_back(d); j++; continue; }
                if ((k[j] == "y" || k[j] == "e") && j + 1 < k.size() && DiaDeLaSemana(k[j + 1]) >= 0) { j++; continue; }
                break;
            }
            if (!ds.empty()) {
                p.semanal = true;
                p.dias = ds;
                hay_fecha = true;
                i = j;
                continue;
            }
        }
        if (!hay_fecha && DiaDeLaSemana(w) >= 0) { dia_semana = DiaDeLaSemana(w); hay_fecha = true; i++; continue; }
        if (!hay_fecha && LeerFecha(w, fd, fm, fy)) { hay_fecha = true; i++; continue; }
        if (p.hora < 0 && LeerHora(w, a, b)) {
            p.hora = a;
            p.minuto = b;
            i++;
            if (i < k.size() && (k[i] == "hs" || k[i] == "horas" || k[i] == "hrs" || k[i] == "h")) i++;
            continue;
        }
        (void)c;
        break;   // de acá en adelante es el texto
    }
    for (size_t j = i; j < orig.size(); j++) p.texto += (p.texto.empty() ? "" : " ") + orig[j];
    while (!p.texto.empty() && (p.texto[0] == '-' || p.texto[0] == ':' || p.texto[0] == ',')) {
        p.texto.erase(0, 1);
        while (!p.texto.empty() && p.texto[0] == ' ') p.texto.erase(0, 1);
    }
    if (ClaveCompra(p.texto).rfind("que ", 0) == 0) p.texto.erase(0, 4);

    if (p.hora < 0) { p.error = "Me falta la hora."; return p; }
    if (p.texto.empty()) { p.error = "Me falta qué le tiene que recordar."; return p; }
    if (p.semanal) return p;

    // Fecha concreta
    struct tm f = hoy;
    f.tm_hour = p.hora;
    f.tm_min = p.minuto;
    f.tm_sec = 0;
    f.tm_isdst = -1;
    if (fy != -1 || fd != 0) {
        f.tm_mday = fd;
        f.tm_mon = fm - 1;
        f.tm_year = (fy > 0 ? fy : hoy.tm_year + 1900) - 1900;
        time_t t = mktime(&f);
        if (fy <= 0 && t < ahora - 24 * 3600) {      // "05/01" dicho en diciembre: el año que viene
            f.tm_year++;
            f.tm_isdst = -1;
            t = mktime(&f);
        }
        if (f.tm_mday != fd) { p.error = "Esa fecha no existe."; return p; }
    } else if (dia_semana >= 0) {
        int hoy_pagina = (hoy.tm_wday + 6) % 7;
        int dif = (dia_semana - hoy_pagina + 7) % 7;
        f.tm_mday += dif;
        time_t t = mktime(&f);
        if (t <= ahora) {        // "el jueves" dicho un jueves después de esa hora: el que viene
            f.tm_mday += 7;
            f.tm_isdst = -1;
            mktime(&f);
        }
    } else {
        if (suma_dias > 0) f.tm_mday += suma_dias;
        time_t t = mktime(&f);
        if (suma_dias < 0 && t <= ahora) {           // sólo la hora: si ya pasó, mañana
            f.tm_mday += 1;
            f.tm_isdst = -1;
            mktime(&f);
        }
    }
    f.tm_isdst = -1;
    if (mktime(&f) <= ahora) { p.error = "Esa fecha y hora ya pasaron."; return p; }
    p.fecha = f;
    return p;
}

const char* const kAyudaRecordar =
    "Usalo así:\n"
    "/recordar 28/11 10:30 Turno con el traumatólogo\n"
    "/recordar mañana a las 9 Viene Gladys\n"
    "/recordar el viernes 18hs Llamar a la gringa\n"
    "/recordar todos los jueves 9:00 Viene Gladys a limpiar\n\n"
    "XiaoZhi se lo dice a esa hora y lo repite hasta que toque el botón; si no confirma, te aviso.\n"
    "Para ver o borrar: /recordatorios";

const char* TextoEstado(const std::string& e) {
    if (e == "dicho") return "🔔 Se lo dijo";
    if (e == "esperando") return "🔔 Se lo dijo, esperando que confirme";
    if (e == "confirmo_boton") return "✅ Confirmó con el botón";
    if (e == "confirmo_voz") return "✅ Confirmó con la voz";
    if (e == "no_confirmo") return "⚠️ No confirmó (se lo dijo 3 veces)";
    if (e == "no_dicho") return "⚠️ No se lo pudo decir (no estaba o dormía)";
    return "⏳ Pendiente";
}
}  // namespace

bool Madre::GuardarRecordatorios(const std::string& json) {
    std::lock_guard<std::mutex> lock(mutex_);
    recordatorios_ = json;
    return Guardar("recordat", json);
}

std::string Madre::AgregarRecordatorioTelegram(const std::string& pedido, std::string& botones) {
    if (!RelojEnHora()) return "Todavía no tengo la hora en el equipo. Probá en un ratito.";
    if (pedido.empty()) return kAyudaRecordar;
    time_t ahora = time(nullptr);
    PedidoRecordatorio p = InterpretarRecordar(pedido, ahora);
    if (!p.error.empty()) return "❓ " + p.error + "\n\n" + kAyudaRecordar;

    char hora[16];
    snprintf(hora, sizeof(hora), "%02d:%02d", p.hora, p.minuto);
    static int contador = 0;
    std::string id = "t" + std::to_string((long long)ahora) + "_" + std::to_string(++contador % 100);
    cJSON* o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", id.c_str());
    cJSON_AddStringToObject(o, "texto", p.texto.c_str());
    cJSON_AddStringToObject(o, "hora", hora);
    std::string cuando;
    if (p.semanal) {
        cJSON* dias = cJSON_CreateArray();
        for (int d : p.dias) cJSON_AddItemToArray(dias, cJSON_CreateNumber(d));
        cJSON_AddItemToObject(o, "dias", dias);
        cuando = (p.dias.size() >= 7) ? std::string("todos los días") : "todos los ";
        if (p.dias.size() < 7) {
            for (size_t j = 0; j < p.dias.size(); j++) {
                cuando += (j == 0 ? "" : (j + 1 == p.dias.size() ? " y " : ", "));
                cuando += kDiasPagina[p.dias[j]];
                if (p.dias[j] >= 5) cuando += "s";      // sábados, domingos
            }
        }
    } else {
        char iso[32];
        snprintf(iso, sizeof(iso), "%04d-%02d-%02d", p.fecha.tm_year + 1900, p.fecha.tm_mon + 1, p.fecha.tm_mday);
        cJSON_AddStringToObject(o, "fecha", iso);
        cuando = "el " + FechaLinda(iso);
    }
    cJSON_AddBoolToObject(o, "activo", true);
    cJSON_AddBoolToObject(o, "repetir", true);
    cJSON_AddBoolToObject(o, "avisar", true);
    cJSON_AddStringToObject(o, "origen", "telegram");
    std::string de = QuienMandaTelegram();
    cJSON_AddStringToObject(o, "de", de.c_str());

    cJSON* lista = cJSON_Parse(GetSeccion("recordatorios").c_str());
    if (!cJSON_IsArray(lista)) {
        cJSON_Delete(lista);
        lista = cJSON_CreateArray();
    }
    if (cJSON_GetArraySize(lista) >= 50) {
        cJSON_Delete(lista);
        cJSON_Delete(o);
        return "Ya hay 50 recordatorios cargados. Borrá alguno con /recordatorios.";
    }
    cJSON_AddItemToArray(lista, o);
    char* txt = cJSON_PrintUnformatted(lista);
    bool ok = txt && GuardarRecordatorios(txt);
    if (txt) cJSON_free(txt);
    cJSON_Delete(lista);
    if (!ok) return "⚠️ No pude guardar el recordatorio.";

    ESP_LOGI(TAG, "Recordatorio desde Telegram: %s %s %s", cuando.c_str(), hora, p.texto.c_str());

    // Se lo cuenta enseguida ("Marcelo te agendó...") y queda anotado para esa hora
    std::string para_ella = cuando;
    if (!p.semanal) {
        struct tm hoy;
        localtime_r(&ahora, &hoy);
        if (p.fecha.tm_year == hoy.tm_year && p.fecha.tm_yday == hoy.tm_yday) para_ella = "hoy";
        else if (p.fecha.tm_year == hoy.tm_year && p.fecha.tm_yday == hoy.tm_yday + 1) para_ella = "mañana";
    }
    {
        Aviso a;
        a.texto = de + " te agendó un recordatorio para " + para_ella + " a las " + hora + ": " + p.texto +
                  ". Decile que a esa hora se lo vas a recordar.";
        a.corto = "aviso de recordatorio nuevo (" + p.texto + ")";
        struct tm ya;
        localtime_r(&ahora, &ya);
        char hh[16];
        snprintf(hh, sizeof(hh), "%02d:%02d", ya.tm_hour, ya.tm_min);
        a.hora = hh;
        a.creado_ms = esp_timer_get_time() / 1000;
        a.proximo_ms = a.creado_ms;
        std::lock_guard<std::mutex> lock(mutex_);
        avisos_.push_back(a);
    }
    botones = "{\"inline_keyboard\":[[{\"text\":\"🗑 Borrar este recordatorio\",\"callback_data\":\"rec:del:" + id +
              "\"}],[{\"text\":\"📋 Ver todos\",\"callback_data\":\"rec:ver\"}]]}";
    return "✅ Agendado " + cuando + " a las " + hora + ":\n«" + p.texto + "»\n\n" +
           "📢 Ya se lo cuento ahora (\"" + de + " te agendó un recordatorio…\").\n" +
           "A esa hora se lo dice como \"" + de + " te manda a decir: " + p.texto + "\" y lo repite hasta que " +
           "toque el botón (si no confirma, te aviso).";
}

std::string Madre::ListaRecordatoriosTelegram(std::string& botones) {
    time_t ahora = time(nullptr);
    struct tm hoy;
    localtime_r(&ahora, &hoy);
    char hoy_iso[32];
    snprintf(hoy_iso, sizeof(hoy_iso), "%04d-%02d-%02d", hoy.tm_year + 1900, hoy.tm_mon + 1, hoy.tm_mday);

    cJSON* lista = cJSON_Parse(GetSeccion("recordatorios").c_str());
    if (!cJSON_IsArray(lista) || cJSON_GetArraySize(lista) == 0) {
        cJSON_Delete(lista);
        return "No hay recordatorios cargados.\n\nPara agregar uno: /recordar 28/11 10:30 Turno con el médico";
    }
    // Los que no tienen id (cargados desde la página) reciben uno, para poder borrarlos desde acá
    bool cambio = false;
    int n = 0;
    cJSON* r;
    cJSON_ArrayForEach(r, lista) {
        if (Str(r, "id").empty()) {
            std::string id = "p" + std::to_string((long long)ahora) + "_" + std::to_string(n);
            cJSON_AddStringToObject(r, "id", id.c_str());
            cambio = true;
        }
        n++;
    }
    if (cambio) {
        char* txt = cJSON_PrintUnformatted(lista);
        if (txt) {
            GuardarRecordatorios(txt);
            cJSON_free(txt);
        }
    }

    // Primero los de fecha (por fecha), después los semanales (por hora)
    struct Item { std::string orden, linea, id, boton; };
    std::vector<Item> items;
    cJSON_ArrayForEach(r, lista) {
        std::string texto = Str(r, "texto"), hora = Str(r, "hora"), fecha = Str(r, "fecha"), id = Str(r, "id");
        if (texto.empty()) continue;
        bool de_telegram = Str(r, "origen") == "telegram";
        if (!fecha.empty() && fecha < hoy_iso && !de_telegram) continue;
        bool activo = Bool(r, "activo", true);
        Item it;
        it.id = id;
        if (!fecha.empty()) {
            it.orden = "0" + fecha + hora;
            it.linea = "📅 " + FechaLinda(fecha) + ", " + hora + " — " + texto;
            it.boton = FechaLinda(fecha).substr(FechaLinda(fecha).find(' ') + 1) + " " + hora;
        } else {
            it.orden = "1" + hora;
            it.linea = "🔁 " + hora + " " + TextoDias(r) + " — " + texto;
            it.boton = hora;
        }
        if (!activo) it.linea += " (apagado)";
        if (de_telegram) {
            std::string e = Str(r, "estado");
            it.linea += "\n     " + std::string(TextoEstado(e));
            if (!e.empty() && !Str(r, "estado_cuando").empty()) it.linea += " (" + Str(r, "estado_cuando") + ")";
        }
        std::string corto = texto;
        if (texto.size() > 22) {
            size_t c = 22;
            while (c > 0 && ((unsigned char)texto[c] & 0xC0) == 0x80) c--;   // no cortar una letra con acento
            corto = texto.substr(0, c) + "…";
        }
        it.boton = "🗑 " + it.boton + " " + corto;
        items.push_back(it);
    }
    cJSON_Delete(lista);
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.orden < b.orden; });

    std::string s = "⏰ Recordatorios de mamá (" + std::to_string(items.size()) + "):\n";
    cJSON* teclado = cJSON_CreateArray();
    int con_boton = 0;
    for (auto& it : items) {
        s += "\n" + it.linea;
        if (con_boton < 20 && !it.id.empty() && it.id.size() <= 40) {
            cJSON* fila = cJSON_CreateArray();
            cJSON* b = cJSON_CreateObject();
            cJSON_AddStringToObject(b, "text", it.boton.c_str());
            cJSON_AddStringToObject(b, "callback_data", ("rec:del:" + it.id).c_str());
            cJSON_AddItemToArray(fila, b);
            cJSON_AddItemToArray(teclado, fila);
            con_boton++;
        }
    }
    s += "\n\nTocá un botón para borrarlo. Para agregar: /recordar";
    cJSON* markup = cJSON_CreateObject();
    cJSON_AddItemToObject(markup, "inline_keyboard", teclado);
    char* txt = cJSON_PrintUnformatted(markup);
    if (txt) {
        botones = txt;
        cJSON_free(txt);
    }
    cJSON_Delete(markup);
    return s;
}

std::string Madre::BorrarRecordatorio(const std::string& id) {
    cJSON* lista = cJSON_Parse(GetSeccion("recordatorios").c_str());
    std::string borrado;
    int i = 0;
    cJSON* r;
    cJSON_ArrayForEach(r, lista) {
        if (Str(r, "id") == id) {
            borrado = Str(r, "texto");
            std::string fecha = Str(r, "fecha");
            borrado = Str(r, "hora") + " " + (fecha.empty() ? TextoDias(r) : FechaLinda(fecha)) + " — " + borrado;
            cJSON_DeleteItemFromArray(lista, i);
            break;
        }
        i++;
    }
    if (borrado.empty()) {
        cJSON_Delete(lista);
        return "Ese recordatorio ya no existe (puede que lo hayan borrado desde la página).";
    }
    char* txt = cJSON_PrintUnformatted(lista);
    bool ok = txt && GuardarRecordatorios(txt);
    if (txt) cJSON_free(txt);
    cJSON_Delete(lista);
    return ok ? "🗑 Borré: " + borrado : "⚠️ No pude borrarlo.";
}

void Madre::LimpiarRecordatoriosViejos() {
    if (!RelojEnHora()) return;
    time_t ahora = time(nullptr);
    struct tm hoy;
    localtime_r(&ahora, &hoy);
    char hoy_iso[32];
    snprintf(hoy_iso, sizeof(hoy_iso), "%04d-%02d-%02d", hoy.tm_year + 1900, hoy.tm_mon + 1, hoy.tm_mday);
    // Los de Telegram quedan 7 días más, para ver en la página si los confirmó
    time_t hace7 = ahora - 7 * 24 * 3600;
    struct tm t7;
    localtime_r(&hace7, &t7);
    char hace7_iso[32];
    snprintf(hace7_iso, sizeof(hace7_iso), "%04d-%02d-%02d", t7.tm_year + 1900, t7.tm_mon + 1, t7.tm_mday);
    cJSON* lista = cJSON_Parse(GetSeccion("recordatorios").c_str());
    bool cambio = false;
    for (int i = cJSON_GetArraySize(lista) - 1; i >= 0; i--) {
        const cJSON* r = cJSON_GetArrayItem(lista, i);
        std::string fecha = Str(r, "fecha");
        bool telegram = Str(r, "origen") == "telegram";
        if (!fecha.empty() && fecha < (telegram ? std::string(hace7_iso) : std::string(hoy_iso))) {
            ESP_LOGI(TAG, "Recordatorio vencido (%s), lo borro", fecha.c_str());
            cJSON_DeleteItemFromArray(lista, i);
            cambio = true;
        }
    }
    if (cambio) {
        char* txt = cJSON_PrintUnformatted(lista);
        if (txt) {
            GuardarRecordatorios(txt);
            cJSON_free(txt);
        }
    }
    cJSON_Delete(lista);
}

void Madre::MarcarEstado(const std::string& rec_id, const char* estado) {
    if (rec_id.empty()) return;
    std::lock_guard<std::mutex> lock(estados_mutex_);
    if (estados_pendientes_.size() < 30) estados_pendientes_.push_back({rec_id, estado});
}


// Guarda en cada recordatorio qué pasó la última vez que sonó (lo muestra la página y /recordatorios)
void Madre::AplicarEstados() {
    std::vector<std::pair<std::string, std::string>> cambios;
    {
        std::lock_guard<std::mutex> lock(estados_mutex_);
        if (estados_pendientes_.empty()) return;
        cambios.swap(estados_pendientes_);
    }
    std::string cuando;
    if (RelojEnHora()) {
        time_t t = time(nullptr);
        struct tm tm;
        localtime_r(&t, &tm);
        char buf[48];
        snprintf(buf, sizeof(buf), "%s %d/%d %02d:%02d", kDiasCortos[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1,
                 tm.tm_hour, tm.tm_min);
        cuando = buf;
    }
    cJSON* lista = cJSON_Parse(GetSeccion("recordatorios").c_str());
    bool cambio = false;
    std::vector<std::string> para_julio;
    cJSON* r;
    cJSON_ArrayForEach(r, lista) {
        std::string id = Str(r, "id");
        for (auto& c : cambios) {
            if (c.first != id) continue;
            cJSON_DeleteItemFromObject(r, "estado");
            cJSON_DeleteItemFromObject(r, "estado_cuando");
            cJSON_AddStringToObject(r, "estado", c.second.c_str());
            cJSON_AddStringToObject(r, "estado_cuando", cuando.c_str());
            cambio = true;
            // Los que mandó Julio por Telegram: avisarle cuando ella confirma
            if (Str(r, "origen") == "telegram" && c.second.rfind("confirmo", 0) == 0) {
                para_julio.push_back("✅ Mamá confirmó tu recordatorio «" + Str(r, "texto") + "» (" + cuando + ", " +
                                     (c.second == "confirmo_boton" ? "con el botón" : "con la voz") + ").");
            }
        }
    }
    if (cambio) {
        char* txt = cJSON_PrintUnformatted(lista);
        if (txt) {
            GuardarRecordatorios(txt);
            cJSON_free(txt);
        }
    }
    cJSON_Delete(lista);
    for (auto& m : para_julio) TelegramBot::GetInstance().QueueMessage(m);
}

// Cómo llama mamá a quien manda por Telegram (el administrador): "Julio" -> "Marcelo"
std::string Madre::QuienMandaTelegram() {
    std::string nombre = TelegramBot::GetInstance().AdminName();
    if (nombre.empty()) {
        // No figura en la lista de familiares: buscar al hijo en la pestaña Familia
        cJSON* raiz = cJSON_Parse(GetSeccion("familia").c_str());
        const cJSON* p;
        cJSON_ArrayForEach(p, raiz) {
            if (ClaveCompra(Str(p, "parentesco")) == "hijo") {
                nombre = Str(p, "nombre");
                break;
            }
        }
        cJSON_Delete(raiz);
    }
    if (nombre.empty()) return "Tu hijo";
    return ComoLoLlamaMama(nombre);
}

// "¿Tengo algún aviso?", "no lo escuché, repetímelo"
std::string Madre::TextoAvisosRecientes() {
    std::lock_guard<std::mutex> lock(mutex_);
    time_t ahora = time(nullptr);
    std::string s;
    for (auto& a : avisos_) {
        if (a.intentos == 0) s += "- Todavía no se lo dijiste: " + a.texto + "\n";
    }
    for (auto it = escuchados_.rbegin(); it != escuchados_.rend(); ++it) {
        if (ahora - it->cuando > 12 * 3600) continue;
        struct tm tm;
        localtime_r(&it->cuando, &tm);
        char hh[16];
        snprintf(hh, sizeof(hh), "%02d:%02d", tm.tm_hour, tm.tm_min);
        s += std::string("- A las ") + hh + ": " + it->texto;
        if (it->estado == "confirmo") s += " (ya lo confirmó)";
        else if (it->estado == "no_confirmo") s += " (no lo confirmó a tiempo)";
        else if (it->pide_confirmar) s += " [todavía falta que lo confirme]";
        s += "\n";
    }
    if (s.empty()) return "No hay avisos de las últimas horas: no tiene nada pendiente.";
    return "Avisos de las últimas horas (el más reciente primero):\n" + s;
}

void Madre::RegistrarRecordatoriosTelegram() {
    auto& tg = TelegramBot::GetInstance();
    tg.AddAdminCommand(
        [this](const std::string& cmd, const std::string& arg, std::string& texto, std::string& b) {
            if (cmd == "/recordar") {
                texto = AgregarRecordatorioTelegram(arg, b);
                return true;
            }
            if (cmd == "/recordatorios") {
                texto = ListaRecordatoriosTelegram(b);
                return true;
            }
            return false;
        },
        "/recordar 28/11 10:30 Turno con el médico - agendarle un recordatorio a mamá\n"
        "/recordatorios - ver y borrar los recordatorios");
    tg.AddCallbackHandler([this](const std::string& data, std::string& texto, std::string& b) {
        if (data.rfind("rec:del:", 0) == 0) {
            texto = BorrarRecordatorio(data.substr(8));
            return true;
        }
        if (data == "rec:ver") {
            texto = ListaRecordatoriosTelegram(b);
            return true;
        }
        return false;
    });
}

// ----------------------------------------------------------------------------
// Emergencia: "Sofía, necesito ayuda" / botón apretado 5 segundos
// ----------------------------------------------------------------------------
namespace {

std::vector<std::string> PalabrasClave(const std::string& texto, bool partir_espacios);  // más abajo

// "21:05" (o vacío si el reloj no está en hora)
std::string HoraCorta() {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    if (tm.tm_year + 1900 < 2025) return "";
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

std::string HoraAhora() {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    if (tm.tm_year + 1900 < 2025) return "";
    char buf[48];
    snprintf(buf, sizeof(buf), "%s %d/%d a las %02d:%02d", kDias[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1,
             tm.tm_hour, tm.tm_min);
    return buf;
}

// Minúsculas, sin acentos ni signos: "¡Sofía, me caí!" -> "sofia me cai"
std::string FraseNormalizada(const std::string& texto) {
    std::string t = ClaveCompra(texto);
    std::string r = " ";
    for (char c : t) {
        bool letra = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (letra) {
            r += c;
        } else if (r.back() != ' ') {
            r += ' ';
        }
    }
    if (r.back() != ' ') r += ' ';
    return r;   // con espacios al principio y al final, para buscar palabras enteras
}

}  // namespace

bool Madre::PedirAyuda(const std::string& dijo, const char* origen) {
    int64_t ahora = esp_timer_get_time() / 1000;
    Emergencia e;
    {
        std::lock_guard<std::mutex> lock(emerg_mutex_);
        if (emerg_.activa && ahora - emerg_.inicio_ms < 10 * 60 * 1000) {
            // Ya hay un pedido en marcha: se suma lo que dijo, sin volver a mandar
            if (!dijo.empty() && emerg_.dijo.find(dijo) == std::string::npos && emerg_.dijo.size() < 300) {
                emerg_.dijo += emerg_.dijo.empty() ? dijo : " / " + dijo;
            }
            if (strcmp(origen, "botón") == 0) emerg_hablar_ = true;
            ESP_LOGW(TAG, "Emergencia: ya estaba avisada (%s)", origen);
            return false;
        }
        int id = emerg_.id + 1;
        emerg_ = Emergencia();
        emerg_.id = id;
        emerg_.activa = true;
        emerg_.envios = 1;
        emerg_.inicio_ms = ahora;
        emerg_.ultimo_envio_ms = ahora;
        emerg_.dijo = dijo;
        emerg_.origen = origen;
        emerg_.hora = HoraAhora();
        e = emerg_;
    }
    ESP_LOGW(TAG, "EMERGENCIA (%s): %s", origen, dijo.c_str());
    std::string botones = "{\"inline_keyboard\":[[{\"text\":\"🏃 Voy yo\",\"callback_data\":\"emerg:voy:" +
                          std::to_string(e.id) + "\"}]]}";
    EnviarATodos(TextoEmergencia(e, false), botones);
    Anotar("🚨 " + HoraCorta() + " pidió ayuda" +
           (e.dijo.empty() ? std::string(" con el botón") : ": \"" + e.dijo + "\""));
    if (strcmp(origen, "botón") == 0) emerg_hablar_ = true;   // que XiaoZhi la tranquilice
    return true;
}

void Madre::EnviarATodos(const std::string& texto, const std::string& botones, const std::string& salvo) {
    auto& tg = TelegramBot::GetInstance();
    bool prueba;
    {
        std::lock_guard<std::mutex> lock(emerg_mutex_);
        prueba = esp_timer_get_time() / 1000 < emerg_prueba_hasta_ms_;
    }
    if (prueba) {
        tg.QueueUrgent("🧪 PRUEBA (sólo te llega a vos)\n\n" + texto, "", botones);
        return;
    }
    for (auto& c : tg.GetContacts()) {
        if (c.first != salvo) tg.QueueUrgent(texto, c.first, botones);
    }
}

std::string Madre::TextoEmergencia(const Emergencia& e, bool repeticion) {
    std::string apodo;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apodo = aj_.mama_apodo.empty() ? "Mamá" : aj_.mama_apodo;
    }
    std::string s;
    if (repeticion) {
        s = "🚨 TODAVÍA NADIE RESPONDIÓ\n" + apodo + " pidió ayuda" +
            (e.hora.empty() ? std::string("") : " el " + e.hora) + " (aviso " + std::to_string(e.envios) + " de 4).";
    } else {
        s = "🚨 ¡" + apodo + " necesita ayuda!";
        if (!e.hora.empty()) s += "\n" + e.hora;
    }
    if (e.origen == "botón" && e.dijo.empty()) {
        s += "\nMantuvo apretado el botón de ayuda.";
    } else if (!e.dijo.empty()) {
        s += "\nDijo: \"" + e.dijo + "\"";
    }
    s += "\n\nSi podés ir o llamarla, tocá el botón así los demás saben que alguien va.";
    return s;
}

// Cómo llama mamá a quien tocó "Voy yo" (contacto "Julio" -> "Marcelo", según la pestaña Familia)
std::string Madre::ComoLoLlamaMama(const std::string& nombre_telegram) {
    std::string primero = ClaveCompra(nombre_telegram.substr(0, nombre_telegram.find(' ')));
    if (primero.empty()) return nombre_telegram;
    std::string familia = GetSeccion("familia");
    std::string resultado = nombre_telegram;
    cJSON* raiz = cJSON_Parse(familia.c_str());
    const cJSON* p;
    cJSON_ArrayForEach(p, raiz) {
        std::string nombre = Str(p, "nombre");
        bool es = false;
        for (auto& w : PalabrasClave(nombre, true)) es = es || w == primero;
        for (auto& w : PalabrasClave(Str(p, "apodo"), false)) es = es || w == primero;
        if (!es) continue;
        std::string apodo = Str(p, "apodo");
        apodo = apodo.substr(0, apodo.find(','));
        apodo = Recortar(apodo);
        resultado = apodo.empty() ? nombre.substr(0, nombre.find(' ')) : apodo;
        break;
    }
    cJSON_Delete(raiz);
    return resultado;
}

bool Madre::AtenderEmergencia(const std::string& data, const std::string& chat_id, const std::string& nombre,
                              std::string& respuesta) {
    const std::string prefijo = "emerg:voy:";
    if (data.rfind(prefijo, 0) != 0) return false;
    int id = atoi(data.c_str() + prefijo.size());
    int64_t ahora = esp_timer_get_time() / 1000;
    std::string hora;
    {
        std::lock_guard<std::mutex> lock(emerg_mutex_);
        if (id != emerg_.id) {
            respuesta = "Ese aviso ya terminó. ¡Gracias igual!";
            return true;
        }
        if (emerg_.atendida_ms != 0) {
            respuesta = "Gracias, " + nombre + ". Ya va " + emerg_.quien + ".";
            return true;
        }
        emerg_.atendida_ms = ahora;
        emerg_.activa = false;
        emerg_.quien = nombre;
        hora = emerg_.hora;
    }
    std::string apodo;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        apodo = aj_.mama_apodo.empty() ? "mamá" : aj_.mama_apodo;
    }
    ESP_LOGW(TAG, "Emergencia atendida por %s", nombre.c_str());
    respuesta = "Gracias, " + nombre + ". Le aviso a " + apodo + " que vas en camino.";
    EnviarATodos("✅ " + nombre + " va en camino a ayudar a " + apodo +
                     (hora.empty() ? std::string(".") : " (pidió ayuda el " + hora + ")."),
                 "", chat_id);
    Anotar("✅ " + HoraCorta() + " " + nombre + " respondió que iba a ayudarla");

    // Que XiaoZhi se lo diga a ella
    std::string quien = ComoLoLlamaMama(nombre);
    Aviso a;
    a.texto = quien + " ya está en camino para ayudarte. Quedate tranquila, " + apodo + ".";
    a.corto = "aviso de que " + quien + " va en camino";
    a.hora = HoraCorta();
    a.creado_ms = ahora;
    a.proximo_ms = ahora;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        avisos_.push_back(a);
    }
    return true;
}

std::string Madre::EstadoEmergencia() {
    std::lock_guard<std::mutex> lock(emerg_mutex_);
    if (emerg_.atendida_ms != 0 && esp_timer_get_time() / 1000 - emerg_.atendida_ms < 60 * 60 * 1000) {
        return emerg_.quien + " ya respondió que va en camino.";
    }
    if (emerg_.activa) return "Ya se avisó a toda la familia; todavía nadie respondió.";
    return "";
}

void Madre::RevisarFraseDeMama(const std::string& texto) {
    static const char* const kPedidos[] = {
        " necesito ayuda ", " necesito auxilio ", " auxilio ", " socorro ", " me cai ", " me caigo ",
        " me siento mal ", " me siento muy mal ", " no me siento bien ", " me descompuse ",
        " no puedo respirar ", " me duele el pecho ", " llama a una ambulancia ", " una ambulancia ",
        " llamen a alguien ", " ayudenme ", " pedi ayuda ", " pidan ayuda ", " me estoy muriendo ",
    };
    std::string f = FraseNormalizada(texto);
    bool pide = false;
    for (auto k : kPedidos) {
        if (f.find(k) != std::string::npos) {
            pide = true;
            break;
        }
    }
    // "Ayuda", "Sofía, ayuda", "¡ayuda, ayuda!" (frase corta con "ayuda")
    if (!pide && f.find(" ayuda ") != std::string::npos) {
        int palabras = 0;
        for (size_t i = 1; i < f.size(); i++) palabras += f[i] == ' ';
        pide = palabras <= 4;
    }
    if (pide) PedirAyuda(Recortar(texto), "frase");
}

void Madre::ProcesarEmergencia(int64_t ahora) {
    // Botón: que XiaoZhi le hable para tranquilizarla
    if (emerg_hablar_ && Application::GetInstance().GetDeviceState() == kDeviceStateIdle &&
        !TelegramBot::GetInstance().IsVoiceBusy()) {
        emerg_hablar_ = false;
        Hablar("Necesito ayuda");
    }

    Emergencia e;
    bool repetir = false, vencida = false;
    {
        std::lock_guard<std::mutex> lock(emerg_mutex_);
        if (!emerg_.activa) return;
        if (emerg_.envios < 4 && ahora - emerg_.ultimo_envio_ms >= 5 * 60 * 1000) {
            emerg_.envios++;
            emerg_.ultimo_envio_ms = ahora;
            repetir = true;
        } else if (emerg_.envios >= 4 && ahora - emerg_.ultimo_envio_ms >= 10 * 60 * 1000) {
            emerg_.activa = false;   // 30 minutos sin respuesta: se deja de insistir
            vencida = true;
        }
        e = emerg_;
    }
    if (repetir) {
        ESP_LOGW(TAG, "Emergencia: nadie respondió, aviso %d", e.envios);
        std::string botones = "{\"inline_keyboard\":[[{\"text\":\"🏃 Voy yo\",\"callback_data\":\"emerg:voy:" +
                              std::to_string(e.id) + "\"}]]}";
        EnviarATodos(TextoEmergencia(e, true), botones);
    }
    if (vencida) {
        TelegramBot::GetInstance().QueueUrgent("⚠️ Nadie respondió al pedido de ayuda" +
                                               (e.hora.empty() ? std::string("") : " del " + e.hora) +
                                               ". Dejé de insistir. Por favor, llamala.");
        Anotar("⚠️ " + HoraCorta() + " nadie respondió al pedido de ayuda");
    }
}

void Madre::RegistrarEmergenciaTelegram() {
    auto& tg = TelegramBot::GetInstance();
    tg.AddFamilyCallbackHandler(
        [this](const std::string& data, const std::string& chat_id, const std::string& nombre,
               std::string& respuesta) { return AtenderEmergencia(data, chat_id, nombre, respuesta); });
    tg.AddAdminCommand(
        [this](const std::string& cmd, const std::string& arg, std::string& texto, std::string&) {
            if (cmd != "/prueba_ayuda") return false;
            std::lock_guard<std::mutex> lock(emerg_mutex_);
            if (arg == "no") {
                emerg_prueba_hasta_ms_ = 0;
                texto = "Modo prueba apagado: los pedidos de ayuda les llegan a todos.";
            } else {
                emerg_prueba_hasta_ms_ = esp_timer_get_time() / 1000 + 60 * 60 * 1000;
                emerg_.activa = false;
                texto = "🧪 Modo prueba por 1 hora: los pedidos de ayuda te llegan sólo a vos.\n"
                        "Probá decir \"Sofía, necesito ayuda\" o mantener el botón 5 segundos.\n"
                        "Para terminar antes: /prueba_ayuda no";
            }
            return true;
        },
        "/prueba_ayuda - probar la emergencia sin avisarle a toda la familia (1 hora)");
}

// ----------------------------------------------------------------------------
// Resumen diario (llega por Telegram a la hora elegida; también con /resumen)
// ----------------------------------------------------------------------------
namespace {

std::string HoraMin(time_t t) {
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

std::string Duracion(int64_t ms) {
    int min = (int)(ms / 60000);
    if (min < 60) return std::to_string(min) + " min";
    return std::to_string(min / 60) + " h " + std::to_string(min % 60) + " min";
}

// "mi hijo", "Marcelo", "la gringa" -> nombre del familiar ("Julio"), según la pestaña Familia.
// Se fija en todos los nombres de la persona, en sus apodos (pueden ser varios: "Marcelo, Marce")
// y en el parentesco. Si coincide con más de una persona, no elige (XiaoZhi pregunta cuál).
std::vector<std::string> PalabrasClave(const std::string& texto, bool partir_espacios) {
    std::vector<std::string> r;
    std::string t = ClaveCompra(texto);
    for (const char* sep : {" o ", "/", ";"}) {
        size_t p;
        while ((p = t.find(sep)) != std::string::npos) t.replace(p, strlen(sep), ",");
    }
    size_t i = 0;
    while (i <= t.size()) {
        size_t c = t.find(partir_espacios ? ' ' : ',', i);
        if (partir_espacios) {
            size_t c2 = t.find(',', i);
            if (c2 < c) c = c2;
        }
        std::string w = t.substr(i, c == std::string::npos ? std::string::npos : c - i);
        while (!w.empty() && (w.front() == ' ' || w.front() == '"')) w.erase(0, 1);
        while (!w.empty() && (w.back() == ' ' || w.back() == '"' || w.back() == '.')) w.pop_back();
        for (const char* art : {"el ", "la ", "los ", "las ", "mi "}) {
            if (w.rfind(art, 0) == 0) w.erase(0, strlen(art));
        }
        if (w.size() >= 3) r.push_back(w);
        if (c == std::string::npos) break;
        i = c + 1;
    }
    return r;
}

std::string NombreSegunFamilia(const std::string& familia_json, const std::string& pedido) {
    std::string q = " " + ClaveCompra(pedido) + " ";
    if (q.size() <= 2) return "";
    std::string encontrado;
    int cuantos = 0;
    cJSON* raiz = cJSON_Parse(familia_json.c_str());
    const cJSON* p;
    cJSON_ArrayForEach(p, raiz) {
        std::string nombre = Str(p, "nombre");
        if (nombre.empty()) continue;
        std::vector<std::string> claves = PalabrasClave(nombre, true);         // Julio, Marcelo...
        for (auto& c : PalabrasClave(Str(p, "apodo"), false)) claves.push_back(c);   // marcelo, gringa
        for (auto& c : PalabrasClave(Str(p, "parentesco"), false)) claves.push_back(c);  // hijo
        bool coincide = false;
        for (auto& c : claves) {
            if (q.find(" " + c + " ") != std::string::npos) { coincide = true; break; }
        }
        if (coincide) {
            encontrado = nombre;
            cuantos++;
        }
    }
    cJSON_Delete(raiz);
    return cuantos == 1 ? encontrado : "";
}

}  // namespace

void Madre::Anotar(const std::string& linea) {
    std::lock_guard<std::mutex> lock(diario_mutex_);
    if (diario_.eventos.size() < 40) diario_.eventos.push_back(linea);
}

// Termina un período de descanso (quieta en la cama). Se anota si duró 30 minutos o más.
void Madre::CortarSueno(int64_t ahora, int64_t fin_hace_ms) {
    (void)ahora;
    int64_t duro = sueno_ms_;
    sueno_ms_ = 0;
    if (duro < 30 * 60 * 1000 || !RelojEnHora()) return;
    time_t fin = time(nullptr) - (time_t)(fin_hace_ms / 1000);
    time_t inicio = fin - (time_t)(duro / 1000);
    std::string linea = HoraMin(inicio) + " a " + HoraMin(fin) + " (" + Duracion(duro) + ")";
    ESP_LOGI(TAG, "Descanso: %s", linea.c_str());
    std::lock_guard<std::mutex> lock(diario_mutex_);
    if (diario_.sueno.size() < 12) diario_.sueno.push_back(linea);
}

void Madre::RevisarResumen(const char* hhmm, int minuto) {
    bool activo;
    std::string hora;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        activo = aj_.resumen_activo;
        hora = aj_.resumen_hora;
    }
    if (!activo || hora != hhmm || resumen_minuto_ == minuto) return;
    resumen_minuto_ = minuto;
    std::string texto = TextoResumen();
    ESP_LOGI(TAG, "Resumen diario enviado");
    TelegramBot::GetInstance().QueueMessage(texto);
    std::lock_guard<std::mutex> lock(diario_mutex_);
    diario_ = Diario();
    diario_.desde = time(nullptr);
    diario_.desde_arranque = false;
}

std::string Madre::TextoResumen() {
    std::string nombre;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        nombre = aj_.mama_apodo.empty() ? "mamá" : aj_.mama_apodo;
    }
    int64_t ahora = esp_timer_get_time() / 1000;
    int64_t quieta = sueno_ms_;
    int64_t sin_senal = ahora - ultima_senal_ms_;
    std::string presencia = presencia_ ? presencia_() : "";

    Diario d;
    {
        std::lock_guard<std::mutex> lock(diario_mutex_);
        d = diario_;
    }
    std::string s = "📋 Resumen de " + nombre;
    if (RelojEnHora()) {
        time_t t = time(nullptr);
        struct tm tm;
        localtime_r(&t, &tm);
        char buf[48];
        snprintf(buf, sizeof(buf), " — %s %d/%d, %02d:%02d", kDias[tm.tm_wday], tm.tm_mday, tm.tm_mon + 1,
                 tm.tm_hour, tm.tm_min);
        s += buf;
    }
    if (d.desde_arranque) {
        s += "\n(desde que se encendió el equipo)";
    } else {
        struct tm tm;
        localtime_r(&d.desde, &tm);
        s += std::string("\n(desde las ") + HoraMin(d.desde) + " del " + kDias[tm.tm_wday] + ")";
    }

    s += "\n\n😴 Descanso (quieta en la cama):";
    if (d.sueno.empty() && quieta < 30 * 60 * 1000) s += "\n• Nada largo para anotar.";
    for (auto& l : d.sueno) s += "\n• " + l;
    if (quieta >= 30 * 60 * 1000 && RelojEnHora()) {
        s += "\n• Ahora: descansando desde las " + HoraMin(time(nullptr) - (time_t)(quieta / 1000)) +
             " (" + Duracion(quieta) + ")";
    }

    s += "\n\n💬 Charlas con XiaoZhi: ";
    if (d.charlas == 0) {
        s += "ninguna.";
    } else {
        s += std::to_string(d.charlas) + (d.charlas == 1 ? " vez" : " veces");
        if (d.ultima_charla && RelojEnHora()) s += " (la última a las " + HoraMin(d.ultima_charla) + ")";
        s += ".";
    }
    s += "\n👆 Tocó el botón: " + std::to_string(d.botones) + (d.botones == 1 ? " vez." : " veces.");

    s += "\n\n🔔 Recordatorios y avisos:";
    if (d.eventos.empty()) s += "\n• No hubo.";
    for (auto& l : d.eventos) s += "\n• " + l;

    if (!d.compras.empty()) {
        s += "\n\n🛒 Anotó en compras: ";
        for (size_t i = 0; i < d.compras.size(); i++) s += (i ? ", " : "") + d.compras[i];
        s += ". (/compras para ver la lista)";
    }

    s += "\n\n📡 Ahora: ";
    s += presencia.empty() ? std::string("radar sin datos") : presencia;
    if (sin_senal > 60 * 1000) s += "; última señal de ella hace " + Duracion(sin_senal);
    s += ".";
    return s;
}

void Madre::RegistrarResumenTelegram() {
    auto& tg = TelegramBot::GetInstance();
    tg.AddAdminCommand(
        [this](const std::string& cmd, const std::string&, std::string& texto, std::string&) {
            if (cmd != "/resumen") return false;
            texto = TextoResumen();
            return true;
        },
        "/resumen - cómo viene el día de mamá (descanso, charlas, recordatorios, compras)");
    // "decile a mi hijo que me llame": el apodo o el parentesco de la pestaña Familia
    tg.SetNameResolver([this](const std::string& pedido) {
        return NombreSegunFamilia(GetSeccion("familia"), pedido);
    });
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

// Para ella, simple: qué casillero y cuántas pastillas. Los colores, sólo si pregunta.
// Los nombres de los remedios y las dosis no se le dicen (están en la página para la familia).
std::string Madre::TextoPastillero(bool solo_hoy) {
    static const char* const kFranjas[][3] = {
        {"manana", "Mañana", "mañana"}, {"tarde", "Tarde", "tarde"}, {"noche", "Noche", "noche"}};
    cJSON* raiz = cJSON_Parse(GetSeccion("pastillero").c_str());
    const cJSON* franjas = cJSON_GetObjectItemCaseSensitive(raiz, "franjas");
    int hoy = DiaPaginaHoy();
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    std::string s;
    if (solo_hoy) s = std::string("Hoy es ") + kDias[tm.tm_wday] + ".\n";
    bool hay = false;
    for (auto& f : kFranjas) {
        const cJSON* fr = cJSON_GetObjectItemCaseSensitive(franjas, f[0]);
        if (!cJSON_IsObject(fr)) continue;
        std::string hora = Str(fr, "hora");
        double pastillas = 0;
        std::string colores;
        std::vector<std::string> dias_txt;
        const cJSON* m;
        cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(fr, "meds")) {
            if (Str(m, "nombre").empty()) continue;
            if (solo_hoy && !TieneDia(m, hoy)) continue;
            pastillas += ContarPastillas(Str(m, "dosis"));
            std::string aspecto = Str(m, "aspecto");
            colores += (colores.empty() ? "" : ", ") + (aspecto.empty() ? std::string("una pastilla") : aspecto);
        }
        if (pastillas == 0) continue;
        hay = true;
        s += std::string("- ") + f[1] + (hora.empty() ? "" : " (a las " + hora + ")") + ": ";
        if (solo_hoy) s += std::string("casillero del ") + kDias[tm.tm_wday] + " a la " + f[2] + ", ";
        s += TextoPastillas(pastillas) + ". [Cómo son, sólo si pregunta: " + colores + "]\n";
    }
    cJSON_Delete(raiz);
    if (!hay) {
        return solo_hoy ? s + "Hoy no hay remedios cargados en el pastillero."
                        : "Todavía no hay remedios cargados en el pastillero.";
    }
    s += "\nDecile sólo qué casillero abrir y cuántas pastillas tiene. No nombres los remedios ni las dosis. "
         "Lo que está entre corchetes no lo leas, usalo sólo si pregunta cómo son. Respondé en texto simple, "
         "sin asteriscos ni listas.";
    return s;
}

std::string Madre::TextoRecordatorios(bool solo_hoy) {
    cJSON* raiz = cJSON_Parse(GetSeccion("recordatorios").c_str());
    int hoy = DiaPaginaHoy();
    time_t ahora_t = time(nullptr);
    struct tm tm_hoy;
    localtime_r(&ahora_t, &tm_hoy);
    char hoy_iso[32];
    snprintf(hoy_iso, sizeof(hoy_iso), "%04d-%02d-%02d", tm_hoy.tm_year + 1900, tm_hoy.tm_mon + 1, tm_hoy.tm_mday);
    std::string s;
    const cJSON* r;
    cJSON_ArrayForEach(r, raiz) {
        if (!Bool(r, "activo", true)) continue;
        std::string fecha = Str(r, "fecha");
        if (solo_hoy && (fecha.empty() ? !TieneDia(r, hoy) : fecha != hoy_iso)) continue;
        if (!fecha.empty() && fecha < hoy_iso) continue;
        std::string texto = Str(r, "texto"), hora = Str(r, "hora");
        if (texto.empty()) continue;
        s += "- " + hora + ": " + texto;
        if (!solo_hoy) s += " (" + (fecha.empty() ? TextoDias(r) : "una sola vez, el " + FechaLinda(fecha)) + ")";
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
        "Get which compartment of the user's pillbox she has to open (day and Mañana / Tarde / Noche), "
        "at what time, and how many pills it has. Use it when she asks which pills to take or what is in "
        "the pillbox. Follow the instructions it returns. 'today_only' = true for today only.",
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
        "'[pedir confirmación]', at the end ask her to touch the button or say 'listo' when she has done it. "
        "Other text in brackets is private information for you: never read it aloud, use it only if she asks "
        "(do not read the brackets aloud).",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            std::lock_guard<std::mutex> lock(mutex_);
            if (aviso_actual_.empty()) return std::string("No hay avisos pendientes.");
            std::string s = "Recordatorios para " + (aj_.mama_apodo.empty() ? std::string("la usuaria") : aj_.mama_apodo) + ":\n";
            for (auto& a : aviso_actual_) s += "- " + a + "\n";
            return s;
        });

    mcp.AddTool("self.madre.get_recent_notices",
        "Call it when the user asks whether she has a pending notice, says she did not hear or did not "
        "understand the last notice, or asks to repeat it ('¿tengo algún aviso?', '¿tengo algo pendiente?', "
        "'¿qué me dijiste recién?', 'no escuché el aviso', 'no llegué a escucharlo', 'repetímelo', "
        "'sonó la campana, ¿qué era?'). It returns the notices of the last hours, newest first. Tell her "
        "again, slowly and clearly, the most recent one (or the ones she missed), addressing her by her "
        "name. If one says '[todavía falta que lo confirme]', ask her to touch the button or say 'listo' "
        "when she has done it (do not read the brackets). For messages that family sent by Telegram use "
        "the Telegram tools instead.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue { return TextoAvisosRecientes(); });

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

    mcp.AddTool("self.madre.shopping_add",
        "Add things to the user's shopping list. Use it when she says she needs something, ran out of "
        "something or asks to note something for shopping ('me falta harina', 'anotá yerba', 'se terminó "
        "el azúcar', 'necesito pilas'). 'items': the things in Spanish, short, separated by commas, "
        "keeping quantities she mentions (e.g. 'harina, 2 litros de leche'). Then confirm briefly what "
        "you noted. Her son Julio sees the list on Telegram and does the shopping.",
        PropertyList({Property("items", kPropertyTypeString)}),
        [this](const PropertyList& props) -> ReturnValue {
            return AgregarCompras(props["items"].value<std::string>(), true);
        });

    mcp.AddTool("self.madre.shopping_list",
        "Read the user's shopping list. Use it when she asks what is on the list or what she has to buy.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue { return TextoCompras(false); });

    mcp.AddTool("self.madre.shopping_remove",
        "Remove something from the user's shopping list, when she says she doesn't need it anymore or "
        "already has it ('sacá la yerba', 'ya no hace falta el pan').",
        PropertyList({Property("item", kPropertyTypeString)}),
        [this](const PropertyList& props) -> ReturnValue {
            return QuitarCompra(props["item"].value<std::string>());
        });

    mcp.AddTool("self.madre.shopping_send",
        "Send the shopping list to her son Julio by Telegram right now. Use it only when she explicitly "
        "asks to send the list to Julio ('mandale la lista a Julio').",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            TelegramBot::GetInstance().QueueMessage(TextoCompras(true));
            return std::string("Listo, la lista le va a llegar a Julio por Telegram.");
        });

    mcp.AddTool("self.madre.emergency",
        "EMERGENCY: alert the whole family by Telegram that she needs help. Call it IMMEDIATELY, without "
        "asking anything first, whenever she says she needs help, feels ill or very bad, fell, is hurt, "
        "has chest pain, can't breathe, or asks to call someone urgently or an ambulance "
        "('Sofía, necesito ayuda', 'me caí', 'me siento mal', 'auxilio', 'llamá a alguien'). "
        "Do NOT use it for ordinary help ('ayudame a recordar algo', 'ayudame con la lista'). "
        "'said' = her words, in Spanish. Then follow the instructions it returns: stay calm and "
        "reassuring, speak slowly, in short sentences.",
        PropertyList({Property("said", kPropertyTypeString)}),
        [this](const PropertyList& props) -> ReturnValue {
            std::string dijo = props["said"].value<std::string>();
            bool nuevo = PedirAyuda(dijo, "voz");
            std::string estado = EstadoEmergencia();
            size_t n = TelegramBot::GetInstance().GetContacts().size();
            std::string r = nuevo ? "Ya avisé a toda la familia por Telegram (" + std::to_string(n) +
                                        (n == 1 ? " persona). " : " personas). ")
                                  : "La familia ya estaba avisada. " + estado + " ";
            r += "Decile con calma, en frases cortas: que ya les avisaste a todos, que enseguida alguien la "
                 "va a llamar o a venir, que se quede tranquila y, si puede, sentada o acostada sin moverse "
                 "mucho. Preguntale cómo se siente y si está lastimada. Si dice que le duele el pecho, que no "
                 "puede respirar o que perdió fuerza en un brazo o la cara, decile que es importante llamar "
                 "al 107 (emergencias). No cortes la charla: acompañala. MUY IMPORTANTE: NUNCA le digas "
                 "que alguien ya viene, que está en camino o que va a llegar, ni nombres a nadie como si "
                 "viniera, hasta que el equipo te pase un aviso diciendo quién va en camino (eso llega "
                 "solo cuando un familiar lo confirma). Mientras tanto decí sólo que la familia ya está "
                 "avisada y que enseguida alguien la va a llamar o a venir.";
            return r;
        });

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
