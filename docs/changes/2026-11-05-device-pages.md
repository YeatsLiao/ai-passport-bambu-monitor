# 2026-11-05：机型标题、耗材分页与目录整理

## 行为

- 12 主题顶栏绑定实际识别的 MQTT 型号，未知时 Bambu；AMS 页标题继续表示耗材设备。
- 读取 print.printer_type；连接并订阅后请求 info/get_version，仅采纳 ota 主机模块的已识别 product_name/hw_ver。支持规范名称及已核对的官方型号代码 BL-P001/BL-P002/C11/C12/C13/N1/N2S/O1D。型号缺失不从序列号推测。
- 身份信息不会使遥测变为有效，不改变遥测时间，不作为配对证据；无效、错误模块和失败响应不会覆盖有效型号。
- 四槽 AMS 已有外置耗材行，不再创建重复外置页。有 AMS + HT 时通常为总览、AMS、HT；仅外置时仍可查看独立外置页。
- HT 页继承主题边框、圆角、分隔线、强调色及 Pixel 硬阴影，共用单槽 UI；不创建不存在的盒子占位。
- main 下按 config、bambu、network、device 分类，12 个 style 文件归入 ui/styles；更新固件 CMake、主机测试、模拟器及字体扫描。main/config.h 保持原位置。

## 协议依据与限制

核对日期 2026-11-05：拓竹官方 [MQTT 版本请求及解析](https://github.com/bambulab/BambuStudio/blob/master/src/slic3r/GUI/DeviceManager.cpp)（command_get_version、info.module、print.printer_type）；官方 [X1 Carbon 配置](https://github.com/bambulab/BambuStudio/blob/master/resources/profiles/BBL/machine/Bambu%20Lab%20X1%20Carbon.json)、[H2D 配置](https://github.com/bambulab/BambuStudio/blob/master/resources/profiles/BBL/machine/Bambu%20Lab%20H2D.json)及同目录 P1/A1/X1/X1E 配置的 model_id。没有承诺每个固件都会返回型号，也不承诺读取用户自定义昵称。

型号显示不等于双喷嘴或所有 H 系列字段已支持。仍需实机检查对应打印机返回字段、AMS HT 类型/温湿度、运行堆、断线恢复与按键观感。本轮不自动烧录设备。

## 验证

- 报告解析、型号来源过滤、分页组合、MQTT 生命周期、显示配置故障注入、配网事务回归通过。
- 12 主题 × 中英文共 24 组真实 LVGL 模拟检查通过，包含首页型号、1/3→2/3→3/3 页码、AMS/HT、外置独立页、插拔选择保持及设置交互。人工查看 Apple、Pixel 的型号与 HT 截图。
- 中文字库扫描 146 个所需字符，两种字号均无缺字；字体生成脚本通过 Node 语法检查；git diff --check 通过。
- ESP-IDF 5.5.5 / ESP32-C3 完整重新编译通过；应用大小 `0x1b3dd0`（1,785,296 字节），3 MB 分区剩余 43%。应用 bin、merged-binary.bin 和 full.bin 已同步生成，合并包为 1,850,832 字节。受限环境中的 ccache 报错已通过仅在本地构建关闭缓存解决；未修改项目依赖版本。未烧录。
