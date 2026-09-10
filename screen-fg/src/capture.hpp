#pragma once
#include "frame_source.hpp"
#include <gio/gio.h>
#include <pipewire/pipewire.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace screenfg {

// portal ScreenCast v5 + PipeWire 1.6.2 捕捉
// （CapturedFrame 定義已移至中性 frame.hpp；本模組仍自持 FrameBlock 記憶體）
class Capture : public FrameSource {
public:
    Capture();
    ~Capture() override;
    // CreateSession → SelectSources（picker）→ Start → OpenPipeWireRemote
    // 阻塞到使用者選完視窗；失敗 throw std::runtime_error
    void start() override;
    // 在 start() 前設定：true = 全螢幕 monitor 捕捉（types=1（MONITOR）、無 picker、無視窗 parent）
    void setMonitorMode(bool m);
    // 在 start() 前設定：抓第幾條 stream（monitor 模式下 portal 回多條 streams、0=第一條）
    void setStreamIndex(int i);
    // 非阻塞 poll（保持 DBus 連線健康；PipeWire 主迴圈跑在獨立交替線程）
    void poll() override;
    // 取下一帧（沒有回 nullopt）
    std::optional<CapturedFrame> nextFrame() override;
    // 來源窗口關閉 / 串流斷掉（Q9：工具應自動退出）
    bool isDead() override;
    void stop() override;
private:
    struct Impl;
    static void onSignal(GDBusConnection*, const char* sender, const char* objectPath,
                        const char* iface, const char* signal, GVariant* params, void* user);
    static void onProcess(void*);
    static void onParam(void*, uint32_t, const struct spa_pod*);
    static void onState(void*, enum pw_stream_state, enum pw_stream_state, const char*);
    static void onCtxGlobalAdded(void*, struct pw_global*);
    static void onCtxDriverAdded(void*, struct pw_impl_node*);
    static void waitFor(Impl&, const std::string&, int);
    static std::string portalAsync(Impl&, const char*, const std::string*, const std::string*,
                                  const std::string&, int, bool = false);
    std::unique_ptr<Impl> impl_;
};

} // namespace screenfg
