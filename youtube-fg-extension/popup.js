// popup.js
const statusEl = document.getElementById("status");

function setStatus(text) {
  statusEl.textContent = text;
}

// 暫停／恢復頁面上的 <video>（chrome.scripting，靠 activeTab，點圖示即授予）；找不到 video 或失敗就 no-op。
async function setVideoPlaying(tabId, playing) {
  try {
    await chrome.scripting.executeScript({
      target: { tabId: tabId },
      func: (p) => {
        const v = document.querySelector('video');
        if (v) {
          if (p) { v.play().catch(() => {}); } else { v.pause(); }
        }
      },
      args: [playing],
    });
  } catch (e) {}
}

// 還原來源 tab：解除靜音＋恢復影片＋清除記錄（stop 與啟動失敗 rollback 共用）。
async function restoreTab(tabId) {
  if (tabId == null) return;
  try { await chrome.tabs.update(tabId, { muted: false }); } catch (e) {}
  await setVideoPlaying(tabId, true);
  try { await chrome.storage.local.remove("mutedTabId"); } catch (e) {}
}

async function launch(mult) {
  const [tab] = await chrome.tabs.query({ active: true, currentWindow: true });
  const url = tab && tab.url;
  if (!isVideoUrl(url)) {
    setStatus("此頁非 http(s) 網頁");
    return;
  }
  // 靜音來源 tab ＋ 暫停來源影片（音頻改由 mpv 提供 → 單一播放器，音畫同步最佳）+ 記住 tab id（stop 時解除）
  if (tab && tab.id != null) {
    try {
      await chrome.tabs.update(tab.id, { muted: true });
      await chrome.storage.local.set({ mutedTabId: tab.id });
    } catch (e) {}
    await setVideoPlaying(tab.id, false);
  }
  const profile = multiplierToProfile(mult);
  chrome.runtime.sendNativeMessage("com.fgvk.host", { url, multiplier: mult }, (resp) => {
    if (chrome.runtime.lastError) {
      restoreTab(tab && tab.id);  // 連不上 host → 還原來源 tab
      setStatus("未安裝 host：" + chrome.runtime.lastError.message);
      return;
    }
    if (resp && resp.ok) {
      setStatus(`已啟動（${profile}）· 來源 tab 已靜音＋暫停`);
      // 通知 background worker 開啟 mpv 存活監控（關 mpv 窗自動還原 tab）
      chrome.runtime.sendMessage({ type: "mpv_started" }).catch(() => {});
    } else {
      restoreTab(tab && tab.id);  // 啟動失敗 → 還原來源 tab
      setStatus((resp && resp.error) || "啟動失敗");
    }
  });
}

async function stop() {
  const { mutedTabId } = await chrome.storage.local.get("mutedTabId");
  await restoreTab(mutedTabId);  // 還原來源 tab（解除靜音＋恢復影片）
  chrome.runtime.sendNativeMessage("com.fgvk.host", { stop: true }, (resp) => {
    if (chrome.runtime.lastError) {
      setStatus("未安裝 host：" + chrome.runtime.lastError.message);
      return;
    }
    setStatus(resp && resp.ok ? "已停止" : "停止失敗");
  });
  // 通知 worker 關閉監控（mpv 已被主動停止，不用再等它退出）
  chrome.runtime.sendMessage({ type: "mpv_stopped" }).catch(() => {});
}

document.querySelectorAll("button[data-mult]").forEach((btn) => {
  btn.addEventListener("click", () => launch(Number(btn.dataset.mult)));
});
document.getElementById("stopBtn").addEventListener("click", stop);
