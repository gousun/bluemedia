# BlueMedia - BLE 媒体控制旋钮

ESP32 旋钮 + OLED 的蓝牙媒体遥控器：设备把旋转/按键事件通过 BLE 发给 PC 上位机，由上位机模拟按键控制音乐播放、抖音等任何应用。

```
┌──────────────────────────┐       BLE        ┌───────────────────────────┐
│ ESP32 旋钮 + OLED         │ ──事件(EV:*)──▶  │ PC 上位机 (Python/WebView2)│
│ 模式管理 · OLED显示 · LED │ ◀─命令(VOL/MODE)─ │ 按模式映射 → 键盘/鼠标动作  │
└──────────────────────────┘                   └───────────────────────────┘
```

## 目录结构

| 目录 | 内容 | 文档 |
|---|---|---|
| `firmware/` | ESP32 固件（ESP-IDF v5.1.2，NimBLE，模式管理/OLED/编码器） | [firmware/README.md](firmware/README.md) |
| `desktop/` | PC 上位机（Python + pywebview 毛玻璃界面，含自更新） | [desktop/README.md](desktop/README.md) |

> 结构件 CAD/STL 资料在本地 `hardware/` 目录，不入库。

## 功能一览

- **四模式**：音乐控制 / 抖音控制 / 自定义1 / 自定义2，长按旋钮循环切换
- **自定义模式**：名字（同步设备 OLED）+ 手势映射（键盘/鼠标/快捷键）在 GUI 里编辑，配置持久化
- **音乐/抖音控制**：音量、播放暂停、切歌/切视频
- **OLED**：启动动画、模式名、最近事件、音量条、BLE 状态
- **PC 端**：毛玻璃界面（pywebview/WebView2）、系统音量联动、事件日志、**GitHub Releases 自更新**

## 快速开始

**固件**（VSCode ESP-IDF 扩展或命令行，见 firmware/README）：

```bash
cd firmware
idf.py -p COM38 flash monitor
```

**PC 端**：

```bash
cd desktop
pip install -r requirements.txt
python media_gui.py        # 或双击 一键启动.bat
```

## 版本与更新

- PC 端内置自更新：启动时自动检查 GitHub Releases，发现新版本一键下载重启（`desktop/updater.py`）
- 发新版本：改 `desktop/media_core.py` 的 `APP_VERSION` → 运行 `desktop\build_exe.bat <版本号>` → `gh release create vX.Y.Z dist\BlueMedia-vX.Y.Z.zip`
- 注意：自更新依赖 GitHub API，仓库需为 **public**（私有仓库客户端无凭据无法访问）

## 协议

设备/上位机 BLE 协议（FFE0 服务）见 [firmware/README.md](firmware/README.md)。
