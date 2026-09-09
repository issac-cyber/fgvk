#pragma once
#include <cstdint>
#include <vector>

namespace screenfg {

// 去重器：32×32 luminance grid + mean absolute difference。
// 輸入是 raw BGRA（width*height*4 bytes）。
class Deduplicator {
public:
    explicit Deduplicator(float madThreshold = 3.0f) : threshold_(madThreshold) {}

    // 這幀要不要呈現（與上一幀差 enough）。第一幀永遠 true。
    bool isDifferent(const uint8_t* bgra, uint32_t width, uint32_t height);

    uint64_t droppedCount() const { return dropped_; }

    static constexpr uint32_t GRID = 32;

private:
    float threshold_;
    bool hasPrev_ = false;
    uint64_t dropped_ = 0;
    std::vector<uint8_t> prevGrid_;

    void computeGrid(const uint8_t* bgra, uint32_t w, uint32_t h, std::vector<uint8_t>& out);
};

} // namespace screenfg
