#include "synthetic.hpp"

namespace screenfg {

SyntheticSource::SyntheticSource(uint32_t width, uint32_t height, uint32_t totalFrames)
    : w_(width), h_(height), totalFrames_(totalFrames) {
    buf_ = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(width) * height * 4, 0);
}

void SyntheticSource::start() {
    // 合成來源無需啟動任何東西
}

void SyntheticSource::poll() {
    // 無需 poll
}

std::optional<CapturedFrame> SyntheticSource::nextFrame() {
    if (totalFrames_ > 0 && frameNo_ >= totalFrames_)
        return std::nullopt;
    makeFrame();
    ++frameNo_;
    CapturedFrame f;
    f.data = buf_->data();
    f.width = w_;
    f.height = h_;
    f.stride = w_ * 4;
    f.pixFmt = kPixFmtBgra;
    f.keep = buf_;
    return f;
}

bool SyntheticSource::isDead() {
    return totalFrames_ > 0 && frameNo_ >= totalFrames_;
}

void SyntheticSource::stop() {
    // 無需清理
}

// 產生一帧「保證與上一帧不同」的 BGRA 畫面：
//  - 背景：水平漸變（B 通道依 x 變化）
//  - 移動方塊：x 位置 = (frameNo * 8) % width，顏色隨 frameNo 變化
// 每帧方塊位置 + 顏色都變 → dedup 的 32×32 MAD 必超過門檻 → 判定「不同」→ 呈現。
void SyntheticSource::makeFrame() {
    const uint32_t w = w_, h = h_;
    const uint8_t* base = buf_->data();
    const uint64_t t = frameNo_;
    const uint32_t bx = static_cast<uint32_t>((t * 8) % (w > 16 ? w - 16 : 1));
    const uint32_t by = static_cast<uint32_t>((t * 5) % (h > 16 ? h - 16 : 1));
    const uint8_t r = static_cast<uint8_t>(t * 3);
    const uint8_t g = static_cast<uint8_t>(t * 5);
    const uint8_t b = static_cast<uint8_t>(t * 7);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            // BGRA 順序
            uint8_t* px = const_cast<uint8_t*>(base) + (static_cast<size_t>(y) * w + x) * 4;
            uint8_t B = static_cast<uint8_t>(x * 255 / (w ? w : 1));
            uint8_t G = static_cast<uint8_t>(y * 255 / (h ? h : 1));
            uint8_t R = 0;
            uint8_t A = 255;
            const bool inRect = (x >= bx && x < bx + 16 && y >= by && y < by + 16);
            if (inRect) {
                R = r; G = g; B = b;
            }
            px[0] = B;
            px[1] = G;
            px[2] = R;
            px[3] = A;
        }
    }
}

} // namespace screenfg
