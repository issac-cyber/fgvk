#pragma once
#include <cstdint>

namespace screenfg {

// 最大可放的倍數：contentFps * mult <= displayHz
inline uint32_t maxMultiplier(uint32_t displayHz, uint32_t contentFps) {
    if (contentFps == 0 || displayHz == 0)
        return 1;
    return displayHz / contentFps;
}

// spec: max_mult = floor(display_hz / 60)（window 捕捉上限 60fps）
inline uint32_t maxMultiplierForCapture(uint32_t displayHz) {
    return maxMultiplier(displayHz, 60);
}

} // namespace screenfg
