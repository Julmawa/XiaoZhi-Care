#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace xiaozhi_care::vitals {

struct BloodPressureReading {
    std::string id;
    std::string date;
    std::string time;
    uint16_t systolic{0};
    uint16_t diastolic{0};
    std::string source{"web"};
    std::string notes;
};

class CareVitalsStore {
public:
    static CareVitalsStore& GetInstance();
    bool ListBloodPressure(std::vector<BloodPressureReading>& out, std::string& error);
    bool GetBloodPressure(const std::string& id, BloodPressureReading& out, std::string& error);
    bool AddBloodPressure(BloodPressureReading value, BloodPressureReading& saved, std::string& error);
    bool UpdateBloodPressure(const BloodPressureReading& value, BloodPressureReading& saved, std::string& error);
    bool DeleteBloodPressure(const std::string& id, std::string& error);

private:
    CareVitalsStore() = default;
    static bool Validate(const BloodPressureReading& value, bool require_id, std::string& error);
    static bool IsValidDate(const std::string& value);
    static bool IsValidTime(const std::string& value);
    static bool IsValidId(const std::string& value);
    static std::string MakeId(uint32_t seq);
    static std::string Serialize(const BloodPressureReading& value);
    static bool Deserialize(const std::string& text, BloodPressureReading& value);
};

}  // namespace xiaozhi_care::vitals
