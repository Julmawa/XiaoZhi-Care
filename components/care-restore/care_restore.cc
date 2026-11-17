#include "care_restore.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <utility>
#include <unistd.h>
#include "nvs.h"

namespace xiaozhi_care::restore {
namespace {
constexpr char kStore[] = "care_restore";
Selection active;
thread_local uint16_t candidate = 0;
// Only the serialized HTTP restore handler writes this scratch inventory.
std::map<std::string,std::pair<size_t,uint64_t>> inventory;
constexpr uint64_t kOffset = 14695981039346656037ULL;
uint64_t HashByte(uint64_t hash, unsigned char b) { return (hash ^ b) * 1099511628211ULL; }
bool VerifyFiles() {
    const auto manifest = Path("/care_data/restore.man");
    const std::string prefix = manifest.substr(0,manifest.find("restore.man"));
    FILE* f = std::fopen(manifest.c_str(),"rb");
    if (!f) return false;
    unsigned count=0;
    bool ok=true;
    char line[110];
    unsigned expected_count=0;
    if (!std::fgets(line,sizeof(line),f) || std::sscanf(line,"DP049 %u",&expected_count)!=1 ||
        expected_count<8 || expected_count>410) { std::fclose(f); return false; }
    while (ok && std::fgets(line,sizeof(line),f)) {
        char path[62], suffix;
        unsigned long size;
        unsigned long long expected;
        if (++count > 410 || std::sscanf(line,"%61s %lu %llx %c",path,&size,&expected,&suffix)!=3 ||
            std::strncmp(path,prefix.c_str(),prefix.size())!=0 ||
            std::strchr(path+prefix.size(),'/') || size>65536) { ok=false; break; }
        FILE* file=std::fopen(path,"rb");
        if (!file) { ok=false; break; }
        uint64_t hash=kOffset;
        unsigned long length=0;
        for (int ch; length<=size && (ch=std::fgetc(file))!=EOF; ++length) hash=HashByte(hash,ch);
        ok=length==size && hash==expected && !std::ferror(file);
        if (std::fclose(file)!=0) ok=false;
    }
    ok=ok && count==expected_count && !std::ferror(f);
    return std::fclose(f)==0 && ok;
}

uint64_t Pack(Selection a, Selection b) {
    return uint64_t(a.data) | (uint64_t(a.radio) << 16) |
           (uint64_t(b.data) << 32) | (uint64_t(b.radio) << 48);
}
bool Save(Selection a, Selection b) {
    nvs_handle_t h;
    if (nvs_open(kStore, NVS_READWRITE, &h) != ESP_OK) return false;
    const bool ok = nvs_set_u64(h, "selector", Pack(a,b)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
}

bool ReadTarget(Selection& selected, Selection& previous) {
    nvs_handle_t h;
    const auto opened = nvs_open(kStore, NVS_READONLY, &h);
    uint64_t x = 0;
    if (opened != ESP_ERR_NVS_NOT_FOUND) {
        if (opened != ESP_OK) return false;
        const auto read = nvs_get_u64(h, "selector", &x);
        nvs_close(h);
        if (read != ESP_OK && read != ESP_ERR_NVS_NOT_FOUND) return false;
    }
    selected = {uint16_t(x), uint16_t(x >> 16)};
    previous = {uint16_t(x >> 32), uint16_t(x >> 48)};
    return true;
}
bool Boot() {
    Selection ignored;
    return ReadTarget(active, ignored);
}
bool VerifyBoot() {
    if (active.data == 0) return true;
    nvs_handle_t h;
    if (nvs_open(kStore,NVS_READWRITE,&h)!=ESP_OK) return false;
    uint32_t last=0;
    const auto err=nvs_get_u32(h,"verified",&last);
    if (err!=ESP_OK && err!=ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); return false; }
    if (active.data<=last) { nvs_close(h); return true; }
    if (!VerifyFiles()) {
        nvs_close(h);
        Selection selected, previous;
        if (!ReadTarget(selected,previous) || previous.data>last || !Save(previous,previous)) return false;
        active=previous;
        return true;
    }
    const bool ok=nvs_set_u32(h,"verified",active.data)==ESP_OK && nvs_commit(h)==ESP_OK;
    nvs_close(h);
    return ok;
}
Selection Active() { return active; }
bool Reserve(uint16_t& id) {
    id = 0;
    nvs_handle_t h;
    if (nvs_open(kStore, NVS_READWRITE, &h) != ESP_OK) return false;
    uint32_t last = 0;
    const auto err = nvs_get_u32(h, "allocated", &last);
    Selection target, previous;
    const bool known = ReadTarget(target, previous) && last >= target.data &&
                       last >= target.radio && last >= previous.data && last >= previous.radio;
    const bool ok = known && (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) && last < UINT16_MAX &&
             nvs_set_u32(h, "allocated", last + 1) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    if (ok) id = uint16_t(last + 1);
    return ok;
}
bool Publish(Selection target) {
    Selection selected, previous;
    return ReadTarget(selected, previous) && selected == active && target.data != 0 &&
           target.data != active.data && Save(target, active);
}
bool Revert() {
    Selection selected, previous;
    return ReadTarget(selected, previous) && !(selected == previous) && Save(previous, selected);
}
bool Staging() { return candidate != 0; }
unsigned Generation() { return candidate != 0 ? candidate : active.data; }
Scope::Scope(uint16_t gen) : old_(candidate) { inventory.clear(); candidate = gen; }
Scope::~Scope() { inventory.clear(); candidate = old_; }

std::string Path(const char* original) {
    const auto gen = Generation();
    if (gen == 0) return original;
    const char* name = std::strrchr(original, '/');
    name = name ? name + 1 : original;
    char tag[12];
    std::snprintf(tag, sizeof(tag), "g%04x_", gen);
    const std::string prefix = std::string("/care_data/") + tag;
    // SPIFFS object names are limited to 31 bytes in the supplied sdkconfig.
    if (std::strncmp(name, "daily_routine_", 14) == 0)
        return prefix + "d_" + (name + 14);
    return prefix + name;
}
std::string RadioNamespace(uint16_t gen) {
    if (gen == 0) return "care_radio";
    char name[12];
    std::snprintf(name, sizeof(name), "cr%04x", static_cast<unsigned>(gen));
    return name;
}
bool WriteCandidate(const char* path, const std::string& bytes) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = ok && std::fflush(f) == 0 && ::fsync(::fileno(f)) == 0;
    if (std::fclose(f) != 0) ok = false;
    if (!ok) return false;
    f = std::fopen(path, "rb");
    if (!f) return false;
    uint64_t hash=kOffset;
    for (unsigned char byte : bytes) {
        hash=HashByte(hash,byte);
        if (std::fgetc(f) != byte) { ok = false; break; }
    }
    ok = ok && std::fgetc(f) == EOF && !std::ferror(f);
    ok = std::fclose(f) == 0 && ok;
    if (ok && Staging()) inventory[path]={bytes.size(),hash};
    return ok;
}
bool Seal() {
    if (!Staging() || inventory.size()<8 || inventory.size()>410) return false;
    std::string manifest = "DP049 "+std::to_string(inventory.size())+"\n";
    for (const auto& row : inventory) {
        char line[112];
        const int n=std::snprintf(line,sizeof(line),"%s %lu %016llx\n",row.first.c_str(),
              static_cast<unsigned long>(row.second.first),static_cast<unsigned long long>(row.second.second));
        if (n<0 || n>=int(sizeof(line))) return false;
        manifest.append(line,n);
    }
    return WriteCandidate(Path("/care_data/restore.man").c_str(),manifest);
}
}
