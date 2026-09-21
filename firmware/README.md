# BlueMedia - BLE 媒体控制器

基于 ESP32-WROOM-32E（4MB Flash）+ ESP-IDF v5.1.2 的蓝牙媒体遥控旋钮。
设备把旋转/按键事件通过 BLE 上报给 PC 上位机（`../blueapp/`），由上位机模拟按键控制音乐播放与抖音。

## 功能

- **BLE 事件上报**（NimBLE，设备名 `BlueMedia`）：旋转/短按/双击/长按事件实时推送给上位机
- **四模式**：音乐控制 / 抖音控制 / 自定义1 / 自定义2；**长按旋钮循环切换**（全局）
- **自定义模式**：名字（≤6字符ASCII）与动作映射在上位机编辑，名字通过 `NAME` 命令同步到设备 OLED，动作映射在上位机执行
- **OLED 显示**（SSD1306 128×64）：开机启动动画；运行页显示当前模式名、最近事件（VOL+/NEXT/PREV…）、音量进度条（上位机回传）、BLE 状态
- **音量回显**：上位机读取 Windows 系统音量后回传设备，OLED 实时显示音量条
- **LED**：BLE 连接指示
- **可扩展**：设备只上报事件，动作映射全部在上位机；自定义模式的名字/映射在 GUI 里即可配置

## 硬件引脚

| 外设 | 引脚 | 说明 |
|---|---|---|
| 板载 LED | GPIO2 | BLE 已连接=亮 |
| 编码器 A/B/按键 | GPIO32 / 33 / 13 | 旋钮+按键 |
| OLED I2C | GPIO21(SDA) / GPIO22(SCL) | SSD1306，地址 0x3C，400kHz |

（早期风扇版遗留的 `components/middlewares/fan`、`dht11` 组件源码保留在仓库，应用不再引用，链接时自动裁剪）

## BLE 协议（FFE0 服务）

- **FFE2 = 通知**（设备 → 上位机，文本事件）
- **FFE1 = 写**（上位机 → 设备，文本命令）

| 事件（FFE2 通知） | 含义 |
|---|---|
| `EV:ROT:+1` / `EV:ROT:-1` | 旋钮右转/左转一格 |
| `EV:KEY:S` | 短按 |
| `EV:KEY:D` | 双击 |
| `EV:MODE:n` | 长按切换模式，n=新模式编号(1起) |

| 命令（FFE1 写入） | 作用 |
|---|---|
| `VOL:<0-100>` | 上位机回报系统音量（OLED 音量条） |
| `MODE:<1-n>` | 上位机直接切换模式 |
| `NAME:<1-n>:<name>` | 同步模式名（自定义槽位 3/4，ASCII ≤6 字符，OLED 显示） |
| `STATUS` | 即时回报状态 JSON `{"mode":1,"name":"MUSIC","vol":35,"volok":1}` |

非法命令回报 `{"err":"CMD"}`。

## 按键判定参数（main/main.c）

| 参数 | 默认 | 含义 |
|---|---|---|
| `BTN_LONG_MS` | 700ms | 按住达到即触发长按（切模式，无需等待释放） |
| `BTN_DOUBLE_MS` | 350ms | 第一次释放后该时间内二次按下判为双击 |

## 工程结构

```
bluefan_esp/
├── CMakeLists.txt / partitions.csv / sdkconfig.defaults
├── main/
│   ├── main.c            app_main + 编码器任务（按键状态机）
│   └── ui.c/h            OLED 启动动画 + 状态页（音符图标程序化绘制）
├── components/
│   ├── bsp/
│   │   ├── led/          GPIO2 LED
│   │   ├── encoder/      旋转编码器（5ms轮询；pressed/released 沿检测）
│   │   └── oled/         SSD1306 驱动 + oled_gfx 帧缓冲图形 API
│   └── middlewares/
│       ├── app_state/    模式/事件/音量/BLE状态（互斥保护，模式名表在此扩展）
│       ├── ble_svc/      NimBLE GATT：事件通知 + VOL/MODE/STATUS 命令
│       ├── fan/          （遗留，未参与应用）
│       └── dht11/        （遗留，未参与应用）
```

### 任务

| 任务 | 周期 | 职责 |
|---|---|---|
| encoder | 5ms | 旋转检测 + 按键状态机 → BLE 事件 |
| ui | 200ms | OLED 渲染、LED 指示 |
| NimBLE host | - | BLE 协议栈（内部任务） |

## 构建与烧录

```bash
cd bluefan_esp
idf.py build
idf.py -p COM38 flash monitor
```

- 命令行构建：本机 `export.bat` 的 cmake 版本自检损坏（cmake 3.24.0 目录实为 3.28.3），可用以下批处理（纯 ASCII）：

```bat
set "MSYSTEM="
set "IDF_PATH=E:\esp32\espidf\Espressif\frameworks\esp-idf-v5.1.2"
set "IDF_TOOLS_PATH=E:\esp32\espidf\Espressif"
set "PATH=%IDF_TOOLS_PATH%\python_env\idf5.1_py3.11_env\Scripts;%IDF_TOOLS_PATH%\tools\cmake\3.24.0\bin;%IDF_TOOLS_PATH%\tools\ninja\1.10.2;%IDF_TOOLS_PATH%\tools\xtensa-esp32-elf\esp-12.2.0_20230208\xtensa-esp32-elf\bin;%PATH%"
cd /d E:\PROJECTS\bluefan_esp32wroom32e\bluefan_esp
python %IDF_PATH%\tools\idf.py build
```

- VSCode ESP-IDF 扩展不受影响，直接 Build & Flash 即可（端口 COM38）

## 上位机

见 `../blueapp/README.md`：`pip install -r requirements.txt` 后运行 `python media_host.py`。

## 新增模式（扩展示例）

1. 固件：`app_state.h` 的 `media_mode_t` 加枚举（如 `MEDIA_MODE_VIDEO`），`app_state.c` 的 `mode_names[]` 加名字，编译烧录
2. 上位机：`media_host.py` 的 `MODES` 加同编号条目，四个动作（`rot_up/rot_down/single/double`）写成本文件中的函数或 lambda

BLE 协议无需改动，OLED 自动显示新模式名与 `M n/总数`。
