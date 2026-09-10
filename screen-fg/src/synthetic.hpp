#pragma once
#include "frame_source.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace screenfg {

// 測試用合成幀來源：產生 raw 每帧不同的 BGRA 畫面（16×16 移動方塊 + 漸變）。
// 注意：方塊太小、動不了 32×32 MAD 超過預設閾值 3.0 → dedup 只放行第一幀
//（第一幀永遠 true）；「1 帧呈現」是預期值，管線 + layer 由第一幀觸發。
// 不需 portal / picker，用於驗證呈現端與 layer 激活。
// 跑完 totalFrames 帧後 isDead() 回 true（main 視為來源關閉、乾淨退出）。
class SyntheticSource : public FrameSource {
public:
    SyntheticSource(uint32_t width = 1280, uint32_t height = 720, uint32_t totalFrames = 300);
    void start() override;
    void poll() override;
    std::optional<CapturedFrame> nextFrame() override;
    bool isDead() override;
    void stop() override;
private:
    void makeFrame();
    uint32_t w_ = 0, h_ = 0;
    uint32_t totalFrames_ = 0;
    uint64_t frameNo_ = 0;
    std::shared_ptr<std::vector<uint8_t>> buf_;
};

} // namespace screenfg
