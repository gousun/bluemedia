#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BlueMedia BLE 事件监听：连上设备后打印所有通知（带时间戳），用于诊断事件是否发出。

用法: python ble_listen.py [--timeout 秒]   默认 300 秒
注意: 运行前请先关闭 media_host.py（设备只支持单连接）。
"""
import asyncio
import sys
import time

sys.coinit_flags = 0  # MTA，避免与 comtypes 冲突

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "BlueMedia"
UUID_RX = "0000ffe1-0000-1000-8000-00805f9b34fb"
UUID_TX = "0000ffe2-0000-1000-8000-00805f9b34fb"


def ts():
    return time.strftime("%H:%M:%S")


async def main():
    timeout = 300
    if len(sys.argv) > 2 and sys.argv[1] == "--timeout":
        timeout = int(sys.argv[2])

    while True:    #断线自动重连（设备重启也能跟上）
        print(f"[{ts()}] 扫描 {DEVICE_NAME} ...", flush=True)
        dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=10)
        if dev is None:
            print(f"[{ts()}] [FAIL] 未发现设备广播，5秒后重试", flush=True)
            await asyncio.sleep(5)
            continue
        print(f"[{ts()}] [OK] 发现设备，连接中 ...", flush=True)
        try:
            async with BleakClient(dev, timeout=15.0) as client:
                print(f"[{ts()}] [OK] 已连接，开始监听通知 ...", flush=True)

                def on_notify(_s, data: bytes):
                    print(f"[{ts()}] <== {data.decode('utf-8', 'replace').strip()}", flush=True)

                await client.start_notify(UUID_TX, on_notify)
                await asyncio.sleep(timeout)
        except asyncio.CancelledError:
            raise
        except Exception as e:
            print(f"[{ts()}] [WARN] 连接断开: {type(e).__name__}: {e}，3秒后重连", flush=True)
            await asyncio.sleep(3)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n已退出")
