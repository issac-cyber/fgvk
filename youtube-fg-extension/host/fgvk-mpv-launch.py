#!/usr/bin/env python3
"""Native messaging host：接 {url, multiplier} 或 {stop:true} → 管理 mpv + lsfg-vk FG（2x/3x/4x/10x）。

- replace 語意：新一次啟動會先殺上一支 mpv（PID 持久化在 PID_FILE，因 host 每次 invocation 是短命 process）。
- stop 語意：殺掉運行中的 mpv。
- profile 自動修復：啟動前檢查 conf.toml 有標準 4 支 profile，缺了就補（只補缺的、絕不覆蓋現有的）——防 Lossless Scaling GUI 存檔把 conf.toml 覆蓋掉 profile。
- 錯誤回饋：啟動 mpv 後等 STARTUP_CHECK 秒確認它還活著；若已退出（如影片不可用/DRM/URL 錯），讀 log 回傳有用錯誤。
"""
import json
import os
import select
import signal
import struct
import subprocess
import sys
import time
import tomllib

MPV = "mpv"
MPV_ARGS = ["--vo=gpu", "--gpu-api=vulkan"]
ALLOWED_MULT = (2, 3, 4, 10)
# 強制用第二張 R9700（Vulkan 的 GPU1、PCI 0000:07:00.0）。
# 兩張 R9700 同 device id（1002:7551），須以 PCI BDF 區分；改回 GPU0 換 0000:03:00.0，或刪掉此常數用預設。
GPU_SELECT = "1002:7551:0000:07:00.0"
PID_FILE = os.path.expanduser("~/.config/fgvk/mpv.pid")
CONF = os.path.expanduser("~/.config/lsfg-vk/conf.toml")
MPV_LOG = os.path.expanduser("~/.config/fgvk/mpv.log")
STARTUP_CHECK_SECS = 3.0
STANDARD_PROFILES = {
    "2x FG / 100%": 2,
    "3x FG / 100%": 3,
    "4x FG / 100%": 4,
    "10x FG / 100%": 10,
}


def multiplier_to_profile(mult):
    """multiplier → lsfg-vk profile 名稱；不支援回 None。"""
    if mult not in ALLOWED_MULT:
        return None
    return f"{mult}x FG / 100%"


def build_argv(profile, url, extra_args=None):
    """組出 mpv 啟動命令（argv list）。extra_args 是 site-specific 的附加參數（如 anime1.me 的 Cookie header）。"""
    return [MPV, *MPV_ARGS, *(extra_args or []), url]


def is_valid_video_url(url):
    """mirror url-utils.js isVideoUrl——任何 http(s) URL（mpv + yt-dlp 自動解析）。"""
    return url.startswith("http://") or url.startswith("https://")


def resolve_anime1(url):
    """anime1.me 頁面 → (直接 MP4 src, Cookie header 值)。yt-dlp 解析不了此站，走兩段 API＋Cookie。
    1) GET 集數頁抓 <video data-apireq>；2) POST v.anime1.me/api（d=apireq）拿 src＋3 支 Cookie；
    3) MP4 需帶那 3 支 Cookie（nginx 驗證），media host 會輪轉（muan/miru/...），用 API 回的 src。"""
    import re
    import http.cookiejar
    import urllib.parse
    import urllib.request
    ua = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"}

    def get(opener, u, headers=None, data=None):
        return opener.open(urllib.request.Request(u, headers={**ua, **(headers or {})}, data=data), timeout=30)

    m = re.search(r"anime1\.me/(\d+)", url) or re.search(r"anime1\.me/\?p=(\d+)", url)
    if m:
        page = f"https://anime1.me/{m.group(1)}"
    else:
        m = re.search(r"anime1\.me/\?cat=(\d+)", url)
        if not m:
            raise RuntimeError("無法辨認的 anime1.me URL（需 /postid、?p= 或 ?cat=）")
        cj0 = http.cookiejar.CookieJar()
        op0 = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj0))
        html = get(op0, f"https://anime1.me/?cat={m.group(1)}").read().decode("utf-8", "replace")
        first = re.search(r'<a[^>]*href="https://anime1\.me/(\d+)"', html)
        if not first:
            raise RuntimeError("season 頁找不到任何集數")
        page = f"https://anime1.me/{first.group(1)}"

    cj = http.cookiejar.CookieJar()
    op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))
    html = get(op, page).read().decode("utf-8", "replace")
    m = re.search(r'data-apireq="([^"]+)"', html)
    if not m:
        raise RuntimeError("頁面沒有影片（找不到 data-apireq）")
    apireq = urllib.parse.unquote(m.group(1))
    body = json.loads(get(
        op, "https://v.anime1.me/api",
        {"Content-Type": "application/x-www-form-urlencoded", "Origin": "https://anime1.me", "Referer": page},
        urllib.parse.urlencode({"d": apireq}).encode()).read().decode())
    src = body["s"][0]["src"]
    if not src.startswith("http"):
        src = "https:" + src
    cookie = "; ".join(f"{c.name}={c.value}" for c in cj)
    return src, cookie


def resolve_hanime1(url):
    """hanime1.me watch 頁 → 最高畫質直接 MP4 src。yt-dlp 無此站 extractor；
    <source> tag 直接 MP4（vdownload.hembed.com/{id}-{quality}p.mp4?secure=...），不 gate（plain GET 即可）。"""
    import re
    import urllib.request
    ua = {"User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36"}
    m = re.search(r"hanime1\.me/watch\?v=(\d+)", url)
    if not m:
        raise RuntimeError("無法辨認的 hanime1.me URL（需 /watch?v=<id>）")
    req = urllib.request.Request(f"https://hanime1.me/watch?v={m.group(1)}", headers=ua)
    html = urllib.request.urlopen(req, timeout=30).read().decode("utf-8", "replace")
    srcs = re.findall(r'<source[^>]*src="(https?://[^"]*\.mp4[^"]*)"', html)
    if not srcs:
        raise RuntimeError("頁面沒有影片（找不到 <source> MP4）")

    def quality(s):
        mm = re.search(r"-(\d+)p\.mp4", s)
        return int(mm.group(1)) if mm else 0

    return max(srcs, key=quality)


def read_pid():
    """讀運行中 mpv 的 PID；驗證 alive 且確為 mpv（防 PID 重用殺錯）。無則回 None。"""
    try:
        with open(PID_FILE) as f:
            pid = int(f.read().strip())
    except (FileNotFoundError, ValueError):
        return None
    try:
        os.kill(pid, 0)
        if open(f"/proc/{pid}/comm").read().strip() != "mpv":
            return None
    except (ProcessLookupError, PermissionError, FileNotFoundError):
        return None
    return pid


def write_pid(pid):
    os.makedirs(os.path.dirname(PID_FILE), exist_ok=True)
    with open(PID_FILE, "w") as f:
        f.write(str(pid))


def kill_pid(pid):
    if pid:
        try:
            os.kill(pid, signal.SIGTERM)
        except (ProcessLookupError, PermissionError):
            pass


def wait_dead(pid, timeout=1.0):
    """等 process 退出（舊 mpv 非本 host 的子程序，無法 waitpid，只能 poll /proc）。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            os.kill(pid, 0)
        except (ProcessLookupError, PermissionError):
            return
        time.sleep(0.1)


def do_stop():
    """停止運行中的 mpv；回是否真的有 mpv 被停。"""
    pid = read_pid()
    kill_pid(pid)
    try:
        os.remove(PID_FILE)
    except FileNotFoundError:
        pass
    return pid is not None


def load_profile_names(path=CONF):
    """讀 conf.toml 的 profile 名稱集合；讀不到/不存在/解碼錯回 None。"""
    try:
        with open(path, "rb") as f:
            data = tomllib.load(f)
    except (FileNotFoundError, OSError, tomllib.TOMLDecodeError):
        return None
    return {p.get("name") for p in data.get("profile", [])}


def profile_block(name, mult):
    """組出單一 [[profile]] 的 TOML 文字（標準欄位）。"""
    return (
        "\n[[profile]]\n"
        "flow_scale = 1.0\n"
        f"multiplier = {mult}\n"
        f'name = "{name}"\n'
        "override_present_mode = true\n"
        'pacing_mode = "vsync"\n'
        "performance_mode = false\n"
        "preserve_swapchain_image_count = false\n"
    )


def ensure_profiles(path=CONF):
    """確保標準 4 支 profile 存在（只補缺的、絕不覆蓋現有的）。回 (修復的名稱 list, 錯誤字串或 None)。"""
    if not os.path.exists(path):
        return [], f"lsfg-vk 的 conf.toml 不存在（{path}），請先開啟一次 Lossless Scaling 建立設定"
    names = load_profile_names(path)
    if names is None:
        return [], f"讀取 {path} 失敗（TOML 解碼錯誤？）"
    missing = [n for n in STANDARD_PROFILES if n not in names]
    if not missing:
        return [], None
    try:
        with open(path, "a") as f:
            for n in missing:
                f.write(profile_block(n, STANDARD_PROFILES[n]))
    except OSError as e:
        return [], f"寫入 {path} 失敗：{e}"
    return missing, None


def read_mpve_error(log, max_lines=5):
    """從 mpv log 抓錯誤（優先含錯誤關鍵字的行）。"""
    try:
        lines = [l.strip() for l in open(log, encoding="utf-8", errors="ignore").read().splitlines() if l.strip()]
    except OSError:
        return "mpv 啟動後立即退出（無法讀取 log）"
    errs = [l for l in lines if any(k in l for k in ("ERROR", "error", "Failed", "failed", "denied", "unavailable", "truncated"))]
    pick = (errs or lines)[-max_lines:]
    return ("mpv 立即退出：" + " | ".join(pick))[:300]


def launch(profile_name, url, extra_args=None):
    """啟動 mpv；等 STARTUP_CHECK 秒確認存活。回 (ok, 錯誤字串或 None)。"""
    env = dict(os.environ, LSFGVK_PROFILE=profile_name, MESA_VK_DEVICE_SELECT=GPU_SELECT)
    old = read_pid()
    kill_pid(old)  # replace：先殺舊 mpv
    if old:
        wait_dead(old)  # 等舊的退出、釋放 GPU，避免新 mpv 的 Vulkan 初始化撞車
    os.makedirs(os.path.dirname(MPV_LOG), exist_ok=True)
    lf = open(MPV_LOG, "wb")
    proc = subprocess.Popen(build_argv(profile_name, url, extra_args), env=env,
                           start_new_session=True, stdin=subprocess.DEVNULL,
                           stdout=lf, stderr=lf)
    time.sleep(STARTUP_CHECK_SECS)
    # proc.poll() 正確偵測退出（含 reap zombie）；os.kill(pid,0) 會被 zombie 騙到（以為還活著）
    alive = proc.poll() is None
    lf.close()  # mpv 持有自己的 fd，關掉 parent 端的 handle 安全
    if not alive:
        return False, read_mpve_error(MPV_LOG)
    write_pid(proc.pid)
    return True, None


def read_message():
    """讀 Native Messaging 訊息（4 字节 little-endian 長度 + JSON）。"""
    raw = sys.stdin.buffer.read(4)
    if len(raw) < 4:
        return None
    (length,) = struct.unpack("<I", raw)
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def write_message(obj):
    """寫 Native Messaging 回應。"""
    data = json.dumps(obj).encode("utf-8")
    sys.stdout.buffer.write(struct.pack("<I", len(data)))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def monitor_loop():
    """connectNative port 模式：常駐監控 mpv 存活（每 ~1s），mpv 從運行→退出（使用者手動關窗）時 push {event:mpv_exited}。
    只讀 PID_FILE（read_pid 只驗證、不殺），不與 launch/stop 的 host 衝突。"""
    last_pid = None
    while True:
        cur = read_pid()
        if last_pid is not None and cur is None:
            write_message({"event": "mpv_exited"})
            last_pid = None
        if cur is not None:
            last_pid = cur
        try:
            r, _, _ = select.select([sys.stdin], [], [], 1.0)
        except OSError:
            break
        if r:
            raw = sys.stdin.buffer.read(4)
            if len(raw) < 4:
                break  # port 斷開（worker 關閉 / Chrome 關）
            (length,) = struct.unpack("<I", raw)
            try:
                msg = json.loads(sys.stdin.buffer.read(length).decode("utf-8"))
            except Exception:
                continue
            if msg.get("stop"):
                write_message({"ok": True, "stopped": do_stop()})


def main():
    try:
        msg = read_message()
    except Exception as e:
        write_message({"ok": False, "error": f"訊息解析失敗: {e}"})
        return 1
    if msg is None:
        return 0
    if msg.get("monitor"):
        monitor_loop()
        return 0
    if msg.get("stop"):
        write_message({"ok": True, "stopped": do_stop()})
        return 0
    url = msg.get("url", "")
    mult = msg.get("multiplier", 0)
    profile = multiplier_to_profile(mult)
    if profile is None:
        write_message({"ok": False, "error": f"倍率 {mult} 不支援（只接受 2/3/4/10）"})
        return 1
    if not is_valid_video_url(url):
        write_message({"ok": False, "error": "不是 http(s) 網址"})
        return 1
    extra_args = []
    # 注意：hanime1.me 的域名「包含」anime1.me 子字串，須先查 hanime1（if/elif 互斥）
    if "hanime1.me" in url:  # site-specific：yt-dlp 無 extractor，<source> 直接 MP4（不 gate）
        try:
            url = resolve_hanime1(url)
        except Exception as e:
            write_message({"ok": False, "error": f"hanime1.me 解析失敗：{e}"})
            return 1
    elif "anime1.me" in url:  # site-specific：yt-dlp 解析不了，走兩段 API＋Cookie
        try:
            url, cookie = resolve_anime1(url)
            extra_args = ["--http-header-fields=Cookie: " + cookie]
        except Exception as e:
            write_message({"ok": False, "error": f"anime1.me 解析失敗：{e}"})
            return 1
    repaired, err = ensure_profiles()  # profile 自動修復
    if err:
        write_message({"ok": False, "error": err})
        return 1
    ok, err = launch(profile, url, extra_args)  # 啟動＋存活檢查（錯誤回饋）
    if not ok:
        write_message({"ok": False, "error": err})
        return 1
    write_message({"ok": True, "repaired": repaired})
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        import tempfile
        assert multiplier_to_profile(2) == "2x FG / 100%"
        assert multiplier_to_profile(3) == "3x FG / 100%"
        assert multiplier_to_profile(4) == "4x FG / 100%"
        assert multiplier_to_profile(10) == "10x FG / 100%"
        assert multiplier_to_profile(5) is None
        assert build_argv("3x FG / 100%", "https://x") == ["mpv", "--vo=gpu", "--gpu-api=vulkan", "https://x"]
        assert is_valid_video_url("https://www.youtube.com/watch?v=dQw4w9WgXcQ")
        assert is_valid_video_url("https://example.com/video.mp4")
        assert not is_valid_video_url("chrome://extensions")
        assert not is_valid_video_url("file:///tmp/a.mp4")
        # profile 自動修復（用 temp 檔，不碰真實 conf.toml）
        blk = profile_block("2x FG / 100%", 2)
        assert 'name = "2x FG / 100%"' in blk and "multiplier = 2" in blk
        with tempfile.TemporaryDirectory() as d:
            conf = os.path.join(d, "conf.toml")
            with open(conf, "w") as f:
                f.write('version = 2\n\n[global]\nlog_level = "info"\n\n[[profile]]\nmultiplier = 2\nname = "2x FG / 100%"\n')
            assert load_profile_names(conf) == {"2x FG / 100%"}
            repaired, err = ensure_profiles(conf)
            assert err is None
            assert repaired == ["3x FG / 100%", "4x FG / 100%", "10x FG / 100%"]
            names = load_profile_names(conf)  # 修復後仍是有效 TOML 且 4 支齊
            assert names is not None and set(names) == set(STANDARD_PROFILES)
            repaired2, err2 = ensure_profiles(conf)  # 冪等：再跑一次不加
            assert err2 is None and repaired2 == []
        print("host selftest passed")
        sys.exit(0)
    sys.exit(main())
