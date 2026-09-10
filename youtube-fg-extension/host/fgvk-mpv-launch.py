#!/usr/bin/env python3
"""Native messaging host：接 {url, multiplier} 或 {stop:true} → 管理 mpv + lsfg-vk FG（2x/3x/4x/10x）。

replace 語意：新一次啟動會先殺上一支 mpv（PID 持久化在 PID_FILE，因 host 每次 invocation 是短命 process）。
stop 語意：殺掉運行中的 mpv。
"""
import json
import os
import signal
import struct
import subprocess
import sys
import time

MPV = "mpv"
MPV_ARGS = ["--vo=gpu", "--gpu-api=vulkan"]
ALLOWED_MULT = (2, 3, 4, 10)
# 強制用第二張 R9700（Vulkan 的 GPU1、PCI 0000:07:00.0）。
# 兩張 R9700 同 device id（1002:7551），須以 PCI BDF 區分；改回 GPU0 換 0000:03:00.0，或刪掉此常數用預設。
GPU_SELECT = "1002:7551:0000:07:00.0"
PID_FILE = os.path.expanduser("~/.config/fgvk/mpv.pid")


def multiplier_to_profile(mult):
    """multiplier → lsfg-vk profile 名稱；不支援回 None。"""
    if mult not in ALLOWED_MULT:
        return None
    return f"{mult}x FG / 100%"


def build_argv(profile, url):
    """組出 mpv 啟動命令（argv list）。"""
    return [MPV, *MPV_ARGS, url]


def is_valid_video_url(url):
    """mirror url-utils.js isVideoUrl——任何 http(s) URL（mpv + yt-dlp 自動解析）。"""
    return url.startswith("http://") or url.startswith("https://")


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


def launch(profile_name, url):
    env = dict(os.environ, LSFGVK_PROFILE=profile_name, MESA_VK_DEVICE_SELECT=GPU_SELECT)
    old = read_pid()
    kill_pid(old)  # replace：先殺舊 mpv
    if old:
        wait_dead(old)  # 等舊的退出、釋放 GPU，避免新 mpv 的 Vulkan 初始化撞車
    proc = subprocess.Popen(build_argv(profile_name, url), env=env,
                           start_new_session=True,
                           stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                           stderr=subprocess.DEVNULL)
    write_pid(proc.pid)
    return True


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


def main():
    try:
        msg = read_message()
    except Exception as e:
        write_message({"ok": False, "error": f"訊息解析失敗: {e}"})
        return 1
    if msg is None:
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
    try:
        launch(profile, url)
    except FileNotFoundError:
        write_message({"ok": False, "error": "mpv 未安裝"})
        return 1
    write_message({"ok": True})
    return 0


if __name__ == "__main__":
    if "--selftest" in sys.argv:
        assert multiplier_to_profile(2) == "2x FG / 100%"
        assert multiplier_to_profile(3) == "3x FG / 100%"
        assert multiplier_to_profile(4) == "4x FG / 100%"
        assert multiplier_to_profile(10) == "10x FG / 100%"
        assert multiplier_to_profile(5) is None
        assert build_argv("3x FG / 100%", "https://x") == ["mpv", "--vo=gpu", "--gpu-api=vulkan", "https://x"]
        assert is_valid_video_url("https://www.youtube.com/watch?v=dQw4w9WgXcQ")
        assert is_valid_video_url("https://www.bilibili.com/video/BV1xx411c7mD")
        assert is_valid_video_url("https://example.com/video.mp4")
        assert not is_valid_video_url("chrome://extensions")
        assert not is_valid_video_url("file:///tmp/a.mp4")
        print("host selftest passed")
        sys.exit(0)
    sys.exit(main())
