#pragma once
#include <cstdint>
#include <memory>

namespace screenfg {

// 正規化 pixel format：BGRA（== SPA_VIDEO_FORMAT_BGRA == 44）。
// 中性常量（不依賴 PipeWire 標頭），讓 frame 定義與 PW 解耦。
inline constexpr int kPixFmtBgra = 44;

// 一帧捕捉畫面（中性定義，不依賴 portal / PipeWire）。
// data 指到來源自持的 BGRA copy（stride = width*4，無 padding）。
// pixFmt 已正規化為 BGRA（= kPixFmtBgra）。有效到該 block 被覆蓋 / 來源析構
// （FrameBlock 自持記憶體，無跨線程生命週期問題）。
struct CapturedFrame {
    const uint8_t* data = nullptr;
    uint32_t width = 0, height = 0;
    uint32_t stride = 0; // = width*4（BGRA）
    int pixFmt = -1;     // 正規化後 = kPixFmtBgra
    std::shared_ptr<void> keep; // 持有 pixel 儲存體（frame 存活期間 data 有效）
};

} // namespace screenfg
