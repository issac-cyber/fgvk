#pragma once
#include "frame.hpp"

#include <optional>

namespace screenfg {

// frame 來源的抽象介面：Capture（portal + PipeWire）與 SyntheticSource（測試用合成幀）
// 共同實作，讓 main 主迴圈不需 caring 來源是哪一種。
class FrameSource {
public:
    virtual ~FrameSource() = default;
    virtual void start() = 0;
    virtual void poll() = 0;
    virtual std::optional<CapturedFrame> nextFrame() = 0;
    virtual bool isDead() = 0;
    virtual void stop() = 0;
};

} // namespace screenfg
