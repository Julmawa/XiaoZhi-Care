
from pathlib import Path
from datetime import datetime
import shutil

ROOT = Path(r"C:\xiaozhi-esp32_prueba3")
RH = ROOT / "components/care-radio/include/care_radio/radio_service.h"
RC = ROOT / "components/care-radio/radio_service.cc"
MCP = ROOT / "components/care-mcp/include/care_mcp_xiaozhi.h"
PAGE = ROOT / "components/care-web/care_web_page.cc"
DATA = ROOT / "components/care-web/care_web_data.cc"
APP = ROOT / "main/application.cc"
FILES = [RH, RC, MCP, PAGE, DATA, APP]

for p in FILES:
    if not p.exists():
        raise SystemExit(f"ERROR: no existe {p}")

h = RH.read_text(encoding="utf-8")
cc = RC.read_text(encoding="utf-8")
mcp = MCP.read_text(encoding="utf-8")
page = PAGE.read_text(encoding="utf-8")
data = DATA.read_text(encoding="utf-8")
app = APP.read_text(encoding="utf-8")

MARK = "DP043C_RADIO_FINISHING"
if any(MARK in x for x in [h, cc, mcp, page, data, app]):
    print("DP-043C ya está aplicado. No se hicieron cambios.")
    raise SystemExit(0)

req = {
    "DP043B header": "DP043B_RADIO_AAC_VOICE" in h,
    "DP043B cc": "DP043B_RADIO_AAC_VOICE" in cc,
    "radio control": '"care.radio_control"' in mcp,
    "web radios": "DP043A_RADIO_STATIONS_WEB" in page,
    "backend radios": "HandleMaintenanceRadioStationsPut" in data,
    "wifi DP042C": "DP042C_RADIO_WIFI_PERFORMANCE" in app,
}
bad = [k for k, v in req.items() if not v]
if bad:
    raise SystemExit("ERROR: faltan prerrequisitos:\n - " + "\n - ".join(bad))

def repl(text, old, new, label):
    if old not in text:
        raise SystemExit(f"ERROR: no encontré {label}")
    return text.replace(old, new, 1)

def replace_func(text, sig, new, label):
    s = text.find(sig)
    if s < 0:
        raise SystemExit(f"ERROR: no encontré {label}")
    b = text.find("{", s)
    depth = 0
    ins = False
    q = ""
    esc = False
    for i in range(b, len(text)):
        ch = text[i]
        if ins:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == q:
                ins = False
        else:
            if ch in "\"'":
                ins = True
                q = ch
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[:s] + new.rstrip() + "\n" + text[i + 1:]
    raise SystemExit(f"ERROR: no pude cerrar {label}")

# 1) Callback de actividad de red
h = repl(
    h,
    '''    bool Init(OutputCallback output_cb);

    bool Play();
''',
    '''    bool Init(OutputCallback output_cb);

    // DP043C_RADIO_FINISHING
    using NetworkActivityCallback = std::function<void(bool active)>;
    void SetNetworkActivityCallback(NetworkActivityCallback callback);

    bool Play();
''',
    "API Init/Play",
)

h = repl(
    h,
    '''    OutputCallback output_cb_;

    std::atomic<bool> initialized_{false};
''',
    '''    OutputCallback output_cb_;
    NetworkActivityCallback network_activity_cb_;

    std::atomic<bool> initialized_{false};
''',
    "output_cb_",
)

pos = cc.find("bool RadioService::Play()")
if pos < 0:
    raise SystemExit("ERROR: no encontré Play")
cc = (
    cc[:pos]
    + r'''// DP043C_RADIO_FINISHING
void RadioService::SetNetworkActivityCallback(
    NetworkActivityCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    network_activity_cb_ = std::move(callback);
}

'''
    + cc[pos:]
)

play = r'''bool RadioService::Play() {
    if (!initialized_.load()) return false;

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stations_.empty() ||
            default_station_index_ < 0 ||
            default_station_index_ >= static_cast<int>(stations_.size())) {
            return false;
        }

        const auto& s =
            stations_[static_cast<size_t>(default_station_index_)];
        station_name_ = s.name;
        station_url_ = s.url;
        cb = network_activity_cb_;
    }

    ClearPcmBuffer();
    stream_generation_.fetch_add(1);
    user_paused_.store(false);
    wants_playing_.store(true);

    if (cb) cb(true);

    ESP_LOGI(kTag, "Play requested: %s", GetStationName().c_str());
    return true;
}'''
cc = replace_func(cc, "bool RadioService::Play()", play, "Play")

cc = repl(
    cc,
    '''        station_name_ = stations_[static_cast<size_t>(selected)].name;
        station_url_ = stations_[static_cast<size_t>(selected)].url;
    }

    ClearPcmBuffer();
''',
    '''        station_name_ = stations_[static_cast<size_t>(selected)].name;
        station_url_ = stations_[static_cast<size_t>(selected)].url;
    }

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }

    ClearPcmBuffer();
''',
    "final PlayStation",
)

cc = repl(
    cc,
    '''    wants_playing_.store(true);

    ESP_LOGI(
        kTag,
        "Play station selector='%s' -> %s",
''',
    '''    wants_playing_.store(true);
    if (cb) cb(true);

    ESP_LOGI(
        kTag,
        "Play station selector='%s' -> %s",
''',
    "callback PlayStation",
)

cc = repl(
    cc,
    '''        station_name_ = stations_[static_cast<size_t>(next)].name;
        station_url_ = stations_[static_cast<size_t>(next)].url;
    }

    ClearPcmBuffer();
''',
    '''        station_name_ = stations_[static_cast<size_t>(next)].name;
        station_url_ = stations_[static_cast<size_t>(next)].url;
    }

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }

    ClearPcmBuffer();
''',
    "final NextStation",
)

cc = repl(
    cc,
    '''    wants_playing_.store(true);

    ESP_LOGI(kTag, "Next station: %s", GetStationName().c_str());
''',
    '''    wants_playing_.store(true);
    if (cb) cb(true);

    ESP_LOGI(kTag, "Next station: %s", GetStationName().c_str());
''',
    "callback NextStation",
)

stop = r'''void RadioService::Stop() {
    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }

    wants_playing_.store(false);
    user_paused_.store(false);
    stream_generation_.fetch_add(1);
    streaming_.store(false);
    ClearPcmBuffer();

    if (cb) cb(false);

    ESP_LOGI(kTag, "Stop requested");
}'''
cc = replace_func(cc, "void RadioService::Stop()", stop, "Stop")

cc = repl(
    cc,
    '''    wants_playing_.store(true);
    user_paused_.store(false);
    buffer_ready_.store(false);
    ESP_LOGI(kTag, "User resume");
''',
    '''    wants_playing_.store(true);
    user_paused_.store(false);
    buffer_ready_.store(false);

    NetworkActivityCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = network_activity_cb_;
    }
    if (cb) cb(true);

    ESP_LOGI(kTag, "User resume");
''',
    "Resume",
)

# 2) StatusJson con nombre real
status = r'''std::string RadioService::StatusJson() const {
    std::string state = "stopped";

    if (wants_playing_.load()) {
        if (user_paused_.load()) state = "paused";
        else if (system_paused_.load()) state = "system_paused";
        else if (streaming_.load()) state = "playing";
        else state = "connecting";
    }

    return std::string("{\"ok\":true,\"state\":\"") +
           state +
           "\",\"station\":\"" +
           JsonEscape(GetStationName()) +
           "\",\"instruction\":\"Usa exactamente station como nombre de la emisora; no lo inventes, traduzcas ni corrijas.\"}";
}'''
cc = replace_func(
    cc,
    "std::string RadioService::StatusJson() const",
    status,
    "StatusJson",
)

# 3) Application: PERFORMANCE al arrancar desde panel
app = repl(
    app,
    '''    care_radio.SetSystemPaused(
        GetDeviceState() != kDeviceStateIdle);
''',
    '''    care_radio.SetSystemPaused(
        GetDeviceState() != kDeviceStateIdle);

    // DP043C_RADIO_FINISHING
    care_radio.SetNetworkActivityCallback([this](bool active) {
        Schedule([this, active]() {
            auto& b = Board::GetInstance();

            if (active) {
                b.SetPowerSaveLevel(PowerSaveLevel::PERFORMANCE);
                ESP_LOGI(TAG, "Radio network active: WiFi PERFORMANCE");
            } else if (GetDeviceState() == kDeviceStateIdle) {
                b.SetPowerSaveLevel(PowerSaveLevel::LOW_POWER);
                ESP_LOGI(TAG, "Radio stopped in idle: WiFi LOW_POWER");
            }
        });
    });
''',
    "radio init",
)

# 4) Backend: radio_active + test/stop_test
data = repl(
    data,
    '''    cJSON_AddStringToObject(
        out,
        "active_station",
        radio.GetStationName().c_str());

    cJSON* items = cJSON_AddArrayToObject(out, "items");
''',
    '''    cJSON_AddStringToObject(
        out,
        "active_station",
        radio.GetStationName().c_str());
    cJSON_AddBoolToObject(
        out,
        "radio_active",
        radio.WantsPlaying());

    cJSON* items = cJSON_AddArrayToObject(out, "items");
''',
    "active_station",
)

data = repl(
    data,
    '''    } else if (op == "delete" || op == "default") {
        cJSON* index_json =
            cJSON_GetObjectItemCaseSensitive(json, "index");

        if (cJSON_IsNumber(index_json)) {
            const int index = index_json->valueint;

            if (op == "delete") {
                ok = radio.DeleteStation(index);
            } else {
                ok = radio.SetDefaultStation(index);
            }
        }
    }
''',
    '''    } else if (
        op == "delete" ||
        op == "default" ||
        op == "test") {
        cJSON* index_json =
            cJSON_GetObjectItemCaseSensitive(json, "index");

        if (cJSON_IsNumber(index_json)) {
            const int index = index_json->valueint;

            if (op == "delete") {
                ok = radio.DeleteStation(index);
            } else if (op == "default") {
                ok = radio.SetDefaultStation(index);
            } else {
                ok = radio.PlayStation(std::to_string(index + 1));
                if (ok) {
                    ESP_LOGI(
                        "CARE_WEB",
                        "Radio test started: %s",
                        radio.GetStationName().c_str());
                }
            }
        }
    } else if (op == "stop_test") {
        radio.Stop();
        ok = true;
        ESP_LOGI("CARE_WEB", "Radio test stopped");
    }
''',
    "ops radio",
)

# 5) UI: Probar + Detener
page = repl(
    page,
    '''      '<div class="radioStationActions">'+
        (!def?'<button class="secondary" type="button" data-radio-default="'+i+'">Predeterminada</button>':'')+
        '<button class="secondary" type="button" data-radio-edit="'+i+'">Editar</button>'+
''',
    '''      '<div class="radioStationActions">'+
        '<button class="secondary" type="button" data-radio-test="'+i+'">'+
          ((radioStationsData.radio_active&&radioStationsData.active_station===s.name)?'Sonando':'Probar')+
        '</button>'+
        (!def?'<button class="secondary" type="button" data-radio-default="'+i+'">Predeterminada</button>':'')+
        '<button class="secondary" type="button" data-radio-edit="'+i+'">Editar</button>'+
''',
    "botones fila",
)

page = repl(
    page,
    '''  list.querySelectorAll('[data-radio-edit]').forEach(btn=>{
    btn.onclick=()=>editRadioStation(Number(btn.dataset.radioEdit));
  });
''',
    '''  list.querySelectorAll('[data-radio-test]').forEach(btn=>{
    btn.onclick=()=>testRadioStation(Number(btn.dataset.radioTest));
  });
  list.querySelectorAll('[data-radio-edit]').forEach(btn=>{
    btn.onclick=()=>editRadioStation(Number(btn.dataset.radioEdit));
  });
''',
    "listeners test",
)

page = repl(
    page,
    '''function editRadioStation(index){''',
    '''async function testRadioStation(index){
  const s=(radioStationsData.items||[])[index];
  if(!s)return;

  try{
    showMsg('Probando '+s.name+'...');
    const j=await api('/api/maintenance/radio-stations',{
      method:'PUT',
      body:{op:'test',index},
      csrfRequired:true
    });

    radioStationsData=j.data||radioStationsData;
    renderRadioStations();
    showMsg('Reproduciendo '+(radioStationsData.active_station||s.name));
  }catch(e){
    showMsg('No se pudo probar la emisora: '+readableError(e.message),'error');
  }
}

async function stopRadioTest(){
  try{
    const j=await api('/api/maintenance/radio-stations',{
      method:'PUT',
      body:{op:'stop_test'},
      csrfRequired:true
    });

    radioStationsData=j.data||radioStationsData;
    renderRadioStations();
    showMsg('Radio detenida');
  }catch(e){
    showMsg('No se pudo detener la radio: '+readableError(e.message),'error');
  }
}

function editRadioStation(index){''',
    "funciones test",
)

page = repl(
    page,
    '''          <div id="radioStationList" class="radioStationList"></div>
        </div>
''',
    '''          <div id="radioStationList" class="radioStationList"></div>
          <div class="actions" style="margin-top:12px">
            <button id="stopRadioTestBtn" class="secondary" type="button">Detener radio</button>
          </div>
        </div>
''',
    "botón detener",
)

page = repl(
    page,
    '''  if($('cancelRadioStationBtn'))$('cancelRadioStationBtn').onclick=resetRadioStationForm;
  loadRadioStations();
''',
    '''  if($('cancelRadioStationBtn'))$('cancelRadioStationBtn').onclick=resetRadioStationForm;
  if($('stopRadioTestBtn'))$('stopRadioTestBtn').onclick=stopRadioTest;
  loadRadioStations();
''',
    "bind detener",
)

# 6) MCP: nombre real exacto
mcp = repl(
    mcp,
    '''"Despues de play/next/status usa el campo station devuelto; "
        "nunca inventes el nombre de la emisora.",''',
    '''"Despues de action=play o action=next responde 'Listo, puse <station>.' "
        "usando EXACTAMENTE el campo station devuelto. "
        "Para action=status usa exactamente station. "
        "No traduzcas, corrijas, completes ni inventes nombres de emisoras.",''',
    "instrucción MCP",
)

checks = {
    "callback": "SetNetworkActivityCallback" in h and "SetNetworkActivityCallback" in cc,
    "wifi panel": "Radio network active: WiFi PERFORMANCE" in app,
    "test backend": 'op == "test"' in data,
    "stop backend": 'op == "stop_test"' in data,
    "radio_active": '"radio_active"' in data,
    "Probar": "data-radio-test" in page,
    "Detener": "stopRadioTestBtn" in page,
    "nombre real": "usando EXACTAMENTE el campo station" in mcp,
}
failed = [k for k, v in checks.items() if not v]
if failed:
    raise SystemExit("ERROR validación:\n - " + "\n - ".join(failed))

stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
for p in FILES:
    b = p.with_name(p.name + f".before-dp043c-finishing-{stamp}.bak")
    shutil.copy2(p, b)
    print("Backup:", b)

RH.write_text(h, encoding="utf-8")
RC.write_text(cc, encoding="utf-8")
MCP.write_text(mcp, encoding="utf-8")
PAGE.write_text(page, encoding="utf-8")
DATA.write_text(data, encoding="utf-8")
APP.write_text(app, encoding="utf-8")

print()
print("==============================================")
print(" DP-043C - TERMINACION DE RADIO")
print("==============================================")
print(" - botón Probar por emisora")
print(" - botón Detener radio")
print(" - indicador Sonando")
print(" - WiFi PERFORMANCE al iniciar desde panel")
print(" - respuestas de voz con nombre real exacto")
print()
print("Ahora ejecutá: idf.py build")
