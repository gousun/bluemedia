#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BlueMedia 共享核心：BLE 客户端 + 事件分发 + 模式映射（media_gui / media_host 共用）

线程模型：BLE 跑在独立后台线程(asyncio)，回调从 BLE 线程发出，
接收方（GUI/CLI）自行保证线程安全。

功能扩展：MODES 表加同编号条目；固件端 app_state.h 的 media_mode_t
加枚举即可，BLE 协议不变。
"""
import sys

# 关键：pycaw/comtypes 默认把 COM 初始化为 STA，与 bleak(WinRT) 要求的 MTA 冲突，
# 会导致扫描阶段 TimeoutError 崩溃。必须在 import comtypes 之前设置 MTA
# （注意 Windows 常量：COINIT_MULTITHREADED = 0x0）。
sys.coinit_flags = 0  # COINIT_MULTITHREADED

import asyncio
import ctypes
import json
import os
import sys as _sys
import threading

APP_VERSION = "1.0.0"
# GitHub 自更新仓库（owner/repo）。发布 Release（附件为 zip 包）后，
# 客户端启动时会检查最新 Release 并可一键自更新。
UPDATE_REPO = "gousun/bluemedia"

# 打包成 exe（PyInstaller onedir）后，模块在 _internal 里，资源与用户数据
# 必须放在 exe 同级目录：统一用 BASE_DIR 定位 web/ 与 user_modes.json
if getattr(_sys, "frozen", False):
    BASE_DIR = os.path.dirname(_sys.executable)
else:
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))

from bleak import BleakClient, BleakScanner

try:
    from pynput.keyboard import Controller, KeyCode, Key
    from pynput.mouse import Button as MouseButton
    from pynput.mouse import Controller as MouseController
except ImportError:
    sys.exit("缺少 pynput 库，请先执行: pip install -r requirements.txt")

try:
    from ctypes import cast, POINTER
    from comtypes import CLSCTX_ALL
    from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
    HAS_PYCAW = True
except ImportError:
    HAS_PYCAW = False

DEVICE_NAME = "BlueMedia"
UUID_RX = "0000ffe1-0000-1000-8000-00805f9b34fb"  # 写命令 → 设备
UUID_TX = "0000ffe2-0000-1000-8000-00805f9b34fb"  # 事件通知 ← 设备

KB = Controller()
MOUSE = MouseController()


def press_key(key):
    KB.press(key)
    KB.release(key)


def media_volume_up():
    press_key(Key.media_volume_up)


def media_volume_down():
    press_key(Key.media_volume_down)


def media_next():
    press_key(Key.media_next)


def media_prev():
    press_key(Key.media_previous)


def media_play_pause():
    press_key(Key.media_play_pause)


# ---------------- 模式映射表（功能扩展改这里） ----------------
# 抖音音量如需用客户端内置音量，把 rot_up/rot_down 改成
#   lambda: press_key(Key.right) / lambda: press_key(Key.left)
# （←→键，抖音窗口前台生效）
# "ui"：GUI 按钮文案（与动作保持同步）

MODES = {
    1: {
        "name": "音乐控制",
        "rot_up":   media_volume_up,      # 音量+
        "rot_down": media_volume_down,    # 音量-
        "single":   media_play_pause,     # 短按：播放/暂停
        "double":   media_next,           # 双击：下一首
        "ui": {"rot": "音量", "single": "播放 / 暂停", "double": "下一首"},
    },
    2: {
        "name": "抖音控制",
        "rot_up":   media_volume_up,                  # 音量+（系统音量）
        "rot_down": media_volume_down,                # 音量-
        "single":   lambda: press_key(Key.down),      # 短按：下一个视频
        "double":   lambda: press_key(Key.space),     # 双击：暂停/播放（空格键）
        "ui": {"rot": "音量", "single": "下一个视频", "double": "暂停 / 播放"},
        # 上一视频暂无手势；如想用媒体键暂停可改为 media_play_pause（无需前台）
    },
}

# ---------------- 自定义模式（槽位3/4：名字与映射可在GUI编辑，名字同步ESP32） ----------------
# 绑定值格式：
#   "key:<pynput Key名>"        单个键盘键，如 key:f5 / key:media_play_pause
#   "char:<单字符>"             字符键，如 char:a
#   "mouse:<按键名>"            鼠标按键 left/right/middle/x1/x2（在光标位置点击）
#   "combo:<修饰键+键>"         快捷键，修饰键 ctrl/alt/shift/win，如 combo:ctrl+c
#   ""                          未映射

USER_CONFIG_PATH = os.path.join(BASE_DIR, "user_modes.json")
USER_MODE_DEFAULTS = {"name": "", "single": "", "double": "", "rot_up": "", "rot_down": ""}

KEY_LABELS = {
    "space": "空格", "enter": "回车", "esc": "Esc", "tab": "Tab",
    "up": "↑", "down": "↓", "left": "←", "right": "→",
    "delete": "Delete", "backspace": "退格", "home": "Home", "end": "End",
    "insert": "Insert", "page_up": "PgUp", "page_down": "PgDn",
    "caps_lock": "大写锁定", "print_screen": "截屏键",
    "media_volume_up": "音量+", "media_volume_down": "音量-", "media_mute": "静音",
    "media_play_pause": "播放/暂停", "media_next": "下一首", "media_previous": "上一首",
}
MOD_KEYS = {"ctrl": Key.ctrl, "alt": Key.alt, "shift": Key.shift, "win": Key.cmd}
MOD_LABELS = {"ctrl": "Ctrl", "alt": "Alt", "shift": "Shift", "win": "Win"}
MOUSE_LABELS = {"left": "鼠标左键", "right": "鼠标右键", "middle": "鼠标中键",
                "x1": "鼠标侧键1", "x2": "鼠标侧键2"}


def _default_user_mode(idx):
    return {"name": f"USER{idx - 2}", "single": "", "double": "", "rot_up": "", "rot_down": ""}


def load_user_config():
    cfg = {"3": _default_user_mode(3), "4": _default_user_mode(4)}
    try:
        with open(USER_CONFIG_PATH, encoding="utf-8") as f:
            saved = json.load(f)
        for k in ("3", "4"):
            if isinstance(saved.get(k), dict):
                for key in cfg[k]:
                    if key in saved[k]:
                        cfg[k][key] = str(saved[k][key])[:40]
    except Exception:
        pass
    return cfg


def save_user_mode(mode, name, bindings):
    """保存自定义模式（写配置文件 + 更新MODES名）。name 已由调用方清洗为<=6位ASCII。"""
    u = USER_CFG[str(mode)]
    u["name"] = name
    u.update(bindings)
    try:
        with open(USER_CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(USER_CFG, f, ensure_ascii=False, indent=2)
    except Exception:
        pass
    MODES[mode]["name"] = name or f"USER{mode - 2}"


def run_binding(spec):
    """执行一条绑定（在调用者的上下文中模拟按键/点击）。异常向上抛，由dispatch捕获。"""
    if not spec:
        return
    kind, _, val = spec.partition(":")
    if kind == "key":
        k = getattr(Key, val, None)
        if k is not None:
            press_key(k)
    elif kind == "char":
        key = KeyCode.from_char(val)
        KB.press(key)
        KB.release(key)
    elif kind == "mouse":
        b = getattr(MouseButton, val, None)
        if b is not None:
            MOUSE.click(b)
    elif kind == "combo":
        parts = val.split("+")
        keys = [MOD_KEYS[p] for p in parts[:-1] if p in MOD_KEYS]
        last = parts[-1]
        keys.append(getattr(Key, last, None) or KeyCode.from_char(last))
        for k in keys:
            KB.press(k)
        for k in reversed(keys):
            KB.release(k)


def describe_binding(spec):
    """绑定值 → 中文描述（GUI 按钮文案）"""
    if not spec:
        return "未设置"
    kind, _, val = spec.partition(":")
    if kind == "key":
        return KEY_LABELS.get(val, val.upper() if len(val) <= 3 else val)
    if kind == "char":
        return f"按键 {val.upper()}"
    if kind == "mouse":
        return MOUSE_LABELS.get(val, val)
    if kind == "combo":
        parts = val.split("+")
        mods = [MOD_LABELS.get(p, p) for p in parts[:-1]]
        last = parts[-1]
        mods.append(KEY_LABELS.get(last, last.upper() if len(last) <= 3 else last))
        return "+".join(mods)
    return spec


USER_CFG = load_user_config()

MODES[3] = {
    "name": USER_CFG["3"]["name"],
    "rot_up":   lambda: run_binding(USER_CFG["3"]["rot_up"]),
    "rot_down": lambda: run_binding(USER_CFG["3"]["rot_down"]),
    "single":   lambda: run_binding(USER_CFG["3"]["single"]),
    "double":   lambda: run_binding(USER_CFG["3"]["double"]),
    "custom":   True,
}
MODES[4] = {
    "name": USER_CFG["4"]["name"],
    "rot_up":   lambda: run_binding(USER_CFG["4"]["rot_up"]),
    "rot_down": lambda: run_binding(USER_CFG["4"]["rot_down"]),
    "single":   lambda: run_binding(USER_CFG["4"]["single"]),
    "double":   lambda: run_binding(USER_CFG["4"]["double"]),
    "custom":   True,
}


def _com_mta():
    """当前线程未初始化 COM 时初始化为 MTA。
    BLE 线程由 bleak/WinRT 初始化，但 GUI（pywebview 桥接线程）等调用 pycaw
    的线程需要自行初始化，否则 COM 调用失败。"""
    try:
        ctypes.windll.ole32.CoInitializeEx(None, 0)   # COINIT_MULTITHREADED
    except Exception:
        pass


volume_error = None    #最近一次音量读取失败原因（诊断用）


def _endpoint_volume():
    """取系统默认输出设备的 IAudioEndpointVolume。
    新版 pycaw：AudioUtilities.GetSpeakers() 返回 AudioDevice（直接带 EndpointVolume 属性）；
    旧版：返回需 Activate 的原始对象。两种都兼容。"""
    dev = AudioUtilities.GetSpeakers()
    ev = getattr(dev, "EndpointVolume", None)
    if ev is not None:
        return ev
    return dev.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)


def get_volume():
    """Windows 系统音量 0~100；pycaw 不可用/调用失败返回 None。
    注意：COM 初始化跟随调用线程，GUI 线程请经 MediaClient.call_in_ble_thread 调用。"""
    global volume_error
    if not HAS_PYCAW:
        return None
    _com_mta()
    try:
        return round(_endpoint_volume().GetMasterVolumeLevelScalar() * 100)
    except Exception as e:
        volume_error = f"{type(e).__name__}: {e}"
        return None


def set_volume(pct):
    """设置 Windows 系统音量 0~100；pycaw 不可用时返回 False"""
    if not HAS_PYCAW:
        return False
    _com_mta()
    try:
        _endpoint_volume().SetMasterVolumeLevelScalar(
            max(0, min(100, int(pct))) / 100.0, None)
        return True
    except Exception:
        return False


class MediaClient:
    """BLE 客户端：自动扫描/连接/重连 + 事件分发。
    回调（on_event/on_status/on_connection/on_log）在 BLE 后台线程中调用。"""

    def __init__(self, on_event=None, on_status=None, on_connection=None, on_log=None, address=None):
        self.on_event = on_event or (lambda text: None)
        self.on_status = on_status or (lambda st: None)
        self.on_connection = on_connection or (lambda ok, info: None)
        self.on_log = on_log or (lambda text: None)
        self.address = address            # None=按名称扫描
        self.mode = 1                     # 当前模式（1起，与设备 EV:MODE:n 一致）
        self.connected = False
        self._stop = False
        self._loop = None
        self._client = None
        self._thread = threading.Thread(target=self._thread_main, daemon=True)

    # ---- 生命周期 ----
    def start(self):
        self._thread.start()

    def stop(self):
        self._stop = True

    def _thread_main(self):
        try:
            asyncio.run(self._main())
        except Exception as e:
            self.on_log(f"BLE线程异常退出: {type(e).__name__}: {e}")

    async def _main(self):
        self._loop = asyncio.get_running_loop()
        while not self._stop:
            try:
                if self.address:
                    dev = await BleakScanner.find_device_by_address(self.address, timeout=8)
                else:
                    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
                if dev is None:
                    self.on_connection(False, "未发现设备")
                    await asyncio.sleep(2)
                    continue
                self.on_log(f"连接 {dev.address} ...")
                async with BleakClient(dev, timeout=15.0) as client:
                    self._client = client
                    self.connected = True
                    self.on_connection(True, dev.address)
                    await client.start_notify(UUID_TX, self._on_notify)
                    await client.write_gatt_char(UUID_RX, b"STATUS")
                    while not self._stop and client.is_connected:
                        await asyncio.sleep(0.5)
                    self.connected = False
                    self._client = None
                    if not self._stop:
                        self.on_connection(False, "连接断开")
            except asyncio.CancelledError:
                return
            except Exception as e:
                self.connected = False
                self._client = None
                self.on_connection(False, f"{type(e).__name__}: {e}")
                if not self._stop:
                    await asyncio.sleep(3)
        self.on_log("BLE线程已停止")

    # ---- 数据接收/分发 ----
    def _on_notify(self, _sender, data: bytes):
        text = data.decode("utf-8", "replace").strip()
        if not text:
            return
        if text.startswith("{"):
            try:
                st = json.loads(text)
                if "mode" in st:
                    self.mode = int(st["mode"])
                self.on_status(st)
            except json.JSONDecodeError:
                self.on_log(f"状态解析失败: {text}")
            return
        self.on_event(text)
        try:
            self.dispatch(text)
        except Exception as e:
            self.on_log(f"事件处理失败: {type(e).__name__}: {e}")

    def dispatch(self, event: str):
        """把设备事件映射为当前模式的动作（GUI 手动按钮也用它发合成事件）"""
        if event.startswith("EV:MODE:"):
            try:
                self.mode = int(event.split(":", 2)[2])
            except ValueError:
                return
            name = MODES.get(self.mode, {}).get("name", "?")
            self.on_log(f"设备切换到模式 {self.mode}: {name}")
            if self.mode == 2:
                self.on_log("抖音模式：请保持抖音窗口为前台焦点")
            return
        if event.startswith("EV:KEY:L"):
            return  # 长按由设备切换模式并另发 EV:MODE，不重复处理

        mode = MODES.get(self.mode)
        if mode is None:
            self.on_log(f"模式 {self.mode} 未定义映射")
            return

        if event.startswith("EV:ROT:"):
            d = event.split(":", 2)[2]
            (mode["rot_up"] if d.startswith("+") else mode["rot_down"])()
            vol = get_volume()
            if vol is not None:
                self.send(f"VOL:{vol}")
        elif event == "EV:KEY:S":
            mode["single"]()
        elif event == "EV:KEY:D":
            mode["double"]()
        else:
            self.on_log(f"未知事件: {event}")

    # ---- 发送 ----
    def call_in_ble_thread(self, fn, timeout=1.0):
        """在 BLE 后台线程执行 fn（该线程 COM 为 MTA，pycaw 等调用可靠）。
        阻塞至完成或超时；未连接/超时返回 None。"""
        loop = self._loop
        if loop is None:
            return None
        fut = asyncio.run_coroutine_threadsafe(self._run_fn(fn), loop)
        try:
            return fut.result(timeout)
        except Exception:
            return None

    async def _run_fn(self, fn):
        return fn()

    def send(self, text: str):
        """线程安全发送命令给设备"""
        loop, client = self._loop, self._client
        if loop is None or client is None or not self.connected:
            self.on_log(f"未连接，丢弃: {text}")
            return
        asyncio.run_coroutine_threadsafe(self._send_task(text), loop)

    async def _send_task(self, text: str):
        try:
            if self._client and self._client.is_connected:
                await self._client.write_gatt_char(UUID_RX, text.encode("ascii"))
        except Exception as e:
            self.on_log(f"发送失败 {text}: {e}")

    def set_mode(self, n: int):
        """上位机直接切换设备模式（1起）"""
        if n in MODES:
            self.mode = n
            self.send(f"MODE:{n}")
        else:
            self.on_log(f"未知模式编号: {n}")
