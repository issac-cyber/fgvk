#!/usr/bin/env python3
"""Native Messaging host：收 {url, multiplier} → 啟動 mpv + lsfg-vk FG（2x/3x/4x）。"""
import json
import os
import struct
import subprocess
import sys

MPV = "mpv"
MPV_ARGS = ["--vo=gpu", "--gpu-api=vulkan"]
ALLOWED_MULT = (2, 3, 4)


def multiplier_to_profile(mult):
    """multiplier → lsfg-vk profile 名；不支援回 None。"""
    if mult not in ALLOWED_MULT:
        return None
    return f"{mult}x FG / 100%"


def build_argv(profile, url):
    """組 mpv 啟動命令（argv 清單）。"""
    return [MPV, *MPV_ARGS, url]


def read_message():
    """讀 Native Messaging 訊息（4-byte little-endian 長度 + JSON）。"""
    raw = sys.stdin.buffer.read(4)
    if len(raw) < 4:
        return None  # stdin 關閉（Chrome 結束 host）
    (length,) = struct.unpack("<I", raw)
    return json.loads(sys.stdin.buffer.read(length).decode("utf-8"))


def write_message(obj):
    """寫 Native Messaging 回應。"""
    data = json.dumps(obj).encode("utf-8")
    sys.stdout.buffer.write(struct.pack("<I", len(data)))
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def launch(profile_name, url):
    env = dict(os.environ, LSFGVK_PROFILE=profile_name)
    subprocess.Popen(build_argv(profile_name, url), env=env,
                     start_new_session=True,
                     stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL)
    return True


def main():
    try:
        msg = read_message()
    except Exception as e:
        write_message({"ok": False, "error": f"訊息解析失敗: {e}"})
        return 1
    if msg is None:
        return 0
    url = msg.get("url", "")
    mult = msg.get("multiplier", 0)
    profile = multiplier_to_profile(mult)
    if profile is None:
        write_message({"ok": False, "error": f"倍率 {mult} 不支援（只接受 2/3/4）"})
        return 1
    if not url.startswith("https://www.youtube.com/watch?v="):
        write_message({"ok": False, "error": "不是 YouTube 影片網址"})
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
        assert multiplier_to_profile(5) is None
        assert build_argv("3x FG / 100%", "https://x") == ["mpv", "--vo=gpu", "--gpu-api=vulkan", "https://x"]
        print("host selftest passed")
        sys.exit(0)
    sys.exit(main())
