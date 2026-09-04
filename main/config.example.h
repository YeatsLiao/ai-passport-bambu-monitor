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
#define CFG_PRINTER_IP      "192.168.1.XXX"
#define CFG_PRINTER_SERIAL  "YOUR_PRINTER_SERIAL"
#define CFG_ACCESS_CODE     "YOUR_ACCESS_CODE"
#define CFG_MQTT_PORT       8883

// ============================================================================
// 3. UI 风格（7 选 1，烧录前改这一个宏）
//
//   STYLE_BAMBU       拓竹原厂工业风 — 深蓝标题 + 白卡片 + 绿进度（参考 TRAE）
//   STYLE_CYBER       赛博极简监控风 — 纯黑底 + 冰蓝霓虹
//   STYLE_SHEIKAH     希卡石板风     — 深蓝科技 + 青蓝冷光
//   STYLE_WHITE       纯白素雅风     — 白底灰字 + 发丝线大字排版
//   STYLE_INDUSTRIAL  硬核工控风     — 深灰黑 + LED 段式进度与状态灯
//   STYLE_NEON        极简霓虹极客风 — 哑光黑 + 浅紫/浅青
//   STYLE_PIXEL       像素机器人风   — 天空蓝 + 纸牌墨边 + 眨眼机器人
// ============================================================================
#define CFG_UI_STYLE  STYLE_BAMBU

#define STYLE_BAMBU       1
#define STYLE_CYBER       2
#define STYLE_SHEIKAH     3
#define STYLE_WHITE       4
#define STYLE_INDUSTRIAL  5
#define STYLE_NEON        6
#define STYLE_PIXEL       7

// ============================================================================
// 4. 组件排序（烧录前调整显示内容和顺序）
//
// 组件编号:
//   1 = 喷嘴温度   2 = 热床温度   3 = 腔体温度
//   4 = 层数       5 = 进度%      6 = 剩余时间
//   7 = 打印状态   8 = 打印速度   9 = AMS 信息
//
// 示例（取消注释并修改）:
//   #define CFG_COMPONENT_ORDER  {5, 4, 1, 2, 7, 8}
// 不定义则使用各风格的默认顺序。
// ============================================================================
// #define CFG_COMPONENT_ORDER  {5, 4, 1, 2, 7, 8}

// ============================================================================
// 5. 语言（烧录前切换）
//
//   LANG_EN  英文（默认，Montserrat 字体原生支持）
//   LANG_CN  中文（需要额外中文字体，当前暂用英文占位）
// ============================================================================
#define CFG_LANG  LANG_EN

#define LANG_EN  1
#define LANG_CN  2

// ============================================================================
// 6. 显示设置
// ============================================================================
#define CFG_BACKLIGHT_PERCENT   80
#define CFG_MQTT_BUFFER_SIZE    (16 * 1024)
#define CFG_MQTT_RECONNECT_SEC  10
#define CFG_STATUS_TIMEOUT_SEC  60
