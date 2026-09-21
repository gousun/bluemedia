#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BlueMedia BLE 诊断工具：扫描 → 连接 → 订阅通知 → 命令往返测试

用法:
    python ble_test.py            扫描并列出所有BLE设备（看设备是否在广播）
    python ble_test.py --connect  扫到 BlueMedia 后继续完整链路测试
"""
import argparse
import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

DEVICE_NAME = "BlueMedia"
UUID_RX = "0000ffe1-0000-1000-8000-00805f9b34fb"
UUID_TX = "0000ffe2-0000-1000-8000-00805f9b34fb"

got_notify = []


def on_notify(_sender, data: bytes):
    text = data.decode("utf-8", "replace").strip()
    got_notify.append(text)
    print(f"  <== 通知: {text}")


async def scan_and_report(timeout=12.0):
    print(f"扫描 {timeout:.0f} 秒 ...")
    devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
    print(f"共发现 {len(devs)} 个BLE设备，有名称的：")
    named = []
    for d, adv in devs.values():
        if d.name:
            named.append((d.name, d.address, adv.rssi))
    for name, addr, rssi in sorted(named):
        print(f"  {addr}  {name}  RSSI={rssi}")
    target = next((devs[a][0] for n, a, _ in named if n == DEVICE_NAME), None)
    if target:
        print(f"\n[OK] 找到目标设备 {DEVICE_NAME}: {target.address}")
    else:
        print(f"\n[FAIL] 未发现 {DEVICE_NAME} 正在广播")
    return target


async def connect_test(device):
    print(f"\n连接 {device.address} ...")
    async with BleakClient(device, timeout=15.0) as client:
        print("[OK] 已连接")
        print("设备端服务:", [str(s) for s in client.services])
        await client.start_notify(UUID_TX, on_notify)
        print("\n写入命令 STATUS ...")
        await client.write_gatt_char(UUID_RX, b"STATUS")
        await asyncio.sleep(2.0)
        print("\n写入命令 MODE:2 ...")
        await client.write_gatt_char(UUID_RX, b"MODE:2")
        await asyncio.sleep(1.5)
        print("\n写入命令 VOL:66 ...")
        await client.write_gatt_char(UUID_RX, b"VOL:66")
        await asyncio.sleep(1.5)
        if got_notify:
            print(f"\n[OK] 链路正常，共收到 {len(got_notify)} 条通知")
        else:
            print("\n[WARN] 已连接但未收到任何通知（FFE2订阅/固件通知路径需检查）")


async def main():
    parser = argparse.ArgumentParser(description="BlueMedia BLE 诊断")
    parser.add_argument("--connect", action="store_true", help="扫到设备后做完整链路测试")
    parser.add_argument("--address", default=None, help="跳过扫描直连")
    args = parser.parse_args()

    if args.address:
        device = await BleakScanner.find_device_by_address(args.address, timeout=8.0)
        if device is None:
            print(f"[FAIL] 地址 {args.address} 未找到")
            return
    else:
        device = await scan_and_report()
        if device is None or not args.connect:
            return

    if device:
        try:
            await connect_test(device)
        except Exception as e:
            print(f"[FAIL] 连接/测试异常: {type(e).__name__}: {e}")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n已退出")
