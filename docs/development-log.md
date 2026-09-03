# 开发日志

完整记录 AI Passport Bambu Monitor 从立项到运行的全部开发过程，包括踩过的坑、方案演进和最终实现。

---

## 阶段 0：需求确认

**目标**：基于 FoloToy AI Passport 硬件，做一个拓竹打印机局域网监控设备。

需求：

| 功能 | 说明 |
|------|------|
| MQTT-TLS 连接 | 局域网直连拓竹打印机，数据不出局域网 |
| 实时状态显示 | 温度、进度、层数、状态、AMS |
| 多风格 UI | 4 种风格 + 4 种主题，烧录前配置 |
| 3 按键交互 | UP/DOWN 翻页，OK 刷新 |
| 独立仓库 | `ai-passport-bambu-monitor`（官方 `ai-passport` 只作为 BSP 来源） |

可行性结论：AI Passport 有 WiFi + 240×320 彩屏 + 3 按键，非常适合做监控设备。ESP-IDF 内置 MQTT 客户端支持 TLS，拓竹打印机 MQTT 协议已有 BambuHelper 和 AtomS3R-BambuMonitor 两个参考实现。

---

## 阶段 1：项目骨架搭建

### 完成的工作

1. **项目结构搭建**
   - 创建独立仓库 `ai-passport-bambu-monitor`
   - 从 `ai-passport` 复制并精简 BSP 组件（只保留 display + button + pins，移除 audio/battery/i2c）
   - 配置 ESP-IDF 工程文件：`CMakeLists.txt`、`sdkconfig.defaults`、`partitions.csv`

2. **配置系统设计**
   - `config.example.h` 模板文件（WiFi/打印机/UI 配置）
   - `.gitignore` 排除 `config.h`（含个人凭证，不提交）

3. **核心模块实现**
   - `bambu_state.h/c`：打印机状态数据结构
   - `bambu_mqtt.h/c`：WiFi 连接 + MQTT-TLS 客户端 + JSON 解析
   - `ui_theme.h/c`：4 种颜色主题
   - `ui_monitor.h/c`：4 种 UI 风格布局
   - `main.c`：启动流程 + 按键回调

### 编译问题与修复

| 问题 | 原因 | 修复 |
|------|------|------|
| `Failed to resolve component 'esp_mqtt'` | ESP-IDF 5.5.x 组件名变更 | REQUIRES 用 `mqtt` 不是 `esp_mqtt` |
| `Failed to resolve component 'cJSON'` | cJSON 是 mqtt 的内部依赖 | 从 REQUIRES 移除 cJSON |
| `BUG: component_requirements.py` | 根目录多余的 `idf_component.yml` 干扰组件解析 | 删除根目录 `idf_component.yml` |
| `lvgl` 在 REQUIRES 中导致错误 | LVGL 通过 component manager 管理，不应在 REQUIRES 中 | 从 REQUIRES 移除 lvgl |

**经验**：ESP-IDF 5.5.x 的组件名与旧版不同（`esp_mqtt` → `mqtt`），且内置组件不需要在 `idf_component.yml` 中声明。

---

## 阶段 2：首次编译 —— LVGL API 适配

### 问题与修复

| 问题 | 原因 | 修复 |
|------|------|------|
| `bambu_state_t` 类型冲突 | enum 和 struct 都叫 `bambu_state_t` | enum 改名为 `bambu_print_state_t` |
| `LV_SYMBOL_HEART undeclared` | LVGL 9.5 不存在该 symbol | 改为 `LV_SYMBOL_OK` |
| `esp_timer_get_time implicit declaration` | 缺少头文件 | 添加 `#include "esp_timer.h"` + REQUIRES 加 `esp_timer` |
| `esp_mqtt_client_config_t has no member named 'tls'` | ESP-IDF 5.5.x MQTT API 变更 | `.tls` → `.broker.verification` |
| Dashboard 风格 `ui_theme_colors_t` 错误 | 直接用了类型名而非函数调用 | 改为 `ui_theme_get_colors()` |

**经验**：LVGL 9.x 相比 8.x API 变化较大，部分 symbol 名称变更。ESP-IDF 5.5.x 的 MQTT 配置结构体从扁平结构改为分层嵌套结构。

---

## 阶段 3：TLS 连接失败 —— 最持久的坑

### 现象

MQTT 连接打印机时报错：
```
E (xxx) esp-tls-mbedtls: No server verification option set...
E (xxx) esp-tls-mbedtls: Failed to set client TLS configuration
```

### 排查过程

1. **第一次尝试**：添加 `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y` → 不生效
2. **第二次尝试**：删除 sdkconfig 重新生成 → 仍不生效
3. **第三次尝试**：设置 `use_global_ca_store = true` → 仍不生效
4. **第四次尝试**：提取打印机自签名证书嵌入固件 → 用户质疑"其他人怎么做的？"

### 关键发现

查看两个参考项目的实现：

- **BambuHelper**（Arduino 框架）：`c.tls->setInsecure();`
- **AtomS3R-BambuMonitor**（Arduino 框架）：`netClient.setInsecure();`

两者都用 `setInsecure()` 跳过证书验证。ESP-IDF 的等价操作是什么？

查阅 ESP-IDF 5.5.5 源码 `esp-tls/Kconfig`：

```
config ESP_TLS_SKIP_SERVER_CERT_VERIFY
    bool "Skip server certificate verification by default"
    depends on ESP_TLS_INSECURE    ← 关键！
```

**根因**：`ESP_TLS_SKIP_SERVER_CERT_VERIFY` 依赖 `ESP_TLS_INSECURE`，只开一个不生效！

### 最终修复

`sdkconfig.defaults` 同时添加两个配置项：

```
CONFIG_ESP_TLS_INSECURE=y
CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y
```

删除 sdkconfig 缓存后重新编译，MQTT-TLS 连接成功：

```
I (3531) bambu_mqtt: MQTT 已连接
```

**教训**：ESP-IDF 的 Kconfig 存在依赖关系，某些选项必须配合前置选项才能生效。修改 sdkconfig.defaults 后必须 `del sdkconfig && idf.py fullclean` 清除缓存。

---

## 阶段 4：LVGL UI Crash —— Guru Meditation Error

### 现象

MQTT 连接成功后，UI 加载瞬间崩溃：
```
Guru Meditation Error: Core 0 panic'ed (Load access fault)
--- 0x42019218: get_prop_core at lv_obj_style.c:847
```

### 诊断

初始实现中 `update_ui()` 通过遍历屏幕子对象 + `user_data` 标签查找 UI 元素指针：

```c
// 错误方式：遍历查找
for (int i = 0; i < lv_obj_get_child_cnt(s_scr); i++) {
    lv_obj_t *child = lv_obj_get_child(s_scr, i);
    void *tag = lv_obj_get_user_data(child);
    // ...
}
```

问题：
1. 嵌套容器中的元素（如 title_bar 里的 status 标签）无法被找到
2. LVGL 内部对象的 user_data 访问导致无效指针

### 修复

重构为全局 `ui` 结构体直接保存对象指针：

```c
static struct {
    lv_obj_t *nozzle, *bed, *chamber;
    lv_obj_t *layer, *percent, *remain;
    lv_obj_t *state, *speed, *status;
    lv_obj_t *bar;
} ui;
```

所有 `build_*_ui()` 函数创建对象后直接赋值到结构体成员，`update_ui()` 通过 `if (ui.nozzle)` 判空后直接访问，不再遍历。

---

## 阶段 5：卡片风格翻页实现

### 问题

UP/DOWN 按键按下后，页码计数器变化但 UI 没有更新。原因：`next_page()` / `prev_page()` 只是 TODO 桩代码。

### 修复

1. 创建内容区域容器 `s_content_area`（标题栏下方，不可滚动）
2. 实现 3 个页面内容构建函数：
   - `build_page0_content()`：主状态（进度、温度、层数、速度）
   - `build_page1_content()`：温度详情 + 风扇
   - `build_page2_content()`：AMS 托盘信息
3. `rebuild_page()` 清空容器 → 重置 UI 指针 → 按当前页重建
4. `update_page_indicator()` 更新页码显示（如 "2/3"）
5. `update_ui()` 增加风扇数据更新（Page 1 专属字段）

---

## 阶段 6：多风格 UI 重构 + 电池/NTP + AMS 完整解析（v2 架构）

### 新架构：翻页改用显示/隐藏切换

早期方案是翻页时 `lv_obj_clean(s_scr)` 清屏重建，在 ESP32-C3 低内存下反复出现顶部/底部栏不更新等渲染异常。v2 架构彻底放弃 destroy/rebuild：

- 标题栏、底部栏、两页卡片在首次 `build()` 时一次性创建
- 翻页只切换 `LV_OBJ_FLAG_HIDDEN`，指针永远有效
- 页码标签由风格文件自己管理（不再经由框架层 setter）

### 顶栏新布局

```
Bambu        15:31        BAT:100%
```

- 左：Bambu 标题
- 中：SNTP 实时时间（ntp.aliyun.com + pool.ntp.org，时区 CST-8，未同步显示 --:--）
- 右：CW2017 电池电量（I2C 0x63，芯片不存在时静默跳过）
- Connecting 状态移入卡片 CMP_STATE 组件（MQTT 未连接时显示）

### AMS 完整解析（三个连环坑）

以真实抓包报文 `docs/Topic-device.json` 为准，修正了三个解析错误：

1. **JSON 路径少一层嵌套**：真实结构是 `print.ams.ams[unit].tray[]`（AMS 对象内嵌套 AMS 单元数组），原代码找 `print.ams.tray` 永远为 NULL，导致 AMS 页全空
2. **字符串数字字段**：`tray_now`、`tray.id` 都是字符串（`"0"`~`"3"`），用 `valueint` 读取恒为 0
3. **颜色字段类型**：`cols` 是 JSON 数组不是字符串，必须读 `tray_color` 字符串（或 `cols[0]`），否则色块永远灰色

新增外挂料槽（Ext/虚拟托盘）解析：`print.vt_tray`，与 AMS 料槽共用 `parse_tray_obj()`。AMS 页显示 5 行（#1~#4 + Ext），每行前置色块，颜色取 `tray_color` 前 6 位 hex 渲染。

---

## 踩坑总结（给后来者）

| # | 坑 | 教训 |
|---|-----|------|
| 1 | `esp_mqtt` 组件找不到 | ESP-IDF 5.5.x 内置 MQTT 组件名为 `mqtt` |
| 2 | `cJSON` 组件找不到 | cJSON 是 mqtt 的内部依赖，不需要单独声明 |
| 3 | `idf_component.yml` 导致编译错误 | 根目录不要放 `idf_component.yml`，内置组件不需要声明 |
| 4 | 类型名冲突 | enum 和 struct 不要用相同的 typedef 名 |
| 5 | `LV_SYMBOL_HEART` 不存在 | LVGL 9.5 的 symbol 集合与 8.x 不同 |
| 6 | MQTT `.tls` 成员不存在 | ESP-IDF 5.5.x 改为 `.broker.verification` 嵌套结构 |
| 7 | TLS 跳过验证不生效 | `ESP_TLS_SKIP_SERVER_CERT_VERIFY` 依赖 `ESP_TLS_INSECURE` |
| 8 | LVGL Guru Meditation Error | 不要用 user_data + 遍历查找 UI 对象，用结构体直接保存指针 |
| 9 | 翻页无效果 | v1: 翻页必须重建 UI 内容；v2: 改为对象一次性创建 + HIDDEN 切换 |
| 10 | sdkconfig 修改不生效 | 必须 `del sdkconfig && idf.py fullclean` 清除缓存 |
| 11 | 反复改代码设备行为没变化 | 先查 `config.h` 的 `CFG_UI_STYLE` 实际编译哪个风格文件，用 map 文件验证符号是否被链接 |
| 12 | AMS 数据全空 | JSON 路径是 `print.ams.ams[].tray[]`，不是 `print.ams.tray` |
| 13 | 颜色/料槽号读不到 | `cols` 是数组、`tray_now`/`id` 是字符串，cJSON 取值前先确认字段真实类型 |
| 14 | 低内存下 destroy/rebuild 翻页渲染异常 | UI 对象一次性创建，翻页只切 `LV_OBJ_FLAG_HIDDEN` |

---

## 提交历史（分步提交记录）

```
docs: 同步 README/开发日志 + MQTT 报文样例 + UI 设计描述
style: AMS 标签去掉颜色 hex 文本（色块已表达颜色）
fix: tray_color 字段解析（cols 是数组不是字符串）
feat: 外挂料槽 vt_tray 解析 + AMS 页 5 槽位 + MQTT 颜色色块
fix: AMS JSON 路径修正（ams.ams[].tray[]）+ 字符串数字字段
feat: 顶栏 Bambu 标题 + NTP 实时时间 + 电池
fix: style_neon 应用新架构（此前改错了文件，设备编译的是 neon 风格）
feat: 翻页改显示/隐藏切换 + CW2017 电池驱动 + 多风格 UI 框架
feat: 添加 Windows 一键编译脚本
fix: 标题栏/底部栏主题颜色 + 持久化架构
docs: 添加项目文档和开发日志
fix: 修复 LVGL UI crash 和卡片风格翻页
fix: 修复 MQTT-TLS 连接（ESP-TLS Kconfig 依赖）
feat: 实现 MQTT-TLS 打印机连接和 JSON 解析
feat: 实现打印机状态数据结构和 UI 监控页面
feat: 添加 BSP 硬件驱动组件
feat: 初始化项目结构和构建配置
```
