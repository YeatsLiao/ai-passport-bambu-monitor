# 开发文档

## 项目概述

AI Passport Bambu Monitor 是基于 FoloToy AI Passport 硬件的拓竹打印机局域网监控设备。设备通过 WiFi 连接局域网，使用 MQTT-TLS 协议与拓竹打印机通信，实时显示打印状态、温度、进度、AMS 耗材等信息。

## 功能

| 功能 | 说明 |
|------|------|
| 实时状态监控 | 喷嘴/热床/腔体温度、打印进度、层数、剩余时间、打印状态 |
| AMS 信息 | 耗材类型、颜色、剩余量、当前使用托盘 |
| 风扇转速 | 冷却风扇、部件风扇百分比 |
| 4 种 UI 风格 | 简洁文本 / 仪表盘 / 卡片（分页）/ 可爱风 |
| 4 种颜色主题 | 深色 / 浅色 / 拓竹绿 / 马卡龙 |
| 3 按键交互 | UP/DOWN 翻页，OK 手动刷新 |
| 自动重连 | WiFi 和 MQTT 断线自动重连 |

## 技术架构

```
┌─────────────────────────────────────────────┐
│                  main/                       │
│  main.c          启动流程 + 按键分发         │
│  bambu_mqtt.h/c  WiFi + MQTT-TLS + JSON 解析 │
│  bambu_state.h/c 打印机状态数据结构           │
│  ui/                                         │
│    ui_monitor.h/c  监控页面（4 种风格）        │
│    ui_theme.h/c    颜色主题系统               │
├─────────────────────────────────────────────┤
│               components/bsp/                │
│  bsp_display / bsp_button / bsp_pins         │
├─────────────────────────────────────────────┤
│   ESP-IDF 5.5.x + LVGL 9.5 + esp-mqtt       │
└─────────────────────────────────────────────┘
```

### 硬件层 (BSP)

复用 `ai-passport` 仓库的 BSP 组件（精简版）：

- **bsp_display**: ST7789P 240×320 显示屏驱动 + LVGL 集成（`bsp_lvgl_lock/unlock`）
- **bsp_button**: 三按键输入（上/下/确认），支持单击/长按事件
- **bsp_pins**: ESP32-C3 引脚定义

### 通信层 (MQTT-TLS)

基于 ESP-IDF 内置 MQTT 客户端，TLS 加密连接：

- **连接目标**: `mqtts://<打印机IP>:8883`
- **认证**: 用户名 `bblp`，密码为 LAN Access Code
- **订阅 Topic**: `device/<SERIAL>/report`
- **推送 Topic**: `device/<SERIAL>/request`
- **TLS**: 跳过证书验证（拓竹使用自签名证书）

### 应用层：数据流

```
打印机 MQTT Broker                 设备
  ── report JSON ──>    mqtt_event_handler()
                          └─ parse_print_json()
                               └─ 更新 g_bambu_state
                                     └─ refresh_timer_cb (1s)
                                          └─ update_ui() → LVGL 刷新
```

JSON 数据结构（`print` 对象内的关键字段）：

| 字段 | 类型 | 说明 |
|------|------|------|
| `nozzle_temper` | float | 喷嘴当前温度 |
| `bed_temper` | float | 热床当前温度 |
| `chamber_temper` | float | 腔体温度（X1 系列） |
| `mc_percent` | int | 打印进度 0-100 |
| `mc_remaining_time` | int | 剩余时间（分钟） |
| `gcode_state` | string | 打印状态 IDLE/RUNNING/PAUSE/FINISH/FAILED |
| `spd_lvl` | int | 速度等级 1-5 |
| `cooling_fan_speed` | int | 冷却风扇 0-100 |
| `ams.tray` | array | AMS 托盘信息 |

## 代码结构

```
├── main/
│   ├── main.c              # 入口: 初始化 + 启动画面 + 按键回调
│   ├── bambu_state.h/c     # 打印机状态数据结构 + 状态枚举
│   ├── bambu_mqtt.h/c      # WiFi 连接 + MQTT-TLS 客户端 + JSON 解析
│   ├── config.example.h    # 配置模板（复制为 config.h 后修改）
│   ├── CMakeLists.txt
│   └── ui/
│       ├── ui_theme.h/c    # 4 种颜色主题定义
│       ├── ui_monitor.h/c  # 监控页面（4 种风格布局 + 分页逻辑）
├── components/bsp/         # 硬件抽象层（显示 + 按键）
├── docs/
│   ├── README.md           # 本文档
│   └── development-log.md  # 开发日志（方案演进与踩坑记录）
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
└── .gitignore
```

## 构建与烧录

### 环境要求

- ESP-IDF 5.5.x（需先激活 ESP-IDF 命令行环境）
- 目标：ESP32-C3 / 8MB Flash / 无 PSRAM

### 构建步骤

```bash
# 1. 从模板创建配置文件
cp main/config.example.h main/config.h
# 编辑 config.h 填入 WiFi 和打印机信息

# 2. 编译
idf.py build

# 3. 烧录
idf.py -p COM3 flash monitor    # COM 口号按实际修改
```

> 修改 `sdkconfig.defaults` 后需全量清理再构建：
> `del sdkconfig && idf.py fullclean && idf.py build`

### 配置说明

| 配置项 | 说明 | 获取方式 |
|--------|------|----------|
| `CFG_WIFI_SSID` | WiFi 名称 | — |
| `CFG_WIFI_PASSWORD` | WiFi 密码 | — |
| `CFG_PRINTER_IP` | 打印机局域网 IP | 打印机屏幕 设置 > 网络 |
| `CFG_PRINTER_SERIAL` | 打印机序列号（15 位） | 设置 > 设备 > 序列号 |
| `CFG_ACCESS_CODE` | LAN 访问码（8 位） | 设置 > 网络 > 访问码 |

> **注意**：`config.h` 包含 WiFi 密码和打印机访问码，已在 `.gitignore` 中排除。

## 关键实现细节

### TLS 自签名证书处理

拓竹打印机使用自签名证书，局域网监控设备需要跳过证书验证。ESP-IDF 的 Kconfig 存在依赖关系：

```
CONFIG_ESP_TLS_INSECURE=y                          # 必须先开启
CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y            # 依赖上面的选项
```

Arduino 框架的 `setInsecure()` 等价于以上两个 Kconfig 选项。

### LVGL 线程安全

所有 LVGL 操作必须在 LVGL 任务中或持锁执行：

```c
// 按键回调中操作 LVGL
if (bsp_lvgl_lock(500)) {
    ui_monitor_key(btn, ev);
    bsp_lvgl_unlock();
}
```

### 无 PSRAM 内存优化

ESP32-C3 无 PSRAM，已做以下优化：

- MQTT 缓冲区：16KB（可配置）
- LVGL DMA 缓冲：20 行单缓冲（~9.6KB）
- JSON 解析：cJSON 解析后立即释放原始字符串
- TLS：跳过证书验证（避免 CA 证书存储分配）

### 卡片风格分页

卡片/可爱风格使用 3 页分页浏览：

| 页面 | 内容 |
|------|------|
| Page 0 | 主状态：进度百分比、进度条、温度、层数、速度 |
| Page 1 | 温度详情：喷嘴/热床/腔体温度 + 风扇转速 |
| Page 2 | AMS 信息：4 个托盘的耗材类型、颜色、剩余量 |

翻页时清空内容容器重建，页码指示器自动更新。

## 故障排查

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 编译找不到 `mqtt` 组件 | ESP-IDF 5.5.x 组件名变更 | REQUIRES 用 `mqtt` 不是 `esp_mqtt` |
| `No server verification option set` | TLS Kconfig 依赖缺失 | 同时开启 `ESP_TLS_INSECURE` 和 `SKIP_SERVER_CERT_VERIFY` |
| LVGL Guru Meditation Error | UI 对象指针无效访问 | 用结构体保存指针，不用 user_data 遍历 |
| 中文显示方块 | Montserrat 字体无中文 | UI 文案使用英文或 LV_SYMBOL |
| `sdkconfig` 修改不生效 | 旧 sdkconfig 缓存 | `del sdkconfig && idf.py fullclean` |

## 参考项目

- [BambuHelper](https://github.com/Keralots/BambuHelper) — 功能丰富的拓竹监控固件（PlatformIO + Arduino）
- [AtomS3R-BambuMonitor](../../AtomS3R-BambuMonitor) — 轻量级 MQTT 协议参考（Arduino 单文件）
- [ai-passport](../../ai-passport) — 硬件 BSP 和构建系统参考
- [ai-passport-tiktok-remote](../../ai-passport-tiktok-remote) — 同硬件的蓝牙遥控器项目

## 扩展方向

- 电池供电 + 低功耗休眠（按键唤醒）
- 打印完成通知（声音/振动）
- 多打印机切换监控
- 打印历史统计
- 中文字库支持
