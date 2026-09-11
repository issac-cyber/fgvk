// background.js — MV3 service worker：常駐監控 mpv 存活
//
// 原理：chrome.runtime.connectNative 的 port 開啟時，host 常駐 + 此 worker 不被 Chrome 暫停。
// host 的 monitor_loop 每 ~1s 檢查 mpv PID；mpv 從運行→退出（使用者手動關窗）時 push
// {event:"mpv_exited"} 回來，worker 還原來源 tab（解除靜音＋播放影片）並關閉監控。
// 讓「使用者自己關 mpv」也能還原 Chrome 音頻/影片，不必非按 popup 的「停止」。

let monitorPort = null;

async function restoreTab() {
  const { mutedTabId } = await chrome.storage.local.get("mutedTabId");
  if (mutedTabId == null) return;
  try {
    await chrome.tabs.update(mutedTabId, { muted: false }); // 還原音頻（tabs 權限即可）
  } catch (e) {}
  try {
    await chrome.scripting.executeScript({
      target: { tabId: mutedTabId },
      func: () => {
        const v = document.querySelector("video");
        if (v) v.play().catch(() => {}); // 還原影片播放（需 host 權限）
      },
    });
  } catch (e) {}
  await chrome.storage.local.remove("mutedTabId");
}

function openMonitor() {
  if (monitorPort) return;
  let port;
  try {
    port = chrome.runtime.connectNative("com.fgvk.host");
  } catch (e) {
    return;
  }
  monitorPort = port;
  port.onMessage.addListener((msg) => {
    if (msg && msg.event === "mpv_exited") {
      restoreTab();
      closeMonitor();
    }
  });
  port.onDisconnect.addListener(() => {
    if (monitorPort === port) monitorPort = null;
  });
  port.postMessage({ monitor: true });
}

function closeMonitor() {
  if (monitorPort) {
    try {
      monitorPort.disconnect();
    } catch (e) {}
    monitorPort = null;
  }
}

chrome.runtime.onMessage.addListener((msg) => {
  if (!msg) return;
  if (msg.type === "mpv_started") openMonitor();
  else if (msg.type === "mpv_stopped") closeMonitor();
});

// worker 啟動時若已有進行中的 session（browser 重啟 / worker 重新實例化），重開監控
chrome.storage.local.get("mutedTabId").then(({ mutedTabId }) => {
  if (mutedTabId != null) openMonitor();
});
