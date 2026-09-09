#include "dedup.hpp"
#include "test_framework.hpp"

#include <vector>

using namespace screenfg;

static void test_dedup() {
    Deduplicator d;

    std::vector<uint8_t> img(64 * 64 * 4, 200);
    // 第一幀永遠 true
    CHECK(d.isDifferent(img.data(), 64, 64));
    // 完全相同 → false
    CHECK(!d.isDifferent(img.data(), 64, 64));
    CHECK_EQ(d.droppedCount(), 1);

    // 微小變化（1 像素 +3/255，grid 平均 ≈ 0 < 3）→ false
    std::vector<uint8_t> img2 = img;
    img2[0] = 203;
    CHECK(!d.isDifferent(img2.data(), 64, 64));
    CHECK_EQ(d.droppedCount(), 2);

    // 大變化（R 通道 200→50，Y 差 ≈ 59）→ true
    std::vector<uint8_t> img3 = img;
    for (size_t i = 0; i < img3.size(); i += 4)
        img3[i] = 50;
    CHECK(d.isDifferent(img3.data(), 64, 64));

    // 再回大變化 → true（與上一幀比）
    CHECK(d.isDifferent(img.data(), 64, 64));

    // 阈值邊界：MAD 剛好等於 threshold → true
    Deduplicator d1(1.0f);
    std::vector<uint8_t> a(8 * 8 * 4, 100);
    std::vector<uint8_t> b = a;
    // 半數像素 R 加 1 → 每 cell Y 差 = 77/256 ≈ 0.3 → MAD 0.3 < 1
    for (size_t i = 0; i < b.size(); i += 8)
        b[i] = 101;
    d1.isDifferent(a.data(), 8, 8);
    CHECK(!d1.isDifferent(b.data(), 8, 8));

    // 不同尺寸不 crash
    std::vector<uint8_t> small(4 * 4 * 4, 10);
    CHECK(d.isDifferent(small.data(), 4, 4));
}

static Registrar reg("dedup", test_dedup);
