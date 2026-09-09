#include "frame.hpp"
#include "frame_source.hpp"
#include "synthetic.hpp"
#include "test_framework.hpp"

#include <memory>
#include <optional>

using namespace screenfg;

// 最小 fake FrameSource：證明 seam 可被替換、不需 portal / PipeWire。
class FakeSource : public FrameSource {
public:
    void start() override {}
    void poll() override {}
    std::optional<CapturedFrame> nextFrame() override {
        if (n_++ >= 2)
            return std::nullopt;
        CapturedFrame f;
        f.data = px_;
        f.width = 1;
        f.height = 1;
        f.stride = 4;
        f.pixFmt = kPixFmtBgra;
        return f;
    }
    bool isDead() override { return n_ >= 2; }
    void stop() override {}
private:
    uint8_t px_[4] = {1, 2, 3, 255};
    int n_ = 0;
};

static void test_frame_source() {
    // fake 替換 seam（無 PW / portal）
    std::unique_ptr<FrameSource> src = std::make_unique<FakeSource>();
    auto f = src->nextFrame();
    CHECK(f.has_value());
    if (f) {
        CHECK_EQ(f->pixFmt, kPixFmtBgra);
        CHECK_EQ(f->width, 1);
    }

    // SyntheticSource（中性真實實作）回傳 BGRA 幀
    SyntheticSource syn(32, 32, 2);
    auto f2 = syn.nextFrame();
    CHECK(f2.has_value());
    if (f2) {
        CHECK_EQ(f2->pixFmt, kPixFmtBgra);
        CHECK_EQ(f2->width, 32);
        CHECK_EQ(f2->height, 32);
        CHECK_EQ(f2->stride, 32 * 4);
    }
}

static Registrar reg("frame_source", test_frame_source);
