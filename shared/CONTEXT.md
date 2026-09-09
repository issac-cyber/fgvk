# CONTEXT — screen-fg / screen-fg-gui 領域詞彙

 Ubiquitous-language glossary for the screen-fg pair. One canonical term per concept;
 ambiguous phrasings are flagged. See `shared/protocol.hpp` for the wire contract.

## 模組（deep modules 命名）

- **ProtocolDecoder** — `shared/protocol.hpp` 的純 `parse`：行 → `Message`（`Status`|`Exit`）。
  只負責「行是否為合法 status/exit」；缺欄 → `nullopt`（不 default）。
- **ProtocolEncoder** — `shared/protocol.hpp` 的純 `emit*`：值 → 整行 bytes。transport 負責寫。
- **FrameSource** — `screen-fg/src/frame_source.hpp`：frame 來源抽象介面
  （`Capture` portal+PipeWire、`SyntheticSource` 測試合成幀 共同實作）。
  回傳 `CapturedFrame`（定義於中性 `frame.hpp`，非 capture 的 PW 門頭）。
- **ConfigResolver** — `screen-fg/src/config.cpp` 的純 `resolveConfig(tomlText, envMap)`
  + 純 `cardIndexFor(displayIndex)`；`loadConfig` 是「讀檔」adapter。
- **ReexecResolver** — `screen-fg/src/reexec.{hpp,cpp}` 的純 `resolveReexec(state, action, hudOn)`
  → `(target, stateEnv, hudEnv)`；self-path 由 adapter 注入。

## 領域詞彙

- **捕捉 (capture)**：XDG portal ScreenCast v5 抓瀏覽器視窗 → PipeWire 取 frame。
- **去重 (dedup)**：32×32 luminance grid + MAD，決定 frame 是否「不同」→ 才呈現。
- **呈現 (present)**：SDL3 全螢幕視窗 + Vulkan FIFO swapchain（CPU upload + blit）。
- **插幀 (FG / lsfg-vk)**：implicit Vulkan layer 在 present 後做 frame generation。
- **passthrough / 暫停**：re-exec 到 `screen-fg-plain` 複製本，layer 自我 unload → 呈現原始幀。
  狀態走 `SCREENFG_STATE=paused/normal` env。
- **HUD**：運行期畫上幀率/倍數/layer 狀態；`SCREENFG_HUD` env 跨 re-exec 保留。
- **re-exec**：`execl` 同 binary 換 env 重新啟動（保留 argv，多跑一次 portal picker）。
- **synthetic**：`SCREENFG_SYNTHETIC=1` 不接 portal，跑合成幀（驗證呈現+layer）。
- **transport**（GUI）：`ScreenFgProcess` 的 `launch/readLine/kill` —— 負責子程序 + stdio IO，
  **不是** decode；decode 交給 shared ProtocolDecoder。

## 易混淆（flag）

- **display index vs card index**：config 的 `display` 是「FG 視窗覆蓋哪塊螢幕」的 index；
  `cardIndexFor(display)` 純函數映射到 PCI card 序号。**不可混用**（舊 code 曾把兩者當同一個 int）。
- **pause（暫停，re-exec 到 plain）≠ pause 時重跑 picker**：真實捕捉下 re-exec 會再跑
  portal picker（需重選窗）；synthetic 看不到。HUD 靠 env 保留。
- **status heartbeat 的 `state` 欄 vs re-exec 的 `SCREENFG_STATE` env**：前者是子程序向 GUI
  報的即時狀態（running/paused/exiting）；後者是父→子的 re-exec 狀態傳承。語義不同、別混。
