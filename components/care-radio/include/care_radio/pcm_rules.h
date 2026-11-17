#pragma once
#include <cstdint>
namespace xiaozhi_care::radio {
constexpr bool StartBufferedPcm(uint32_t ms, uint32_t minimum, bool has_pcm,
                                bool eof_for_generation) {
    return has_pcm && (ms>=minimum || eof_for_generation);
}
constexpr bool FinishedPcm(bool http_ok, bool queue_drained,
                           bool pcm_written, bool interrupted) {
    return http_ok && queue_drained && pcm_written && !interrupted;
}
}
