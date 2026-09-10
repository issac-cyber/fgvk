# Build `fgvk` From Zero — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the complete `fgvk` project — a `screen-fg` CLI binary that captures the screen via XDG-portal ScreenCast v5, deduplicates frames on the CPU, presents them through a full-screen SDL3 + Vulkan FIFO swapchain, and runs the `lsfg-vk` implicit layer to generate interpolated frames; plus a `screen-fg-gui` GTK4 controller that drives `screen-fg` over a stdio protocol — with **every unit test green** and **both** the synthetic (no-portal) and real (portal) end-to-end runs exiting 0.

**Architecture:** Three sibling components share one pure protocol header. `shared/protocol.hpp` is a header-only, I/O-free module (the single source of truth for the stdio control protocol; both binaries compile the *same* header, so vocabulary/structure cannot drift). `screen-fg/` is a single C++ binary: a `FrameSource` seam (real `Capture` = GDBus portal + PipeWire client, or `SyntheticSource` = generated frames for no-portal testing) feeds a `Deduplicator` (32×32 luminance grid + MAD) which feeds a `Presenter` (SDL3 borderless full-screen window + Vulkan 1.2 FIFO swapchain, CPU-uploaded BGRA); pause/passthrough is a re-exec to a renamed copy so the layer self-unloads. `screen-fg-gui/` is a separate GTK4 (gtkmm-4.0) app that `fork`+`exec`s `screen-fg`, writes commands to its stdin, and reads status JSON from its stdout.

**Tech Stack:** C++20, CMake ≥3.22, SDL3 (`libsdl3-dev`, 3.4.x), PipeWire C API (`libpipewire-0.3-dev`, v1.6.2), Vulkan 1.2 loader (`libvulkan-dev`, 1.4 SDK with a *trimmed* header set), GLib/GDBus (`libglib2.0-dev`, `gio-2.0` — *not* raw libdbus, whose container API is broken on this machine), gtkmm-4.0 (`libgtkmm-4.0-dev`; no libadwaita C++ binding), g++ 15.2, CMake 4.3.

---

## Definition of Done (the "very clear goal")

A build is **done** when ALL of the following are true, each verified by the exact command shown:

1. **Pure unit tests, screen-fg:** `cd screen-fg && ./build/screen-fg-tests` prints `… checks, 0 failures` (reference total: 68 checks) and exits 0.
2. **Pure unit tests, GUI:** `cd screen-fg-gui && ./build/screen-fg-gui-tests` prints `0 failures` and exits 0.
3. **Synthetic e2e (no portal, layer OFF):**
   `cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 DISABLE_LSFGVK=1 ./build/screen-fg`
   → stderr shows `synthetic 模式` then `結束（3 帧捕捉 / 1 帧呈現）`, exit 0.
4. **Synthetic e2e (no portal, layer ON):** same as (3) but **without** `DISABLE_LSFGVK=1` → same output, exit 0.
5. **Real portal e2e (monitor capture):** with `~/.config/screen-fg/config.toml` set to `capture_mode = "monitor"` and `display = 1`:
   `cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" ./build/screen-fg`
   → opens the full-screen window on display 1, presents the captured screen (HUD: `FPS …  X2  2x FG / 100%  …Hz`); on `Esc`/`quit` prints `結束（N 帧捕捉 / M 帧呈現）` with `N > 0, M > 0`, exit 0.
6. **GUI e2e:** `cd screen-fg-gui && ./build/screen-fg-gui` → window shows the auto-detected `screen-fg` path; **啟動** launches `screen-fg` (portal/monitor capture), the status row updates (FPS/mult/layer/state); **停止** exits `screen-fg` cleanly (GUI shows `已停止`).
7. **Regressions preserved:** `screen-fg --passthrough` re-execs to `screen-fg-plain` and runs without interpolation; `screen-fg --config <path>` reads an alternate config; stdin `pause`/`resume`/`hud 0|1`/`quit` all work.

## Machine & environment facts (read before starting)

- Target host: **Ubuntu 26.04, GNOME Wayland, 2× AMD R9700 each driving one 3440×1440 display.** Vulkan enumerates **one** device (dev 0) that supports the surface; GPU selection is effectively unambiguous.
- **`lsfg-vk 2.0.0` implicit layer is installed** at `~/.local/share/vulkan/implicit_layer.d/VkLayer_LSFGVK_frame_generation.json` (layer name `VK_LAYER_LSFGVK_frame_generation`). Its shader container is managed by Steam's `lsfg-vk` update channel (already restored — do not touch it).
- **`~/.config/lsfg-vk/conf.toml` has a `[[profile]]` with `active_in = [ "screen-fg" ]`** (2× / 100%). If it is missing, back up the original to `conf.toml.bak-screenfg` first, then add it. The layer self-unloads when the running binary name does not match `active_in` — this is the entire pause mechanism.
- **Use GDBus/gio-2.0 for the portal, never raw libdbus** (this machine's libdbus 1.16.2 container API is broken).
- **PipeWire is 1.6.2.** The portal hands us a *restricted* PipeWire fd via `OpenPipeWireRemote`; a client on that fd sees only the capture-source node.
- **The Vulkan header is a trimmed set** — a few signatures differ from upstream (see the Vulkan pitfalls in Task 9).
- All five apt deps are already installed; Task 0 verifies them.

## Project layout (create exactly this)

```
fgvk/
  .gitignore                 (build/)
  shared/
    protocol.hpp
  screen-fg/
    CMakeLists.txt
    src/
      main.cpp
      config.hpp  config.cpp
      clamp.hpp
      dedup.hpp   dedup.cpp
      frame.hpp
      frame_source.hpp
      synthetic.hpp synthetic.cpp
      reexec.hpp  reexec.cpp
      capture.hpp capture.cpp
      present.hpp present.cpp
    tests/
      test_framework.hpp
      test_main.cpp
      test_config.cpp
      test_config_resolve.cpp
      test_dedup.cpp
      test_clamp.cpp
      test_reexec.cpp
      test_frame_source.cpp
  screen-fg-gui/
    CMakeLists.txt
    screen-fg-gui.desktop
    src/
      main.cpp
      process.hpp process.cpp
    tests/
      test_protocol_decoder.cpp
  specs/                         (the two specs + design history; reference only)
    screen-fg-pipeline/spec.md
    screen-fg-gui/spec.md
```

> **Naming (important):** the *project/directory* is `fgvk`, but the binary is `screen-fg`, the config dir is `~/.config/screen-fg/`, and the layer `active_in` is `"screen-fg"`. When searching code, grep for `screen-fg`, not `fgvk`.
>
> **Build order is a hard constraint:** `shared/` is a sibling of both binaries; **both** `CMakeLists.txt` reference it via `-I <fgvk>/` and `#include "shared/protocol.hpp"`. `screen-fg-gui` finds the binary via `$SCREENFG_BIN` → `../screen-fg/build/screen-fg`. Do not move these.

## Global rules (apply to every task)

- **TDD:** for every pure module, write the failing test first, run it (must FAIL), then implement, run it (must PASS), then commit.
- **No comments except the documented pitfalls.** The codebase is intentionally comment-light; only the load-bearing "why" comments (portal/GDBus/Vulkan pitfalls) are kept. Do not add explanatory prose.
- **Pure vs adapter split:** pure functions take data + an injected `EnvMap`/self-path and never call `getenv`/`readlink`/`execv`. The thin adapter (file read, real env, self-path, exec) lives beside them and is *not* unit-tested.
- **Vocabulary is a contract.** `screen-fg`'s re-exec env uses `ReexecEnv` (`"normal"`/`"paused"`); the status JSON uses `State` (`"running"`/`"paused"`/`"exiting"`). These are **different vocabularies — never mix them.**
- **`parse` missing/malformed field → `nullopt`** (explicit skip, never a silent default).
- **stdout carries ONLY protocol JSON lines; all logging goes to stderr.**
- Commit after each task's tests pass. Use a conventional message, e.g. `feat(screen-fg): clamp`.

---

## Task 0: Environment check & scaffolding

**Files:**
- Create: directory tree above
- Verify: apt packages, layer, conf.toml

- [ ] **Step 1: Verify apt dependencies**

Run:
```bash
dpkg -l | grep -E "libsdl3-dev|libpipewire-0.3-dev|libvulkan-dev|libglib2.0-dev|libgtkmm-4.0-dev" | awk '{print $2}'
```
Expected: all five present (`libglib2.0-dev`, `libgtkmm-4.0-dev`, `libpipewire-0.3-dev`, `libsdl3-dev`, `libvulkan-dev`). If any are missing:
```bash
sudo apt install libsdl3-dev libpipewire-0.3-dev libvulkan-dev libglib2.0-dev libgtkmm-4.0-dev
```
- [ ] **Step 2: Verify the lsfg-vk layer is installed and configured**

Run:
```bash
ls ~/.local/share/vulkan/implicit_layer.d/ | grep -i lsfgvk
grep -n "active_in" ~/.config/lsfg-vk/conf.toml
```
Expected: a `VkLayer_LSFGVK_frame_generation.json` exists, and some `active_in` line contains `"screen-fg"`. If the profile is missing, back up and add it:
```bash
cp ~/.config/lsfg-vk/conf.toml ~/.config/lsfg-vk/conf.toml.bak-screenfg
```
then append a profile:
```toml
[[profile]]
name = "2x FG / 100%"
multiplier = 2
quality = 100
active_in = [ "screen-fg" ]
```
- [ ] **Step 3: Create the directory tree**

```bash
mkdir -p fgvk/shared fgvk/screen-fg/src fgvk/screen-fg/tests fgvk/screen-fg-gui/src fgvk/screen-fg-gui/tests
printf 'build/\n' > fgvk/.gitignore
```
- [ ] **Step 4: Commit the scaffold**

```bash
cd fgvk && git add -A && git commit -m "chore: scaffold fgvk directory tree"
```

---

## Task 1: `shared/protocol.hpp` (pure protocol module) + standalone test

**Files:**
- Create: `shared/protocol.hpp`
- Create: `shared/test_protocol.cpp` (a tiny standalone test, run directly with g++)

- [ ] **Step 1: Write the failing test** — `shared/test_protocol.cpp`

```cpp
#include "shared/protocol.hpp"

#include <cstdio>
#include <variant>

using namespace screenfg::protocol;

static int failures = 0;
#define CHECK(b) \
    do { if (!(b)) { ++failures; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #b); } } while (0)

int main() {
    // status round-trip
    auto m = parse(emitStatus(12, 2, true, "running"));
    CHECK(m.has_value());
    if (m && std::holds_alternative<Status>(*m)) {
        const auto& s = std::get<Status>(*m);
        CHECK(s.fps == 12);
        CHECK(s.mult == 2);
        CHECK(s.layer);
        CHECK(s.state == "running");
    }
    // exit round-trip
    auto e = parse(emitExit(3));
    CHECK(e.has_value() && std::holds_alternative<Exit>(*e));
    if (e && std::holds_alternative<Exit>(*e))
        CHECK(std::get<Exit>(*e).code == 3);
    // field-presence checks (missing/malformed -> nullopt, never a default)
    CHECK(!parse("{\"type\":\"status\",\"fps\":5}").has_value());
    CHECK(!parse("{\"type\":\"exit\"}").has_value());
    CHECK(!parse("{\"type\":\"bogus\",\"code\":1}").has_value());
    CHECK(!parse("{\"type\":\"status\",\"fps\":\"xx\",\"mult\":1,\"layer\":true,\"state\":\"running\"}").has_value());
    // vocabulary constants
    CHECK(std::string(Cmd::Pause) == "pause");
    CHECK(std::string(Cmd::Resume) == "resume");
    CHECK(std::string(Cmd::Hud) == "hud");
    CHECK(std::string(Cmd::Quit) == "quit");
    CHECK(std::string(State::Running) == "running");
    CHECK(std::string(State::Paused) == "paused");
    CHECK(std::string(State::Exiting) == "exiting");
    printf("%d failures\n", failures);
    return failures ? 1 : 0;
}
```
- [ ] **Step 2: Run it and confirm it FAILS (no header yet)**

Run: `g++ -std=c++20 -I . shared/test_protocol.cpp -o /tmp/test_protocol && /tmp/test_protocol`
Expected: compile error (no `shared/protocol.hpp`).
- [ ] **Step 3: Write `shared/protocol.hpp`**

```cpp
// shared/protocol.hpp — stdio 控制協議，screen-fg 與 screen-fg-gui 共同使用。
// 純（無 I/O）：emit 回傳整行 bytes（含 '\n'，transport 負責寫）；
// parse 回傳 typed Message。兩端編譯同一份 header，詞彙/結構/檢查不可能漂移。
//
// 「兩端一致」檢查：message 的必需欄位缺失 / 格式錯誤 → parse 回 nullopt
// （明確報錯，不默默給 default）。kVersion 為共編譯的合約版本。
#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <variant>

namespace screenfg::protocol {

// 不兼容變更時遞增（兩端編同一份 header，天然一致）。
inline constexpr int kVersion = 1;

// 狀態詞彙（status.state / re-exec 的 SCREENFG_STATE env）。
namespace State {
inline constexpr const char* Running = "running";
inline constexpr const char* Paused = "paused";
inline constexpr const char* Exiting = "exiting";
} // namespace State

// 命令詞彙（stdin，line-oriented）。
namespace Cmd {
inline constexpr const char* Quit = "quit";
inline constexpr const char* Pause = "pause";
inline constexpr const char* Resume = "resume";
inline constexpr const char* Hud = "hud";
} // namespace Cmd

struct Status {
    int fps = 0;
    unsigned int mult = 0;
    bool layer = false;
    std::string state;
};
struct Exit {
    int code = 0;
};

// typed message：Status | Exit。
using Message = std::variant<Status, Exit>;

// ---- 純 emit（回傳整行，含尾 '\n'；transport 負責寫）----

inline std::string emitStatus(int fps, unsigned int mult, bool layer, const std::string& state) {
    return "{\"type\":\"status\",\"fps\":" + std::to_string(fps) +
           ",\"mult\":" + std::to_string(mult) +
           ",\"layer\":" + std::string(layer ? "true" : "false") +
           ",\"state\":\"" + state + "\"}\n";
}

inline std::string emitExit(int code) {
    return "{\"type\":\"exit\",\"code\":" + std::to_string(code) + "}\n";
}

// ---- 純 parse（欄位存在性檢查；缺 / 壞 → nullopt，非默默 default）----

// flat key 抽取：找 "key"，取其後第一個 ':' 之後、到 ',' / '"' / '}' / 空白為止的 token；
// key 不存在 → nullopt。
inline std::optional<std::string> rawField(const std::string& line, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t pos = line.find(k);
    if (pos == std::string::npos)
        return std::nullopt;
    const size_t colon = line.find(':', pos + k.size());
    if (colon == std::string::npos)
        return std::nullopt;
    size_t start = colon + 1;
    while (start < line.size() && line[start] == ' ')
        ++start;
    size_t end = start;
    while (end < line.size() && line[end] != ',' && line[end] != '"' && line[end] != '}' && line[end] != ' ')
        ++end;
    if (end == start)
        return std::nullopt;
    return line.substr(start, end - start);
}

inline std::optional<int> intField(const std::string& line, const char* key) {
    auto v = rawField(line, key);
    if (!v)
        return std::nullopt;
    size_t i = 0;
    if (!v->empty() && (*v)[0] == '-')
        i = 1;
    if (i == v->size())
        return std::nullopt;
    for (size_t j = i; j < v->size(); ++j)
        if (!std::isdigit(static_cast<unsigned char>((*v)[j])))
            return std::nullopt;
    return std::stoi(*v);
}

inline std::optional<bool> boolField(const std::string& line, const char* key) {
    auto v = rawField(line, key);
    if (!v)
        return std::nullopt;
    if (*v == "true")
        return true;
    if (*v == "false")
        return false;
    return std::nullopt;
}

// 字串值（引號內）抽取；key 不存在或無引號值 → nullopt。
inline std::optional<std::string> stringField(const std::string& line, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    const size_t pos = line.find(k);
    if (pos == std::string::npos)
        return std::nullopt;
    const size_t colon = line.find(':', pos + k.size());
    if (colon == std::string::npos)
        return std::nullopt;
    const size_t q1 = line.find('"', colon + 1);
    if (q1 == std::string::npos)
        return std::nullopt;
    const size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos)
        return std::nullopt;
    return line.substr(q1 + 1, q2 - q1 - 1);
}

// 解析一列協議行。nullopt：type 非 status/exit，或任一必需欄位缺失/格式錯誤。
inline std::optional<Message> parse(const std::string& line) {
    const auto type = stringField(line, "type");
    if (!type)
        return std::nullopt;
    if (*type == "status") {
        const auto fps = intField(line, "fps");
        const auto mult = intField(line, "mult");
        const auto layer = boolField(line, "layer");
        const auto state = stringField(line, "state");
        if (!fps || !mult || !layer || !state)
            return std::nullopt;
        Status s;
        s.fps = *fps;
        s.mult = static_cast<unsigned int>(*mult);
        s.layer = *layer;
        s.state = *state;
        return Message{s};
    }
    if (*type == "exit") {
        const auto code = intField(line, "code");
        if (!code)
            return std::nullopt;
        Exit e;
        e.code = *code;
        return Message{e};
    }
    return std::nullopt;
}

} // namespace screenfg::protocol
```
- [ ] **Step 4: Run the test and confirm it PASSES**

Run: `g++ -std=c++20 -I . shared/test_protocol.cpp -o /tmp/test_protocol && /tmp/test_protocol`
Expected: `0 failures`, exit 0.
- [ ] **Step 5: Commit**

```bash
cd fgvk && git add shared/ && git commit -m "feat(shared): stdio control protocol (emit/parse, kVersion=1)"
```

---

## Task 2: screen-fg test framework + CMake tests target

**Files:**
- Create: `screen-fg/tests/test_framework.hpp`
- Create: `screen-fg/tests/test_main.cpp`
- Create: `screen-fg/CMakeLists.txt` (tests target only; the binary target is added in Task 10)

- [ ] **Step 1: Write `screen-fg/tests/test_framework.hpp`**

```cpp
#pragma once
#include <cstdio>
#include <vector>

struct Test {
    const char* name;
    void (*fn)();
};
std::vector<Test>& test_list();
void test_check(bool ok, const char* file, int line, const char* expr);

struct Registrar {
    Registrar(const char* name, void (*fn)()) {
        test_list().push_back(Test{name, fn});
    }
};

#define CHECK(b) test_check((b), __FILE__, __LINE__, #b)
#define CHECK_EQ(a, b) test_check((a) == (b), __FILE__, __LINE__, #a " == " #b)
```
- [ ] **Step 2: Write `screen-fg/tests/test_main.cpp`**

```cpp
#include "test_framework.hpp"

#include <vector>

static std::vector<Test> tests;
static int checks = 0;
static int failures = 0;

std::vector<Test>& test_list() {
    return tests;
}
void test_check(bool ok, const char* file, int line, const char* expr) {
    checks++;
    if (!ok) {
        failures++;
        printf("  FAIL %s:%d  %s\n", file, line, expr);
    }
}

int main() {
    for (auto& t : tests) {
        printf("== %s\n", t.name);
        t.fn();
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
```
- [ ] **Step 3: Write `screen-fg/CMakeLists.txt` (tests target only for now)**

```cmake
cmake_minimum_required(VERSION 3.28)
project(screen-fg CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 註：binary target（link PW/SDL/Vulkan/GIO）在 Task 10 加；
# 這裡先建「純模組測試」target，不 link 任何外部 C 庫。
enable_testing()
add_executable(screen-fg-tests
    tests/test_main.cpp
)
target_include_directories(screen-fg-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME unit COMMAND screen-fg-tests)
```
- [ ] **Step 4: Configure, build, run — confirm the empty harness works**

Run:
```bash
cd screen-fg && cmake -B build && cmake --build build && ./build/screen-fg-tests
```
Expected: `0 checks, 0 failures`, exit 0.
- [ ] **Step 5: Commit**

```bash
cd fgvk && git add screen-fg/ && git commit -m "test(screen-fg): minimal test harness + CMake tests target"
```

---

## Task 3: `clamp.hpp` + test (pure)

**Files:**
- Create: `screen-fg/src/clamp.hpp`
- Create: `screen-fg/tests/test_clamp.cpp`
- Modify: `screen-fg/CMakeLists.txt` (add `tests/test_clamp.cpp` to the tests target)

- [ ] **Step 1: Write the failing test** — `screen-fg/tests/test_clamp.cpp`

```cpp
#include "clamp.hpp"
#include "test_framework.hpp"

using namespace screenfg;

static void test_clamp() {
    // 167Hz / 60fps → 2x
    CHECK_EQ(maxMultiplier(167, 60), 2);
    CHECK_EQ(maxMultiplier(144, 60), 2);
    CHECK_EQ(maxMultiplier(120, 60), 2);
    CHECK_EQ(maxMultiplier(119, 60), 1);
    CHECK_EQ(maxMultiplier(60, 60), 1);
    CHECK_EQ(maxMultiplier(240, 60), 4);
    CHECK_EQ(maxMultiplier(144, 30), 4);
    // 邊界
    CHECK_EQ(maxMultiplier(0, 60), 1);
    CHECK_EQ(maxMultiplier(167, 0), 1);
    // 捕捉上限版本
    CHECK_EQ(maxMultiplierForCapture(167), 2);
    CHECK_EQ(maxMultiplierForCapture(144), 2);
    CHECK_EQ(maxMultiplierForCapture(120), 2);
    CHECK_EQ(maxMultiplierForCapture(119), 1);
    CHECK_EQ(maxMultiplierForCapture(240), 4);
}

static Registrar reg("clamp", test_clamp);
```
- [ ] **Step 2: Add `tests/test_clamp.cpp` to the CMake tests target, build, run — confirm it FAILS**

Edit `screen-fg/CMakeLists.txt`, add `tests/test_clamp.cpp` to the `screen-fg-tests` `add_executable` list.
Run: `cd screen-fg && cmake -B build && cmake --build build && ./build/screen-fg-tests`
Expected: compile error (no `clamp.hpp`).
- [ ] **Step 3: Write `screen-fg/src/clamp.hpp`**

```cpp
#pragma once
#include <cstdint>

namespace screenfg {

// 最大可放的倍數：contentFps * mult <= displayHz
inline uint32_t maxMultiplier(uint32_t displayHz, uint32_t contentFps) {
    if (contentFps == 0 || displayHz == 0)
        return 1;
    return displayHz / contentFps;
}

// spec: max_mult = floor(display_hz / 60)（window 捕捉上限 60fps）
inline uint32_t maxMultiplierForCapture(uint32_t displayHz) {
    return maxMultiplier(displayHz, 60);
}

} // namespace screenfg
```
- [ ] **Step 4: Build, run — confirm it PASSES**

Run: `cd screen-fg && cmake --build build && ./build/screen-fg-tests`
Expected: `0 failures`.
- [ ] **Step 5: Commit**

```bash
cd fgvk && git add screen-fg/ && git commit -m "feat(screen-fg): maxMultiplier clamp (Q4)"
```

---

## Task 4: `dedup.{hpp,cpp}` + test (pure)

**Files:**
- Create: `screen-fg/src/dedup.hpp`
- Create: `screen-fg/src/dedup.cpp`
- Create: `screen-fg/tests/test_dedup.cpp`
- Modify: `screen-fg/CMakeLists.txt` (add `tests/test_dedup.cpp` and `src/dedup.cpp`)

- [ ] **Step 1: Write the failing test** — `screen-fg/tests/test_dedup.cpp`

```cpp
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
```
- [ ] **Step 2: Add `tests/test_dedup.cpp` to CMake, build, run — confirm it FAILS**

Edit `screen-fg/CMakeLists.txt`, add `tests/test_dedup.cpp` to the tests target.
Run: `cd screen-fg && cmake -B build && cmake --build build && ./build/screen-fg-tests`
Expected: compile error (no `dedup.hpp`).
- [ ] **Step 3: Write `screen-fg/src/dedup.hpp`**

```cpp
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
```
- [ ] **Step 4: Write `screen-fg/src/dedup.cpp`**

```cpp
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
```
- [ ] **Step 5: Add `src/dedup.cpp` to the tests target, build, run — confirm PASSES**

Edit `screen-fg/CMakeLists.txt`, add `src/dedup.cpp` to the tests target.
Run: `cd screen-fg && cmake -B build && ./build/screen-fg-tests`
Expected: `0 failures`.
- [ ] **Step 6: Commit**

```bash
cd fgvk && git add screen-fg/ && git commit -m "feat(screen-fg): 32x32 MAD deduplicator (Q2)"
```

---

## Task 5: `reexec.{hpp,cpp}` + test (pure resolver)

> Ordering note: `config.cpp` `#include`s `reexec.hpp`, so the re-exec module must exist before config. Build it first.

**Files:**
- Create: `screen-fg/src/reexec.hpp`
- Create: `screen-fg/src/reexec.cpp`
- Create: `screen-fg/tests/test_reexec.cpp`
- Modify: `screen-fg/CMakeLists.txt` (add `tests/test_reexec.cpp` and `src/reexec.cpp` to the `screen-fg-tests` target)

- [ ] **Step 1: Write the failing test** — `screen-fg/tests/test_reexec.cpp`

```cpp
#include "reexec.hpp"
#include "test_framework.hpp"

#include <optional>
#include <string>

using namespace screenfg;

static void test_reexec() {
    // PassthroughBoot：plain + paused + 不設 HUD env（nullopt）
    auto b = resolveReexec(false, ReexecAction::PassthroughBoot, std::nullopt);
    CHECK_EQ(b.targetName, std::string("screen-fg-plain"));
    CHECK_EQ(b.stateEnv, std::string("paused"));
    CHECK_EQ(b.hudEnv, std::string(""));

    // Pause：plain + paused + HUD env 跟隨
    auto p = resolveReexec(false, ReexecAction::Pause, true);
    CHECK_EQ(p.targetName, std::string("screen-fg-plain"));
    CHECK_EQ(p.stateEnv, std::string("paused"));
    CHECK_EQ(p.hudEnv, std::string("1"));
    auto p0 = resolveReexec(false, ReexecAction::Pause, false);
    CHECK_EQ(p0.hudEnv, std::string("0"));

    // Resume：screen-fg + normal + HUD env
    auto r = resolveReexec(true, ReexecAction::Resume, true);
    CHECK_EQ(r.targetName, std::string("screen-fg"));
    CHECK_EQ(r.stateEnv, std::string("normal"));
    CHECK_EQ(r.hudEnv, std::string("1"));

    // Toggle：paused → screen-fg/normal；normal → plain/paused
    auto t1 = resolveReexec(true, ReexecAction::Toggle, false);
    CHECK_EQ(t1.targetName, std::string("screen-fg"));
    CHECK_EQ(t1.stateEnv, std::string("normal"));
    auto t0 = resolveReexec(false, ReexecAction::Toggle, false);
    CHECK_EQ(t0.targetName, std::string("screen-fg-plain"));
    CHECK_EQ(t0.stateEnv, std::string("paused"));
}

static Registrar reg("reexec", test_reexec);
```
- [ ] **Step 2: Add `tests/test_reexec.cpp` to the CMake tests target, build, run — confirm it FAILS**

Edit `screen-fg/CMakeLists.txt`, add `tests/test_reexec.cpp` to the `screen-fg-tests` `add_executable` list.
Run: `cd screen-fg && cmake -B build && cmake --build build && ./build/screen-fg-tests`
Expected: compile error (no `reexec.hpp`).
- [ ] **Step 3: Write `screen-fg/src/reexec.hpp`**

```cpp
#pragma once
#include <optional>
#include <string>

namespace screenfg {

// re-exec 的 SCREENFG_STATE env 值（注意：與 status JSON 的 state 詞彙不同——
// status 用 "running"，re-exec env 用 "normal"，兩者不可混用）。
namespace ReexecEnv {
inline constexpr const char* Paused = "paused";
inline constexpr const char* Normal = "normal";
} // namespace ReexecEnv

// 請求的 re-exec 動作。
enum class ReexecAction {
    PassthroughBoot, // --passthrough：立即切到 plain 複製本
    Pause,           // 暫停：切到 plain 複製本（layer unload、純呈現）
    Resume,          // 恢復：切回 screen-fg
    Toggle,          // P 鍵：paused <-> normal
};

// 純 resolver 的產物：re-exec 到哪個 binary + 傳遞哪些 env。
struct ReexecPlan {
    std::string targetName; // "screen-fg" 或 "screen-fg-plain"
    std::string stateEnv;   // ReexecEnv::Paused / Normal
    std::string hudEnv;     // "1"/"0"；boot 為 ""（不設 SCREENFG_HUD）
};

// 純（無 I/O、無 self-path、不 execv）：给定當前狀態、請求動作、HUD，決定 re-exec 的
// target + 傳遞 env。self-path 由 adapter 注入（見 reexec.cpp）。
ReexecPlan resolveReexec(bool currentlyPaused, ReexecAction action, std::optional<bool> hud);

// ---------- 注入 self-path 的薄 adapter（碰 process / fs，不純）----------

// /proc/self/exe 的真實路徑（readlink；失敗回 ""）。
std::string selfPath();

// 確保同目錄有最新的 screen-fg-plain 複製本（cp -f，每次都重做避免過期）。
void ensurePlainCopy(const std::string& selfP);

// 依 plan re-exec（組 argv 跳過 --passthrough、setenv STATE、有值才 setenv HUD、execv）。
// 成功不返回；失敗 exit(1)。
void reexec(const ReexecPlan& plan, int argc, char** argv, const std::string& selfP);

} // namespace screenfg
```
- [ ] **Step 4: Write `screen-fg/src/reexec.cpp`**

```cpp
#include "reexec.hpp"

#include <cstring>
#include <cstdlib>
#include <iostream>

#include <unistd.h>

namespace screenfg {

// ---------- 純 resolver ----------

ReexecPlan resolveReexec(bool currentlyPaused, ReexecAction action, std::optional<bool> hud) {
    // HUD env：hud 有值才設（"1"/"0"）；nullopt（boot）不設（""）。
    auto hudEnv = [hud]() -> std::string {
        if (!hud)
            return std::string();
        return *hud ? "1" : "0";
    };
    ReexecPlan p;
    switch (action) {
        case ReexecAction::PassthroughBoot:
            p.targetName = "screen-fg-plain";
            p.stateEnv = ReexecEnv::Paused;
            p.hudEnv = hudEnv();
            break;
        case ReexecAction::Pause:
            p.targetName = "screen-fg-plain";
            p.stateEnv = ReexecEnv::Paused;
            p.hudEnv = hudEnv();
            break;
        case ReexecAction::Resume:
            p.targetName = "screen-fg";
            p.stateEnv = ReexecEnv::Normal;
            p.hudEnv = hudEnv();
            break;
        case ReexecAction::Toggle:
            if (currentlyPaused) {
                p.targetName = "screen-fg";
                p.stateEnv = ReexecEnv::Normal;
            } else {
                p.targetName = "screen-fg-plain";
                p.stateEnv = ReexecEnv::Paused;
            }
            p.hudEnv = hudEnv();
            break;
    }
    return p;
}

// ---------- 注入 self-path 的 adapter ----------

std::string selfPath() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0)
        return {};
    buf[n] = 0;
    return buf;
}

void ensurePlainCopy(const std::string& selfP) {
    auto pos = selfP.rfind('/');
    if (pos == std::string::npos)
        return;
    std::string base = selfP.substr(pos + 1);
    if (base == "screen-fg-plain")
        return;
    std::string plainP = selfP.substr(0, pos + 1) + "screen-fg-plain";
    // 每次都重新複製：build 更新後舊 plain copy 會過期（曾導致 pause re-exec 跑到舊 binary）
    int rc = system(("cp -f '" + selfP + "' '" + plainP + "' 2>/dev/null").c_str());
    (void) rc;
}

void reexec(const ReexecPlan& plan, int argc, char** argv, const std::string& selfP) {
    auto pos = selfP.rfind('/');
    std::string target = (pos == std::string::npos ? std::string() : selfP.substr(0, pos + 1)) + plan.targetName;
    char* argp[64];
    int ni = 0;
    argp[ni++] = const_cast<char*>(target.c_str());
    for (int i = 1; i < argc && ni < 63; ++i) {
        if (strcmp(argv[i], "--passthrough") == 0)
            continue; // 狀態改由 env 帶（避免無限 re-exec）
        argp[ni++] = argv[i];
    }
    argp[ni] = nullptr;
    setenv("SCREENFG_STATE", plan.stateEnv.c_str(), 1);
    if (!plan.hudEnv.empty())
        setenv("SCREENFG_HUD", plan.hudEnv.c_str(), 1); // 保留運行期 HUD 狀態跨 re-exec
    execv(target.c_str(), argp);
    std::cerr << "[screen-fg] re-exec 失敗：" << target << "\n";
    std::exit(1);
}

} // namespace screenfg
```
- [ ] **Step 5: Add `src/reexec.cpp` to the tests target, build, run — confirm PASSES**

Edit `screen-fg/CMakeLists.txt`, add `src/reexec.cpp` to the `screen-fg-tests` target.
Run: `cd screen-fg && cmake --build build && ./build/screen-fg-tests`
Expected: `0 failures`.
- [ ] **Step 6: Commit**

```bash
cd fgvk && git add screen-fg/ && git commit -m "feat(screen-fg): pure re-exec resolver (Q8a) + plain-copy adapter"
```

---

## Task 6: `config.{hpp,cpp}` + tests (pure)

**Files:**
- Create: `screen-fg/src/config.hpp`
- Create: `screen-fg/src/config.cpp`
- Create: `screen-fg/tests/test_config.cpp`
- Create: `screen-fg/tests/test_config_resolve.cpp`
- Modify: `screen-fg/CMakeLists.txt` (add both test files and `src/config.cpp` to the `screen-fg-tests` target)

- [ ] **Step 1: Write the failing tests** — `screen-fg/tests/test_config.cpp`

```cpp
#include "config.hpp"
#include "test_framework.hpp"

#include <stdexcept>
#include <string>

using namespace screenfg;

static void test_config() {
    Config d;
    d.dedupThreshold = 1.5f;
    d.hud = false;
    d.displayIndex = 3;

    // 基本 key 覆蓋 + 預設保留
    Config c = parseConfig(
        "dedup_threshold = 7.25\n"
        "hud = true\n"
        "display = 1\n",
        d);
    CHECK_EQ(c.dedupThreshold, 7.25f);
    CHECK(c.hud);
    CHECK_EQ(c.displayIndex, 1);

    // 引號字串（profile / gpu）
    Config c2 = parseConfig(
        "profile = \"4x FG / 85% [Performance]\"\n"
        "gpu = \"0000:01:00.0\"\n",
        d);
    CHECK_EQ(c2.profile, std::string("4x FG / 85% [Performance]"));
    CHECK_EQ(c2.gpu, std::string("0000:01:00.0"));

    // 未給的 key 保留預設
    Config c3 = parseConfig("", d);
    CHECK_EQ(c3.dedupThreshold, 1.5f);
    CHECK(!c3.hud);
    CHECK_EQ(c3.displayIndex, 3);

    // 註解 + 空行
    Config c4 = parseConfig(
        "# comment\n"
        "\n"
        "dedup_threshold = 2.5\n",
        d);
    CHECK_EQ(c4.dedupThreshold, 2.5f);

    // 無 '=' 的線 → throw
    bool threw = false;
    try {
        parseConfig("this is not toml\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // 空 value → throw
    threw = false;
    try {
        parseConfig("hud =\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // 壞 bool → throw
    threw = false;
    try {
        parseConfig("hud = maybe\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // profile 未加引號 → throw
    threw = false;
    try {
        parseConfig("profile = 2x FG / 100%\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);

    // content_fps_cap
    Config c5 = parseConfig("content_fps_cap = 30\n", d);
    CHECK_EQ(c5.contentFpsCap, 30u);

    // capture_mode：預設 window；monitor 可解析；壞值 throw
    CHECK_EQ(d.captureMode, std::string("window"));
    CHECK_EQ(parseConfig("", d).captureMode, std::string("window"));
    Config cm1 = parseConfig("capture_mode = monitor\n", d);
    CHECK_EQ(cm1.captureMode, std::string("monitor"));
    Config cm2 = parseConfig("capture_mode = window\n", d);
    CHECK_EQ(cm2.captureMode, std::string("window"));
    // 容錯：引號形式亦可
    Config cm3 = parseConfig("capture_mode = \"monitor\"\n", d);
    CHECK_EQ(cm3.captureMode, std::string("monitor"));
    threw = false;
    try {
        parseConfig("capture_mode = bogus\n", d);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

static Registrar reg("config", test_config);
```

and `screen-fg/tests/test_config_resolve.cpp`:

```cpp
#include "config.hpp"
#include "test_framework.hpp"

using namespace screenfg;

static void test_config_resolve() {
    // 純 toml（無 env）
    auto r0 = resolveConfig("display = 2\nhud = false\n", {});
    CHECK_EQ(r0.config.displayIndex, 2);
    CHECK(!r0.config.hud);
    CHECK(!r0.paused);

    // env 覆蓋 toml
    EnvMap env;
    env["SCREENFG_DISPLAY"] = "5";
    env["SCREENFG_HUD"] = "1";
    auto r1 = resolveConfig("display = 2\nhud = false\n", env);
    CHECK_EQ(r1.config.displayIndex, 5);
    CHECK(r1.config.hud);

    // STATE 決定 paused
    EnvMap envPaused;
    envPaused["SCREENFG_STATE"] = "paused";
    CHECK(resolveConfig("", envPaused).paused);
    EnvMap envNormal;
    envNormal["SCREENFG_STATE"] = "normal";
    CHECK(!resolveConfig("", envNormal).paused);

    // cardIndexFor：本機 identity
    CHECK_EQ(cardIndexFor(0), 0);
    CHECK_EQ(cardIndexFor(1), 1);
}

static Registrar reg("config_resolve", test_config_resolve);
```
- [ ] **Step 2: Add both test files to the CMake tests target, build, run — confirm they FAIL**

Edit `screen-fg/CMakeLists.txt`, add `tests/test_config.cpp` and `tests/test_config_resolve.cpp` to the `screen-fg-tests` target.
Run: `cd screen-fg && cmake -B build && cmake --build build && ./build/screen-fg-tests`
Expected: compile error (no `config.hpp`).
- [ ] **Step 3: Write `screen-fg/src/config.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace screenfg {

// 最終設定（persistent settings；env 已 resolve 進去）。
// 注意：只存 displayIndex（哪塊螢幕）；card 序号由純函數 cardIndexFor() 映射，
// 兩者不可混用。
struct Config {
    float dedupThreshold = 3.0f; // MAD on 0..255
    bool hud = true;
    std::string gpu; // PCI bus ID override (e.g. "0000:01:00.0"); empty = auto
    std::string profile = "2x FG / 100%"; // lsfg-vk profile name
    int displayIndex = 0; // which display the FG window covers
    uint32_t contentFpsCap = 60;
    std::string captureMode = "window"; // "window"（spec 預設：picker 選視窗）| "monitor"（全螢幕 monitor 捕捉、無 picker）
};

// 環境變數的注入表示（可測性：不直接碰 getenv）。
using EnvMap = std::unordered_map<std::string, std::string>;

// resolve 的產物：persistent 設定 + 運行期 re-exec 狀態。
struct ResolvedConfig {
    Config config;
    bool paused = false; // 來自 SCREENFG_STATE == "paused"
};

// 純：flat-TOML 子集 text → Config（defaults 打底；malformed throw std::runtime_error）。
Config parseConfig(const std::string& tomlText, Config defaults);

// 純：config.toml text + env（HUD/DISPLAY/STATE）→ 最終設定。
// envMap 鍵（皆可缺）："SCREENFG_HUD"（覆蓋 hud）、"SCREENFG_DISPLAY"（覆蓋 displayIndex）、
//                       "SCREENFG_STATE"（== "paused" → paused）。
ResolvedConfig resolveConfig(const std::string& tomlText, const EnvMap& envMap);

// 純：display index → card 序号（本機 identity：display N 在 card N；
// 具名以區隔「display index」與「card 序号」兩個概念）。
int cardIndexFor(int displayIndex);

// 讀檔 adapter：缺檔案 → 純預設；malformed throw。env 由真實環境收集後交給純 resolveConfig。
ResolvedConfig loadConfig(const std::string& path);

} // namespace screenfg
```
- [ ] **Step 4: Write `screen-fg/src/config.cpp`**

```cpp
#include "config.hpp"

#include "reexec.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace screenfg {

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

Config parseConfig(const std::string& text, Config def) {
    Config c = def;
    std::istringstream ss(text);
    std::string line;
    int lineno = 0;
    while (std::getline(ss, line)) {
        ++lineno;
        auto hash = line.find('#');
        if (hash != std::string::npos)
            line = line.substr(0, hash);
        line = trim(line);
        if (line.empty())
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos)
            throw std::runtime_error("line " + std::to_string(lineno) + ": expected 'key = value'");
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (val.empty())
            throw std::runtime_error("line " + std::to_string(lineno) + ": empty value");
        bool quoted = val.size() >= 2 && val.front() == '"' && val.back() == '"';
        try {
            if (key == "dedup_threshold") {
                c.dedupThreshold = std::stof(val);
            } else if (key == "hud") {
                if (val == "true")
                    c.hud = true;
                else if (val == "false")
                    c.hud = false;
                else
                    throw std::invalid_argument("bool");
            } else if (key == "profile") {
                if (!quoted)
                    throw std::invalid_argument("quoted");
                c.profile = val.substr(1, val.size() - 2);
            } else if (key == "gpu") {
                if (!quoted)
                    throw std::invalid_argument("quoted");
                c.gpu = val.substr(1, val.size() - 2);
            } else if (key == "display") {
                c.displayIndex = std::stoi(val);
            } else if (key == "content_fps_cap") {
                c.contentFpsCap = static_cast<uint32_t>(std::stoul(val));
            } else if (key == "capture_mode") {
                std::string v = val;
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                    v = v.substr(1, v.size() - 2); // 容錯：引號/未引號皆可
                if (v != "window" && v != "monitor")
                    throw std::invalid_argument("window|monitor");
                c.captureMode = v;
            }
            // unknown keys: ignored (forward compatibility)
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("line " + std::to_string(lineno) + ": bad value '" + val + "' for '" + key + "'");
        }
    }
    return c;
}

ResolvedConfig resolveConfig(const std::string& tomlText, const EnvMap& envMap) {
    ResolvedConfig rc;
    rc.config = parseConfig(tomlText, Config{});
    // env 層（覆蓋 toml 層）
    if (auto it = envMap.find("SCREENFG_HUD"); it != envMap.end() && !it->second.empty())
        rc.config.hud = (it->second[0] == '1');
    if (auto it = envMap.find("SCREENFG_DISPLAY"); it != envMap.end() && !it->second.empty())
        rc.config.displayIndex = std::atoi(it->second.c_str());
    if (auto it = envMap.find("SCREENFG_STATE"); it != envMap.end())
        rc.paused = (it->second == ReexecEnv::Paused);
    return rc;
}

int cardIndexFor(int displayIndex) {
    // 本機：2 張 R9700 各驅一塊顯示，display N 在 card N（identity）。
    return displayIndex;
}

ResolvedConfig loadConfig(const std::string& path) {
    std::string text;
    std::ifstream f(path);
    if (f) {
        std::ostringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    EnvMap env;
    const char* keys[] = {"SCREENFG_HUD", "SCREENFG_DISPLAY", "SCREENFG_STATE"};
    for (const char* k : keys)
        if (const char* v = std::getenv(k))
            env[k] = v;
    return resolveConfig(text, env);
}

} // namespace screenfg
```
- [ ] **Step 5: Add `src/config.cpp` to the tests target, build, run — confirm PASSES**

Edit `screen-fg/CMakeLists.txt`, add `src/config.cpp` to the `screen-fg-tests` target.
Run: `cd screen-fg && cmake --build build && ./build/screen-fg-tests`
Expected: `0 failures`.
- [ ] **Step 6: Commit**

```bash
cd fgvk && git add screen-fg/ && git commit -m "feat(screen-fg): flat-TOML config parser + env resolve (pure) + load adapter"
```

---

## Task 7: `frame.hpp` + `frame_source.hpp` + `synthetic.{hpp,cpp}` + test (pure)

**Files:**
- Create: `screen-fg/src/frame.hpp`
- Create: `screen-fg/src/frame_source.hpp`
- Create: `screen-fg/src/synthetic.hpp`
- Create: `screen-fg/src/synthetic.cpp`
- Create: `screen-fg/tests/test_frame_source.cpp`
- Modify: `screen-fg/CMakeLists.txt` (add `tests/test_frame_source.cpp` and `src/synthetic.cpp` to the `screen-fg-tests` target)

- [ ] **Step 1: Write the failing test** — `screen-fg/tests/test_frame_source.cpp`

```cpp
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
```
- [ ] **Step 2: Add `tests/test_frame_source.cpp` to the CMake tests target, build, run — confirm it FAILS**

Edit `screen-fg/CMakeLists.txt`, add `tests/test_frame_source.cpp` to the `screen-fg-tests` target.
Run: `cd screen-fg && cmake -B build && cmake --build build && ./build/screen-fg-tests`
Expected: compile error (no `frame.hpp`).
- [ ] **Step 3: Write `screen-fg/src/frame.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <memory>

namespace screenfg {

// 正規化 pixel format：BGRA（== SPA_VIDEO_FORMAT_BGRA == 44）。
// 中性常量（不依賴 PipeWire 標頭），讓 frame 定義與 PW 解耦。
inline constexpr int kPixFmtBgra = 44;

// 一帧捕捉畫面（中性定義，不依賴 portal / PipeWire）。
// data 指到來源自持的 BGRA copy（stride = width*4，無 padding）。
// pixFmt 已正規化為 BGRA（= kPixFmtBgra）。有效到該 block 被覆蓋 / 來源析構
// （FrameBlock 自持記憶體，無跨線程生命週期問題）。
struct CapturedFrame {
    const uint8_t* data = nullptr;
    uint32_t width = 0, height = 0;
    uint32_t stride = 0; // = width*4（BGRA）
    int pixFmt = -1;     // 正規化後 = kPixFmtBgra
    std::shared_ptr<void> keep; // 持有 pixel 儲存體（frame 存活期間 data 有效）
};

} // namespace screenfg
```
- [ ] **Step 4: Write `screen-fg/src/frame_source.hpp`**

```cpp
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
```
- [ ] **Step 5: Write `screen-fg/src/synthetic.hpp`**

```cpp
#pragma once
#include "frame_source.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace screenfg {

// 測試用合成幀來源：產生 raw 每帧不同的 BGRA 畫面（16×16 移動方塊 + 漸變）。
// 注意：方塊太小、動不了 32×32 MAD 超過預設閾值 3.0 → dedup 只放行第一幀
//（第一幀永遠 true）；「1 帧呈現」是預期值，管線 + layer 由第一幀觸發。
// 不需 portal / picker，用於驗證呈現端與 layer 激活。
// 跑完 totalFrames 帧後 isDead() 回 true（main 視為來源關閉、乾淨退出）。
class SyntheticSource : public FrameSource {
public:
    SyntheticSource(uint32_t width = 1280, uint32_t height = 720, uint32_t totalFrames = 300);
    void start() override;
    void poll() override;
    std::optional<CapturedFrame> nextFrame() override;
    bool isDead() override;
    void stop() override;
private:
    void makeFrame();
    uint32_t w_ = 0, h_ = 0;
    uint32_t totalFrames_ = 0;
    uint64_t frameNo_ = 0;
    std::shared_ptr<std::vector<uint8_t>> buf_;
};

} // namespace screenfg
```
- [ ] **Step 6: Write `screen-fg/src/synthetic.cpp`**

```cpp
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

// 產生一帧 raw 與上一帧不同的 BGRA 畫面：
//  - 背景：水平漸變（B 通道依 x 變化）
//  - 移動方塊：16×16，x 位置 = (frameNo * 8) % width，顏色隨 frameNo 變化
// 注意：方塊面積 ~0.03% 全幀，32×32 格化後 MAD ≈ 0.001 < 預設門檻 3.0
// → dedup 判定「相同」、只放行第一幀（第一幀永遠 true）。管線 + layer 由第一幀觸發。
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
```
- [ ] **Step 7: Add `src/synthetic.cpp` to the tests target, build, run — confirm the FULL pure suite PASSES**

Edit `screen-fg/CMakeLists.txt`, add `src/synthetic.cpp` to the `screen-fg-tests` target.
Run: `cd screen-fg && cmake --build build && ./build/screen-fg-tests`
Expected: `0 failures` (reference total: **74 checks** — clamp 14, dedup 9, config 20, config_resolve 9, reexec 14, frame_source 8).
- [ ] **Step 8: Commit**

```bash
cd fgvk && git add screen-fg/ && git commit -m "feat(screen-fg): neutral CapturedFrame + FrameSource seam + SyntheticSource"
```

---

## Task 8: `capture.{hpp,cpp}` (I/O adapter — GDBus portal + PipeWire)

> This is an **I/O adapter**, not a pure module: it talks to the XDG-portal over GDBus and receives frames from PipeWire over a restricted fd. It is **not** unit-tested (it needs a real portal); it is compiled in Task 10's binary build and verified end-to-end in Task 12 (real portal). **Do not add it to any CMake target yet** — Task 10 adds it to the binary target. Write the code now, keep the tree compiling.

**Files:**
- Create: `screen-fg/src/capture.hpp`
- Create: `screen-fg/src/capture.cpp`

### Portal / GDBus pitfalls (this machine, portal 1.21.1, GLib 2.88) — bake them in
- **Use GDBus/gio-2.0, never raw libdbus** (this machine's libdbus 1.16.2 container API is broken).
- **`CreateSession` must send BOTH `handle_token` AND `session_handle_token`** — portal 1.21.1 NoReply/crashes without the session token.
- **Tokens must match `[a-zA-Z0-9_]`** (the request object path rejects dashes → "Invalid token"). Build with `pid` + counter and underscores.
- **Expected reply types include the outer tuple:** a method returning `o` → `G_VARIANT_TYPE("(o)")`; returning `h` → `"(h)"` (not `"o"`/`"h"`).
- **`g_variant_new("@a{sv}", dict)` ADOPTS the dict ref (does not inc):** after building `params`, do **not** `g_variant_unref(dict)` (double-free). Unref only the `params`.
- **GLib 2.88 `GVariantBuilder` `{sv}` with a `GVariant` arg is a BORROW** (stores the pointer, no inc, `end()` doesn't copy) → a freshly made `GVariant` arg must **not** be unref (the dict keeps it alive until the dict is freed). This applies to the `types`/GVariant args in `makeSelectDict`.
- **Extract unknown-type children with `g_variant_get_child_value`** (returns a `GVariant*` you must unref); `g_variant_get_child`'s format string cannot use `"^"`.
- **Known non-fatal quirk:** `g_dbus_connection_call_sync` prints a `g_atomic_ref_count_dec` assertion on GLib 2.88 (`GLib-CRITICAL`) — it does not crash, exit stays 0. It is a GLib build issue, not our dict/params handling (isolated and verified). Leave it; don't "fix" it.

### PipeWire pitfalls (1.6.2)
- **`pw_init` must already have run** before `pw_main_loop_new` (main() does it via a `PwInitGuard`, Task 10) — otherwise `pw_main_loop_new` fails to load the `support.system` handle.
- **The portal fd is a RESTRICTED connection** (a client on it sees only the capture-source node). Target the **node ID from the `Start` response**, not `PW_ID_ANY` (`ANY` → "no target node available").
- **Do NOT set `PW_STREAM_FLAG_DRIVER`.** The portal's capture source is a `node.driver` clock source that pushes frames by itself; adding the DRIVER flag creates two-driver contention and both nodes stay suspended. Use `PW_STREAM_FLAG_NONE`.
- **`onState` PAUSED is informational only** — do not treat it as "no frames yet". The node is activated asynchronously by mutter/the portal; `set_active`/`trigger_process` are diagnostic logs, not the real trigger.

- [ ] **Step 1: Write `screen-fg/src/capture.hpp`**

```cpp
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
```
- [ ] **Step 2: Write `screen-fg/src/capture.cpp`**

```cpp
#include "capture.hpp"

#include <fcntl.h>
#include <linux/dma-buf.h>
#include <poll.h>
#include <spa/buffer/buffer.h>
#include <spa/param/param.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/raw.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
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
    // ---- PipeWire 1.6.2 ----
    int pwFd = -1;
    int streamNodeId = -1; // Start 回來的 capture source node ID（客戶端要 target 它）
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
GVariant* makeSelectDict(bool monitor) {
    GVariantBuilder b;
    g_variant_builder_init(&b, G_VARIANT_TYPE("a{sv}"));
    std::string ht = makeToken("req");
    g_variant_builder_add(&b, "{sv}", "handle_token", g_variant_new_string(ht.c_str()));
    uint32_t types = monitor ? 1u : 2u; // 1 = MONITOR, 2 = WINDOW
    g_variant_builder_add(&b, "{sv}", "types", g_variant_new_uint32(types));
    return g_variant_builder_end(&b);
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
                            struct dma_buf_sync sync{0}; // flags=0：讀用，無需同步
                            (void) ioctl(d->fd, DMA_BUF_IOCTL_SYNC, &sync);
                        }
                        if (d->fd >= 0 && d->maxsize > 0) {
                            void* m = mmap(nullptr, d->maxsize, PROT_READ, MAP_SHARED, d->fd, 0);
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
                    // streams = a(ua{sv})；第 0 個元素 = (u node_id, a{sv} meta)
                    if (g_variant_n_children(inner) > 0) {
                        GVariant* elem = g_variant_get_child_value(inner, 0); // (u, a{sv})
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
                       : (m == "SelectSources") ? makeSelectDict(monitor)
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
    GVariant* ret = g_dbus_connection_call_sync(im.conn, DEST, PATH, IFACE, method,
        params, G_VARIANT_TYPE("(o)"), G_DBUS_CALL_FLAGS_NONE, (guint32) timeoutMs, nullptr, &e);
    g_variant_unref(params);
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

    // 3. Start（obs 驗證過的流程：註冊 capture source）
    std::string emptyParent;
    portalAsync(im, "Start", &im.sessionHandle, &emptyParent, "streams", 60000);

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
        g_variant_unref(params);
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
    // 本機不當 driver（source 是 node.driver 的 clock source、會自己 push 幀）。
    // 加 DRIVER flag 會造成兩個 driver 衝突、兩節點都 suspended。用 NONE。
    if (pw_stream_connect(im.stream, PW_DIRECTION_INPUT, targetId, PW_STREAM_FLAG_NONE, nullptr, 0) < 0)
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
            g_variant_unref(params);
            g_clear_error(&e);
        }
        g_object_unref(im.conn);
        im.conn = nullptr;
    }
}

} // namespace screenfg
```
- [ ] **Step 3: Commit (code only; it compiles in Task 10)**

```bash
cd fgvk && git add screen-fg/src/capture.hpp screen-fg/src/capture.cpp && git commit -m "feat(screen-fg): portal ScreenCast v5 + PipeWire capture adapter (Q5/Q9)"
```

---

## Task 9: `present.{hpp,cpp}` (I/O adapter — SDL3 + Vulkan FIFO swapchain)

> I/O adapter (not unit-tested): SDL3 borderless full-screen window + Vulkan 1.2 FIFO swapchain, CPU-uploaded BGRA → staging → blit-scale → optional HUD → present. Compiled in Task 10, verified end-to-end in Task 11 (synthetic) and Task 12 (real). **Do not add to a CMake target yet.**

### Vulkan pitfalls (Mesa 26.0.8 radv + SDL3 + this host's TRIMMED vulkan.h) — bake them in
- **`VkImageMemoryBarrier.image` MUST be set.** Zero-init → `VK_NULL_HANDLE` → radv derefs the null image at record and segfaults. `VkImageSubresourceRange` has **no** `image` member (it lives on the barrier itself). Set `.image` on BOTH the staging `DST→SRC` and `SRC→DST` barriers.
- **Swapchain `imageUsage` must include `TRANSFER_SRC | TRANSFER_DST`.** The blit writes the swapchain image (`TRANSFER_DST`); the `lsfg-vk` layer reads the presented frame (`TRANSFER_SRC`). `COLOR_ATTACHMENT` alone → illegal GPU op.
- **`vkQueuePresentKHR` crash root cause = `VkPresentInfoKHR.pSwapchains` left null** (with `swapchainCount=1`). Set `pi.pSwapchains = &im.swap` or radv crashes in the WSI per-image loop.
- **`vkGetSwapchainImagesKHR` must be called TWICE (fill version):** first `(..., &nImg, nullptr)` for the count, then `(..., &nImg, imgs.data())` to fill. A missing fill call leaves `swapImages[i]` null → `vkCreateImageView` derefs null.
- **Vulkan semaphores are ONE-SHOT.** The `acquireSem` signalled by `vkAcquireNextImageKHR` is consumed by the submit `wait`; it **cannot** also be `wait`ed by present (permanently deadlocks, even SIGTERM can't break the blocking Vulkan call). Flow: submit waits `acquireSem` + signals `readySem`; present waits `readySem` only.
- **`shutdown()` must be idempotent.** Both `~Presenter()` and `main()` call it; after destroying, reset `im.dev/inst/window` to null, else a second destroy of a dead device → `vkDeviceWaitIdle: Invalid device`.
- **This host's `vulkan.h` is trimmed** — several signatures differ from upstream. Use exactly: `vkResetCommandBuffer(cmd, flags)` (**no device arg**); `VkColorSpaceKHR` is an enum; `vkCmdCopyBufferToImage`'s 4th arg is `dstImageLayout`; `VK_QUEUE_PRESENT_BIT` is undeclared (not needed here); `VkSubmitInfo` has **no** `pFences`; `VkImageSubresourceRange` has no `image` member.

- [ ] **Step 1: Write `screen-fg/src/present.hpp`**

```cpp
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
```
- [ ] **Step 2: Write `screen-fg/src/present.cpp`**

```cpp
#include "present.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace screenfg {

namespace {

#define VK_CHECK(x)                                                        \
    do {                                                                   \
        VkResult _r = (x);                                                 \
        if (_r != VK_SUCCESS)                                              \
            throw std::runtime_error(std::string(#x) + " 失敗 (VkResult " + \
                                     std::to_string(_r) + ")");            \
    } while (0)

// ---------- 5x7 bitmap font ----------
struct FontChar {
    const char* rows[7];
};
const FontChar kFont[] = {
    // 0: ' '
    {".....", ".....", ".....", ".....", ".....", ".....", "....."},
    // 1: '0'
    {".###.", "#...#", "#..##", "#.#.#", "##...#", "#...#", ".###."},
    // 2: '1'
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."},
    // 3: '2'
    {".###.", "#...#", "....#", ".##..", "#....", "#....", "#####"},
    // 4: '3'
    {".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."},
    // 5: '4'
    {"....#", "...##", "..###", ".####", "#####", "....#", "....#"},
    // 6: '5'
    {"#####", "#....", "#....", "####.", "....#", "....#", ".###."},
    // 7: '6'
    {"..##.", ".#...", "#....", "###..", "#...#", "#...#", ".###."},
    // 8: '7'
    {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."},
    // 9: '8'
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},
    // 10: '9'
    {".###.", "#...#", "#...#", ".####", "....#", "#...#", ".###."},
    // 11: ':'
    {".....", ".....", "..#..", ".....", "..#..", ".....", "....."},
    // 12: '.'
    {".....", ".....", ".....", ".....", ".....", "..#..", "..#.."},
    // 13: 'A'
    {".###.", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
    // 14: 'B'
    {"####.", "#...#", "#...#", "####.", "#...#", "#...#", "####."},
    // 15: 'D'
    {"###..", "#..#.", "#...#", "#...#", "#...#", "#..#.", "###.."},
    // 16: 'E'
    {"#####", "#....", "#....", "####.", "#....", "#....", "#####"},
    // 17: 'F'
    {"#####", "#....", "#....", "####.", "#....", "#....", "#...."},
    // 18: 'H'
    {"#...#", "#...#", "#...#", "#####", "#...#", "#...#", "#...#"},
    // 19: 'I'
    {".###.", "..#..", "..#..", "..#..", "..#..", "..#..", ".###."},
    // 20: 'J'
    {"..###", "...#.", "...#.", "...#.", "...#.", "#..#.", ".##.."},
    // 21: 'K'
    {"#...#", "#..#.", "#.#..", "##...", "#.#..", "#..#.", "#...#"},
    // 22: 'L'
    {"#....", "#....", "#....", "#....", "#....", "#....", "#####"},
    // 23: 'M'
    {"#...#", "##.##", "#.#.#", "#.#.#", "#...#", "#...#", "#...#"},
    // 24: 'N'
    {"#...#", "##..#", "#.#.#", "#..##", "#...#", "#...#", "#...#"},
    // 25: 'O'
    {".###.", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."},
    // 26: 'P'
    {"####.", "#...#", "#...#", "####.", "#....", "#....", "#...."},
    // 27: 'R'
    {"####.", "#...#", "#...#", "####.", "#.#..", "#..#.", "#...#"},
    // 28: 'S'
    {".####", "#....", "#....", ".###.", "....#", "....#", "####."},
    // 29: 'T'
    {"#####", "..#..", "..#..", "..#..", "..#..", "..#..", "..#.."},
    // 30: 'U'
    {"#...#", "#...#", "#...#", "#...#", "#...#", "#...#", ".###."},
    // 31: 'V'
    {"#...#", "#...#", "#...#", "#...#", ".#.#.", "..#..", "..#.."},
    // 32: 'W'
    {"#...#", "#...#", "#...#", "#.#.#", "#.#.#", "##.##", "#...#"},
    // 33: 'X' / 'x'
    {"#...#", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#...#"},
    // 34: 'Y'
    {"#...#", "#...#", ".#.#.", "..#..", "..#..", "..#..", "..#.."},
    // 35: 'C'
    {".###.", "#....", "#....", "#....", "#....", "#....", ".###."},
    // 36: 'G'
    {".###.", "#...#", "#....", "#.###", "#...#", "#...#", ".###."},
    // 37: 'Q'
    {".###.", "#...#", "#...#", "#.#.#", "#..#.", "#...#", ".#.#."},
    // 38: 'Z'
    {"#####", "....#", "...#.", "..#..", ".#...", "#....", "#####"},
    // 39: '%'
    {"##.##", "##.##", "....#", "...#.", "..#..", "#.##.", "#.##."},
};

int fontIndex(char c) {
    if (c >= 'a' && c <= 'z')
        c = (char) (c - 'a' + 'A');
    if (c == ' ')
        return 0;
    if (c >= '0' && c <= '9')
        return 1 + (c - '0');
    switch (c) {
    case ':': return 11;
    case '.': return 12;
    case 'A': return 13;
    case 'B': return 14;
    case 'C': return 35;
    case 'D': return 15;
    case 'E': return 16;
    case 'F': return 17;
    case 'G': return 36;
    case 'H': return 18;
    case 'I': return 19;
    case 'J': return 20;
    case 'K': return 21;
    case 'L': return 22;
    case 'M': return 23;
    case 'N': return 24;
    case 'O': return 25;
    case 'P': return 26;
    case 'Q': return 37;
    case 'R': return 27;
    case 'S': return 28;
    case 'T': return 29;
    case 'U': return 30;
    case 'V': return 31;
    case 'W': return 32;
    case 'X': return 33;
    case 'Y': return 34;
    case 'Z': return 38;
    case '%': return 39;
    default: return 0;
    }
}

constexpr int kScale = 2;
constexpr int kHudW = 512;
constexpr int kHudH = 64;

// ---------- PCI bus ID → DRM card index ----------
std::optional<int> pciToCardIndex(const std::string& pciId) {
    std::string link = "/sys/bus/pci/devices/" + pciId + "/drm";
    char buf[4096];
    ssize_t n = readlink(link.c_str(), buf, sizeof buf - 1);
    if (n <= 0)
        return std::nullopt;
    buf[n] = 0;
    std::string s(buf);
    auto pos = s.rfind("card");
    if (pos == std::string::npos)
        return std::nullopt;
    int idx = atoi(s.c_str() + pos + 4);
    if (idx < 0)
        return std::nullopt;
    return idx;
}

int readIntFile(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const char* p = s.c_str();
    while (*p && !isdigit((unsigned char) *p))
        ++p; // 跳過 "0x" 前綴
    return (int) strtol(p, nullptr, 16);
}

uint32_t pickMemType(const VkMemoryRequirements& mr, const VkPhysicalDeviceMemoryProperties& dmr) {
    for (uint32_t i = 0; i < dmr.memoryTypeCount; ++i)
        if (mr.memoryTypeBits & (1u << i))
            return i;
    return 0;
}

} // namespace

struct Presenter::Impl {
    SDL_Window* window = nullptr;
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice physDev = VK_NULL_HANDLE;
    VkDevice dev = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swap = VK_NULL_HANDLE;
    std::vector<VkImage> swapImages;
    std::vector<VkImageView> swapViews;
    VkFormat swapFmt = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    uint32_t w = 0, h = 0;
    VkSemaphore acquireSem = VK_NULL_HANDLE;
    VkSemaphore readySem = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    // 持久 command pool / buffer（每帧只 reset，不新建——新建 pool 每帧會讓 lsfg-vk layer 崩）
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    bool hudOn = true;
    VkImage hudImage = VK_NULL_HANDLE;
    VkImageView hudView = VK_NULL_HANDLE;
    VkDeviceMemory hudImageMem = VK_NULL_HANDLE;
    VkBuffer hudStaging = VK_NULL_HANDLE;
    VkDeviceMemory hudStagingMem = VK_NULL_HANDLE;
    std::vector<uint8_t> hudPixels;
    std::string lastHudText;
    bool hudDirty = false; // setHudText 寫入 staging buffer 後設 true → 下次 present 才上傳到 hudImage
    uint32_t displayHz_ = 0;
    // CPU 上傳用的持久 staging（frame → buffer → image）
    VkBuffer frameStageBuf = VK_NULL_HANDLE;
    VkDeviceMemory frameStageBufMem = VK_NULL_HANDLE;
    VkImage frameStageImg = VK_NULL_HANDLE;
    VkDeviceMemory frameStageImgMem = VK_NULL_HANDLE;
    uint32_t frameStageW = 0, frameStageH = 0;
    VkImageLayout frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
};

// 延遲建立 CPU 上傳用的持久 staging（buffer + image），尺寸 = swapchain（固定）
void Presenter::ensureFrameStaging(Impl& im) {
    uint32_t w = im.w, h = im.h;
    if (im.frameStageImg != VK_NULL_HANDLE && im.frameStageW == w && im.frameStageH == h)
        return;
    if (im.frameStageImg != VK_NULL_HANDLE) {
        vkDestroyImage(im.dev, im.frameStageImg, nullptr);
        im.frameStageImg = VK_NULL_HANDLE;
    }
    if (im.frameStageImgMem != VK_NULL_HANDLE) {
        vkFreeMemory(im.dev, im.frameStageImgMem, nullptr);
        im.frameStageImgMem = VK_NULL_HANDLE;
    }
    if (im.frameStageBuf != VK_NULL_HANDLE) {
        vkDestroyBuffer(im.dev, im.frameStageBuf, nullptr);
        im.frameStageBuf = VK_NULL_HANDLE;
    }
    if (im.frameStageBufMem != VK_NULL_HANDLE) {
        vkFreeMemory(im.dev, im.frameStageBufMem, nullptr);
        im.frameStageBufMem = VK_NULL_HANDLE;
    }
    size_t bytes = (size_t) w * h * 4;
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VK_CHECK(vkCreateBuffer(im.dev, &bci, nullptr, &im.frameStageBuf));
    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(im.dev, im.frameStageBuf, &mr);
    VkPhysicalDeviceMemoryProperties dmr;
    vkGetPhysicalDeviceMemoryProperties(im.physDev, &dmr);
    uint32_t memType = 0;
    for (uint32_t i = 0; i < dmr.memoryTypeCount; ++i)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (dmr.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            memType = i;
            break;
        }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memType;
    VK_CHECK(vkAllocateMemory(im.dev, &mai, nullptr, &im.frameStageBufMem));
    VK_CHECK(vkBindBufferMemory(im.dev, im.frameStageBuf, im.frameStageBufMem, 0));
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_B8G8R8A8_UNORM;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    VK_CHECK(vkCreateImage(im.dev, &ici, nullptr, &im.frameStageImg));
    VkMemoryRequirements imr;
    vkGetImageMemoryRequirements(im.dev, im.frameStageImg, &imr);
    VkMemoryAllocateInfo imai{};
    imai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imai.allocationSize = imr.size;
    imai.memoryTypeIndex = pickMemType(imr, dmr);
    VK_CHECK(vkAllocateMemory(im.dev, &imai, nullptr, &im.frameStageImgMem));
    VK_CHECK(vkBindImageMemory(im.dev, im.frameStageImg, im.frameStageImgMem, 0));
    im.frameStageW = w;
    im.frameStageH = h;
    im.frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}

Presenter::Presenter() : impl_(new Impl) {}
Presenter::~Presenter() { shutdown(); }
uint32_t Presenter::displayHz() const { return impl_ ? impl_->displayHz_ : 0; }

void Presenter::init(const PresentParams& p) {
    auto& im = *impl_;
    im.hudOn = p.hud;

    if (!SDL_Init(SDL_INIT_VIDEO))
        throw std::runtime_error(std::string("SDL_Init 失敗: ") + SDL_GetError());
    if (!SDL_Vulkan_LoadLibrary(nullptr))
        throw std::runtime_error(std::string("SDL_Vulkan_LoadLibrary 失敗: ") + SDL_GetError());

    int nDisplays = 0;
    SDL_DisplayID* displayIds = SDL_GetDisplays(&nDisplays);
    if (!displayIds || nDisplays == 0)
        throw std::runtime_error("找不到任何 display");
    if (p.displayIndex < 0 || p.displayIndex >= nDisplays)
        throw std::runtime_error("display " + std::to_string(p.displayIndex) + " 不存在（共 " + std::to_string(nDisplays) + " 塊）");
    SDL_DisplayID did = displayIds[p.displayIndex];
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(did);
    im.displayHz_ = (uint32_t) mode->refresh_rate;
    SDL_free(displayIds);
    SDL_Rect bounds{};
    SDL_GetDisplayBounds(did, &bounds);
    im.w = (uint32_t) bounds.w;
    im.h = (uint32_t) bounds.h;

    im.window = SDL_CreateWindow("screen-fg", im.w, im.h,
                                SDL_WINDOW_VULKAN);
    if (!im.window)
        throw std::runtime_error(std::string("SDL_CreateWindow 失敗：") + SDL_GetError());

    Uint32 nExt = 0;
    const char* const* extArr = SDL_Vulkan_GetInstanceExtensions(&nExt);
    std::vector<const char*> instExts;
    if (nExt > 0 && extArr)
        instExts.assign(extArr, extArr + nExt);
    VkApplicationInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "screen-fg";
    ai.apiVersion = VK_MAKE_VERSION(1, 2, 0);
    VkInstanceCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = (uint32_t) instExts.size();
    ici.ppEnabledExtensionNames = instExts.empty() ? nullptr : instExts.data();
    VK_CHECK(vkCreateInstance(&ici, nullptr, &im.inst));

    if (!SDL_Vulkan_CreateSurface(im.window, im.inst, nullptr, &im.surface))
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface 失敗: ") + SDL_GetError());

    uint32_t nDev = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(im.inst, &nDev, nullptr));
    std::vector<VkPhysicalDevice> devs(nDev);
    VK_CHECK(vkEnumeratePhysicalDevices(im.inst, &nDev, devs.data()));

    int wantIdx = -1;
    if (p.gpuPciId) {
        auto ci = pciToCardIndex(p.gpuPciId);
        if (!ci)
            throw std::runtime_error(std::string("找不到 PCI ") + p.gpuPciId);
        wantIdx = *ci;
    }

    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < nDev; ++i) {
        if (wantIdx >= 0 && (int) i != wantIdx)
            continue;
        VkPhysicalDeviceProperties prop;
        vkGetPhysicalDeviceProperties(devs[i], &prop);
        uint32_t nQ = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nQ, nullptr);
        std::vector<VkQueueFamilyProperties> qf(nQ);
        vkGetPhysicalDeviceQueueFamilyProperties(devs[i], &nQ, qf.data());
        bool hasGraphics = false;
        for (auto& q : qf)
            if (q.queueFlags & VK_QUEUE_GRAPHICS_BIT)
                hasGraphics = true;
        if (!hasGraphics)
            continue;
        VkBool32 supported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(devs[i], 0, im.surface, &supported);
        if (!supported)
            continue;
        if (wantIdx >= 0 && p.gpuPciId) {
            std::string sysDir = "/sys/bus/pci/devices/" + std::string(p.gpuPciId);
            int sv = readIntFile(sysDir + "/vendor");
            int sd = readIntFile(sysDir + "/device");
            if ((sv & 0xffff) != (prop.vendorID & 0xffff) || (sd & 0xffff) != (prop.deviceID & 0xffff))
                throw std::runtime_error("PCI 對映不準：Vulkan device " + std::to_string(i) + " 的 vendor/device ≠ sysfs");
        }
        chosen = devs[i];
        break;
    }
    if (chosen == VK_NULL_HANDLE)
        throw std::runtime_error("找不到可用 GPU（surface + " + std::string(p.gpuPciId ? p.gpuPciId : "auto") + "）");

    const char* enable[] = {"VK_KHR_swapchain"};

    uint32_t nQ = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &nQ, nullptr);
    std::vector<VkQueueFamilyProperties> qf(nQ);
    vkGetPhysicalDeviceQueueFamilyProperties(chosen, &nQ, qf.data());
    uint32_t qIdx = 0;
    for (uint32_t i = 0; i < nQ; ++i)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            qIdx = i;
            break;
        }
    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = qIdx;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = enable;
    im.physDev = chosen;
    VK_CHECK(vkCreateDevice(chosen, &dci, nullptr, &im.dev));
    vkGetDeviceQueue(im.dev, qIdx, 0, &im.queue);

    uint32_t nFmt = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(chosen, im.surface, &nFmt, nullptr));
    if (nFmt == 0)
        throw std::runtime_error("沒有 surface format");
    std::vector<VkSurfaceFormatKHR> fmts(nFmt);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(chosen, im.surface, &nFmt, fmts.data()));
    static const VkFormat prefer[] = {VK_FORMAT_B8G8R8A8_UNORM,
                                     VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8_UNORM};
    im.swapFmt = fmts[0].format;
    im.colorSpace = fmts[0].colorSpace;
    for (auto want : prefer)
        for (auto& f : fmts)
            if (f.format == want) {
                im.swapFmt = want;
                im.colorSpace = f.colorSpace;
                break;
            }

    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(chosen, im.surface, &caps));
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = im.surface;
    sci.minImageCount = 3;
    sci.imageFormat = im.swapFmt;
    sci.imageColorSpace = im.colorSpace;
    sci.imageExtent = {im.w, im.h};
    sci.imageArrayLayers = 1;
    // 務必含 TRANSFER_DST（blit 寫入 swapchain image）+ TRANSFER_SRC（lsfg-vk layer 捕捉呈現幀要讀）
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    sci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(im.dev, &sci, nullptr, &im.swap));
    uint32_t nImg = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(im.dev, im.swap, &nImg, nullptr));
    im.swapImages.resize(nImg);
    VK_CHECK(vkGetSwapchainImagesKHR(im.dev, im.swap, &nImg, im.swapImages.data()));
    im.swapViews.resize(nImg);
    for (uint32_t i = 0; i < nImg; ++i) {
        VkImageViewCreateInfo ivi{};
        ivi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivi.image = im.swapImages[i];
        ivi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivi.format = im.swapFmt;
        ivi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(im.dev, &ivi, nullptr, &im.swapViews[i]));
    }

    VkSemaphoreCreateInfo sci2{};
    sci2.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(im.dev, &sci2, nullptr, &im.acquireSem));
    VK_CHECK(vkCreateSemaphore(im.dev, &sci2, nullptr, &im.readySem));
    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VK_CHECK(vkCreateFence(im.dev, &fci, nullptr, &im.fence));
    // 讓 fence 初始 signal（第一次 wait 不阻塞）
    vkQueueSubmit(im.queue, 0, nullptr, im.fence);
    VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
    // 持久 command pool + buffer（只建立一次；每帧 reset，不新建）
    VkCommandPoolCreateInfo cpic{};
    cpic.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    VK_CHECK(vkCreateCommandPool(im.dev, &cpic, nullptr, &im.cmdPool));
    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = im.cmdPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(im.dev, &cbai, &im.cmdBuf));

    { // HUD 資源常備（執行中 setHud(on/off) 即生效；init 時無條件建立）
        im.hudPixels.assign((size_t) kHudW * kHudH * 4, 0);
        VkPhysicalDeviceMemoryProperties dmr0;
        vkGetPhysicalDeviceMemoryProperties(im.physDev, &dmr0);
        VkImageCreateInfo hici{};
        hici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        hici.imageType = VK_IMAGE_TYPE_2D;
        hici.format = VK_FORMAT_R8G8B8A8_UNORM;
        hici.extent = {(uint32_t) kHudW, (uint32_t) kHudH, 1};
        hici.mipLevels = 1;
        hici.arrayLayers = 1;
        hici.samples = VK_SAMPLE_COUNT_1_BIT;
        hici.tiling = VK_IMAGE_TILING_OPTIMAL;
        hici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        hici.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
        VK_CHECK(vkCreateImage(im.dev, &hici, nullptr, &im.hudImage));
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(im.dev, im.hudImage, &mr);
        uint32_t memType = pickMemType(mr, dmr0);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = memType;
        VK_CHECK(vkAllocateMemory(im.dev, &mai, nullptr, &im.hudImageMem));
        VK_CHECK(vkBindImageMemory(im.dev, im.hudImage, im.hudImageMem, 0));
        VkImageViewCreateInfo hvi{};
        hvi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        hvi.image = im.hudImage;
        hvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        hvi.format = VK_FORMAT_R8G8B8A8_UNORM;
        hvi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(im.dev, &hvi, nullptr, &im.hudView));
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = (size_t) kHudW * kHudH * 4;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VK_CHECK(vkCreateBuffer(im.dev, &bci, nullptr, &im.hudStaging));
        vkGetBufferMemoryRequirements(im.dev, im.hudStaging, &mr);
        // staging 要能 map → HOST_VISIBLE
        for (uint32_t i = 0; i < dmr0.memoryTypeCount; ++i)
            if ((mr.memoryTypeBits & (1u << i)) && (dmr0.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
                memType = i;
                break;
            }
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = memType;
        VK_CHECK(vkAllocateMemory(im.dev, &mai, nullptr, &im.hudStagingMem));
        VK_CHECK(vkBindBufferMemory(im.dev, im.hudStaging, im.hudStagingMem, 0));
        // 初始Backdrop（半透明黑）填入 staging buffer + 標記 dirty，避免首帧上傳未初始化 garbage
        void* hm = nullptr;
        VK_CHECK(vkMapMemory(im.dev, im.hudStagingMem, 0, im.hudPixels.size(), 0, &hm));
        if (hm) {
            uint8_t* p = (uint8_t*) hm;
            for (size_t i = 0; i < im.hudPixels.size(); i += 4)
                p[i + 3] = 90;
            vkUnmapMemory(im.dev, im.hudStagingMem);
        }
        im.hudDirty = true;
    }

    fprintf(stderr, "[screen-fg] present 就緒（%ux%u @ %u Hz）\n", im.w, im.h, im.displayHz_);
}

void Presenter::presentFrame(const CapturedFrame& f) {
    auto& im = *impl_;
    VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(im.dev, 1, &im.fence));

    uint32_t idx = 0;
    VK_CHECK(vkAcquireNextImageKHR(im.dev, im.swap, 100000000, im.acquireSem, VK_NULL_HANDLE, &idx));

    // 持久 pool / buffer（fence 已於函式頭 wait → 可安全 reset）
    VkCommandBuffer cmdb = im.cmdBuf;
    VK_CHECK(vkResetCommandBuffer(cmdb, 0));
    VkCommandBufferBeginInfo cbi{};
    cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VK_CHECK(vkBeginCommandBuffer(cmdb, &cbi));

    bool ok = false;
    if (f.data && f.width > 0 && f.height > 0) {
        ensureFrameStaging(im);
        // 上幀留下的 staging layout 是 SRC → 先切回 DST
        VkImageSubresourceRange rr{};
        rr.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        rr.levelCount = 1;
        rr.layerCount = 1;
        if (im.frameStageLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
            VkImageMemoryBarrier rb{};
            rb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            rb.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            rb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            rb.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            rb.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            rb.image = im.frameStageImg;
            rb.subresourceRange = rr;
            vkCmdPipelineBarrier(cmdb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &rb);
            im.frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        // CPU 上傳：最近鄰把 f（f.width×f.height）縮放到 staging（im.w×im.h）
        size_t rowBytes = (size_t) im.w * 4;
        void* m = nullptr;
        VK_CHECK(vkMapMemory(im.dev, im.frameStageBufMem, 0, rowBytes * im.h, 0, &m));
        if (m) {
            uint8_t* dst = (uint8_t*) m;
            const uint32_t sw = f.width, sh = f.height;
            for (uint32_t y = 0; y < im.h; ++y) {
                uint32_t sy = (uint32_t)(((uint64_t) y * sh) / im.h);
                if (sy >= sh) sy = sh - 1;
                const uint8_t* srcRow = f.data + (size_t) sy * (size_t) sw * 4;
                uint8_t* drow = dst + (size_t) y * rowBytes;
                for (uint32_t x = 0; x < im.w; ++x) {
                    uint32_t sx = (uint32_t)(((uint64_t) x * sw) / im.w);
                    if (sx >= sw) sx = sw - 1;
                    memcpy(drow + (size_t) x * 4, srcRow + (size_t) sx * 4, 4);
                }
            }
            vkUnmapMemory(im.dev, im.frameStageBufMem);
            // buffer → staging image
            VkBufferImageCopy copy{};
            copy.bufferOffset = 0;
            copy.bufferRowLength = 0; // 0 = tightly packed
            copy.bufferImageHeight = im.h;
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.mipLevel = 0;
            copy.imageSubresource.baseArrayLayer = 0;
            copy.imageSubresource.layerCount = 1;
            copy.imageOffset = {0, 0, 0};
            copy.imageExtent = {im.w, im.h, 1};
            vkCmdCopyBufferToImage(cmdb, im.frameStageBuf, im.frameStageImg,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
            // staging layout：DST → SRC（供 blit）
            VkImageMemoryBarrier ib{};
            ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            ib.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            ib.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            ib.image = im.frameStageImg;
            ib.subresourceRange = rr;
            vkCmdPipelineBarrier(cmdb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &ib);
            im.frameStageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            // 1:1 blit staging → swapchain（兩者皆 im.w×im.h）
            VkImageSubresourceLayers sub{};
            sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            sub.baseArrayLayer = 0;
            sub.layerCount = 1;
            VkImageBlit blit{};
            blit.srcSubresource = sub;
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {(int32_t) im.w, (int32_t) im.h, 1};
            blit.dstSubresource = sub;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {(int32_t) im.w, (int32_t) im.h, 1};
            vkCmdBlitImage(cmdb, im.frameStageImg, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           im.swapImages[idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, &blit, VK_FILTER_NEAREST);
            ok = true;
        }
    }

    if (ok && im.hudOn && im.hudImage != VK_NULL_HANDLE) {
        // setHudText 寫入 staging buffer 後，在此上傳到 GPU image（與後續 blit 同一 cmd buffer → 順序保證）
        if (im.hudDirty) {
            VkBufferImageCopy hcopy{};
            hcopy.bufferOffset = 0;
            hcopy.bufferRowLength = 0; // 0 = tightly packed
            hcopy.bufferImageHeight = kHudH;
            hcopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            hcopy.imageSubresource.mipLevel = 0;
            hcopy.imageSubresource.baseArrayLayer = 0;
            hcopy.imageSubresource.layerCount = 1;
            hcopy.imageOffset = {0, 0, 0};
            hcopy.imageExtent = {kHudW, kHudH, 1};
            vkCmdCopyBufferToImage(cmdb, im.hudStaging, im.hudImage,
                                   VK_IMAGE_LAYOUT_GENERAL, 1, &hcopy);
            im.hudDirty = false;
        }
        VkImageSubresourceLayers hs{};
        hs.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        hs.baseArrayLayer = 0;
        hs.layerCount = 1;
        VkImageBlit hblit{};
        hblit.srcSubresource = hs;
        hblit.srcOffsets[0] = {0, 0, 0};
        hblit.srcOffsets[1] = {kHudW, kHudH, 1};
        hblit.dstSubresource = hs;
        hblit.dstOffsets[0] = {(int32_t) im.w - kHudW - 20, (int32_t) im.h - kHudH - 20, 0};
        hblit.dstOffsets[1] = {(int32_t) im.w - 20, (int32_t) im.h - 20, 1};
        vkCmdBlitImage(cmdb, im.hudImage, VK_IMAGE_LAYOUT_GENERAL,
                       im.swapImages[idx], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, 1, &hblit, VK_FILTER_LINEAR);
    }

    VK_CHECK(vkEndCommandBuffer(cmdb));

    if (!ok) {
        // 無可呈現幀：空 submit（消耗 acquireSem 避免下帧 double-signal）讓 fence 照樣 signal
        VkPipelineStageFlags wst = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.pWaitSemaphores = &im.acquireSem;
        si.waitSemaphoreCount = 1;
        si.pWaitDstStageMask = &wst;
        VK_CHECK(vkQueueSubmit(im.queue, 1, &si, im.fence));
        VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
        return;
    }

    VkPipelineStageFlags wstage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.pWaitSemaphores = &im.acquireSem;
    si.waitSemaphoreCount = 1;
    si.pWaitDstStageMask = &wstage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmdb;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &im.readySem;
    VK_CHECK(vkQueueSubmit(im.queue, 1, &si, im.fence));

    // present（只 wait readySem；acquireSem 已被 submit 消耗，不可再 wait → 死鎖）
    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &im.readySem;
    pi.swapchainCount = 1;
    pi.pSwapchains = &im.swap;
    uint32_t i2 = idx;
    pi.pImageIndices = &i2;
    VK_CHECK(vkQueuePresentKHR(im.queue, &pi));

    // 等 submit 完成
    VK_CHECK(vkWaitForFences(im.dev, 1, &im.fence, VK_TRUE, UINT64_MAX));
}

void Presenter::setHudText(const std::string& text) {
    auto& im = *impl_;
    if (!im.hudOn || im.hudStaging == VK_NULL_HANDLE || text == im.lastHudText)
        return;
    im.lastHudText = text;
    std::fill(im.hudPixels.begin(), im.hudPixels.end(), 0);
    for (size_t i = 0; i < im.hudPixels.size(); i += 4)
        im.hudPixels[i + 3] = 90; // 半透明黑底
    int scale = kScale;
    int x0 = 6, y0 = 6;
    for (size_t i = 0; i < text.size(); ++i) {
        int fi = fontIndex(text[i]);
        for (int r = 0; r < 7; ++r)
            for (int c = 0; c < 5; ++c) {
                if (kFont[fi].rows[r][c] != '#')
                    continue;
                for (int sy = 0; sy < scale; ++sy)
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = x0 + (int) i * 6 * scale + c * scale + sx;
                        int py = y0 + r * scale + sy;
                        if (px >= kHudW || py >= kHudH)
                            continue;
                        size_t off = ((size_t) py * kHudW + px) * 4;
                        im.hudPixels[off] = 255;
                        im.hudPixels[off + 1] = 255;
                        im.hudPixels[off + 2] = 255;
                        im.hudPixels[off + 3] = 255;
                    }
            }
    }
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(im.dev, im.hudStagingMem, 0, im.hudPixels.size(), 0, &mapped));
    memcpy(mapped, im.hudPixels.data(), im.hudPixels.size());
    vkUnmapMemory(im.dev, im.hudStagingMem);
    im.hudDirty = true; // 文字已寫入 staging buffer，等下次 present 上傳到 GPU image
}

void Presenter::setHud(bool on) {
    auto& im = *impl_;
    im.hudOn = on;
}

void Presenter::shutdown() {
    auto& im = *impl_;
    if (im.dev) {
        vkDeviceWaitIdle(im.dev);
        if (im.frameStageImg)
            vkDestroyImage(im.dev, im.frameStageImg, nullptr);
        if (im.frameStageImgMem)
            vkFreeMemory(im.dev, im.frameStageImgMem, nullptr);
        if (im.frameStageBuf)
            vkDestroyBuffer(im.dev, im.frameStageBuf, nullptr);
        if (im.frameStageBufMem)
            vkFreeMemory(im.dev, im.frameStageBufMem, nullptr);
        if (im.hudStaging)
            vkDestroyBuffer(im.dev, im.hudStaging, nullptr);
        if (im.hudStagingMem)
            vkFreeMemory(im.dev, im.hudStagingMem, nullptr);
        if (im.hudImage)
            vkDestroyImage(im.dev, im.hudImage, nullptr);
        if (im.hudImageMem)
            vkFreeMemory(im.dev, im.hudImageMem, nullptr);
        if (im.hudView)
            vkDestroyImageView(im.dev, im.hudView, nullptr);
        for (auto v : im.swapViews)
            vkDestroyImageView(im.dev, v, nullptr);
        if (im.swap)
            vkDestroySwapchainKHR(im.dev, im.swap, nullptr);
        if (im.acquireSem)
            vkDestroySemaphore(im.dev, im.acquireSem, nullptr);
        if (im.readySem)
            vkDestroySemaphore(im.dev, im.readySem, nullptr);
        if (im.fence)
            vkDestroyFence(im.dev, im.fence, nullptr);
        if (im.cmdBuf)
            vkFreeCommandBuffers(im.dev, im.cmdPool, 1, &im.cmdBuf);
        if (im.cmdPool)
            vkDestroyCommandPool(im.dev, im.cmdPool, nullptr);
        vkDestroyDevice(im.dev, nullptr);
        im.dev = VK_NULL_HANDLE;
    }
    if (im.inst) {
        vkDestroyInstance(im.inst, nullptr);
        im.inst = VK_NULL_HANDLE;
    }
    if (im.window) {
        SDL_DestroyWindow(im.window);
        im.window = nullptr;
        im.window = nullptr;
    }
    SDL_Vulkan_UnloadLibrary();
    SDL_Quit();
}

} // namespace screenfg
```
- [ ] **Step 3: Commit (code only; it compiles in Task 10)**

```bash
cd fgvk && git add screen-fg/src/present.hpp screen-fg/src/present.cpp && git commit -m "feat(screen-fg): SDL3 fullscreen + Vulkan FIFO presenter + HUD (Q1/Q3/Q7)"
```

---

## Task 10: `main.cpp` + final `CMakeLists.txt` → build the binary

> This task wires the last screen-fg piece (main) and **replaces** `screen-fg/CMakeLists.txt` with the complete final file (it subsumes every incremental edit from Tasks 2–7). After it, `cmake --build build` produces both `screen-fg` (binary, links PW/SDL/Vulkan/GIO) and `screen-fg-tests` (pure).

**Files:**
- Create: `screen-fg/src/main.cpp`
- Replace: `screen-fg/CMakeLists.txt` (full final version)

### Startup order (main.cpp) — keep it exact
1. `ensurePlainCopy` (re-`cp` `screen-fg-plain` **every boot** — a stale copy after a rebuild has caused pause re-exec to run the old binary) → parse `--config` → `loadConfig` (toml + env).
2. `--passthrough` → immediate re-exec to the plain copy (boot does **not** set the HUD env).
3. Frame source: `SCREENFG_SYNTHETIC` non-`"0"` → `SyntheticSource(1280,720,N)`; else `Capture` (`setMonitorMode(captureMode=="monitor")`). `start()` **blocks until the user picks a window**; on throw → emit exit JSON, return 1.
4. `Presenter.init` (fullscreen + swapchain + GPU); on throw → `source->stop()` + exit JSON + return 1.
5. Clamp (`maxMult = floor(hz/60)`; `contentFpsCap < 60` → re-clamp + warn) + startup health checks (layer enumerated? `active_in` has `"screen-fg"`? free VRAM ≥ 2048 MB?) — **warn, never silently degrade**.

### Main loop (each iteration, in order)
1. 1 Hz status heartbeat (`emitStatus`) — independent of whether frames arrived (stable GUI updates).
2. `pollStdinCmd` (non-blocking: poll fd 0, accumulate full lines, strip `\r`): `quit` → exit; `pause`/`resume` → re-exec **only on a real state change** (same-state = no-op); `hud 1|0` → `presenter.setHud` (immediate, no restart).
3. `SDL_WaitEventTimeout(200ms)`: `SDL_EVENT_QUIT` / `SDLK_ESCAPE` → exit; `SDLK_P` → Toggle re-exec.
4. `source->poll()` (keeps DBus healthy; PipeWire runs on its own thread) → `isDead()` (synthetic finished / source window closed, Q9) → exit.
5. `nextFrame()` (nullopt → continue) → fps (1 s window) → `frameCount++` → `dedup.isDifferent` → if different: `presentCount++` + update HUD text + `presentFrame`.
6. Exit cleanup: `exiting` status → stderr `結束（N 帧捕捉 / M 帧呈現）` → `source->stop()` → `presenter.shutdown()` → exit JSON → return 0.

### PipeWire init guard
`pw_init` **must** run before `pw_main_loop_new` (else `pw_main_loop_new` can't load the `support.system` handle → "No such file or directory"). Use an RAII `PwInitGuard` so `pw_deinit` runs on every return path (including error returns).

- [ ] **Step 1: Write `screen-fg/src/main.cpp`**

```cpp
#include "capture.hpp"
#include "clamp.hpp"
#include "config.hpp"
#include "dedup.hpp"
#include "frame_source.hpp"
#include "present.hpp"
#include "reexec.hpp"
#include "synthetic.hpp"

#include "shared/protocol.hpp"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include <sys/stat.h>

#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

using namespace screenfg;

namespace {

// ---------- health check ----------
bool layerEnumerated() {
    uint32_t n = 0;
    if (vkEnumerateInstanceLayerProperties(&n, nullptr) != VK_SUCCESS || n == 0)
        return false;
    std::vector<VkLayerProperties> props(n);
    if (vkEnumerateInstanceLayerProperties(&n, props.data()) != VK_SUCCESS)
        return false;
    for (auto& p : props)
        if (strcmp(p.layerName, "VK_LAYER_LSFGVK_frame_generation") == 0)
            return true;
    return false;
}

// conf.toml 裡有沒有 active_in 包含 "screen-fg" 的 profile（FG 才會啟動）
bool confHasActiveScreenFg() {
    const char* home = getenv("HOME");
    if (!home)
        return false;
    std::ifstream f(std::string(home) + "/.config/lsfg-vk/conf.toml");
    if (!f)
        return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.find("active_in") != std::string::npos &&
            line.find("\"screen-fg\"") != std::string::npos)
            return true;
    }
    return false;
}

uint64_t readNumFile(const std::string& path) {
    std::ifstream f(path);
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return strtoull(s.c_str(), nullptr, 10);
}

// ---------- HUD ----------
char hudBuf[96];
void makeHud(char* buf, size_t n, int fps, uint32_t mult, const char* profile, uint32_t hz, bool paused) {
    if (paused)
        snprintf(buf, n, "PAUSED  FPS %d  %dHz", fps, hz);
    else
        snprintf(buf, n, "FPS %d  X%d  %s  %dHz", fps, mult, profile, hz);
}

// ---------- GUI stdio channel ----------
// stdout = status JSON（只有 JSON 行，log 全走 stderr）；stdin = 控制命令（行式）
void writeAll(int fd, const char* b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, b + off, n - off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break; // EPIPE 等：GUI 已關閉
        }
        off += (size_t) r;
    }
}

// 發端：protocol::emit* 產整行 bytes（純），本 transport 負責寫（含 EPIPE/EINTR 處理）。
void emitStatus(int fps, uint32_t mult, bool layer, const char* state) {
    std::string s = protocol::emitStatus(fps, mult, layer, state);
    writeAll(STDOUT_FILENO, s.data(), s.size());
}

void emitExit(int code) {
    std::string s = protocol::emitExit(code);
    writeAll(STDOUT_FILENO, s.data(), s.size());
}

struct CtrlCmd { std::string name; std::string arg; };

// 非阻擋：poll stdin（fd 0），累積完整行，回傳一個命令（無則 nullopt）
std::optional<CtrlCmd> pollStdinCmd() {
    static std::string pending;
    struct pollfd p;
    p.fd = 0;
    p.events = POLLIN;
    p.revents = 0;
    if (poll(&p, 1, 0) > 0) {
        char buf[256];
        ssize_t r = read(0, buf, sizeof buf);
        if (r > 0)
            pending.append(buf, (size_t) r);
    }
    size_t nl;
    while ((nl = pending.find('\n')) != std::string::npos) {
        std::string cmd = pending.substr(0, nl);
        pending.erase(0, nl + 1);
        if (!cmd.empty() && cmd.back() == '\r')
            cmd.pop_back();
        if (cmd.empty())
            continue;
        CtrlCmd out;
        size_t sp = cmd.find(' ');
        if (sp == std::string::npos)
            out.name = cmd;
        else {
            out.name = cmd.substr(0, sp);
            out.arg = cmd.substr(sp + 1);
        }
        for (auto& c : out.name)
            c = (char) tolower((unsigned char) c);
        return out;
    }
    return std::nullopt;
}

} // namespace

// pww_init 必須在 pw_main_loop_new 之前呼叫（實測：缺了它 pw_main_loop_new 載不動
// support.system handle → "No such file or directory"；pw-dump 等工具就是靠它）。
// RAII guard 保證每個 return 路徑都跑 pw_deinit（含 error return）。
struct PwInitGuard {
    explicit PwInitGuard(int* a, char*** v) { pw_init(a, v); }
    ~PwInitGuard() { pw_deinit(); }
};

int main(int argc, char** argv) {
    std::string selfP = selfPath();
    ensurePlainCopy(selfP);
    PwInitGuard pwGuard(&argc, &argv);

    // 1. config（--config <path> 可選；否則用預設路徑）；
    //    resolveConfig 把 toml + env（HUD/DISPLAY/STATE）一次性 resolve 成最終設定 + paused
    std::string cfgPath;
    for (int i = 1; i + 1 < argc; ++i)
        if (strcmp(argv[i], "--config") == 0) {
            cfgPath = argv[i + 1];
            break;
        }
    if (cfgPath.empty()) {
        const char* home = getenv("HOME");
        cfgPath = std::string(home ? home : "") + "/.config/screen-fg/config.toml";
    }
    ResolvedConfig rc = loadConfig(cfgPath);
    Config& cfg = rc.config;
    bool hudOn = cfg.hud; // env（SCREENFG_HUD）已在 resolveConfig resolve；跨 re-exec 保留運行期 HUD
    bool paused = rc.paused; // 來自 SCREENFG_STATE == "paused"

    // --passthrough：立即 re-exec 成 plain 複製本（無 FG 的純呈現）；boot 不設 HUD env
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--passthrough") == 0)
            reexec(resolveReexec(false, ReexecAction::PassthroughBoot, std::nullopt), argc, argv, selfP);
    }
    if (paused)
        std::cerr << "[screen-fg] passthrough 模式（layer 已 unload，無插幀）\n";

    // 2. frame 來源：預設走 portal 捕捉（先跑 picker，別被全螢幕視窗蓋住）；
    //    SCREENFG_SYNTHETIC=1 → 合成幀（不需 portal/picker，用於驗證呈現管線 + layer）
    const char* synEnv = getenv("SCREENFG_SYNTHETIC");
    const bool synthetic = synEnv && synEnv[0] != '0' && synEnv[0] != '\0';
    uint32_t synFrames = 300;
    if (synthetic) {
        const char* n = getenv("SCREENFG_SYNTHETIC_FRAMES");
        if (n && n[0])
            synFrames = static_cast<uint32_t>(strtoul(n, nullptr, 10));
        std::cerr << "[screen-fg] synthetic 模式（合成 " << synFrames
                  << " 帧，驗證呈現管線 + layer）\n";
    }
    std::unique_ptr<FrameSource> source;
    if (synthetic)
        source = std::make_unique<SyntheticSource>(1280, 720, synFrames);
    else {
        auto cap = std::make_unique<Capture>();
        cap->setMonitorMode(cfg.captureMode == "monitor");
        source = std::move(cap);
    }
    try {
        source->start();
    } catch (const std::exception& e) {
        std::cerr << "[screen-fg] 來源啟動失敗：" << e.what() << "\n";
        emitExit(1);
        return 1;
    }

    // 3. 呈現（視窗 + swapchain + GPU 選擇）
    Presenter presenter;
    PresentParams pp;
    pp.hud = hudOn;
    pp.displayIndex = cfg.displayIndex; // env（SCREENFG_DISPLAY）已在 resolveConfig resolve
    pp.gpuPciId = cfg.gpu.empty() ? nullptr : cfg.gpu.c_str();
    try {
        presenter.init(pp);
    } catch (const std::exception& e) {
        std::cerr << "[screen-fg] 呈現初始化失敗：" << e.what() << "\n";
        source->stop();
        emitExit(1);
        return 1;
    }
    uint32_t hz = presenter.displayHz();

    // 4. clamp（Q4）+ health check
    uint32_t maxMult = maxMultiplierForCapture(hz);
    if (maxMult < 1)
        maxMult = 1;
    if (cfg.contentFpsCap < 60) {
        uint32_t m = maxMultiplier(hz, cfg.contentFpsCap);
        if (m < maxMult) {
            maxMult = m;
            std::cerr << "[screen-fg] 警告：倍數 clamp 到 " << maxMult << "x（" << hz << "Hz / "
                     << cfg.contentFpsCap << "fps cap）\n";
        }
    } else {
        std::cerr << "[screen-fg] 最大倍數 " << maxMult << "x（" << hz << "Hz / 60fps 捕捉）\n";
    }
    if (!layerEnumerated())
        std::cerr << "[screen-fg] 警告：找不到 layer VK_LAYER_LSFGVK_frame_generation"
                     "（檢查 ~/.local/share/vulkan/implicit_layer.d/）\n";
    if (!confHasActiveScreenFg())
        std::cerr << "[screen-fg] 警告：conf.toml 沒有 active_in 含 \"screen-fg\" 的 profile"
                     " → layer 會自我 unload、FG 不會啟動\n";
    {
        char path[256];
        snprintf(path, sizeof path, "/sys/class/drm/card%d/device", cardIndexFor(cfg.displayIndex));
        uint64_t total = readNumFile(std::string(path) + "/mem_info_vram_total");
        uint64_t used = readNumFile(std::string(path) + "/mem_info_vram_used");
        if (total > 0 && used < total) {
            uint64_t freeMiB = (total - used) / (1024 * 1024);
            if (freeMiB < 2048)
                std::cerr << "[screen-fg] 警告：VRAM 只剩 " << freeMiB << " MB（可能有其他程序在佔 GPU）\n";
        }
    }

    // 5. 主迴圈
    Deduplicator dedup(cfg.dedupThreshold);
    int64_t frameCount = 0;
    int64_t presentCount = 0;
    int fps = 0;
    auto fpsT0 = std::chrono::steady_clock::now();
    int fpsN = 0;
    bool running = true;
    auto lastStatusAt = std::chrono::steady_clock::now();

    // 初始 status（啟動即發，讓 GUI 馬上拿到 state）
    emitStatus(0, maxMult, !paused, paused ? protocol::State::Paused : protocol::State::Running);

    while (running) {
        // 狀態心跳（1Hz，與是否收到幀無關，讓 GUI 穩定更新）
        auto nowS = std::chrono::steady_clock::now();
        if (nowS - lastStatusAt >= std::chrono::seconds(1)) {
            lastStatusAt = nowS;
            emitStatus(fps, maxMult, !paused, paused ? protocol::State::Paused : protocol::State::Running);
        }

        // stdin 控制命令（GUI stdio channel）
        if (auto cmd = pollStdinCmd()) {
            if (cmd->name == protocol::Cmd::Quit) {
                running = false;
            } else if (cmd->name == protocol::Cmd::Pause) {
                if (!paused)
                    reexec(resolveReexec(paused, ReexecAction::Pause, hudOn), argc, argv, selfP);
            } else if (cmd->name == protocol::Cmd::Resume) {
                if (paused)
                    reexec(resolveReexec(paused, ReexecAction::Resume, hudOn), argc, argv, selfP);
            } else if (cmd->name == protocol::Cmd::Hud) {
                hudOn = (cmd->arg == "1");
                presenter.setHud(hudOn);
            }
        }
        if (!running)
            break;

        SDL_Event ev;
        while (SDL_WaitEventTimeout(&ev, 200) > 0) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            if (ev.type == SDL_EVENT_KEY_DOWN) {
                if (ev.key.key == SDLK_ESCAPE) {
                    running = false;
                    break;
                }
                if (ev.key.key == SDLK_P) {
                    // Q8a：暫停 = re-exec 到 plain 複製本（繼續呈現原始幀、無插幀）
                    reexec(resolveReexec(paused, ReexecAction::Toggle, hudOn), argc, argv, selfP);
                }
            }
        }
        if (!running)
            break;

        source->poll();
        if (source->isDead()) {
            std::cerr << "[screen-fg] 來源結束（synthetic 跑完 / 來源視窗關閉）→ 退出（Q9）\n";
            break;
        }

        auto frame = source->nextFrame();
        if (!frame)
            continue;

        // fps
        fpsN++;
        auto now = std::chrono::steady_clock::now();
        if (now - fpsT0 >= std::chrono::seconds(1)) {
            fps = fpsN;
            fpsN = 0;
            fpsT0 = now;
        }
        frameCount++;

        // dedup（Q2）：frame 已是 CPU 端的 BGRA copy，直接比對
        bool present = dedup.isDifferent(frame->data, frame->width, frame->height);

        if (present) {
            presentCount++;
            makeHud(hudBuf, sizeof hudBuf, fps, maxMult,
                   cfg.profile.c_str(), hz, paused);
            if (hudOn)
                presenter.setHudText(hudBuf);
            presenter.presentFrame(*frame);
        }
    }

    emitStatus(fps, maxMult, !paused, protocol::State::Exiting);
    std::cerr << "[screen-fg] 結束（" << frameCount << " 帧捕捉 / " << presentCount
               << " 帧呈現）\n";
    source->stop();
    presenter.shutdown();
    emitExit(0);
    return 0;
}
```
- [ ] **Step 2: Replace `screen-fg/CMakeLists.txt` with the full final file**

```cmake
cmake_minimum_required(VERSION 3.28)
project(screen-fg CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(PIW REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)

find_library(VULKAN_LIB vulkan)
find_path(VULKAN_INCLUDE vulkan/vulkan.h)

add_executable(screen-fg
    src/main.cpp
    src/config.cpp
    src/capture.cpp
    src/dedup.cpp
    src/present.cpp
    src/reexec.cpp
    src/synthetic.cpp
)
target_include_directories(screen-fg PRIVATE
    ${VULKAN_INCLUDE}
    ${PIW_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(screen-fg PRIVATE
    PkgConfig::SDL3
    PkgConfig::PIW
    PkgConfig::GIO
    ${VULKAN_LIB}
)

enable_testing()
# 純模組測試：不 link PW/SDL/Vulkan（frame.hpp / reexec / config / protocol 皆純）
add_executable(screen-fg-tests
    tests/test_main.cpp
    tests/test_config.cpp
    tests/test_config_resolve.cpp
    tests/test_dedup.cpp
    tests/test_clamp.cpp
    tests/test_reexec.cpp
    tests/test_frame_source.cpp
    src/config.cpp
    src/dedup.cpp
    src/reexec.cpp
    src/synthetic.cpp
)
target_include_directories(screen-fg-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME unit COMMAND screen-fg-tests)
```
- [ ] **Step 3: Build both targets — confirm the binary compiles and the pure suite still passes**

Run:
```bash
cd screen-fg && cmake -B build && cmake --build build && ./build/screen-fg-tests
```
Expected: build succeeds (produces `build/screen-fg` and `build/screen-fg-tests`), and `0 failures`.
- [ ] **Step 4: Commit**

```bash
cd fgvk && git add screen-fg/ && git commit -m "feat(screen-fg): main loop + stdio channel + CMake binary target (links PW/SDL/Vulkan/GIO)"
```

---

## Task 11: Synthetic end-to-end (no portal) — verify the present pipeline + layer

> This exercises the real `Presenter` (SDL3 + Vulkan swapchain + blit + HUD) and the `lsfg-vk` layer with **no** portal/picker. The synthetic block is 16×16 (too small to push the 32×32 MAD over the default 3.0 threshold) → dedup passes **only the first frame** (first frame is always true) → `3 帧捕捉 / 1 帧呈現` is the expected result, and the pipeline + layer are triggered by that first frame.

- [ ] **Step 1: Synthetic e2e, layer OFF (present pipeline only)**

Run:
```bash
cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 DISABLE_LSFGVK=1 ./build/screen-fg
```
Expected: stderr shows `synthetic 模式` then `結束（3 帧捕捉 / 1 帧呈現）`, exit 0.
- [ ] **Step 2: Synthetic e2e, layer ON (present + lsfg-vk interpolation)**

Run:
```bash
cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 ./build/screen-fg
```
Expected: same output, exit 0 (the layer must be enumerated and the `active_in` profile present, or a startup warning prints — a warning is acceptable here; a crash is not).
- [ ] **Step 3: Commit (no code change — verification checkpoint)**

If Steps 1–2 pass, no code changes are expected. If they fail, diagnose against the Task 9 Vulkan pitfalls (most commonly: a missing `pSwapchains`, a missing barrier `.image`, or `imageUsage` missing `TRANSFER_SRC|DST`).

---

## Task 12: Real portal end-to-end (monitor capture)

> Exercises the full `Capture` (GDBus portal + PipeWire over the restricted fd) with `capture_mode = "monitor"` (no picker). Requires the layer + `active_in` profile from Task 0.

- [ ] **Step 1: Confirm the local config uses monitor capture on display 1**

`~/.config/screen-fg/config.toml` should contain (create if missing):
```toml
capture_mode = "monitor"
display = 1
```
- [ ] **Step 2: Run the real portal e2e**

Run:
```bash
cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" ./build/screen-fg
```
Expected: the full-screen window opens on display 1 and presents the captured screen (HUD: `FPS …  X2  2x FG / 100%  …Hz`). Press `Esc` (or `quit` on stdin) → stderr prints `結束（N 帧捕捉 / M 帧呈現）` with `N > 0, M > 0`, exit 0.
- [ ] **Step 3: Diagnose if frames are 0**

If `N > 0` but `M == 0`, the present pipeline is fine but dedup dropped everything — that means the captured frames are identical (static screen) or the capture is delivering the same buffer. If `N == 0`, the PipeWire stream never delivered: check the stderr `stream target node=…` line (it must be the node ID from `Start`, not `ANY`), and confirm `PW_STREAM_FLAG_NONE` (not `DRIVER`).
- [ ] **Step 4: Commit (no code change — verification checkpoint)**

---

## Task 13: `screen-fg-gui` CMake + `process.{hpp,cpp}` (the stdio transport)

> The GUI is a separate GTK4 (gtkmm-4.0) app that `fork`+`exec`s `screen-fg` and drives it over stdio. The transport (`process.cpp`) owns the pipes/threads; **decoding** (line → status/exit) is the shared `protocol::parse` (pure). No libadwaita (no C++ binding; GNOME's default GTK4 theme is Adwaita, so it looks identical).

**Files:**
- Create: `screen-fg-gui/CMakeLists.txt`
- Create: `screen-fg-gui/src/process.hpp`
- Create: `screen-fg-gui/src/process.cpp`

### gtkmm 4.0 / process-control pitfalls (this machine, gtkmm 4.20 / Ubuntu 26.04) — bake them in
- **`Gtk::Application` needs `add_window(*win)`,** otherwise the app exits immediately (`run()` returns 0, process lives <0.2 s). `present()` alone is not enough.
- **Reader uses `poll` + chunked read (not blocking, not per-byte).** The loop is `pollFd` (100 ms timeout) + 8 KB chunked `read` + a `pending` buffer that extracts whole lines. This is what lets the destructor do a **bounded** wait via `stopping_` (reader exits within ~100 ms). Do **not** switch back to blocking `read` or per-byte line reads (a reused buffer that isn't cleared **accumulates** → the decoder always sees the first `"fps":0` of the accumulated line → every status shows `fps:0`). Hand **each** line to the pure `protocol::parse` separately.
- **Destructor must not block on `join()`.** Old code `reader_.join()` blocked until the child's stdout hit EOF — if the child never exits (mid-picker / stuck in present), app shutdown **hangs for minutes**. Now: `stopping_.store(true)` → reader exits within ~100 ms (bounded join) → close stdin (child stdin EOF) → `waitpid(WNOHANG)` (non-blocking) → if the child is still alive, `kill(SIGTERM)` to reap it (else it's orphaned to init).
- **Pause/resume = re-exec (screen-fg side).** The layer is a per-process global singleton, config read once → can't toggle at runtime → pause re-execs the whole binary → **`source->start()` re-runs the XDG portal picker (re-pick the window)**; not visible in synthetic mode. The GUI warns in the pause-button tooltip + log. Runtime HUD state is preserved across re-exec via the `SCREENFG_HUD` env (the re-exec'd process reads it back on boot).
- **gtkmm 4.0 API details:** enums are all scoped (`Gtk::Orientation::HORIZONTAL/VERTICAL`, `Gtk::Align::START/CENTER`, `Gtk::WrapMode::WORD_CHAR`, `Gtk::PolicyType::NEVER/AUTOMATIC`); `Gtk::make_managed<T>()` returns a raw `T*` (**not** a `Glib::RefPtr` — it's shared_ptr-based and won't take a raw ptr); `Gtk::Box::append(Widget&)` takes one arg (expand via `set_hexpand(true)` on the child); `Gtk::TextBuffer` uses `get_bounds(begin,end)` + `insert` (returns the end iter) + `scroll_to(iter, margin)`; `fs::canonical` returns a path (throws, not an optional).
- **`Gtk::Switch` signal:** `signal_state_set()` is `SignalProxy<bool(bool)>`; `connect` must **explicitly** pass the `after` arg: `sig.connect([this](bool){ …; return false; }, false)`. (`property_active().signal_notify()` does not exist in this version.)
- **Do not use `char` literals for multi-byte UTF-8:** `find('（')` overflows the multichar constant (value is truncated) → use the string form `find("（")`.

- [ ] **Step 1: Write `screen-fg-gui/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.22)
project(screen-fg-gui CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
# 註：libadwaita 無 C++ (gtkmm) binding，本機只有 C 標頭；
# 故用純 gtkmm，GNOME 預設 GTK4 主題即 Adwaita，外觀相同。
pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-4.0)

add_executable(screen-fg-gui
    src/main.cpp
    src/process.cpp
)
target_include_directories(screen-fg-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_link_libraries(screen-fg-gui
    PRIVATE
    PkgConfig::GTKMM
)

enable_testing()
# 純 ProtocolDecoder 測試：不需 gtkmm（只用 shared/protocol.hpp + ScreenFgStatus 純結構）
add_executable(screen-fg-gui-tests
    tests/test_protocol_decoder.cpp
)
target_include_directories(screen-fg-gui-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
add_test(NAME unit COMMAND screen-fg-gui-tests)
```
- [ ] **Step 2: Write `screen-fg-gui/src/process.hpp`**

```cpp
#pragma once
// screen-fg 子程序控制（transport）：fork+exec、持 stdin/stdout 管線、寫命令、waitpid。
// 解碼（行 → status/exit）交給 shared/protocol.hpp 的純 ProtocolDecoder；本模組只負責 IO。
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ScreenFgStatus {
    int fps = 0;
    unsigned mult = 0;
    bool layer = false;
    std::string state; // "running" / "paused" / "exiting"
};

class ScreenFgProcess {
public:
    ScreenFgProcess();
    ~ScreenFgProcess();

    // 回傳是否成功 fork+exec
    bool start(const std::string& binary, const std::vector<std::string>& extraArgs);
    void sendCommand(const std::string& cmd); // 詞彙見 shared/protocol.hpp 的 Cmd
    void quit();                             // 送 quit 命令（退出由 exit callback 異步處理；destructor 會 bounded 等待 + SIGTERM 兜底）

    void setStatusCb(std::function<void(const ScreenFgStatus&)> cb);
    void setExitCb(std::function<void(int)> cb);
    void setLogCb(std::function<void(const std::string&)> cb); // 可選：子程序 stderr

    bool running() const { return running_.load(); }
    pid_t pid() const { return pid_; }

private:
    void readerLoop();
    void logLoop();

    pid_t pid_ = -1;
    int stdinW_ = -1; // 父持有：寫命令
    int stdoutR_ = -1; // 父持有：讀 status
    int stderrR_ = -1; // 父持有：讀 log（可選）
    std::thread reader_;
    std::thread stderrReader_;
    std::mutex stdinMu_;
    std::atomic<bool> running_{false};
    std::atomic<bool> exitReported_{false};
    std::atomic<bool> stopping_{false}; // destructor 設 true → reader 於 ~100ms 內退出（bounded join，不卡 app 退出）
    std::function<void(const ScreenFgStatus&)> statusCb_;
    std::function<void(int)> exitCb_;
    std::function<void(const std::string&)> logCb_;
};
```
- [ ] **Step 3: Write `screen-fg-gui/src/process.cpp`**

```cpp
#include "process.hpp"

#include "shared/protocol.hpp"

#include <glib.h>

#include <variant>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace proto = screenfg::protocol;

namespace {

void postIdle(std::function<void()> fn) {
    auto* cap = new std::function<void()>(std::move(fn));
    g_idle_add([](void* p) -> gboolean {
        auto* f = static_cast<std::function<void()>*>(p);
        (*f)();
        delete f;
        return G_SOURCE_REMOVE;
    }, cap);
}

void writeAll(int fd, const char* b, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = write(fd, b + off, n - off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return; // EPIPE 等
        }
        off += (size_t) r;
    }
}

// 等 fd 有資料（100ms 超時）：回 1=有資料、0=超時/可回圈檢查 stopping_、-1=錯
int pollFd(int fd) {
    struct pollfd p;
    p.fd = fd;
    p.events = POLLIN;
    p.revents = 0;
    int pr = poll(&p, 1, 100);
    if (pr < 0)
        return (errno == EINTR) ? 0 : -1;
    if (pr == 0)
        return 0;
    return 1;
}

int waitExitCode(pid_t pid) {
    int st = 0;
    while (waitpid(pid, &st, 0) < 0) {
        if (errno == EINTR)
            continue;
        return 0; // ECHILD 等
    }
    if (WIFEXITED(st))
        return WEXITSTATUS(st);
    if (WIFSIGNALED(st))
        return 128 + WTERMSIG(st);
    return 0;
}

} // namespace

ScreenFgProcess::ScreenFgProcess() = default;

ScreenFgProcess::~ScreenFgProcess() {
    if (running_.load())
        sendCommand(proto::Cmd::Quit);
    stopping_.store(true); // reader 於 ~100ms 內退出（bounded join，不讓 app 退出卡死）
    if (stdinW_ >= 0) { close(stdinW_); stdinW_ = -1; } // 子程序 stdin EOF
    if (reader_.joinable())
        reader_.join();
    if (stderrReader_.joinable())
        stderrReader_.join();
    if (stdoutR_ >= 0) { close(stdoutR_); stdoutR_ = -1; }
    if (stderrR_ >= 0) { close(stderrR_); stderrR_ = -1; }
    if (pid_ > 0) {
        int st = 0;
        if (waitpid(pid_, &st, WNOHANG) == 0) // 子程序還活著（沒處理 quit）→ 收掉
            kill(pid_, SIGTERM);
    }
}

bool ScreenFgProcess::start(const std::string& binary, const std::vector<std::string>& extraArgs) {
    if (running_.load())
        return false;

    int stdinPipe[2], stdoutPipe[2], stderrPipe[2];
    if (pipe(stdinPipe) != 0 || pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0)
        return false;

    pid_ = fork();
    if (pid_ < 0) {
        for (auto fd : {stdinPipe[0], stdinPipe[1], stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1]})
            close(fd);
        return false;
    }

    if (pid_ == 0) {
        // 子程序
        dup2(stdinPipe[0], 0);
        dup2(stdoutPipe[1], 1);
        dup2(stderrPipe[1], 2);
        for (auto fd : {stdinPipe[0], stdinPipe[1], stdoutPipe[0], stdoutPipe[1], stderrPipe[0], stderrPipe[1]})
            close(fd);

        std::vector<const char*> argv;
        argv.push_back(binary.c_str());
        for (auto& a : extraArgs)
            argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execv(binary.c_str(), const_cast<char**>(argv.data()));
        _exit(127); // exec 失敗
    }

    // 父程序
    close(stdinPipe[0]);
    close(stdoutPipe[1]);
    close(stderrPipe[1]);
    stdinW_ = stdinPipe[1];
    stdoutR_ = stdoutPipe[0];
    stderrR_ = stderrPipe[0];
    running_.store(true);
    exitReported_.store(false);
    stopping_.store(false);

    try {
        reader_ = std::thread(&ScreenFgProcess::readerLoop, this);
        stderrReader_ = std::thread(&ScreenFgProcess::logLoop, this);
    } catch (...) {
        // thread 建立失敗：殺掉子程序 + 清 fd，避免 leak
        kill(pid_, SIGKILL);
        waitpid(pid_, nullptr, 0);
        pid_ = -1;
        running_.store(false);
        for (auto fd : {stdinW_, stdoutR_, stderrR_})
            if (fd >= 0)
                close(fd);
        stdinW_ = stdoutR_ = stderrR_ = -1;
        return false;
    }
    return true;
}

void ScreenFgProcess::readerLoop() {
    char chunk[8192];
    std::string pending;
    bool eof = false;
    while (!stopping_.load()) {
        int pr = pollFd(stdoutR_);
        if (pr < 0)
            break;
        if (pr == 0)
            continue; // 超時 → 回圈頂檢查 stopping_
        ssize_t r = read(stdoutR_, chunk, sizeof chunk);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0) { eof = true; break; }
        pending.append(chunk, (size_t) r);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            // 解碼交給 shared ProtocolDecoder（純 parse）；transport 只負責 IO + 回調
            auto msg = proto::parse(line);
            if (!msg)
                continue; // 缺欄 / 格式錯 → 明確跳過（不默默 default）
            if (std::holds_alternative<proto::Status>(*msg)) {
                const auto& ps = std::get<proto::Status>(*msg);
                ScreenFgStatus s;
                s.fps = ps.fps;
                s.mult = ps.mult;
                s.layer = ps.layer;
                s.state = ps.state;
                auto cb = statusCb_;
                if (cb)
                    postIdle([cb, s] { cb(s); });
            } else {
                int code = std::get<proto::Exit>(*msg).code;
                if (!exitReported_.exchange(true)) {
                    auto cb = exitCb_;
                    if (cb)
                        postIdle([cb, code] { cb(code); });
                }
            }
        }
    }
    if (eof) {
        // EOF：收屍 + 補 exit（若子程序被殺、沒發 exit JSON）
        int code = waitExitCode(pid_);
        if (!exitReported_.exchange(true)) {
            auto cb = exitCb_;
            if (cb)
                postIdle([cb, code] { cb(code); });
        }
    }
    running_.store(false);
}

void ScreenFgProcess::logLoop() {
    char chunk[8192];
    std::string pending;
    while (!stopping_.load()) {
        int pr = pollFd(stderrR_);
        if (pr < 0)
            break;
        if (pr == 0)
            continue;
        ssize_t r = read(stderrR_, chunk, sizeof chunk);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0)
            break; // EOF
        pending.append(chunk, (size_t) r);
        size_t pos;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            pending.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            auto cb = logCb_;
            if (cb)
                postIdle([cb, line] { cb(line); });
        }
    }
}

void ScreenFgProcess::setStatusCb(std::function<void(const ScreenFgStatus&)> cb) {
    statusCb_ = std::move(cb);
}
void ScreenFgProcess::setExitCb(std::function<void(int)> cb) {
    exitCb_ = std::move(cb);
}
void ScreenFgProcess::setLogCb(std::function<void(const std::string&)> cb) {
    logCb_ = std::move(cb);
}

void ScreenFgProcess::sendCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lk(stdinMu_);
    if (stdinW_ < 0)
        return;
    std::string msg = cmd + "\n";
    writeAll(stdinW_, msg.data(), msg.size());
}

void ScreenFgProcess::quit() {
    sendCommand(proto::Cmd::Quit);
}
```
- [ ] **Step 4: Commit**

```bash
cd fgvk && git add screen-fg-gui/CMakeLists.txt screen-fg-gui/src/process.hpp screen-fg-gui/src/process.cpp && git commit -m "feat(screen-fg-gui): CMake + fork/exec stdio transport (decode via shared protocol)"
```

---

## Task 14: `screen-fg-gui/src/main.cpp` (the GTK4 UI) + `.desktop`

**Files:**
- Create: `screen-fg-gui/src/main.cpp`
- Create: `screen-fg-gui/screen-fg-gui.desktop`

> `findBinary()` search order (first hit wins): 1) `$SCREENFG_BIN` (env, if the path exists); 2) `build/screen-fg` (CWD-relative); 3) `../screen-fg/build/screen-fg`; 4) `/usr/local/bin/screen-fg`; 5) `<gui exe dir>/../screen-fg/build/screen-fg`; 6) every `PATH` segment (`<dir>/screen-fg`). If none, the Entry shows `（找不到，請手動填路徑）` and the start button is blocked.

> Runtime state machine: start/stop button — not running → `proc_.start(bin, {})` (button → 「停止」, state → "starting…"); running → `proc_.quit()` (sends `quit`; the exit is handled **asynchronously** by the exit callback). State label: the status `state` (running/paused/exiting) → on exit, code 0 = 「已停止」, non-0 = 「錯誤結束 (N)」. Pause/resume button label follows state (running→「暫停」, paused→「恢復」); pressing sends pause/resume; the log + tooltip warn that **pause re-runs the capture (re-picks the window) in real-capture mode**. HUD switch → `hud 1|0` (immediate, no restart). Log area = the child's stderr line-by-line (logLoop) + GUI-side events.

- [ ] **Step 1: Write `screen-fg-gui/src/main.cpp`**

```cpp
#include <gtkmm.h>

#include "process.hpp"

#include "shared/protocol.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace proto = screenfg::protocol;

namespace {

bool fileExists(const std::string& p) {
    struct stat st;
    return ::access(p.c_str(), F_OK) == 0 || stat(p.c_str(), &st) == 0;
}

// 找 screen-fg binary：env → 相對候補 → PATH
std::string findBinary() {
    if (const char* e = std::getenv("SCREENFG_BIN"))
        if (fileExists(e))
            return e;
    std::vector<std::string> cands = {
        "build/screen-fg",
        "../screen-fg/build/screen-fg",
        "/usr/local/bin/screen-fg",
    };
    try {
        auto ex = fs::canonical(fs::absolute("/proc/self/exe"));
        cands.push_back((ex.parent_path() / ".." / "screen-fg" / "build" / "screen-fg").generic_string());
    } catch (...) {
    }
    for (auto& c : cands)
        if (fileExists(c))
            return fs::absolute(c).string();
    if (const char* path = std::getenv("PATH")) {
        std::string cur;
        for (const char* p = path; *p; ++p) {
            if (*p == ':') {
                if (!cur.empty() && fileExists(cur + "/screen-fg"))
                    return cur + "/screen-fg";
                cur.clear();
            } else {
                cur += *p;
            }
        }
        if (!cur.empty() && fileExists(cur + "/screen-fg"))
            return cur + "/screen-fg";
    }
    return {};
}

// 簡單「標題 + 後綴」列（取代 Adw::ActionRow，無 C++ binding）；suffix 為 managed 指標
Gtk::Box* makeRow(const std::string& title, Gtk::Widget* suffix) {
    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    auto lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->set_halign(Gtk::Align::START);
    box->append(*lbl);
    if (suffix) {
        suffix->set_hexpand(true);
        box->append(*suffix);
    }
    return box;
}

} // namespace

class MainWindow : public Gtk::Window {
public:
    MainWindow() {
        set_title("screen-fg");
        set_default_size(560, 640);

        box_ = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 16);
        box_->set_margin_top(16);
        box_->set_margin_bottom(16);
        box_->set_margin_start(16);
        box_->set_margin_end(16);
        set_child(*box_);

        // binary 路徑
        binEntry_ = Gtk::make_managed<Gtk::Entry>();
        std::string bin = findBinary();
        binEntry_->set_text(bin.empty() ? std::string("（找不到，請手動填路徑）") : bin);
        box_->append(*makeRow("screen-fg binary", binEntry_));

        // 啟動 / 停止
        startBtn_ = Gtk::make_managed<Gtk::Button>("啟動");
        startBtn_->set_hexpand(true);
        startBtn_->signal_clicked().connect([this] { onStartStop(); });
        box_->append(*startBtn_);

        // 狀態
        auto status = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
        status->set_halign(Gtk::Align::CENTER);
        fpsLbl_ = Gtk::make_managed<Gtk::Label>("FPS 0");
        fpsLbl_->add_css_class("title-2");
        multLbl_ = Gtk::make_managed<Gtk::Label>("x0");
        multLbl_->add_css_class("title-2");
        layerLbl_ = Gtk::make_managed<Gtk::Label>("layer off");
        stateLbl_ = Gtk::make_managed<Gtk::Label>("idle");
        stateLbl_->add_css_class("dim-label");
        status->append(*fpsLbl_);
        status->append(*multLbl_);
        status->append(*layerLbl_);
        status->append(*stateLbl_);
        box_->append(*status);

        // 暫停 / 恢復
        pauseBtn_ = Gtk::make_managed<Gtk::Button>("暫停");
        pauseBtn_->set_tooltip_text("暫停 = passthrough（無插幀）。注意：真實捕捉模式下暫停會重新啟動捕捉（重跑選窗）");
        pauseBtn_->signal_clicked().connect([this] { onPause(); });
        box_->append(*makeRow("暫停 / 恢復", pauseBtn_));

        // HUD
        hudSwitch_ = Gtk::make_managed<Gtk::Switch>();
        hudSwitch_->set_active(true);
        hudSwitch_->signal_state_set().connect([this](bool) { onHud(); return false; }, false);
        box_->append(*makeRow("HUD", hudSwitch_));

        // log
        logView_ = Gtk::make_managed<Gtk::TextView>();
        logView_->set_editable(false);
        logView_->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
        logView_->set_vexpand(true);
        auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
        scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
        scroll->set_child(*logView_);
        scroll->set_vexpand(true);
        box_->append(*scroll);

        proc_.setStatusCb([this](const ScreenFgStatus& s) { onStatus(s); });
        proc_.setExitCb([this](int code) { onExit(code); });
        proc_.setLogCb([this](const std::string& line) { appendLog(line); });
    }

private:
    void onStartStop() {
        if (!proc_.running()) {
            std::string bin = binEntry_->get_text();
            if (bin.empty() || bin.find("（") != std::string::npos) {
                appendLog("錯誤：請先填 screen-fg binary 路徑");
                return;
            }
            if (proc_.start(bin, {})) {
                started_ = true;
                startBtn_->set_label("停止");
                stateLbl_->set_text("starting…");
                appendLog("已啟動 " + bin);
            } else {
                appendLog("錯誤：啟動失敗（fork/exec）");
            }
        } else {
            proc_.quit();
            appendLog("停止中…");
        }
    }

    void onPause() {
        bool toPause = lastState_ != proto::State::Paused;
        proc_.sendCommand(toPause ? proto::Cmd::Pause : proto::Cmd::Resume);
        if (toPause)
            appendLog("注意：暫停會重新啟動捕捉（真實捕捉模式下會重跑選窗）");
    }

    void onHud() {
        proc_.sendCommand(std::string(proto::Cmd::Hud) + " " + (hudSwitch_->get_active() ? "1" : "0"));
    }

    void onStatus(const ScreenFgStatus& s) {
        lastState_ = s.state;
        fpsLbl_->set_text("FPS " + std::to_string(s.fps));
        multLbl_->set_text("x" + std::to_string(s.mult));
        layerLbl_->set_text(s.layer ? "layer on" : "layer off");
        stateLbl_->set_text(s.state);
        pauseBtn_->set_label(s.state == proto::State::Paused ? "恢復" : "暫停");
    }

    void onExit(int code) {
        started_ = false;
        startBtn_->set_label("啟動");
        lastState_ = "";
        stateLbl_->set_text(code == 0 ? "已停止" : "錯誤結束 (" + std::to_string(code) + ")");
        fpsLbl_->set_text("FPS 0");
        appendLog("screen-fg 結束（code " + std::to_string(code) + "）");
    }

    void appendLog(const std::string& line) {
        auto buf = logView_->get_buffer();
        Gtk::TextIter begin, end;
        buf->get_bounds(begin, end);
        auto newEnd = buf->insert(end, line + "\n"); // 回傳插入文本末端的 iterator
        logView_->scroll_to(newEnd, 0.0);
    }

    Gtk::Box* box_ = nullptr;
    Gtk::Entry* binEntry_ = nullptr;
    Gtk::Button* startBtn_ = nullptr;
    Gtk::Label* fpsLbl_ = nullptr, * multLbl_ = nullptr, * layerLbl_ = nullptr, * stateLbl_ = nullptr;
    Gtk::Button* pauseBtn_ = nullptr;
    Gtk::Switch* hudSwitch_ = nullptr;
    Gtk::TextView* logView_ = nullptr;
    ScreenFgProcess proc_;
    bool started_ = false;
    std::string lastState_;
};

class GuiApp : public Gtk::Application {
    MainWindow* win_ = nullptr;
public:
    GuiApp() : Gtk::Application("com.issac.screenfg-gui") {}
    ~GuiApp() override {
        delete win_;
        win_ = nullptr;
    }
    void on_activate() override {
        if (!win_) {
            win_ = new MainWindow();
            add_window(*win_);
        }
        win_->present();
    }
};

int main(int argc, char** argv) {
    GuiApp app;
    return app.run(argc, argv);
}
```
- [ ] **Step 2: Write `screen-fg-gui/screen-fg-gui.desktop`**

```ini
[Desktop Entry]
Type=Application
Name=screen-fg
Name[zh_TW]=screen-fg 插帧控制
Comment=screen-fg 插帧 GUI 控制器
Exec=env SCREENFG_BIN=/home/issac/公共/fgvk/screen-fg/build/screen-fg screen-fg-gui
Icon=video-display
Terminal=false
Categories=AudioVideo;Video;
```
- [ ] **Step 3: Build the GUI**

Run:
```bash
cd screen-fg-gui && cmake -B build && cmake --build build
```
Expected: build succeeds (produces `build/screen-fg-gui` and `build/screen-fg-gui-tests`).
- [ ] **Step 4: Commit**

```bash
cd fgvk && git add screen-fg-gui/src/main.cpp screen-fg-gui/screen-fg-gui.desktop && git commit -m "feat(screen-fg-gui): GTK4 main window + binary autodetect + .desktop"
```

---

## Task 15: GUI ProtocolDecoder test (verifies the shared protocol from the GUI side)

> This test uses **only** `shared/protocol.hpp` + the pure `ScreenFgStatus` struct (no gtkmm linked). It verifies emit/parse round-trip, the `Status → ScreenFgStatus` mapping, field-presence checks (missing → `nullopt`), and the vocabulary constants. Because the protocol is already built and tested in Tasks 1 and 7, this is expected to **pass on the first run**; if it fails, the shared header has a regression.

**Files:**
- Create: `screen-fg-gui/tests/test_protocol_decoder.cpp`

- [ ] **Step 1: Write `screen-fg-gui/tests/test_protocol_decoder.cpp`**

```cpp
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
```
- [ ] **Step 2: Build and run — expect 0 failures**

Run:
```bash
cd screen-fg-gui && cmake --build build && ./build/screen-fg-gui-tests
```
Expected: `0 failures`, exit 0.
- [ ] **Step 3: Commit**

```bash
cd fgvk && git add screen-fg-gui/tests/test_protocol_decoder.cpp && git commit -m "test(screen-fg-gui): ProtocolDecoder (shared parse + ScreenFgStatus mapping)"
```

---

## Task 16: GUI end-to-end

> Launches the GUI, which auto-detects `screen-fg`, forks it (portal/monitor capture), and drives it over stdio.

- [ ] **Step 1: Run the GUI e2e**

Run:
```bash
cd screen-fg-gui && ./build/screen-fg-gui
```
Expected: the window shows the auto-detected `screen-fg` path in the binary Entry; clicking **啟動** launches `screen-fg` (portal/monitor capture opens the full-screen window), the status row updates (FPS / mult / layer / state); clicking **停止** makes `screen-fg` exit cleanly and the GUI shows 「已停止」 (state label) and a `screen-fg 結束（code 0）` log line.
- [ ] **Step 2: Verify the log area and HUD switch**

With `screen-fg` running, confirm the log area streams `screen-fg`'s stderr lines, and that toggling the HUD switch sends `hud 1|0` (the `screen-fg` HUD appears/disappears without a restart).
- [ ] **Step 3: Commit (no code change — verification checkpoint)**

---

## Task 17: Final regression (run the full Definition of Done)

Run each of the seven Definition-of-Done checks and confirm all pass:

- [ ] **1. screen-fg pure tests** — `cd screen-fg && ./build/screen-fg-tests` → `… 74 checks, 0 failures`, exit 0.
- [ ] **2. GUI pure tests** — `cd screen-fg-gui && ./build/screen-fg-gui-tests` → `0 failures`, exit 0.
- [ ] **3. Synthetic e2e, layer OFF** — `cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 DISABLE_LSFGVK=1 ./build/screen-fg` → `synthetic 模式` … `結束（3 帧捕捉 / 1 帧呈現）`, exit 0.
- [ ] **4. Synthetic e2e, layer ON** — same without `DISABLE_LSFGVK=1` → same, exit 0.
- [ ] **5. Real portal e2e (monitor)** — `cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" ./build/screen-fg` (config `capture_mode = "monitor"`, `display = 1`) → fullscreen window on display 1 with HUD; `Esc` → `結束（N 帧捕捉 / M 帧呈現）` with `N > 0, M > 0`, exit 0.
- [ ] **6. GUI e2e** — `cd screen-fg-gui && ./build/screen-fg-gui` → auto-detected path; 啟動 → capture + status updates; 停止 → 「已停止」.
- [ ] **7. Regressions** — `screen-fg --passthrough` re-execs to `screen-fg-plain` and runs without interpolation; `screen-fg --config <path>` reads the alternate config; stdin `pause`/`resume`/`hud 0|1`/`quit` all work.

- [ ] **Final commit**

```bash
cd fgvk && git add -A && git commit -m "feat(fgvk): complete screen-fg + screen-fg-gui build (all DoD checks pass)"
```

## Verification checklist (paste-ready)

```bash
# 1. screen-fg pure unit tests
cd fgvk/screen-fg && ./build/screen-fg-tests

# 2. GUI pure unit tests
cd fgvk/screen-fg-gui && ./build/screen-fg-gui-tests

# 3. synthetic e2e, layer OFF
cd fgvk/screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" \
  SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 DISABLE_LSFGVK=1 ./build/screen-fg

# 4. synthetic e2e, layer ON
cd fgvk/screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" \
  SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 ./build/screen-fg

# 5. real portal e2e (monitor; config capture_mode=monitor display=1)
cd fgvk/screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" ./build/screen-fg

# 6. GUI e2e
cd fgvk/screen-fg-gui && ./build/screen-fg-gui

# 7. passthrough regression
cd fgvk/screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" ./build/screen-fg --passthrough
```
