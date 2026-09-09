#pragma once
#include "capture.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <string>

namespace screenfg {

struct PresentParams {
    bool hud = true;
    int displayIndex = 0;
    const char* gpuPciId = nullptr; // PCI bus ID（如 "0000:01:00.0"）；null = auto
};

// borderless 全螢幕視窗 + Vulkan FIFO swapchain + CPU 上傳 + 選配 HUD
class Presenter {
public:
    Presenter();
    ~Presenter();
    // 失敗 throw std::runtime_error
    void init(const PresentParams& p);
    // CPU 上傳 BGRA → staging → blit 到 swapchain（scale）→ 選配 HUD → FIFO present
    void presentFrame(const CapturedFrame& f);
    void setHudText(const std::string& text);
    void setHud(bool on); // 執行中 HUD on/off（資源於 init 常備，切換即生效）
    uint32_t displayHz() const;
    void shutdown();
private:
    struct Impl;
    static void ensureFrameStaging(Impl& im); // 依 swapchain 尺寸建立
    std::unique_ptr<Impl> impl_;
};

} // namespace screenfg
