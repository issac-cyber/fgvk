// GUI 端的 ProtocolDecoder 測試：shared/protocol.hpp 的純 parse 就是 GUI 解碼用的一切。
// 驗證：emit/parse 回環、parse→ScreenFgStatus 映射、欄位存在性檢查（缺欄 → nullopt）。
#include "process.hpp" // ScreenFgStatus（純結構）

#include "shared/protocol.hpp"

#include <cstdio>
#include <variant>

using namespace screenfg::protocol;

static int failures = 0;
#define CHECK(b) \
    do { \
        if (!(b)) { \
            ++failures; \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #b); \
        } \
    } while (0)

int main() {
    // status 回環 → 映射到 GUI 的 ScreenFgStatus（解碼路徑）
    auto m = parse(emitStatus(12, 2, true, "running"));
    CHECK(m.has_value());
    if (m) {
        CHECK(std::holds_alternative<Status>(*m));
        ScreenFgStatus g;
        if (std::holds_alternative<Status>(*m)) {
            const auto& s = std::get<Status>(*m);
            g.fps = s.fps;
            g.mult = s.mult;
            g.layer = s.layer;
            g.state = s.state;
        }
        CHECK(g.fps == 12);
        CHECK(g.mult == 2);
        CHECK(g.layer);
        CHECK(g.state == "running");
    }

    // exit 回環
    auto e = parse(emitExit(3));
    CHECK(e.has_value() && std::holds_alternative<Exit>(*e));
    if (e && std::holds_alternative<Exit>(*e))
        CHECK(std::get<Exit>(*e).code == 3);

    // 欄位存在性檢查（缺欄 → nullopt，非默默 default）
    CHECK(!parse("{\"type\":\"status\",\"fps\":5}").has_value());
    CHECK(!parse("{\"type\":\"exit\"}").has_value());
    CHECK(!parse("{\"type\":\"bogus\",\"code\":1}").has_value());
    // 壞值 → nullopt
    CHECK(!parse("{\"type\":\"status\",\"fps\":\"xx\",\"mult\":1,\"layer\":true,\"state\":\"running\"}").has_value());

    // 詞彙常數（單一來源）
    CHECK(std::string(Cmd::Pause) == "pause");
    CHECK(std::string(Cmd::Resume) == "resume");
    CHECK(std::string(State::Running) == "running");
    CHECK(std::string(State::Paused) == "paused");

    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
