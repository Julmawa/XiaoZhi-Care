#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace xiaozhi_care::voice {

struct VoiceRecordingInfo {
    std::string id;
    std::string label;
    std::string text;
    std::string file_path;
    // DP040B_FASE1_GROUP_TARGETS
    // Nombre legacy conservado por compatibilidad.
    // Puede contener un recordatorio Care o un destino agrupado del pastillero:
    //   r000123
    //   pillbox:segundo|11:00
    std::string reminder_id;
    uint32_t duration_ms{0};
    size_t size_bytes{0};
    bool enabled{true};
};

struct VoiceRecordingLimits {
    size_t max_recordings{12};
    size_t max_file_bytes{32 * 1024};
    uint32_t max_duration_ms{10 * 1000};
};

struct VoiceRecordingValidation {
    bool ok{false};
    std::string error;
    uint32_t duration_ms{0};
    size_t size_bytes{0};
};

class VoiceRecordingStore {
public:
    static VoiceRecordingStore& GetInstance();

    bool Init();
    bool IsReady() const { return ready_; }
    const VoiceRecordingLimits& Limits() const { return limits_; }

    std::vector<VoiceRecordingInfo> List();
    bool Find(const std::string& id, VoiceRecordingInfo& out);
    bool FindByReminder(const std::string& reminder_id, VoiceRecordingInfo& out);
    bool LoadAudio(const std::string& id, std::string& audio_bytes, VoiceRecordingInfo* info = nullptr);
    bool Save(const std::string& label,
              const std::string& text,
              const std::string& reminder_id,
              const uint8_t* data,
              size_t size,
              VoiceRecordingInfo& saved,
              std::string& error);

    // Empty reminder_id means "desasignar".
    // A reminder can have at most one recording assigned.
    bool AssignToReminder(const std::string& id,
                          const std::string& reminder_id,
                          std::string& error);
    bool UnassignReminder(const std::string& reminder_id);
    bool Delete(const std::string& id, std::string* error = nullptr);

    VoiceRecordingValidation ValidateOggOpus(const uint8_t* data, size_t size) const;

private:
    VoiceRecordingStore() = default;
    bool ready_{false};
    VoiceRecordingLimits limits_{};

    std::string NextId(const std::vector<VoiceRecordingInfo>& existing) const;
    bool LoadIndex(std::vector<VoiceRecordingInfo>& values);
    bool SaveIndex(const std::vector<VoiceRecordingInfo>& values);
    static std::string EscapeFileId(const std::string& id);
};

}  // namespace xiaozhi_care::voice
