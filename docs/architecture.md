# 源码架构

## 目录与职责

- `main/main.c`：启动、主循环、按键事件、网络生命周期协调。不在按键回调内启停网络。
- `main/config/`：app_config，默认配置合并、NVS、显示参数与 stable/trial 配置事务。
- `main/bambu/`：协议与状态。bambu_report 校验/合并 report；bambu_state 发布一致快照；bambu_pages 决定逻辑页目录；pairing_probe 校验候选打印机首包。
- `main/network/`：bambu_mqtt 管理 STA/MQTT 与分片重组；wifi_prov 管理 AP、HTTP、验证和取消；prov_form 负责严格表单解析与配置合并。
- `main/device/`：串口截图协议及屏幕读取。
- `main/ui/`：监控框架、设置交互、单槽耗材页、主题参数与语言。业务页面选择集中在 ui_monitor，不在主题内推断设备数量。
- `main/ui/styles/`：12 套主题布局。现有两套内容对象用于总览/四槽页，按选定设备绑定快照；单槽 HT/外置页共用 ui_filament，复用主题参数。
- `main/ui/fonts/`：生成的 LVGL 中文字库及声明。
- `main/config.example.h`、本地 `main/config.h`：编译期默认配置。保留原路径，凭据文件不搬迁、不提交。
- `components/bsp/`：显示、按键、电池和引脚；`components/dns_server/`：热点 DNS。
- `tools/tests/`：编译真实 C 模块的主机回归；`tools/ui/`：真实 LVGL 运行与截图、色板预览页；`tools/font/`：中文字库生成与 CJK 覆盖检查；`tools/device/`：设备截图工具。
- `docs/`：开发入口、架构、UI 说明、历史实施记录；`docs/changes/`：新增变更记录。历史文档内路径是当时快照，现行路径以本文为准。
- `build/`、`managed_components/`：构建产物与依赖，不手工维护为业务源码。

## 数据流与边界

MQTT 完整报文交给 bambu_report，经验证后合并到状态副本并提交。UI 每秒复制一次快照；逻辑页跟踪 AMS 设备 ID，数组乱序或插入设备时不会跳到另一台。四槽显示通过投影视图复用原主题，不为每台设备额外创建整套对象。

身份报文与监控报文分开判断：型号可更新顶栏，但不代表打印数据新鲜，也不代表指定序列号配对成功。候选配对只验证有效遥测，不发布到监控状态。

配网表单先验证，再写 trial；启动后有效遥测确认后升为 stable，否则恢复旧配置。配置存储细节集中在 config，网络任务由 main 协调。

## 构建维护

`main/CMakeLists.txt` 显式列举源文件，并公开 main/config/bambu/network/device/ui 头文件搜索目录。内部引用使用头文件名，不依赖跨目录 `../` 链。主机测试与 UI 模拟器使用同样的模块位置；字体采集递归扫描 UI 源文件，排除生成字库。

扩展新机型时先加入脱敏报告和回归，再扩展协议模型；扩展主题只调整样式，避免复制分页、配对或配置事务逻辑。双喷嘴、多外置盘和全机型能力矩阵仍属后续工作。
