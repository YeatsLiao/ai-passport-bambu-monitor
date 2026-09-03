# ai-passport-bambu-monitor

基于 FoloToy AI Passport（ESP32-C3）的拓竹打印机局域网监控设备。

通过 MQTT-TLS 连接拓竹打印机，实时显示打印状态、温度、进度、AMS 等信息。

## 特性

- **4 种 UI 风格**：简洁文本 / 仪表盘 / 卡片 / 可爱风
- **4 种颜色主题**：深色 / 浅色 / 拓竹绿 / 马卡龙
- **3 按键交互**：UP/DOWN 翻页，OK 刷新
- **局域网直连**：无需云端，数据不出局域网
- **支持 X1/P1/A1 系列**：自动适配不同型号的数据格式

## 硬件要求

- FoloToy AI Passport（ESP32-C3 + 240×320 ST7789P + 3 按键）
- 拓竹打印机（X1-Carbon / X1 / P1P / P1S / A1 / A1 Mini 等）
- 同一局域网（2.4GHz WiFi）

## 快速开始

### 1. 创建配置文件

```bash
# 从模板复制配置文件（config.h 已在 .gitignore 中，不会提交到仓库）
cp main/config.example.h main/config.h
```

编辑 `main/config.h`：

```c
// WiFi
#define CFG_WIFI_SSID       "你的WiFi名称"
#define CFG_WIFI_PASSWORD   "你的WiFi密码"

// 打印机
#define CFG_PRINTER_IP      "192.168.1.XXX"    // 打印机 IP
#define CFG_PRINTER_SERIAL  "YOUR_SERIAL"      // 序列号（15 位）
#define CFG_ACCESS_CODE     "YOUR_CODE"        // 访问码（8 位）

// UI 风格（1=简洁 2=仪表盘 3=卡片 4=可爱）
#define CFG_UI_STYLE  STYLE_CARD

// 颜色主题（1=深色 2=浅色 3=拓竹绿 4=马卡龙）
#define CFG_THEME  THEME_DARK
```

> **注意**：`config.h` 包含你的 WiFi 密码和打印机访问码，已在 `.gitignore` 中排除，请勿手动提交到仓库。

### 2. 编译烧录

```bash
# 设置 ESP-IDF 环境
. $IDF_PATH/export.sh

# 编译
idf.py build

# 烧录
idf.py -p /dev/ttyACM0 flash
```

### 3. 查看打印机信息

- **IP**：打印机屏幕 设置 > 网络
- **序列号**：设置 > 设备 > 序列号（15 位）
- **访问码**：设置 > 网络 > 访问码（8 位，每次重启变化）

## UI 风格说明

| 风格 | 宏定义 | 特点 |
|------|--------|------|
| 简洁文本 | `STYLE_COMPACT` | 信息密度高，类似 AtomS3R |
| 仪表盘 | `STYLE_DASHBOARD` | 弧线温度表 + 进度环 |
| 卡片 | `STYLE_CARD` | 分页浏览，类似 TRAE |
| 可爱 | `STYLE_CUTE` | 圆角 + 马卡龙色 + 装饰 |

## 按键操作

| 按键 | 短按 | 长按 |
|------|------|------|
| UP | 上一页（卡片/可爱风格） | — |
| DOWN | 下一页（卡片/可爱风格） | — |
| OK | 刷新数据（pushall） | 退出（预留） |

## 文档

- [开发文档](docs/README.md) — 技术架构、数据流、构建步骤
- [开发日志](docs/development-log.md) — 完整开发过程与踩坑记录

## 项目结构

```
ai-passport-bambu-monitor/
├── components/bsp/          # 硬件抽象层（显示 + 按键 + LVGL）
│   ├── include/
│   │   ├── bsp_pins.h       # 引脚定义
│   │   ├── bsp_display.h    # 显示接口
│   │   └── bsp_button.h     # 按键接口
│   └── src/
├── main/
│   ├── config.example.h   # ★ 配置模板（复制为 config.h 后修改）
│   ├── config.h           # 个人配置（.gitignore 排除，勿提交）
│   ├── main.c               # 入口
│   ├── bambu_state.h/c      # 打印机状态数据结构
│   ├── bambu_mqtt.h/c       # MQTT-TLS 客户端
│   └── ui/
│       ├── ui_theme.h/c     # 颜色主题系统
│       └── ui_monitor.h/c   # 监控页面（4 种风格 + 分页）
├── docs/
│   ├── README.md            # 开发文档（架构/数据流/构建）
│   └── development-log.md   # 开发日志（踩坑记录）
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
└── .gitignore
```

## 内存优化

ESP32-C3 无 PSRAM，已做以下优化：

- MQTT 缓冲区：16KB（可配置）
- LVGL DMA 缓冲：20 行单缓冲（~9.6KB）
- JSON 解析：cJSON 流式解析
- TLS：跳过证书验证（拓竹自签名证书）

## 参考项目

- [BambuHelper](https://github.com/Keralots/BambuHelper) — 功能丰富的拓竹监控固件
- [AtomS3R-BambuMonitor](../AtomS3R-BambuMonitor) — 轻量级 MQTT 协议参考
- [ai-passport](../ai-passport) — 硬件 BSP 和构建系统参考

## License

MIT
