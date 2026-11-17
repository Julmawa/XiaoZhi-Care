#pragma once

#include <cstdint>
#include <string>

namespace xiaozhi_care::restore {
struct Selection {
    uint16_t data = 0, radio = 0;
    bool operator==(const Selection& other) const { return data == other.data && radio == other.radio; }
};
// Called once, after NVS init and before CARE repositories/radio start.
bool Boot();
bool VerifyBoot(); // after non-destructive care_data mount and before readers
bool Seal();
Selection Active();
bool ReadTarget(Selection& selected, Selection& previous);
bool Reserve(uint16_t& id);
bool Publish(Selection target);
bool Revert();
unsigned Generation();
bool Staging();
std::string Path(const char* original);
std::string RadioNamespace(uint16_t generation);
bool WriteCandidate(const char* path, const std::string& bytes);
class Scope {
public:
    explicit Scope(uint16_t candidate);
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    uint16_t old_;
};
}
