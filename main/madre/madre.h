// Equipo de mamá: datos de la familia, recordatorios, pastillero y ajustes.
//
// - Guarda los datos en una partición NVS propia ("madre", 64 KB).
// - Pone el reloj en hora argentina (SNTP + zona ART3).
// - Sirve la página de configuración en http://IP-del-equipo (puerto 80).
// - Registra herramientas para que XiaoZhi consulte estos datos.
//
// Sólo se compila con CONFIG_MADRE_ENABLE (configuración "Madre").

#pragma once

#include <atomic>
#include <string_view>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

typedef struct httpd_req httpd_req_t;
struct cJSON;
typedef void* httpd_handle_t;

class Madre {
public:
    static Madre& GetInstance() {
        static Madre instancia;
        return instancia;
    }

    // Llamar una vez al arrancar (después de Telegram, antes de app.Run()).
    void Start();

    // La placa informa cómo describir la presencia actual (radar), para la página.
    void SetPresenceProvider(std::function<std::string()> proveedor) { presencia_ = std::move(proveedor); }

    // La placa informa el estado del radar: 0 = sin datos, 1 = nadie, 2 = en movimiento, 3 = quieta.
    void SetRadarProvider(std::function<int()> proveedor) { radar_ = std::move(proveedor); }

    // La placa aplica los ajustes del radar (alcance en puertas de 75 cm, segundos para "nadie",
    // avisos de prueba por Telegram).
    void SetRadarConfigurator(std::function<void(int puertas, int segundos, bool avisos)> f) {
        radar_config_ = std::move(f);
    }

    // La placa avisa cada vez que se toca el botón (confirma recordatorios y cuenta como señal de vida).
    void NotificarBoton();

    // Tono de las notificaciones (campana, main/madre/campana.ogg)
    static std::string_view SonidoCampana();

    // ---- Emergencia ("Sofía, necesito ayuda", botón apretado 5 segundos)
    // Avisa a todos los familiares de Telegram. Se puede llamar desde cualquier tarea (no bloquea).
    // origen: "voz" (lo pidió la IA), "frase" (palabras detectadas por el equipo) o "botón".
    // Devuelve true si es un pedido nuevo (false si ya había uno en marcha).
    bool PedirAyuda(const std::string& dijo, const char* origen);
    // Texto que reconoció el servidor de lo que dijo mamá: si pide ayuda, dispara la emergencia
    // aunque la IA no responda.
    void RevisarFraseDeMama(const std::string& texto);

    // Datos guardados (JSON). Seguros para llamar desde cualquier tarea.
    std::string GetSeccion(const std::string& seccion);

    // Dirección de la página ("http://192.168.0.233") o vacío si no hay WiFi.
    std::string DireccionPagina();

    // Fecha y hora local en palabras ("miércoles 25 de noviembre de 2026, 09:40, de mañana").
    // Devuelve vacío si el reloj todavía no está en hora.
    std::string FechaEnPalabras();

private:
    Madre() = default;

    std::mutex mutex_;
    bool iniciado_ = false;
    bool almacenamiento_ok_ = false;
    std::string particion_ = "madre";
    std::string familia_ = "[]";
    std::string recordatorios_ = "[]";
    std::string pastillero_ = "{}";
    std::string ajustes_ = "{}";
    std::string compras_ = "[]";
    httpd_handle_t servidor_ = nullptr;
    std::function<std::string()> presencia_;
    std::function<int()> radar_;
    std::function<void(int, int, bool)> radar_config_;

    // ---- Ajustes en uso (se leen de la sección "ajustes")
    struct Ajustes {
        bool radar_activo = true;
        int radar_puertas = 2;
        int radar_sin_nadie = 5;
        bool radar_avisos = false;
        bool saludo_activo = true;
        int saludo_horas_quieta = 2;
        bool saludo_alternar = true;
        bool saludo_decir_fecha = true;
        std::vector<std::string> saludo_frases;
        bool bienestar_activo = true;
        int bienestar_horas = 14;
        int volumen = 70;
        std::string mama_apodo;
        bool noticias_activo = true;
        bool noticias_en_saludo = false;
        std::string noticias_fuente = "infobae";
        int noticias_cantidad = 3;
        bool resumen_activo = true;
        std::string resumen_hora = "22:00";
        int remedios_avisos = 3;     // veces que se le repite el aviso de los remedios
        int remedios_minutos = 10;   // minutos entre un aviso y el siguiente
    };
    Ajustes aj_;
    void AplicarAjustes();
    bool CambiarAjuste(const char* seccion, const char* campo, cJSON* valor);  // toma valor
    std::string TextoAjustes(std::string& botones);
    void RegistrarAjustesTelegram();

    // ---- Recordatorios pendientes
    struct Aviso {
        std::string texto;
        bool repetir = false;
        bool avisar = false;
        bool remedio = false;
        std::string hora;          // "21:00" (para el resumen)
        std::string corto;         // texto corto para el resumen ("remedios de la noche")
        std::string rec_id;        // id del recordatorio (para registrar qué pasó)
        std::string privado;       // dato para la IA que no se lee salvo que pregunte (colores)
        int intentos = 0;          // veces que se dijo
        int64_t creado_ms = 0;
        int64_t proximo_ms = 0;    // cuándo decirlo (otra vez)
    };
    std::vector<Aviso> avisos_;
    // Repeticiones de un aviso (los de remedios se regulan en Ajustes). Llamar con mutex_ tomado.
    int VecesAviso(const Aviso& a) const;
    int MinutosAviso(const Aviso& a) const;
    std::vector<std::string> aviso_actual_;     // lo que se está diciendo ahora
    int ultimo_minuto_ = -1;
    void RevisarAgenda(int64_t ahora_ms);
    void ProcesarAvisos(int64_t ahora_ms);
    void ConfirmarAvisos(const char* como);

    // ---- Presencia, saludo y bienestar
    int estado_radar_ = 0;
    int64_t ultimo_movimiento_ms_ = 0;
    int64_t mov_inicio_ms_ = 0;            // comienzo del movimiento actual
    int64_t sueno_ms_ = 0;                 // tiempo acumulado quieta (los movimientos cortos no lo cortan)
    int64_t nadie_ms_ = 0;                 // tiempo seguido sin nadie
    int64_t ultimo_tick_ms_ = 0;
    bool estaba_libre_ = true;             // para contar las charlas
    int64_t activacion_propia_ms_ = 0;     // cuándo XiaoZhi habló por su cuenta (aviso o saludo)
    bool saludo_pendiente_ = false;
    int64_t saludo_desde_ms_ = 0;
    int64_t ultima_conversacion_ms_ = 0;
    int64_t ultima_senal_ms_ = 0;
    bool bienestar_alertado_ = false;
    size_t indice_frase_ = 0;
    void ActualizarPresencia(int64_t ahora_ms);
    void RevisarBienestar(int64_t ahora_ms);
    bool Durmiendo(int64_t ahora_ms);
    bool PuedeHablar(int64_t ahora_ms, bool es_saludo);
    void Hablar(const char* frase_activacion);
    std::string TextoSaludo();

    // ---- Noticias (se bajan solas cada 30 minutos con el equipo libre)
    std::vector<std::string> noticias_;
    std::string noticias_de_;            // fuente de las noticias guardadas
    int64_t noticias_ms_ = 0;            // cuándo se bajaron
    int64_t noticias_intento_ms_ = 0;    // último intento
    void ActualizarNoticias(int64_t ahora_ms);
    std::string TextoNoticias();

    // ---- Emergencia
    struct Emergencia {
        bool activa = false;        // se pidió ayuda y todavía nadie respondió "Voy yo"
        int id = 0;
        int envios = 0;
        int64_t inicio_ms = 0;
        int64_t ultimo_envio_ms = 0;
        std::string dijo, origen, hora, quien;
        int64_t atendida_ms = 0;    // cuándo alguien respondió (0 = nadie)
    };
    std::mutex emerg_mutex_;        // orden: emerg_mutex_ solo (no se toma con otros)
    Emergencia emerg_;
    std::atomic<bool> emerg_hablar_{false};   // botón: que XiaoZhi la tranquilice
    std::atomic<bool> emerg_enviar_{false};   // hay un aviso nuevo para mandar
    void ProcesarEmergencia(int64_t ahora);
    std::string TextoEmergencia(const Emergencia& e, bool repeticion);
    std::string EstadoEmergencia();
    void RegistrarEmergenciaTelegram();
    bool AtenderEmergencia(const std::string& data, const std::string& chat_id, const std::string& nombre,
                           std::string& respuesta);
    void EnviarATodos(const std::string& texto, const std::string& botones, const std::string& salvo = "");
    std::string ComoLoLlamaMama(const std::string& nombre_telegram);
    int64_t emerg_prueba_hasta_ms_ = 0;     // modo prueba: los avisos van sólo al administrador

    // ---- Resumen diario por Telegram (y /resumen)
    struct Diario {
        time_t desde = 0;                    // desde cuándo cuenta (0 = desde que se encendió)
        bool desde_arranque = true;
        std::vector<std::string> sueno;      // "03:10–11:05 (7 h 55 min)"
        std::vector<std::string> eventos;    // recordatorios, saludos, avisos (en orden)
        std::vector<std::string> compras;    // lo que anotó con la voz
        int charlas = 0;
        time_t ultima_charla = 0;
        int botones = 0;
    };
    std::mutex diario_mutex_;                // orden: mutex_ -> diario_mutex_ (nunca al revés)
    Diario diario_;
    int resumen_minuto_ = -1;
    void Anotar(const std::string& linea);
    void CortarSueno(int64_t ahora, int64_t fin_hace_ms);
    void RevisarResumen(const char* hhmm, int minuto);
    std::string TextoResumen();
    void RegistrarResumenTelegram();

    // ---- Recordatorios desde Telegram (/recordar 28/11 10:30 Turno con el médico)
    void RegistrarRecordatoriosTelegram();
    bool GuardarRecordatorios(const std::string& json);
    std::string AgregarRecordatorioTelegram(const std::string& pedido, std::string& botones);
    std::string ListaRecordatoriosTelegram(std::string& botones);
    std::string BorrarRecordatorio(const std::string& id);
    void LimpiarRecordatoriosViejos();
    // Qué pasó con cada recordatorio ("esperando", "confirmo_boton", "no_confirmo"...).
    // Se encola desde cualquier tarea y se guarda en la tarea madre.
    std::mutex estados_mutex_;
    std::vector<std::pair<std::string, std::string>> estados_pendientes_;
    void MarcarEstado(const std::string& rec_id, const char* estado);

    // Avisos que ya le dijo (para "¿tengo algún aviso?", "no lo escuché, repetímelo")
    struct Escuchado {
        time_t cuando = 0;
        int64_t creado_ms = 0;     // identifica el aviso
        std::string texto;
        bool pide_confirmar = false;
        std::string estado;        // "", "confirmo", "no_confirmo"
    };
    std::vector<Escuchado> escuchados_;   // protegido por mutex_
    std::string TextoAvisosRecientes();
    std::string QuienMandaTelegram();
    void AplicarEstados();

    // ---- Lista de compras (ella la arma con la voz; Julio la pide con /compras)
    std::string AgregarCompras(const std::string& items, bool de_mama = false);
    std::string QuitarCompra(const std::string& item);
    std::string TextoCompras(bool para_telegram);
    void VaciarCompras();
    void RegistrarComprasTelegram();

    // Almacenamiento
    void IniciarAlmacenamiento();
    std::string Leer(const char* clave, const char* por_defecto);
    bool Guardar(const char* clave, const std::string& valor);
    std::string* Campo(const std::string& seccion, const char** clave_nvs, bool* es_lista);

    // Reloj
    void IniciarReloj();

    // Tarea principal
    static void TareaEntrada(void* arg);
    void Tarea();

    // Servidor web
    void IniciarServidor();
    static int PaginaHandler(httpd_req_t* req);
    static int ConfigHandler(httpd_req_t* req);
    static int GuardarHandler(httpd_req_t* req);
    std::string EstadoJson();

    // Herramientas para XiaoZhi
    void RegistrarHerramientas();
    std::string TextoFamilia();
    std::string TextoMama();
    std::string TextoPastillero(bool solo_hoy);
    std::string TextoRecordatorios(bool solo_hoy);
};
