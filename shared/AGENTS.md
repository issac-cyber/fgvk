# shared

`screen-fg` 與 `screen-fg-gui` 共同的**純函式模組**（無 I/O、無外部 C 庫）。兩個專案的 `CMakeLists.txt` 都 reference 此目錄（`-I <fgvk>/`，`#include "shared/protocol.hpp"`）。

## Key files

- `protocol.hpp` — stdio 控制協議的唯一來源（單一 header-only 模組）：
  - `Message`（`std::variant<Status, Exit>`）+ 詞彙常數（`State`/`Cmd`）+ `kVersion`
  - **純 emit**：`emitStatus(fps,mult,layer,state)` / `emitExit(code)` → 整行 bytes（含 `\n`），transport 負責寫
  - **純 parse**：`parse(line) → optional<Message>`；缺欄位/格式錯 → `nullopt`（明確報錯，**不默默 default**）
  - 純 field 助手：`rawField` / `intField` / `boolField` / `stringField`（key 缺失 → `nullopt`）

## 規則

- 純函式：不得碰 process / display / GDBus / stdin·stdout 本體（IO 留各專案的 transport）。
- 加字段要過**欄位存在性檢查**（parse 缺欄 → nullopt），並遞增 `kVersion`。
- 不引入外部 C 庫（保持兩專案無須額外 link）。
