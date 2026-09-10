# shared

`screen-fg` 與 `screen-fg-gui` 共同的**純函式模組**（無 I/O、無外部 C 庫）。兩個專案的 `CMakeLists.txt` 都 reference 此目錄（`-I <fgvk>/`，`#include "shared/protocol.hpp"`）——**兩端編譯同一份 header，協議詞彙/結構不可能漂移**。

## Key files

- `protocol.hpp` — stdio 控制協議的唯一來源（單一 header-only 模組，namespace `screenfg::protocol`）：
  - `kVersion = 1` — 共編譯合約版本；不兼容變更時遞增
  - 結構：`Status`（`int fps`、`unsigned int mult`、`bool layer`、`std::string state`）/ `Exit`（`int code`）；`Message = std::variant<Status, Exit>`
  - 詞彙常數：`State` {`Running`="running"、`Paused`="paused"、`Exiting`="exiting"}；`Cmd` {`Quit`="quit"、`Pause`="pause"、`Resume`="resume"、`Hud`="hud"}
  - **純 emit**（回傳整行 bytes、含尾 `'\n'`；transport 負責寫）：
    - `emitStatus(fps,mult,layer,state)` → `{"type":"status","fps":N,"mult":N,"layer":true|false,"state":"..."}\n`
    - `emitExit(code)` → `{"type":"exit","code":N}\n`
    - 格式：flat 單行、無空格、key 順序固定（parse 依賴此格式；**勿加空格/巢狀**）
  - **純 parse**：`parse(line) → optional<Message>`；`type` 必須是 status/exit；status 的四個必需欄（fps/mult/layer/state）全存在且有效、exit 的 `code` 存在且有效；任一缺/壞 → `nullopt`（明確報錯，**不默默 default**）
  - 純 field 助手（key 缺失 → `nullopt`）：
    - `rawField` — key 要帶引號；值 = 第一個 `:` 後的 token，到 `,` / `"` / `}` / 空白為止（flat token 抽取，**非** JSON string 解析——兩端同用 emit 才安全）
    - `intField` — 只接受可選前置 `-` + 純數字（空/含非數字 → nullopt）
    - `boolField` — 只接受 `true` / `false`（大小寫敏感、其他值 → nullopt）
    - `stringField` — `:` 後第一段引號內的值（無引號值 → nullopt）
- decoder 測試：`screen-fg-gui/tests/test_protocol_decoder.cpp`（純，不 link gtkmm）

## 規則

- 純函式：不得碰 process / display / GDBus / stdin·stdout 本體（IO 留各專案的 transport）。
- 加字段要過**欄位存在性檢查**（parse 缺欄 → nullopt），並遞增 `kVersion`；同時更新兩端使用者（screen-fg / screen-fg-gui）與協議測試。
- 不引入外部 C 庫（保持兩專案無須額外 link）。
- **詞彙區別**：`screen-fg` 的 re-exec env 用 `ReexecEnv`（"normal"/"paused"），與 status JSON 的 "running"/"paused"/"exiting" 是**不同詞彙**——不可混用（見 `screen-fg/src/reexec.hpp` 註解）。
