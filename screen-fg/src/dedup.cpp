#include "dedup.hpp"

#include <vector>

namespace screenfg {

void Deduplicator::computeGrid(const uint8_t* bgra, uint32_t w, uint32_t h, std::vector<uint8_t>& out) {
    out.assign(GRID * GRID, 0);
    // row/col -> grid cell 映射（避免內層除法）
    std::vector<uint32_t> rowCell(h), colCell(w);
    for (uint32_t y = 0; y < h; ++y)
        rowCell[y] = y * GRID / h;
    for (uint32_t x = 0; x < w; ++x)
        colCell[x] = x * GRID / w;

    std::vector<uint64_t> sums(GRID * GRID, 0);
    std::vector<uint32_t> counts(GRID * GRID, 0);
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t cy = rowCell[y];
        const uint8_t* row = bgra + (size_t)y * w * 4;
        for (uint32_t x = 0; x < w; ++x) {
            const uint8_t* px = row + (size_t)x * 4;
            // Y = 0.299R + 0.587G + 0.114B  (77+150+29 = 256)
            uint32_t Y = (77u * px[2] + 150u * px[1] + 29u * px[0]) >> 8;
            size_t cell = (size_t)cy * GRID + colCell[x];
            sums[cell] += Y;
            counts[cell]++;
        }
    }
    for (uint32_t i = 0; i < GRID * GRID; ++i)
        out[i] = (counts[i] == 0) ? 0 : uint8_t((sums[i] + counts[i] / 2) / counts[i]);
}

bool Deduplicator::isDifferent(const uint8_t* bgra, uint32_t w, uint32_t h) {
    std::vector<uint8_t> cur;
    computeGrid(bgra, w, h, cur);
    if (!hasPrev_) {
        hasPrev_ = true;
        prevGrid_.swap(cur);
        return true;
    }
    uint64_t mad = 0;
    for (uint32_t i = 0; i < GRID * GRID; ++i) {
        uint32_t a = cur[i], b = prevGrid_[i];
        mad += (a >= b) ? (a - b) : (b - a);
    }
    float madAvg = float(mad) / float(GRID * GRID);
    bool diff = madAvg >= threshold_;
    if (!diff)
        dropped_++;
    prevGrid_.swap(cur);
    return diff;
}

} // namespace screenfg
