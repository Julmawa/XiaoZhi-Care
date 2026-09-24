#include "care_vitals/care_vitals_store.h"

#include <algorithm>
#include <cstdio>
#include <mutex>
#include <vector>
#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

namespace xiaozhi_care::vitals {
namespace {
constexpr char kTag[] = "CARE_VITALS";
constexpr char kNamespace[] = "care_vitals";
constexpr char kSequenceKey[] = "bp_seq";
constexpr uint32_t kMaxStoredReadings = 365;
std::mutex g_mutex;

std::string ReadString(const cJSON* root, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : "";
}
int ReadInt(const cJSON* root, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? item->valueint : 0;
}
bool Open(nvs_handle_t& h, nvs_open_mode_t mode, std::string& error) {
    const esp_err_t err = nvs_open(kNamespace, mode, &h);
    if (err != ESP_OK) {
        error = "NVS_OPEN_FAILED";
        ESP_LOGE(kTag, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
bool ReadRecord(nvs_handle_t h, const std::string& key, std::string& out) {
    size_t size = 0;
    if (nvs_get_str(h, key.c_str(), nullptr, &size) != ESP_OK || size == 0) return false;
    std::vector<char> buf(size);
    if (nvs_get_str(h, key.c_str(), buf.data(), &size) != ESP_OK) return false;
    out.assign(buf.data());
    return true;
}
}  // namespace

CareVitalsStore& CareVitalsStore::GetInstance() {
    static CareVitalsStore instance;
    return instance;
}

bool CareVitalsStore::IsValidDate(const std::string& v) {
    if (v.size()!=10 || v[4]!='-' || v[7]!='-') return false;
    for (size_t i=0;i<v.size();++i) {
        if (i==4 || i==7) continue;
        if (v[i]<'0' || v[i]>'9') return false;
    }
    int y=std::stoi(v.substr(0,4)), m=std::stoi(v.substr(5,2)), d=std::stoi(v.substr(8,2));
    return y>=2020 && y<=2200 && m>=1 && m<=12 && d>=1 && d<=31;
}
bool CareVitalsStore::IsValidTime(const std::string& v) {
    if (v.size()!=5 || v[2]!=':') return false;
    for (int i : {0,1,3,4}) if (v[i]<'0' || v[i]>'9') return false;
    int h=(v[0]-'0')*10+(v[1]-'0'), m=(v[3]-'0')*10+(v[4]-'0');
    return h>=0 && h<=23 && m>=0 && m<=59;
}
bool CareVitalsStore::IsValidId(const std::string& v) {
    if (v.size()!=8 || v.rfind("bp",0)!=0) return false;
    for (size_t i=2;i<v.size();++i) if (v[i]<'0' || v[i]>'9') return false;
    return true;
}
bool CareVitalsStore::Validate(const BloodPressureReading& v, bool require_id, std::string& e) {
    if (require_id && !IsValidId(v.id)) { e="INVALID_ID"; return false; }
    if (!IsValidDate(v.date)) { e="INVALID_DATE"; return false; }
    if (!IsValidTime(v.time)) { e="INVALID_TIME"; return false; }
    if (v.systolic<1 || v.systolic>400 || v.diastolic<1 || v.diastolic>400) {
        e="INVALID_BLOOD_PRESSURE"; return false;
    }
    if (v.notes.size()>160 || v.source.size()>16) { e="TEXT_TOO_LONG"; return false; }
    return true;
}
std::string CareVitalsStore::MakeId(uint32_t seq) {
    char b[16]; std::snprintf(b,sizeof(b),"bp%06lu",(unsigned long)seq); return b;
}
std::string CareVitalsStore::Serialize(const BloodPressureReading& v) {
    cJSON* r=cJSON_CreateObject();
    cJSON_AddStringToObject(r,"id",v.id.c_str());
    cJSON_AddStringToObject(r,"date",v.date.c_str());
    cJSON_AddStringToObject(r,"time",v.time.c_str());
    cJSON_AddNumberToObject(r,"systolic",v.systolic);
    cJSON_AddNumberToObject(r,"diastolic",v.diastolic);
    cJSON_AddStringToObject(r,"source",v.source.c_str());
    cJSON_AddStringToObject(r,"notes",v.notes.c_str());
    char* raw=cJSON_PrintUnformatted(r); std::string out=raw?raw:"";
    if (raw) {
        cJSON_free(raw);
    }
    cJSON_Delete(r);
    return out;
}
bool CareVitalsStore::Deserialize(const std::string& text, BloodPressureReading& v) {
    cJSON* r=cJSON_Parse(text.c_str()); if(!r)return false;
    v.id=ReadString(r,"id"); v.date=ReadString(r,"date"); v.time=ReadString(r,"time");
    v.systolic=(uint16_t)ReadInt(r,"systolic"); v.diastolic=(uint16_t)ReadInt(r,"diastolic");
    v.source=ReadString(r,"source"); v.notes=ReadString(r,"notes");
    cJSON_Delete(r); std::string e; return Validate(v,true,e);
}
bool CareVitalsStore::ListBloodPressure(std::vector<BloodPressureReading>& out, std::string& error) {
    std::lock_guard<std::mutex> lock(g_mutex); out.clear();
    nvs_handle_t h;
    if(!Open(h,NVS_READONLY,error)) {
        nvs_handle_t hw; std::string tmp;
        if(Open(hw,NVS_READWRITE,tmp)){ nvs_close(hw); error.clear(); return true; }
        return false;
    }
    uint32_t seq=0; esp_err_t err=nvs_get_u32(h,kSequenceKey,&seq);
    if(err==ESP_ERR_NVS_NOT_FOUND){ nvs_close(h); error.clear(); return true; }
    if(err!=ESP_OK){ nvs_close(h); error="NVS_READ_FAILED"; return false; }
    for(uint32_t i=1;i<=seq;++i){
        std::string t; if(!ReadRecord(h,MakeId(i),t))continue;
        BloodPressureReading v; if(Deserialize(t,v))out.push_back(std::move(v));
    }
    nvs_close(h);
    std::sort(out.begin(),out.end(),[](const auto&a,const auto&b){
        if(a.date!=b.date)return a.date>b.date;
        if(a.time!=b.time)return a.time>b.time;
        return a.id>b.id;
    });
    error.clear(); return true;
}
bool CareVitalsStore::GetBloodPressure(const std::string& id, BloodPressureReading& out, std::string& error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if(!IsValidId(id)){error="INVALID_ID";return false;}
    nvs_handle_t h; if(!Open(h,NVS_READONLY,error))return false;
    std::string t; bool found=ReadRecord(h,id,t); nvs_close(h);
    if(!found){error="NOT_FOUND";return false;}
    if(!Deserialize(t,out)){error="CORRUPT_RECORD";return false;}
    error.clear(); return true;
}
bool CareVitalsStore::AddBloodPressure(BloodPressureReading v, BloodPressureReading& saved, std::string& error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    v.id.clear(); if(v.source.empty())v.source="web";
    if(!Validate(v,false,error))return false;
    nvs_handle_t h; if(!Open(h,NVS_READWRITE,error))return false;
    uint32_t seq=0; esp_err_t err=nvs_get_u32(h,kSequenceKey,&seq);
    if(err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND){nvs_close(h);error="NVS_READ_FAILED";return false;}
    uint32_t existing=0;
    for(uint32_t i=1;i<=seq;++i){size_t s=0;if(nvs_get_str(h,MakeId(i).c_str(),nullptr,&s)==ESP_OK)++existing;}
    if(existing>=kMaxStoredReadings){nvs_close(h);error="LIMIT_REACHED";return false;}
    ++seq; v.id=MakeId(seq); std::string j=Serialize(v);
    if(j.empty() || nvs_set_str(h,v.id.c_str(),j.c_str())!=ESP_OK ||
       nvs_set_u32(h,kSequenceKey,seq)!=ESP_OK || nvs_commit(h)!=ESP_OK){
        nvs_close(h);error="NVS_WRITE_FAILED";return false;
    }
    nvs_close(h); saved=v; error.clear();
    ESP_LOGI(kTag,"Blood pressure saved id=%s",v.id.c_str()); return true;
}
bool CareVitalsStore::UpdateBloodPressure(const BloodPressureReading& v, BloodPressureReading& saved, std::string& error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if(!Validate(v,true,error))return false;
    nvs_handle_t h;if(!Open(h,NVS_READWRITE,error))return false;
    size_t s=0;if(nvs_get_str(h,v.id.c_str(),nullptr,&s)!=ESP_OK){nvs_close(h);error="NOT_FOUND";return false;}
    std::string j=Serialize(v);
    if(j.empty()||nvs_set_str(h,v.id.c_str(),j.c_str())!=ESP_OK||nvs_commit(h)!=ESP_OK){
        nvs_close(h);error="NVS_WRITE_FAILED";return false;
    }
    nvs_close(h);saved=v;error.clear();return true;
}
bool CareVitalsStore::DeleteBloodPressure(const std::string& id, std::string& error) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if(!IsValidId(id)){error="INVALID_ID";return false;}
    nvs_handle_t h;if(!Open(h,NVS_READWRITE,error))return false;
    esp_err_t err=nvs_erase_key(h,id.c_str());
    if(err==ESP_ERR_NVS_NOT_FOUND){nvs_close(h);error="NOT_FOUND";return false;}
    if(err!=ESP_OK||nvs_commit(h)!=ESP_OK){nvs_close(h);error="NVS_WRITE_FAILED";return false;}
    nvs_close(h);error.clear();return true;
}
}  // namespace xiaozhi_care::vitals
