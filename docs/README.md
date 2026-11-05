# 开发文档

当前代码：2026-11-05。目标硬件为 FoloToy AI Passport / ESP32-C3，ESP-IDF 5.5.5、LVGL 9。源码已完成多设备分页、设置交互、配网回退及机型标题；本轮未烧录，真实打印机兼容性仍需验收。

## 导航

- [源码架构和目录职责](architecture.md)
- [本轮机型标题、页面去重和目录整理](changes/2026-11-05-device-pages.md)
- [第一、二批：报告解析与多 AMS/HT](batch12-implementation-2026-11-02.md)
- [第三批：设置页](batch3-implementation-2026-11-03.md)
- [第四批：配网与恢复](batch4-implementation-2026-11-03.md)
- [UI 设计](UI-DESIGN.md)、[历史开发日志](development-log.md)
- [原排查快照](project-audit-2026-11-02.md)（其中问题状态和源码路径以快照日期为准）

## 页面与名称

顶栏优先显示 MQTT 中识别出的打印机型号，例如 X1C、H2D；没有可靠字段时显示 Bambu。型号不是用户自定义设备昵称，也不表示该机型所有功能均已适配。读取 `print.printer_type`；订阅 report 成功后发送 `info/get_version`，仅从 ota 主机模块的 `product_name` / 可识别 `hw_ver` 获取备用型号。AMS 模块不能覆盖打印机型号。

型号信息单独更新，不延长打印数据有效期，也不能通过配对验证。未知型号不猜测；重新连接清空旧型号，等待新报告。

页面按真实报文动态生成：总览 → 已发现的各 AMS / AMS HT → 设置。四槽 AMS 页含外置耗材一行，因此存在四槽页时不再重复生成外置页。没有四槽 AMS 时，已上报的外置耗材才独立成页；HT + 外置可分别显示。未接入的 HT 不显示占位页。

AMS 1 表示耗材盒类型和编号，与 X1C/H2D 打印机型号不同。四槽页仍保留 AMS 名称；打印机型号统一放在顶栏。HT 显示单槽耗材、已知温湿度和干燥状态，共用主题色、直角/圆角、分隔线、边框和 Pixel 阴影；未上报值为 `--`。

## 构建与烧录

在 ESP-IDF 5.5.5 终端内：

```powershell
Set-Location 'D:\2.Project\ai-passport-bambu-monitor'
idf.py build
if ($LASTEXITCODE -eq 0) { idf.py -p COM6 flash monitor }
```

串口以实际设备为准，Ctrl+] 退出日志。`main/config.h` 保留原位置且不入库；无需新增凭据即可复用设备 NVS，未配置设备走热点配网。普通 flash 不执行整片擦除。

需要全量包时，构建成功后重新执行 `idf.py merge-bin`，使用新生成的 `build/merged-binary.bin`。不要把旧的 full/merged 文件当成本轮固件。

## 回归验证

主机检查需 GCC/Clang，UI 模拟器另需 GCC/G++、CMake、Ninja；设置 `IDF_PATH` 为本机 ESP-IDF 目录。

```powershell
python tools/tests/test_reports.py
python tools/tests/test_mqtt_lifecycle.py
python tools/tests/test_display_config.py
python tools/tests/test_provisioning.py
python tools/font/check_cjk_coverage.py
python tools/ui/ui_simulator.py --idf D:/1.Soft/Espressif/frameworks/esp-idf-v5.5.5
```

模拟器运行真实 LVGL 与 12 主题的中英文组合；截图位于 `build/ui-simulator/previews/<主题>-<语言>/`。主机测试不能代替真实网络、按键、NVS 断电和 ESP32-C3 堆内存验收。
