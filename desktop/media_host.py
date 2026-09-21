#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BlueMedia PC 端控制台版（无界面，推荐日常用 media_gui.py 图形界面）

用法: python media_host.py [--address XX:XX:XX:XX:XX:XX]
输入 q 回车退出。
"""
import argparse

from media_core import MODES, MediaClient


def main():
    parser = argparse.ArgumentParser(description="BlueMedia 控制台版")
    parser.add_argument("--address", default=None, help="设备MAC地址直连（可选）")
    args = parser.parse_args()

    client = MediaClient(
        on_event=lambda t: print(f"\n[事件] {t}", flush=True),
        on_status=lambda st: print(
            f"\n[状态] 模式{st.get('mode')}({MODES.get(st.get('mode'), {}).get('name', '?')}) "
            f"音量回报={st.get('vol')}", flush=True),
        on_connection=lambda ok, info: print(
            f"\n[连接] {'已连接 ' + info if ok else '未连接: ' + info}", flush=True),
        on_log=lambda t: print(f"[日志] {t}", flush=True),
        address=args.address,
    )
    client.start()
    print("控制台模式：直接在设备上操作即可；输入 q 回车退出。")
    try:
        while input().strip().lower() not in ("q", "quit", "exit"):
            pass
    except (EOFError, KeyboardInterrupt):
        pass
    client.stop()


if __name__ == "__main__":
    main()
