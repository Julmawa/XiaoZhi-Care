#pragma once

#include "care_radio/radio_service.h"

#include <esp_log.h>

#include <string>

#include "care_mcp_service.h"
#include "mcp_server.h"

namespace xiaozhi_care {

// This small adapter is intentionally header-only. It is compiled from the
// XiaoZhi "main" component, where mcp_server.h already belongs, avoiding a
// circular ESP-IDF component dependency between care-mcp and main.
inline void RegisterCareMcpTools() {
    static bool registered = false;
    if (registered) {
        return;
    }

    auto& server = McpServer::GetInstance();
    auto& service = CareMcpService::GetInstance();

    server.AddTool(
        "care.get_profile",
        "Consulta el perfil personal local de XiaoZhi Care. Usar para preguntas sobre el nombre, apodo, cumpleaños, ciudad o zona horaria del usuario. El parametro field puede ser basic, birthday, city o timezone. Nunca devuelve notas privadas.",
        PropertyList({
            Property("field", kPropertyTypeString, std::string("basic")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_profile");
            return service.GetProfile(properties["field"].value<std::string>());
        });

    server.AddTool(
        "care.find_person",
        "Busca una persona importante en la memoria local por nombre, apodo, alias o relacion (por ejemplo hija, nieta, vecino, medico). USAR SOLO para identidad, parentesco basico, cumpleaños, telefono o direccion. NO uses esta herramienta para responder gustos, que le gusta hacer, comida, musica, television, hobbies, mascotas, perro, gato, costumbres, habitos, siesta ni suele hacer. Para gustos o actividades usa care.get_hobbies; para comida usa care.get_food; para costumbres/siesta usa care.get_habits; para mascota usa care.get_pets. field controla el dato minimo a devolver: basic, birthday, phone o address. Usar basic si solo se necesita saber quien es. Si found=false pero clarification_required=true y hay similar_name_candidates, NO elijas automaticamente: pregunta brevemente al usuario si se referia a uno de esos nombres y vuelve a consultar solo despues de su confirmacion.",
        PropertyList({
            Property("query", kPropertyTypeString),
            Property("field", kPropertyTypeString, std::string("basic")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.find_person");
            const uint32_t care_t0_ms = esp_log_timestamp();
            ReturnValue care_result = service.FindPerson(properties["query"].value<std::string>(),
                                         properties["field"].value<std::string>());
            ESP_LOGI("CARE_MCP_TIME", "tool=care.find_person elapsed_ms=%lu",
                     (unsigned long)(esp_log_timestamp() - care_t0_ms));
            return care_result;
        });

    server.AddTool(
        "care.get_hobbies",
        "Herramienta obligatoria para preguntas sobre hobbies, actividades favoritas, que le gusta hacer, que le encanta hacer o pasatiempos de una persona. Devuelve SOLO preferencias category=hobby. No mezcles comida, mascotas ni costumbres.",
        PropertyList({
            Property("owner", kPropertyTypeString, std::string("profile")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_hobbies");
            const uint32_t care_t0_ms = esp_log_timestamp();
            ReturnValue care_result = service.GetPreference("hobby",
                                         properties["owner"].value<std::string>());
            ESP_LOGI("CARE_MCP_TIME", "tool=care.get_hobbies elapsed_ms=%lu",
                     (unsigned long)(esp_log_timestamp() - care_t0_ms));
            return care_result;
        });


    server.AddTool(
        "care.get_food",
        "Herramienta obligatoria para preguntas sobre comida, comidas favoritas, que le gusta comer, asado, picadas o alimentos preferidos de una persona. Devuelve SOLO preferencias category=food. No mezcles hobbies, mascotas ni costumbres. Se permite un comentario calido breve, por ejemplo 'bien argentino', sin agregar datos nuevos.",
        PropertyList({
            Property("owner", kPropertyTypeString, std::string("profile")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_food");
            const uint32_t care_t0_ms = esp_log_timestamp();
            ReturnValue care_result = service.GetPreference("food",
                                         properties["owner"].value<std::string>());
            ESP_LOGI("CARE_MCP_TIME", "tool=care.get_food elapsed_ms=%lu",
                     (unsigned long)(esp_log_timestamp() - care_t0_ms));
            return care_result;
        });


    server.AddTool(
        "care.get_pets",
        "Herramienta obligatoria para preguntas sobre mascota, perro, gato, animal o como se llama la mascota de una persona. Devuelve SOLO preferencias category=pets. No mezcles hobbies, comida ni costumbres.",
        PropertyList({
            Property("owner", kPropertyTypeString, std::string("profile")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_pets");
            const uint32_t care_t0_ms = esp_log_timestamp();
            ReturnValue care_result = service.GetPreference("pets",
                                         properties["owner"].value<std::string>());
            ESP_LOGI("CARE_MCP_TIME", "tool=care.get_pets elapsed_ms=%lu",
                     (unsigned long)(esp_log_timestamp() - care_t0_ms));
            return care_result;
        });


    server.AddTool(
        "care.get_habits",
        "Herramienta obligatoria para preguntas sobre costumbres, habitos, rutinas cotidianas, suele hacer, habitualmente o siesta de una persona. Ejemplos: que costumbre tiene Marcelo, que suele hacer Marcelo, Marcelo duerme la siesta. Devuelve SOLO preferencias category=habits. No mezcles hobbies, comida, mascotas, gustos generales ni cuidados. Si no hay datos habits, deci que no hay costumbres registradas.",
        PropertyList({
            Property("owner", kPropertyTypeString, std::string("profile")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_habits");
            const uint32_t care_t0_ms = esp_log_timestamp();
            ReturnValue care_result = service.GetPreference("habits",
                                         properties["owner"].value<std::string>());
            ESP_LOGI("CARE_MCP_TIME", "tool=care.get_habits elapsed_ms=%lu",
                     (unsigned long)(esp_log_timestamp() - care_t0_ms));
            return care_result;
        });


    server.AddTool(
        "care.get_reminders",
        "Consulta recordatorios locales activos y memoria reciente. date usa YYYY-MM-DD; si se envia vacio usa la fecha actual del reloj del dispositivo. Incluye recordatorios de hoy y recordatorios proximos cuando entran en la ventana Empezar a recordar. Aplica recurrencias none, daily, weekly, monthly y yearly. Para preguntas como que tengo hoy, que tengo manana, que pasa esta semana o me olvide de algo, usa safe_message y los campos memory_text/when_text. Nunca expongas notas privadas ni tags internos.",
        PropertyList({
            Property("date", kPropertyTypeString, std::string("")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_reminders");
            return service.GetReminders(properties["date"].value<std::string>());
        });


    server.AddTool(
        "care.add_todo_reminder",
        "Guarda una cosa para hacer creada por voz y la registra como recordatorio local visible en la pestaña Recordatorios del panel web. Usar cuando la persona diga frases como 'me haces acordar que...', 'recordame que...', 'no me dejes olvidar que...', 'tengo que comprar...', 'tengo que llamar...'. El campo text debe contener solo la cosa para hacer, sin la frase introductoria. Ejemplo: si dice 'me haces acordar que compre leche y galletitas', text='comprar leche y galletitas'. Responde siempre en español usando SOLO safe_message. No uses la palabra tecnica pendiente salvo que la persona la use. EXCEPCION IMPORTANTE: no uses esta herramienta para ordenes de radio como prender, poner, escuchar, cambiar, pausar, continuar o apagar la radio, aunque la frase contenga 'quiero', 'tenes que' o 'prender'. Esas ordenes usan exclusivamente care.radio_control.",
        PropertyList({
            Property("text", kPropertyTypeString),
            Property("related_person_id", kPropertyTypeString, std::string("")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.add_todo_reminder");
            return service.AddTodoReminder(properties["text"].value<std::string>(),
                                           properties["related_person_id"].value<std::string>());
        });


    server.AddTool(
        "care.get_pillbox_plan",
        "HERRAMIENTA AUTORITATIVA DEL PASTILLERO. "
        "DEBES usar esta herramienta cuando el usuario mencione explicitamente "
        "remedio, remedios, pastilla, pastillas, medicacion, tomar, pastillero o casillero. "
        "Ejemplos prioritarios: 'que remedio me toca', 'que remedios me tocan', "
        "'que pastillas me tocan', 'que medicacion me toca', "
        "'que tengo que tomar', 'que casillero me toca' o "
        "'que pastillas tengo que tomar'. "
        "Estas expresiones indican que el usuario esta hablando especificamente del pastillero "
        "y NO de otros recordatorios o actividades. "
        "Esta es la UNICA herramienta autorizada para decidir la proxima toma valida del pastillero "
        "y para detectar tomas pasadas sin verificar mediante review_group_id. "
        "Primero informa la proxima toma actual o futura. "
        "Luego puede preguntar por una toma pasada sin confirmar. "
        "Nunca presentes una toma pasada como algo que deba tomar ahora. "
        "Nunca uses care.get_next_routine para una consulta explicitamente relacionada con el pastillero. "
        "Responde SOLO usando safe_message. "
        "No nombres medicamentos especificos.",
        PropertyList({
            Property(
                "day",
                kPropertyTypeString,
                std::string("today")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI(
                "CARE_MCP",
                "Tool call: care.get_pillbox_plan");

            return service.GetPillboxPlan(
                properties["day"].value<std::string>());
        });

    server.AddTool(
        "care.get_today_routines",
        "Herramienta para obtener una lista GENERAL de actividades y recordatorios de hoy. "
        "Usala cuando el usuario solicite explicitamente una lista general de lo que tiene para hacer hoy. "
        "NO usar para preguntas especificas sobre remedios, pastillas, medicacion, tomar, pastillero o casilleros. "
        "Para esas consultas DEBES usar care.get_pillbox_plan.",
        PropertyList({
            Property("weekday", kPropertyTypeInteger, 0, 0, 7),
            Property("time", kPropertyTypeString, std::string("")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_today_routines");
            return service.GetDueRoutines(properties["weekday"].value<int>(),
                                          properties["time"].value<std::string>(),
                                          "today");
        });

    server.AddTool(
        "care.get_next_routine",
        "Herramienta para consultar la proxima ACTIVIDAD O RECORDATORIO GENERAL. "
        "Usala para preguntas como 'que me toca', 'que sigue', "
        "'cual es la proxima actividad' o 'que tengo pendiente', "
        "cuando el usuario NO mencione remedios, pastillas, medicacion, tomar, pastillero o casilleros. "
        "Puede incluir actividades como llamar a una persona, salir, realizar una tarea "
        "o atender otro recordatorio general. "
        "Si el usuario pregunta 'que remedio me toca', 'que pastillas me tocan', "
        "'que tengo que tomar', 'que casillero me toca' o menciona explicitamente el pastillero, "
        "DEBES usar care.get_pillbox_plan. "
        "Esta herramienta NO realiza la revision de tomas pasadas del pastillero.",
        PropertyList({
            Property("weekday", kPropertyTypeInteger, 0, 0, 7),
            Property("time", kPropertyTypeString, std::string("")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_next_routine");
            return service.GetDueRoutines(properties["weekday"].value<int>(),
                                          properties["time"].value<std::string>(),
                                          "next");
        });


    server.AddTool(
        "care.verify_pillbox_group",
        "Resuelve mediante COMPROBACION VISUAL una toma PASADA del pastillero "
        "que care.get_pillbox_plan devolvio como review_group_id. "
        "Usa EXACTAMENTE ese group_id. "
        "result=empty cuando el casillero esta vacio: la toma queda confirmed. "
        "result=contains cuando las pastillas siguen presentes: la toma queda missed. "
        "result=uncertain si la persona no puede determinarlo: no se registra ningun resultado. "
        "NO usar esta herramienta para un horario futuro. "
        "NO inventes el group_id. "
        "NO nombres medicamentos.",
        PropertyList({
            Property(
                "group_id",
                kPropertyTypeString,
                std::string("")),
            Property(
                "result",
                kPropertyTypeString,
                std::string("uncertain")),
        }),
        [&service](
            const PropertyList& properties)
            -> ReturnValue {

            ESP_LOGI(
                "CARE_MCP",
                "Tool call: care.verify_pillbox_group");

            return service.VerifyPillboxGroup(
                properties["group_id"]
                    .value<std::string>(),
                properties["result"]
                    .value<std::string>());
        });

    server.AddTool(
        "care.confirm_pillbox_group",
        "Confirma TODO el casillero del pastillero que acaba de informar "
        "care.get_pillbox_plan. "
        "Usa esta herramienta SOLO cuando la persona confirma claramente "
        "que ya tomo ese casillero, por ejemplo: 'si, ya lo tome', "
        "'ya lo tome', 'listo, ya tome esas pastillas'. "
        "DEBES pasar exactamente current_group_id devuelto por la consulta "
        "care.get_pillbox_plan inmediatamente anterior. "
        "NO inventes, reconstruyas ni adivines group_id. "
        "No usar para manana ni para preguntas informativas.",
        PropertyList({
            Property(
                "group_id",
                kPropertyTypeString,
                std::string("")),
        }),
        [&service](
            const PropertyList& properties)
            -> ReturnValue {

            ESP_LOGI(
                "CARE_MCP",
                "Tool call: care.confirm_pillbox_group");

            return service.ConfirmPillboxGroup(
                properties["group_id"]
                    .value<std::string>());
        });

    server.AddTool(
        "care.record_routine_execution",
        "Registra por voz que la persona ya hizo, omitio o pospuso algo que estaba anotado. USAR SOLO despues de una confirmación explícita y clara del usuario, por ejemplo 'si, ya lo hice', 'confirmo que lo hice', 'si, anotalo', 'lo omiti' o 'lo hago despues'. Si la frase del usuario es confusa, cortada, mal transcripta o ambigua, NO llames esta herramienta: primero pregunta 'Queres que deje anotado que ya hiciste eso?'. Requiere que previamente una consulta de rutina haya devuelto un routine_id. No afirmar ingesta medica: dejar anotado que ya lo hizo no significa asegurar que tomo un medicamento. Si no tenes routine_id, primero consulta care.get_current_routines o care.get_next_routine segun corresponda.",
        PropertyList({
            Property("routine_id", kPropertyTypeString),
            Property("event", kPropertyTypeString, std::string("confirmed")),
            Property("message", kPropertyTypeString, std::string("")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.record_routine_execution");
            return service.RecordRoutineExecution(properties["routine_id"].value<std::string>(),
                                                  properties["event"].value<std::string>(),
                                                  properties["message"].value<std::string>());
        });

    server.AddTool(
        "care.get_daily_status",
        "Resumen diario autoritativo de cosas para hacer y eventos. Usar para resumen general del dia, 'como voy con mis cosas' o balance de cosas hechas y pendientes. Para 'que falta hacer hoy' usa care.get_pending_today. Para 'que hice hoy' o 'que confirme hoy' usa care.get_completed_today. Para la voz evita 'rutina' y usa 'cosas para hacer hoy', 'lo que te toca' y 'deje anotado que ya lo hiciste'. No afirmar ingesta medica; responder con safe_message y, si corresponde, listar cosas por estado.",
        PropertyList({
            Property("date", kPropertyTypeString, std::string("")),
            Property("weekday", kPropertyTypeInteger, 0, 0, 7),
            Property("time", kPropertyTypeString, std::string("")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_daily_status");
            return service.GetDailyStatus(properties["date"].value<std::string>(),
                                          properties["weekday"].value<int>(),
                                          properties["time"].value<std::string>());
        });


    server.AddTool(
        "care.get_pending_today",
        "Alias autoritativo para preguntas como 'que falta hacer hoy', 'que tengo pendiente hoy', 'que me queda por hacer' o 'que falta'. DEBES usar esta herramienta para pendientes del dia. Responde con safe_message y enumera solo las cosas pendientes devueltas. Evita la palabra rutina: usa cosas para hacer hoy o lo que te toca. No afirmar ingesta medica.",
        PropertyList({
            Property("date", kPropertyTypeString, std::string("")),
            Property("weekday", kPropertyTypeInteger, 0, 0, 7),
            Property("time", kPropertyTypeString, std::string("")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_pending_today");
            return service.GetPendingToday(properties["date"].value<std::string>(),
                                           properties["weekday"].value<int>(),
                                           properties["time"].value<std::string>());
        });

    server.AddTool(
        "care.find_family_member",
        "Busca una persona o usuaria principal dentro del mapa familiar de XiaoZhi Care. DEBES usar esta herramienta para preguntas como 'quien es Ale', 'quien es Martina', 'como se llama mi nieta', 'quien es mi hija', 'quien es la esposa de Ale' o cualquier duda de nombres y parentescos familiares. Resultado autoritativo: responde solo con family_member y relationships. Si found=false, no inventes parentescos; deci que no esta anotado o pedi aclaracion si hay candidatos.",
        PropertyList({
            Property("query", kPropertyTypeString),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.find_family_member");
            return service.FindFamilyMember(properties["query"].value<std::string>());
        });

    server.AddTool(
        "care.get_person_connections",
        "Alias para consultar vinculos de una persona concreta. Usar para preguntas como 'con quien vive Ale', 'quien es la mama de Martina', 'quien es la pareja de Ale' o 'que vinculos tiene Marcelo'. person debe ser nombre, apodo, alias, relacion o profile. Responde solo con las conexiones devueltas por el firmware. Si no hay datos, no inventes.",
        PropertyList({
            Property("person", kPropertyTypeString),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_person_connections");
            return service.GetPersonConnections(properties["person"].value<std::string>());
        });

    server.AddTool(
        "care.who_is_family_member",
        "Alias autoritativo para preguntas directas como 'quien es Julieta', 'quien es Ale', 'quien es Martina', 'quien es Miriam', 'quien es Marcelo' o 'quien es esa persona'. DEBES usar esta herramienta antes de responder sobre identidad familiar. Si found=false, responde safe_message y no uses memoria externa.",
        PropertyList({
            Property("query", kPropertyTypeString),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.who_is_family_member");
            return service.WhoIsFamilyMember(properties["query"].value<std::string>());
        });

    server.AddTool(
        "care.get_children",
        "Alias autoritativo para preguntas como 'quienes son mis hijos', 'como se llaman mis hijos', 'tengo hijos anotados' o 'hijos de Yolita'. DEBES usar esta herramienta para hijos/as. person puede quedar vacio o ser profile para la usuaria principal; tambien puede ser un nombre/apodo. Responde solo con relatives y safe_message. Si relative_count=0, no inventes.",
        PropertyList({
            Property("person", kPropertyTypeString, std::string("profile")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_children");
            return service.GetChildrenOf(properties["person"].value<std::string>());
        });

    server.AddTool(
        "care.get_grandchildren",
        "Alias autoritativo para preguntas como 'como se llama mi nieta', 'quienes son mis nietos', 'tengo nietos', 'mi nieta' o 'nietos de Yolita'. DEBES usar esta herramienta para nietos/as. person puede quedar vacio o ser profile para la usuaria principal. Responde solo con relatives y safe_message. Si relative_count=0, no inventes nombres.",
        PropertyList({
            Property("person", kPropertyTypeString, std::string("profile")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_grandchildren");
            return service.GetGrandchildrenOf(properties["person"].value<std::string>());
        });

    server.AddTool(
        "care.get_partner_of",
        "Alias autoritativo para preguntas como 'quien es la pareja de Marcelo', 'quien es la esposa de Ale', 'con quien esta casada Ale', 'pareja de Julieta' o 'esposo de'. DEBES usar esta herramienta para pareja/esposa/esposo. Responde solo con relatives y safe_message. Si relative_count=0, no inventes.",
        PropertyList({
            Property("person", kPropertyTypeString),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_partner_of");
            return service.GetPartnerOf(properties["person"].value<std::string>());
        });



    // DP041_LATEST_NEWS
    // DP041B_NEWS_SYSTEM_CONFIG
    server.AddTool(
        "care.get_latest_news",
        "NOTICIAS Y ACTUALIDAD. Usa esta herramienta cuando el usuario pida "
        "noticias o quiera saber que esta pasando, por ejemplo: 'leeme las "
        "noticias', 'quiero saber que pasa', 'que paso hoy', 'que novedades "
        "hay', 'poneme al dia', 'que esta pasando en Argentina', 'que esta "
        "pasando en el mundo', 'noticias de tecnologia' o 'noticias de "
        "deportes'. IMPORTANTE: si pide noticias sin especificar categoria, "
        "NO preguntes que categoria quiere. Llama inmediatamente a esta "
        "herramienta con category='default'; la categoria predeterminada se "
        "configura desde Sistema en XiaoZhi Care. Si menciona Argentina, mundo, "
        "tecnologia o deportes, usa esa categoria. NO la uses para agenda "
        "personal: 'que tengo para hacer hoy', 'que me falta hacer' o 'que "
        "tengo pendiente' siguen usando rutinas/pendientes. Lee la cantidad "
        "de titulares configurada en Sistema, con titulo y fuente.",
        PropertyList({
            Property("category", kPropertyTypeString, std::string("default")),
        }),
        [&service](const PropertyList& properties) -> ReturnValue {
            ESP_LOGI("CARE_MCP", "Tool call: care.get_latest_news");
            return service.GetLatestNews(
                properties["category"].value<std::string>()
            );
        });


    // DP042A_RADIO_MP3_BASE
    // DP042A_TOOL_LIMIT_FIX
    // Una sola herramienta MCP controla todas las acciones de radio.
    // Esto evita superar el límite global de 32 herramientas del servidor.
    // DP042A_RADIO_MP3_BASE
    // DP042A_TOOL_LIMIT_FIX
    // DP043B_RADIO_AAC_VOICE
    server.AddTool(
        "care.radio_control",
        "RADIO POR INTERNET. Usa esta UNICA herramienta para TODA orden o "
        "consulta sobre radio. action='play' para poner/prender/escuchar radio. "
        "Si el usuario nombra una emisora, pasa station con ese nombre: "
        "'pone Radio Aspen' -> action='play', station='Radio Aspen'; "
        "'pone La Red' -> action='play', station='La Red'. "
        "Para 'pone la segunda radio', action='play', station='2'. "
        "Si no nombra emisora, station='' y se usa la predeterminada. "
        "action='next' para 'cambia de emisora', 'pone otra radio' o "
        "'siguiente emisora'. action='list' para 'que radios tengo', "
        "'que emisoras hay' o 'decime las radios guardadas'. "
        "action='stop' para apagar/parar/sacar la radio. "
        "action='pause' para pausar. action='resume' para continuar. "
        "action='status' para saber que radio esta sonando. "
        "IMPORTANTE: una orden de prender, apagar, cambiar o escuchar radio "
        "NUNCA es un recordatorio y NO debe usar care.add_todo_reminder. "
        "Despues de action=play o action=next responde 'Listo, puse <station>.' "
        "usando EXACTAMENTE el campo station devuelto. "
        "Para action=status usa exactamente station. "
        "No traduzcas, corrijas, completes ni inventes nombres de emisoras.",
        PropertyList({
            Property("action", kPropertyTypeString, std::string("play")),
            Property("station", kPropertyTypeString, std::string("")),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& radio =
                xiaozhi_care::radio::RadioService::GetInstance();

            const std::string action =
                properties["action"].value<std::string>();
            const std::string station =
                properties["station"].value<std::string>();

            ESP_LOGI(
                "CARE_MCP",
                "Tool call: care.radio_control action=%s station=%s",
                action.c_str(),
                station.c_str());

            if (action == "play") {
                const bool ok = station.empty()
                    ? radio.Play()
                    : radio.PlayStation(station);

                if (!ok) {
                    return std::string(
                        "{\"ok\":false,"
                        "\"message\":\"No encontre esa emisora. Usa action=list para consultar las radios guardadas.\"}");
                }
                return radio.StatusJson();
            }

            if (action == "next") {
                if (!radio.NextStation()) {
                    return std::string(
                        "{\"ok\":false,\"message\":\"No pude cambiar de emisora.\"}");
                }
                return radio.StatusJson();
            }

            if (action == "list") {
                return radio.StationsJson();
            }

            if (action == "stop") {
                radio.Stop();
                return std::string(
                    "{\"ok\":true,\"state\":\"stopped\",\"message\":\"Radio apagada.\"}");
            }

            if (action == "pause") {
                const bool ok = radio.Pause();
                return ok
                    ? std::string("{\"ok\":true,\"state\":\"paused\",\"message\":\"Radio pausada.\"}")
                    : std::string("{\"ok\":false,\"message\":\"La radio no estaba reproduciendo.\"}");
            }

            if (action == "resume") {
                const bool ok = radio.Resume();
                return ok
                    ? radio.StatusJson()
                    : std::string("{\"ok\":false,\"message\":\"No pude reanudar la radio.\"}");
            }

            if (action == "status") {
                return radio.StatusJson();
            }

            return std::string(
                "{\"ok\":false,\"message\":\"Accion invalida. Usa play, next, list, stop, pause, resume o status.\"}");
        });


    registered = true;
    ESP_LOGI("CARE_MCP", "Registered 24 XiaoZhi Care MCP tools");
}

}  // namespace xiaozhi_care
