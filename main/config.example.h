// main/config.example.h —— 配置文件模板
//
// 使用方法:
//   1. 复制本文件为 config.h:  copy config.example.h config.h
//   2. 修改 config.h 中的 WiFi / 打印机 / UI 配置
//   3. idf.py build && idf.py flash
//
// config.h 已在 .gitignore 中，不会被提交到仓库。
// 请勿直接修改本文件，也请勿将 config.h 提交到仓库（含你的 WiFi 密码和访问码）。
#pragma once

// ============================================================================
// 1. WiFi 配置
// ============================================================================
#define CFG_WIFI_SSID       "YOUR_WIFI_SSID"
#define CFG_WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

// ============================================================================
// 2. 拓竹打印机 MQTT 配置
// ============================================================================
// 打印机局域网 IP（在打印机屏幕 设置 > 网络 里查看）
#define CFG_PRINTER_IP      "192.168.1.XXX"
// 打印机序列号（15 位，在 设置 > 设备 > 序列号 或机身标签上）
#define CFG_PRINTER_SERIAL  "YOUR_PRINTER_SERIAL"
// LAN 访问码（8 位，在 设置 > 网络 > 访问码，每次重启打印机会变化）
#define CFG_ACCESS_CODE     "YOUR_ACCESS_CODE"
// MQTT 端口（拓竹固定 8883，一般不需要改）
#define CFG_MQTT_PORT       8883

// ============================================================================
// 3. UI 风格选择（烧录前改这一个宏即可切换整体外观）
//
//   STYLE_COMPACT    简洁文本风  — 信息密度高，全部信息一屏显示
//   STYLE_DASHBOARD  仪表盘风    — 圆形温度表 + 进度环，视觉冲击强
//   STYLE_CARD       卡片风      — 大百分比 + 分页浏览，类似 TRAE 设备
//   STYLE_CUTE       可爱风      — 圆角 + 马卡龙色 + emoji 装饰
// ============================================================================
#define CFG_UI_STYLE  STYLE_CARD

// 风格枚举值（不要改）
#define STYLE_COMPACT   1
#define STYLE_DASHBOARD 2
#define STYLE_CARD      3
#define STYLE_CUTE      4

// ============================================================================
// 4. 组件排序（烧录前调整显示内容的顺序）
//
// 每个风格预设了一套默认组件顺序，你也可以自定义。
// 组件编号:
//   1 = 喷嘴温度   2 = 热床温度   3 = 腔体温度
//   4 = 层数       5 = 进度%      6 = 剩余时间
//   7 = 打印状态   8 = 打印速度   9 = AMS 信息
//
// 自定义示例（取消注释并修改）:
//   #define CFG_COMPONENT_ORDER  {1, 2, 4, 5, 6, 7}
// 不定义则使用各风格的默认顺序。
// ============================================================================
// #define CFG_COMPONENT_ORDER  {1, 2, 4, 5, 6, 7}

// ============================================================================
// 5. 颜色主题（烧录前改颜色）
//
//   THEME_DARK    深色背景（默认，适合大多数场景）
//   THEME_LIGHT   浅色背景（明亮环境）
//   THEME_BAMBU   拓竹绿主题（品牌感）
//   THEME_PASTEL  马卡龙 pastel（柔和，STYLE_CUTE 会自动使用此主题）
// ============================================================================
#define CFG_THEME  THEME_DARK

#define THEME_DARK   1
#define THEME_LIGHT  2
#define THEME_BAMBU  3
#define THEME_PASTEL 4

// ============================================================================
// 6. 显示设置
// ============================================================================
#define CFG_BACKLIGHT_PERCENT   80          // 背光亮度 0-100
#define CFG_MQTT_BUFFER_SIZE    (16 * 1024) // MQTT 接收缓冲区（字节），X1 完整推送较大，建议 >= 16KB
#define CFG_MQTT_RECONNECT_SEC  10          // MQTT 断线重连间隔（秒）
#define CFG_STATUS_TIMEOUT_SEC  60          // 无数据超时判定（秒）
