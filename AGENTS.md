# fgvk

fg（frame generation）專案：瀏覽器影片即時插幀（本機個人用）。

## Scope

三個同層組件＋ spec。**相對結構不可動**：`shared/` 是兩專案的 sibling（`-I <fgvk>/` 引用），`screen-fg-gui` 靠 `../screen-fg/build/screen-fg` 找 binary。

| 組件 | 內容 |
|------|------|
| `screen-fg/` | 主 binary：portal ScreenCast 捕捉 → CPU 去重 → SDL3 全螢幕 + Vulkan FIFO swapchain 呈現 → `lsfg-vk` layer 插幀（依 `specs/screen-fg-pipeline/spec.md` LOCKED v1.0） |
| `screen-fg-gui/` | GUI 控制器（gtkmm-4.0，獨立程式，fork+exec `screen-fg` 走 stdio 控制；依 `specs/screen-fg-gui/spec.md`） |
| `shared/` | 純函式協議模組（`protocol.hpp`，兩專案共用的 stdio 控制協議**單一來源**） |
| `specs/` | 兩份 spec + 設計史（`screen-fg-pipeline/`、`screen-fg-gui/` 各自的 `spec.md` + `map.md` + `issues/` + `research/`） |

各組件的建置 / 操作 / 陷阱全在**它自己的 `AGENTS.md`**（本檔只索引，不重複）。

## 建置 & 驗證

- screen-fg：`cd screen-fg && cmake -B build && cmake --build build`；純模組測試 `./build/screen-fg-tests`（68 checks）
- screen-fg-gui：`cd screen-fg-gui && cmake -B build && cmake --build build`；ProtocolDecoder 測試 `./build/screen-fg-gui-tests`
- 無門戶 e2e（synthetic）：`cd screen-fg && DBUS_SESSION_BUS_ADDRESS="unix:path=/run/user/1000/bus" SCREENFG_SYNTHETIC=1 SCREENFG_SYNTHETIC_FRAMES=3 SCREENFG_DISPLAY=1 ./build/screen-fg`
- 系統依賴：`libsdl3-dev libpipewire-0.3-dev libvulkan-dev libglib2.0-dev libgtkmm-4.0-dev`

## 命名

`fgvk` 是專案/目錄名；binary 仍叫 `screen-fg`（`screen-fg-plain` 複製本、`~/.config/screen-fg/`、layer `active_in`="screen-fg" 全維持）。
