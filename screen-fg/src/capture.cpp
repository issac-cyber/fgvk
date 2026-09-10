#include "capture.hpp"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <poll.h>
#include <spa/buffer/buffer.h>
#include <spa/param/format-utils.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <spa/pod/builder.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace screenfg {

namespace {

 struct Pending {
    std::string wantKey;
    std::string result;
    int streamNodeId = -1; // Start 的 streams[0].uint32（capture source 的 node ID）
    bool done = false;
};

// 一帧（自持 BGRA pixel 記憶體）
struct FrameBlock {
    std::vector<uint8_t> pixels;
    uint32_t w = 0, h = 0, stride = 0;
    int pixFmt = -1;
};

} // namespace

struct Capture::Impl {
    // ---- DBus / portal（GDBus）----
    GDBusConnection* conn = nullptr;
    std::unordered_map<std::string, Pending> pendings;
    std::string sessionHandle;
    std::string parentWindow;
    bool monitorMode = false;
    std::string error;
    std::string selectRestoreToken; // 存檔的 token，SelectSources 帶入（restore 成功免對話框）
    std::string restoreToken;       // Start Response options 回來的 restore_token（single-use、要存檔）
    // ---- PipeWire 1.6.2 ----
    int pwFd = -1;
    int streamNodeId = -1; // Start 回來的 capture source node ID（客戶端要 target 它）
    int streamIndex = 0; // 抓第幾條 stream（monitor 模式下 portal 回多條 streams）
    struct pw_main_loop* ml = nullptr;
    struct pw_context* ctx = nullptr;
    struct pw_core* core = nullptr;
    struct pw_stream* stream = nullptr;
    spa_hook pwHook;
    spa_hook ctxHook;
    int globalCount = 0;
    // negotiating 到的格式（來自 param_changed）
    uint32_t fmtW = 0, fmtH = 0;
    int pixFmt = -1;
    // 幀隊列（producer=PipeWire 線程 / consumer=main）
    std::mutex qMtx;
    std::deque<std::shared_ptr<FrameBlock>> q;
    std::atomic<bool> dead{false};
    std::atomic<bool> stopFlag{false};
    std::thread loopThread;
};

namespace {

std::string makeToken(const char* prefix) {
    static uint32_t n = 0;
    char buf[96];
    // portal 的 request object path 只接受 [a-zA-Z0-9_]（dash 會被拒），用 underscore
    snprintf(buf, sizeof buf, "%s_%d_%u", prefix, (int) getpid(), ++n);
    return buf;
}

// 建一個 {k: v} 的 a{sv} GVariant。注意：傳給 g_variant_new 的 "@a{sv}" 會
// adopt 這個 ref（不再 inc），所以呼叫方建完 params 後「不要」再 unref 它。
GVariant* makeDict(const std::string& key, const std::string& value) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&b, "{sv}", key.c_str(), g_variant_new_string(value.c_str()));
    return g_variant_builder_end(&b);
}

// 空 a{sv}（同樣：被 "@a{sv}" adopt 後不要再 unref）
GVariant* makeEmptyDict() {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    return g_variant_builder_end(&b);
}

// CreateSession 專用：同時帶 handle_token 與 session_handle_token。
// portal 1.21.1 若缺 session_handle_token 會 NoReply/crash（實測）。
GVariant* makeCreateSessionDict() {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    std::string ht = makeToken("req");
    std::string st = makeToken("sess");
    g_variant_builder_add(&b, "{sv}", "handle_token", g_variant_new_string(ht.c_str()));
    g_variant_builder_add(&b, "{sv}", "session_handle_token", g_variant_new_string(st.c_str()));
    return g_variant_builder_end(&b);
}

// SelectSources 專用：handle_token + types（uint32 bitmask，portal 1.21.1 ScreenCast v5）。
// 1=MONITOR / 2=WINDOW / 4=VIRTUAL（0 非有效、GNOME backend 會拒）。
// window 模式明確 request WINDOW（2），還原 spec 的 picker 選視窗行為；
// monitor 模式 request MONITOR（1）、無 picker、tolerate 空 parent。
// 注意：舊 code 傳的 "sources" 選項在 portal 1.21.1 不存在，會被 xdp_filter_options
// 靜默丟棄、backend 落回 default=MONITOR（monitor 模式只是巧合工作）。
// GLib 2.88 的 GVariantBuilder {sv} 對 GVariant 參數是「借用」（存指標、不 inc ref、
// end() 也不 copy）→ 新造的 GVariant 參數**不要 unref**（dict 引用它、活到 dict 釋放）。
GVariant* makeSelectDict(bool monitor, const std::string& restoreToken) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    std::string ht = makeToken("req");
    g_variant_builder_add(&b, "{sv}", "handle_token", g_variant_new_string(ht.c_str()));
    uint32_t types = monitor ? 1u : 2u; // 1 = MONITOR, 2 = WINDOW
    g_variant_builder_add(&b, "{sv}", "types", g_variant_new_uint32(types));
    // persist_mode=2（portal ScreenCast v4+）：session 持久到被明確 revoke 為止。
    // 下次啟動帶上次的 restore_token 還原 → portal-gnome 跳過同意對話框。
    g_variant_builder_add(&b, "{sv}", "persist_mode", g_variant_new_uint32(2));
    if (!restoreToken.empty())
        g_variant_builder_add(&b, "{sv}", "restore_token", g_variant_new_string(restoreToken.c_str()));
    return g_variant_builder_end(&b);
}

// 從 a{sv} dict 抽一個 entry（回 GVariant* 值或 nullptr；呼叫方負責 unref）。
// 參數 d 本身不 take ref（呼叫方擁有）。
GVariant* dictEntry(GVariant* d, const char* k) {
    if (!d || !g_variant_is_of_type(d, G_VARIANT_TYPE("a{sv}")))
        return nullptr;
    gsize n = g_variant_n_children(d);
    for (gsize i = 0; i < n; ++i) {
        GVariant* e = g_variant_get_child_value(d, i); // {sv}
        GVariant* key = g_variant_get_child_value(e, 0);
        const char* ks = g_variant_get_string(key, nullptr);
        g_variant_unref(key);
        if (ks && std::string(ks) == k) {
            GVariant* v = g_variant_get_child_value(e, 1); // v
            GVariant* inner = g_variant_get_child_value(v, 0);
            g_variant_unref(v);
            g_variant_unref(e);
            return inner;
        }
        g_variant_unref(e);
    }
    return nullptr;
}

// 從 a{sv} 抽一個 string entry（缺回 ""）
std::string dictStr(GVariant* d, const char* k) {
    GVariant* v = dictEntry(d, k);
    if (!v)
        return "";
    std::string out;
    if (g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) {
        char* s = nullptr;
        g_variant_get(v, "s", &s);
        if (s) { out = s; g_free(s); }
    }
    g_variant_unref(v);
    return out;
}

// 把一帧 source pixel 轉成正規 BGRA（dst stride = w*4）。
// 支援 BGRx / BGRA / RGBx / RGBA；其它 pixfmt 回 false。
bool convertToBgra(const uint8_t* src, uint32_t srcStride, int pixFmt,
                  uint32_t w, uint32_t h, uint8_t* dst) {
    const uint32_t rowBytes = w * 4;
    const bool fast = (srcStride == rowBytes);
    switch (pixFmt) {
        case SPA_VIDEO_FORMAT_BGRA:
            if (fast) {
                for (uint32_t y = 0; y < h; y++)
                    memcpy(dst + (size_t) y * rowBytes, src + (size_t) y * srcStride, rowBytes);
                return true;
            }
            for (uint32_t y = 0; y < h; y++) {
                const uint8_t* s = src + (size_t) y * srcStride;
                uint8_t* d = dst + (size_t) y * rowBytes;
                for (uint32_t x = 0; x < w; x++) {
                    d[x * 4 + 0] = s[x * 4 + 0]; // B
                    d[x * 4 + 1] = s[x * 4 + 1]; // G
                    d[x * 4 + 2] = s[x * 4 + 2]; // R
                    d[x * 4 + 3] = s[x * 4 + 3]; // A
                }
            }
            return true;
        case SPA_VIDEO_FORMAT_BGRx:
            if (fast) {
                for (uint32_t y = 0; y < h; y++) {
                    uint8_t* d = dst + (size_t) y * rowBytes;
                    memcpy(d, src + (size_t) y * srcStride, rowBytes);
                    for (uint32_t x = 0; x < w; x++)
                        d[x * 4 + 3] = 0xFF; // X → 不透明
                }
                return true;
            }
            for (uint32_t y = 0; y < h; y++) {
                const uint8_t* s = src + (size_t) y * srcStride;
                uint8_t* d = dst + (size_t) y * rowBytes;
                for (uint32_t x = 0; x < w; x++) {
                    d[x * 4 + 0] = s[x * 4 + 0];
                    d[x * 4 + 1] = s[x * 4 + 1];
                    d[x * 4 + 2] = s[x * 4 + 2];
                    d[x * 4 + 3] = 0xFF;
                }
            }
            return true;
        case SPA_VIDEO_FORMAT_RGBA:
        case SPA_VIDEO_FORMAT_RGBx: {
            const bool alpha = (pixFmt == SPA_VIDEO_FORMAT_RGBA);
            for (uint32_t y = 0; y < h; y++) {
                const uint8_t* s = src + (size_t) y * srcStride;
                uint8_t* d = dst + (size_t) y * rowBytes;
                for (uint32_t x = 0; x < w; x++) {
                    d[x * 4 + 0] = s[x * 4 + 2]; // B
                    d[x * 4 + 1] = s[x * 4 + 1]; // G
                    d[x * 4 + 2] = s[x * 4 + 0]; // R
                    d[x * 4 + 3] = alpha ? s[x * 4 + 3] : 0xFF;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

} // namespace

// ---------- PipeWire 1.6.2 事件（static 成員） ----------

void Capture::onProcess(void* data) {
    auto* im = static_cast<Capture::Impl*>(data);
    if (im->stopFlag.load())
        return;
    struct pw_buffer* buf;
    while ((buf = pw_stream_dequeue_buffer(im->stream)) != nullptr) {
        if (im->fmtW && im->fmtH) {
            struct spa_buffer* sb = buf->buffer;
            if (sb && sb->n_datas > 0) {
                struct spa_data* d = &sb->datas[0];
                uint8_t* src = nullptr;
                void* mapped = nullptr;
                switch (d->type) {
                    case SPA_DATA_MemPtr:
                        src = (uint8_t*) d->data;
                        break;
                    case SPA_DATA_MemFd:
                    case SPA_DATA_DmaBuf:
                        if (d->type == SPA_DATA_DmaBuf && d->fd >= 0) {
                            struct dma_buf_sync sync{0};
                            sync.flags = DMA_BUF_SYNC_READ; // START|READ：要求 GPU flush、讀到新幀（本來 flags=0 是 no-op → 凍結）
                            (void) ioctl(d->fd, DMA_BUF_IOCTL_SYNC, &sync);
                        }
                        if (d->fd >= 0 && d->maxsize > 0) {
                            void* m = mmap(nullptr, d->maxsize, PROT_READ, MAP_SHARED, d->fd, d->mapoffset);
                            if (m != MAP_FAILED) {
                                mapped = m;
                                src = (uint8_t*) m;
                            }
                        }
                        break;
                    default:
                        break;
                }
                if (src) {
                    struct spa_chunk* ch = d->chunk;
                    uint32_t srcStride = ch ? ch->stride : (uint32_t) im->fmtW * 4;
                    uint8_t* srcBase = src + (ch ? ch->offset : 0);
                    auto blk = std::make_shared<FrameBlock>();
                    blk->w = im->fmtW;
                    blk->h = im->fmtH;
                    blk->stride = blk->w * 4;
                    blk->pixFmt = SPA_VIDEO_FORMAT_BGRA;
                    blk->pixels.resize((size_t) blk->w * blk->h * 4);
                    if (convertToBgra(srcBase, srcStride, im->pixFmt, blk->w, blk->h, blk->pixels.data())) {
                        std::lock_guard<std::mutex> lk(im->qMtx);
                        if (im->q.size() >= 4)
                            im->q.pop_front(); // 落後就丟最舊（保低延遲）
                        im->q.push_back(std::move(blk));
                    }
                }
                if (d->type == SPA_DATA_DmaBuf && d->fd >= 0) {
                    struct dma_buf_sync sync{0};
                    sync.flags = DMA_BUF_SYNC_READ | DMA_BUF_SYNC_END; // END|READ：收尾同步
                    (void) ioctl(d->fd, DMA_BUF_IOCTL_SYNC, &sync);
                }
                if (mapped)
                    munmap(mapped, d->maxsize);
            }
        }
        // 立即归还 buffer（別卡住 portal 的 buffer 池）
        pw_stream_return_buffer(im->stream, buf);
    }
}

void Capture::onParam(void* data, uint32_t id, const struct spa_pod* param) {
    auto* im = static_cast<Capture::Impl*>(data);
    if (id != SPA_PARAM_Format || !param)
        return;
    struct spa_video_info info;
    memset(&info, 0, sizeof info);
    if (spa_format_video_parse(param, &info) < 0)
        return;
    im->fmtW = info.info.raw.size.width;
    im->fmtH = info.info.raw.size.height;
    im->pixFmt = (int) info.info.raw.format;
    fprintf(stderr, "[screen-fg] 捕捉格式 %ux%u pixfmt=%d\n", im->fmtW, im->fmtH, im->pixFmt);
}

void Capture::onState(void* data, enum pw_stream_state /*old*/, enum pw_stream_state state, const char* error) {
    auto* im = static_cast<Capture::Impl*>(data);
    fprintf(stderr, "[screen-fg] 串流 state=%d err=%s\n", (int) state, error ? error : "-");
    if (state == PW_STREAM_STATE_PAUSED) {
        fprintf(stderr, "[screen-fg] PAUSED: driving=%d lazy=%d\n",
                pw_stream_is_driving(im->stream), pw_stream_is_lazy(im->stream));
        int rc = pw_stream_set_active(im->stream, true);
        fprintf(stderr, "[screen-fg] set_active(true) rc=%d\n", rc);
        int rc2 = pw_stream_trigger_process(im->stream);
        fprintf(stderr, "[screen-fg] trigger_process rc=%d\n", rc2);
    }
    if (state == PW_STREAM_STATE_ERROR) {
        im->dead.store(true);
    }
}

// core 連線建立時，server 的每個 global（含 capture source node）會觸發 global_added。
// 用來確認連線是否真的建立（pw_global_get_* 是 internal、不在 public API，故只數個數）。
void Capture::onCtxGlobalAdded(void* data, struct pw_global* global) {
    auto* im = static_cast<Capture::Impl*>(data);
    im->globalCount++;
    fprintf(stderr, "[screen-fg] global_added #%d\n", (int) im->globalCount);
}
void Capture::onCtxDriverAdded(void* data, struct pw_impl_node* node) {
    auto* im = static_cast<Capture::Impl*>(data);
    (void)node;
    fprintf(stderr, "[screen-fg] driver_added（core 連線建立）\n");
}

// ---------- portal DBus 靜態成員 ----------

// Response signal：args (u result, a{sv} body, a{sv} options)
void Capture::onSignal(GDBusConnection*, const char*, const char* objectPath,
                       const char*, const char*, GVariant* params, void* user) {
    auto* im = static_cast<Capture::Impl*>(user);
    if (!objectPath)
        return;
    auto it = im->pendings.find(objectPath);
    if (it == im->pendings.end())
        return;
    // params = (u result, a{sv} body, a{sv} options)
    guint result = 0;
    g_variant_get_child(params, 0, "u", &result);
    if (result != 0) {
        im->error = "portal response " + std::to_string(result) + " (denied/cancelled?)";
        it->second.done = true;
        return;
    }
    // body = a{sv}；每個 entry = {sv} = (key: s, value: v)；value 是 variant，
    // 真正的值在 val 的 child 0。用 g_variant_get_child_value 抽未知型別。
    GVariant* body = g_variant_get_child_value(params, 1);
    if (body) {
        gsize n = g_variant_n_children(body);
        for (gsize i = 0; i < n; ++i) {
            GVariant* entry = g_variant_get_child_value(body, i); // {sv}
            GVariant* key = g_variant_get_child_value(entry, 0); // s
            const char* keyStr = g_variant_get_string(key, nullptr);
            if (keyStr && std::string(keyStr) == it->second.wantKey) {
                GVariant* val = g_variant_get_child_value(entry, 1); // v
                GVariant* inner = g_variant_get_child_value(val, 0); // 真正的值
                if (std::string(keyStr) == "streams") {
                    // streams = a(ua{sv})；用 capture_monitor 選第幾條（越界落回第 0 條）
                    gsize ns = g_variant_n_children(inner);
                    if (ns > 0) {
                        int idx = im->streamIndex;
                        if (idx < 0 || (gsize)idx >= ns)
                            idx = 0;
                        GVariant* elem = g_variant_get_child_value(inner, (gsize)idx); // (u, a{sv})
                        GVariant* nidVar = g_variant_get_child_value(elem, 0); // u
                        if (g_variant_is_of_type(nidVar, G_VARIANT_TYPE_UINT32))
                            it->second.streamNodeId = (int) g_variant_get_uint32(nidVar);
                        g_variant_unref(nidVar);
                        g_variant_unref(elem);
                    }
                } else {
                    const char* valStr = g_variant_get_string(inner, nullptr);
                    if (valStr)
                        it->second.result = valStr;
                }
                g_variant_unref(inner);
                g_variant_unref(val);
            }
            g_variant_unref(key);
            g_variant_unref(entry);
        }
        g_variant_unref(body);
    }
    // Response signal 第三參 options（a{sv}）：persist_mode=2 獲許時 restore_token (s)
    // 從這裡回來（single-use；下次 SelectSources 要用，故存到 im 供 start() 存檔）
    GVariant* opts = g_variant_get_child_value(params, 2);
    if (opts) {
        std::string rt = dictStr(opts, "restore_token");
        if (!rt.empty()) {
            im->restoreToken = rt;
            fprintf(stderr, "[screen-fg] 收到 restore_token（len=%zu）\n", rt.size());
        }
        g_variant_unref(opts);
    }
    it->second.done = true;
}

void Capture::waitFor(Impl& im, const std::string& reqPath, int timeoutMs) {
    auto t0 = std::chrono::steady_clock::now();
    GMainContext* ctx = g_main_context_default();
    while (true) {
        auto p = im.pendings.find(reqPath);
        if (p != im.pendings.end() && p->second.done)
            break;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > timeoutMs)
            throw std::runtime_error("portal 超时: " + reqPath);
        g_main_context_iteration(ctx, TRUE);
    }
}

// 執行一次 portal 呼叫（回 o handle），再等 Response signal
std::string Capture::portalAsync(Impl& im, const char* method, const std::string* session,
                                const std::string* parent, const std::string& wantKey, int timeoutMs,
                                bool monitor) {
    GError* e = nullptr;
    const char* DEST = "org.freedesktop.portal.Desktop";
    const char* PATH = "/org/freedesktop/portal/desktop";
    const char* IFACE = "org.freedesktop.portal.ScreenCast";
    // CreateSession 用同時帶兩個 token 的 dict（portal 1.21.1 缺 session token 會 NoReply）
    std::string m(method);
    GVariant* dict = (m == "CreateSession") ? makeCreateSessionDict()
                        : (m == "SelectSources") ? makeSelectDict(monitor, im.selectRestoreToken)
                        : makeDict("handle_token", makeToken("req"));
    // "@a{sv}" 讓 g_variant_new adopt dict 的 ref（不再 inc），所以建完 params
    // 之後「不要」unref dict（會 double-free）。
    GVariant* params;
    if (m == "CreateSession")
        params = g_variant_new("(@a{sv})", dict);
    else if (m == "SelectSources")
        params = g_variant_new("(o@a{sv})", session->c_str(), dict);
    else if (m == "Start")
        params = g_variant_new("(os@a{sv})", session->c_str(), parent->c_str(), dict);
    else
        throw std::runtime_error(std::string("unknown portal method: ") + method);
    if (!params) { throw std::runtime_error("g_variant_new 失敗"); }

    // D-Bus 方法回傳在 wire 上是 tuple：回 o 的方法 → expected type "(o)"
    // 注意：GLib 2.88 的 g_dbus_message_set_body 用 g_variant_ref_sink adopt params
    // 的 ref、message dispose 時 unref → call_sync 會自行釋放 params，**不要**再 unref
    //（雙重釋放 = UAF 寫入已釋放記憶體、會延遲腐化 PipeWire 節點、segfault）。
    GVariant* ret = g_dbus_connection_call_sync(im.conn, DEST, PATH, IFACE, method,
        params, G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, (guint32) timeoutMs, nullptr, &e);
    if (!ret) {
        std::string msg = e ? e->message : "no reply";
        g_clear_error(&e);
        throw std::runtime_error(std::string("portal call 失敗: ") + method + ": " + msg);
    }
    char* handle = nullptr;
    g_variant_get(ret, "(o)", &handle);
    std::string reqPath = handle ? handle : "";
    g_free(handle);
    g_variant_unref(ret);

    im.pendings[reqPath] = Pending{wantKey, {}, -1, false};
    im.error.clear();
    waitFor(im, reqPath, timeoutMs);
    if (!im.error.empty()) {
        std::string err = im.error;
        im.error.clear();
        throw std::runtime_error(std::string(method) + ": " + err);
    }
    if (im.pendings[reqPath].streamNodeId != -1)
        im.streamNodeId = im.pendings[reqPath].streamNodeId;
    return im.pendings[reqPath].result;
}

// ---------- 公開介面 ----------

Capture::Capture() : impl_(new Impl) {}

Capture::~Capture() { stop(); }

void Capture::setMonitorMode(bool m) { impl_->monitorMode = m; }

void Capture::setStreamIndex(int i) { impl_->streamIndex = i; }

void Capture::start() {
    auto& im = *impl_;
    GError* err = nullptr;
    im.conn = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &err);
    if (!im.conn) {
        std::string m = err ? err->message : "unknown";
        g_clear_error(&err);
        throw std::runtime_error("無法連 session bus: " + m);
    }
    g_dbus_connection_signal_subscribe(im.conn,
        "org.freedesktop.portal.Desktop",
        "org.freedesktop.portal.Request",
        "Response",
        nullptr, nullptr,
        G_DBUS_SIGNAL_FLAGS_NONE,
        &Capture::onSignal,
        &im,
        nullptr);

    // 1. CreateSession（務必帶 session_handle_token——portal 1.21.1 crash bug）
    im.sessionHandle = portalAsync(im, "CreateSession", nullptr, nullptr, "session_handle", 60000);
    if (im.sessionHandle.empty())
        throw std::runtime_error("CreateSession 沒有回 session_handle");

    // 2. SelectSources（window 模式走 picker 選視窗、阻塞到選完；monitor 模式無 picker、無視窗 parent）
    //    persist_mode=2 + 上次存檔的 restore_token：首次執行對話框可能出現（點一次 Allow）；
    //    之後 token 還原 session、portal-gnome 在 restore 成功時跳過對話框。
    const char* home = getenv("HOME");
    std::string tokPath = std::string(home ? home : "") + "/.config/screen-fg/restore_token";
    {
        std::ifstream tf(tokPath);
        std::getline(tf, im.selectRestoreToken);
    }
    if (!im.selectRestoreToken.empty())
        fprintf(stderr, "[screen-fg] SelectSources: 用 restore_token 還原（預期免對話框）...\n");
    else
        fprintf(stderr, "[screen-fg] SelectSources: persist_mode=2（無存檔 token、首次可能出對話框）...\n");
    try {
        if (im.monitorMode) {
            fprintf(stderr, "[screen-fg] monitor 捕捉（全螢幕 monitor、無 picker）...\n");
            im.parentWindow = portalAsync(im, "SelectSources", &im.sessionHandle, nullptr, "parent", 300000, true);
            fprintf(stderr, "[screen-fg] monitor 已選 %s\n",
                    im.parentWindow.empty() ? "(monitor)" : im.parentWindow.c_str());
        } else {
            fprintf(stderr, "[screen-fg] 請在 picker 視窗選要捕捉的視窗...\n");
            im.parentWindow = portalAsync(im, "SelectSources", &im.sessionHandle, nullptr, "parent", 300000, false);
            if (im.parentWindow.empty())
                throw std::runtime_error("SelectSources 沒有選到視窗");
            fprintf(stderr, "[screen-fg] 已選視窗 %s\n", im.parentWindow.c_str());
        }
    } catch (const std::runtime_error& ex) {
        if (!im.selectRestoreToken.empty())
            fprintf(stderr, "[screen-fg] 提示：存檔 restore_token 可能過期、可刪 %s 後重試\n", tokPath.c_str());
        throw;
    }

    // 3. Start（obs 驗證過的流程：註冊 capture source）
    std::string emptyParent;
    portalAsync(im, "Start", &im.sessionHandle, &emptyParent, "streams", 60000);

    // 3a. persist_mode=2 獲許 → single-use restore_token 從 Response options 回來、存檔供下次還原
    if (!im.restoreToken.empty()) {
        (void) mkdir((std::string(home ? home : "") + "/.config/screen-fg").c_str(), 0755);
        std::ofstream of(tokPath, std::ios::trunc);
        if (of) {
            of << im.restoreToken;
            fprintf(stderr, "[screen-fg] restore_token 已存檔 → %s\n", tokPath.c_str());
        } else {
            fprintf(stderr, "[screen-fg] 警告：restore_token 寫 %s 失敗\n", tokPath.c_str());
        }
        im.restoreToken.clear();
    }

    // 4. OpenPipeWireRemote → fd
    {
        GError* e2 = nullptr;
        GVariant* dict = makeEmptyDict();
        // "@a{sv}" adopt dict 的 ref → 建完 params 不要再 unref dict
        GVariant* params = g_variant_new("(o@a{sv})", im.sessionHandle.c_str(), dict);
        if (!params) throw std::runtime_error("g_variant_new 失敗");
        GUnixFDList* outFds = nullptr;
        // 回傳 h 的方法 → expected type "(h)"（wire 上是 tuple）
        GVariant* ret = g_dbus_connection_call_with_unix_fd_list_sync(
            im.conn, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
            "org.freedesktop.portal.ScreenCast", "OpenPipeWireRemote",
            params, G_VARIANT_TYPE("(h)"), G_DBUS_CALL_FLAGS_NONE, 15000,
            nullptr, &outFds, nullptr, &e2);
        // params 被 call_sync adopt（見上註解）→ 不要再 unref
        if (!ret) {
            std::string m = e2 ? e2->message : "no reply";
            g_clear_error(&e2);
            throw std::runtime_error("OpenPipeWireRemote: " + m);
        }
        g_variant_unref(ret);
        if (!outFds || g_unix_fd_list_get_length(outFds) == 0) {
            g_object_unref(outFds);
            throw std::runtime_error("OpenPipeWireRemote 沒有回 fd");
        }
        im.pwFd = g_unix_fd_list_get(outFds, 0, nullptr);
        g_object_unref(outFds);
        // 診斷：PipeWire pipe 應是 unix socket；確認 fd 型別
        {
            struct stat st{};
            if (fstat(im.pwFd, &st) == 0)
                fprintf(stderr, "[screen-fg] pwFd=%d type=0%o (socket=0140000)\n", im.pwFd, (unsigned)(st.st_mode & S_IFMT));
            else
                fprintf(stderr, "[screen-fg] pwFd=%d fstat 失敗（fd 可能已關閉）\n", im.pwFd);
        }
    }

    // 5. PipeWire 1.6.2 client（remote fd）。pw_init 已在 main() 跑過（PwInitGuard），
    //    故 pw_main_loop_new 能正常載 support.system handle。
    im.ml = pw_main_loop_new(nullptr);
    if (!im.ml)
        throw std::runtime_error("pw_main_loop_new 失敗");
    im.ctx = pw_context_new(pw_main_loop_get_loop(im.ml), nullptr, 0);
    if (!im.ctx)
        throw std::runtime_error("pw_context_new 失敗");
    // 診斷：core 連線建立時 global_added / driver_added 會觸發（確認連線 + 列出可用 node）
    struct pw_context_events ctxEvs;
    memset(&ctxEvs, 0, sizeof ctxEvs);
    ctxEvs.version = PW_VERSION_CONTEXT_EVENTS;
    ctxEvs.global_added = &Capture::onCtxGlobalAdded;
    ctxEvs.driver_added = &Capture::onCtxDriverAdded;
    pw_context_add_listener(im.ctx, &im.ctxHook, &ctxEvs, &im);
    im.core = pw_context_connect_fd(im.ctx, im.pwFd, nullptr, 0);
    if (!im.core)
        throw std::runtime_error("pw_context_connect_fd 失敗");
    struct pw_properties* props = pw_properties_new(
        PW_KEY_MEDIA_TYPE, "Video",
        PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_MEDIA_ROLE, "Screen",
        PW_KEY_NODE_NAME, "screen-fg",
        nullptr);
    im.stream = pw_stream_new(im.core, "screen-fg", props);
    if (!im.stream)
        throw std::runtime_error("pw_stream_new 失敗");
    struct pw_stream_events evs;
    memset(&evs, 0, sizeof evs);
    evs.version = PW_VERSION_STREAM_EVENTS;
    evs.state_changed = &Capture::onState;
    evs.param_changed = &Capture::onParam;
    evs.process = &Capture::onProcess;
    pw_stream_add_listener(im.stream, &im.pwHook, &evs, &im);
    // 客戶端要 target portal 建立的 capture source node（Start 回來的 node ID），不是
    // PW_ID_ANY（fd 連線是受限連線、只暴露該 source，ANY 找不到 → "no target node available"）。
    int targetId = (im.streamNodeId != -1) ? im.streamNodeId : PW_ID_ANY;
    fprintf(stderr, "[screen-fg] stream target node=%d\n", targetId);
    // 主動遞 EnumFormat 過濾器（raw video）——這是觸發 mutter screencast source 開始格式
    // 協商的關鍵：沒遞 filter，source 不會推格式（onParam 永不觸發）→ stream 卡 PAUSED、fps=0。
    uint8_t podBuf[1024];
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(podBuf, sizeof(podBuf));
    const struct spa_pod* params[1];
    params[0] = (const struct spa_pod*) spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType,    SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw));
    if (pw_stream_connect(im.stream, PW_DIRECTION_INPUT, targetId,
            PW_STREAM_FLAG_AUTOCONNECT, params, 1) < 0)
        throw std::runtime_error("pw_stream_connect 失敗");
    // 主迴圈跑在獨立交替線程（阻塞 run）
    im.loopThread = std::thread([ml = im.ml] { pw_main_loop_run(ml); });
    fprintf(stderr, "[screen-fg] 捕捉就緒\n");
}

void Capture::poll() {
    auto& im = *impl_;
    if (im.conn)
        g_main_context_iteration(g_main_context_default(), FALSE);
}

std::optional<CapturedFrame> Capture::nextFrame() {
    auto& im = *impl_;
    std::lock_guard<std::mutex> lk(im.qMtx);
    if (im.q.empty())
        return std::nullopt;
    auto blk = std::move(im.q.front());
    im.q.pop_front();
    CapturedFrame f;
    f.data = blk->pixels.data();
    f.width = blk->w;
    f.height = blk->h;
    f.stride = blk->stride;
    f.pixFmt = blk->pixFmt;
    f.keep = std::move(blk);
    return f;
}

bool Capture::isDead() {
    auto& im = *impl_;
    if (im.dead.load())
        return true;
    if (im.stream) {
        const char* err = nullptr;
        if (pw_stream_get_state(im.stream, &err) == PW_STREAM_STATE_ERROR) {
            fprintf(stderr, "[screen-fg] 捕捉串流中斷（%s）\n", err ? err : "unknown");
            im.dead.store(true);
            return true;
        }
    }
    return false;
}

void Capture::stop() {
    auto& im = *impl_;
    if (im.stopFlag.exchange(true))
        return;
    if (im.ml)
        pw_main_loop_quit(im.ml);
    if (im.loopThread.joinable())
        im.loopThread.join();
    if (im.stream)
        pw_stream_destroy(im.stream);
    if (im.core)
        pw_core_disconnect(im.core);
    if (im.ctx)
        pw_context_destroy(im.ctx);
    if (im.ml)
        pw_main_loop_destroy(im.ml);
    im.ml = nullptr;
    if (im.pwFd >= 0)
        close(im.pwFd);
    if (im.conn) {
        if (!im.sessionHandle.empty()) {
            GError* e = nullptr;
            GVariant* params = g_variant_new("(o)", im.sessionHandle.c_str());
            (void) g_dbus_connection_call_sync(im.conn, "org.freedesktop.portal.Desktop",
                im.sessionHandle.c_str(), "org.freedesktop.portal.Session", "Close",
                params, nullptr, G_DBUS_CALL_FLAGS_NONE, 500, nullptr, &e);
            // params 被 call_sync adopt → 不要再 unref
            g_clear_error(&e);
        }
        g_object_unref(im.conn);
        im.conn = nullptr;
    }
}

} // namespace screenfg
