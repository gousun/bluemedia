#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BlueMedia 控制台 GUI（pywebview + HTML 毛玻璃界面）

BLE 逻辑在 media_core.MediaClient（独立后台线程），本文件只做：
  - 创建 WebView2 窗口加载 web/index.html
  - js_api 桥：JS 轮询 poll() 取事件队列、调用 set_mode/manual/set_volume
"""
import os
import queue
import sys
import traceback

import webview

import media_core
import updater
from media_core import (HAS_PYCAW, MediaClient, describe_binding, get_volume,
                        set_volume)

HERE = media_core.BASE_DIR


class Api:
    """暴露给 JS 的桥（window.pywebview.api.*）"""

    def __init__(self):
        self.q = queue.Queue()
        self.vol_err_reported = False
        self.client = MediaClient(
            on_event=lambda t: self.q.put(("event", t)),
            on_status=lambda st: self.q.put(("status", st)),
            on_connection=lambda ok, info: self.q.put(("conn", (ok, info))),
            on_log=lambda t: self.q.put(("log", t)),
        )
        # 连接建立后把自定义模式名同步给设备（设备侧名字存在RAM，重启后需重新同步）
        orig_conn = self.client.on_connection

        def on_conn(ok, info):
            if ok:
                for m in (3, 4):
                    name = media_core.USER_CFG[str(m)]["name"]
                    if name:
                        self.client.send(f"NAME:{m}:{name}")
            orig_conn(ok, info)
        self.client.on_connection = on_conn

    def _volume(self):
        """音量读取统一放 BLE 线程（COM 环境可靠）；失败原因上报一次"""
        vol = self.client.call_in_ble_thread(get_volume, timeout=1.5)
        err = media_core.volume_error
        if vol is None and err and not self.vol_err_reported:
            self.vol_err_reported = True
            self.q.put(("log", f"音量读取失败: {err}"))
        return vol

    # ---- JS 轮询：取事件 + 最新音量/状态 ----
    def poll(self):
        events = []
        try:
            while True:
                events.append(self.q.get_nowait())
        except queue.Empty:
            pass
        return {
            "events": events,
            "volume": self._volume(),
            "connected": self.client.connected,
            "mode": self.client.mode,
        }

    # ---- JS 初始化：整体状态 ----
    def state(self):
        modes = []
        for i, m in sorted(media_core.MODES.items()):
            entry = {"id": i, "name": m["name"], "ui": m.get("ui", {}),
                     "custom": bool(m.get("custom"))}
            if m.get("custom"):
                cfg = media_core.USER_CFG[str(i)]
                entry["ui"] = {"rot": "音量",
                               "single": describe_binding(cfg["single"]),
                               "double": describe_binding(cfg["double"])}
                entry["cfg"] = cfg
            modes.append(entry)
        return {
            "connected": self.client.connected,
            "mode": self.client.mode,
            "has_pycaw": HAS_PYCAW,
            "modes": modes,
        }

    # ---- 自定义模式保存（名字 + 四手势映射） ----
    def save_custom(self, mode, name, bindings):
        mode = int(mode)
        if mode not in (3, 4):
            return {"ok": False, "err": "槽位编号无效"}
        # 名字：仅保留可打印ASCII并截断到6位（OLED字库限制）
        name = "".join(c for c in str(name) if 32 <= ord(c) < 127)[:6].strip()
        if not name:
            name = f"USER{mode - 2}"
        clean = {}
        for k in ("single", "double", "rot_up", "rot_down"):
            v = str(bindings.get(k, "")) if bindings else ""
            if v and not (v.startswith(("key:", "char:", "mouse:", "combo:"))
                          and len(v) <= 60):
                v = ""
            clean[k] = v
        media_core.save_user_mode(mode, name, clean)
        self.client.send(f"NAME:{mode}:{name}")     # 同步名字到设备 OLED
        self.q.put(("log", f"自定义模式{mode} 已保存：{name}"))
        return {"ok": True, "name": name}

    # ---- 动作 ----
    def set_mode(self, n):
        self.client.set_mode(int(n))

    def manual(self, ev):
        """GUI 按钮合成事件（走当前模式映射）"""
        try:
            self.client.dispatch(ev)
        except Exception as e:
            self.q.put(("log", f"动作失败: {type(e).__name__}: {e}"))

    def set_volume(self, v):
        self.client.call_in_ble_thread(lambda: set_volume(int(v)), timeout=1.5)

    # ---- 自更新（GitHub Releases） ----
    def check_update(self):
        try:
            return updater.check_update(media_core.UPDATE_REPO)
        except Exception as e:
            return {"error": f"{type(e).__name__}: {e}"}

    def apply_update(self):
        def task():
            res = updater.apply_update(self._update_asset)
            self.q.put(("log", "更新完成，应用即将重启..." if res.get("ok")
                        else f"更新失败: {res.get('err')}"))
            if res.get("ok"):
                updater.exit_app()
        try:
            res = updater.check_update(media_core.UPDATE_REPO)
            if res.get("error"):
                return {"ok": False, "err": res["error"]}
            if not res.get("available"):
                return {"ok": False, "err": "已是最新版本"}
            self._update_asset = res.get("asset")
            if not self._update_asset:
                return {"ok": False, "err": "Release 没有zip附件"}
            self.q.put(("log", f"正在下载 {res['latest']} 更新包..."))
            import threading
            threading.Thread(target=task, daemon=True).start()
            return {"ok": True, "latest": res["latest"]}
        except Exception as e:
            return {"ok": False, "err": f"{type(e).__name__}: {e}"}


def main():
    api = Api()
    api.client.start()

    webview.create_window(
        "BlueMedia 媒体控制台",
        os.path.join(HERE, "web", "index.html"),
        js_api=api,
        width=940,
        height=720,
        min_size=(840, 620),
        background_color="#0b1020",
    )
    try:
        webview.start()
    except Exception:
        # pythonw 无控制台，致命错误落盘便于排查
        with open(os.path.join(HERE, "media_gui_error.log"), "w", encoding="utf-8") as f:
            f.write(traceback.format_exc())
        raise


if __name__ == "__main__":
    main()
