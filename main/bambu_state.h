// main/bambu_state.h —— 拓竹打印机状态数据结构
#pragma once

#include <stdint.h>
#include <stdbool.h>

// 打印状态枚举（来自 gcode_state 字段）
typedef enum {
    BAMBU_STATE_IDLE = 0,
    BAMBU_STATE_RUNNING,
    BAMBU_STATE_PAUSE,
    BAMBU_STATE_FINISH,
    BAMBU_STATE_FAILED,
    BAMBU_STATE_PREPARE,   // 准备中（热床/喷嘴加热）
    BAMBU_STATE_UNKNOWN,
} bambu_print_state_t;

// AMS 单个托盘信息
typedef struct {
    char   color[8];       // 颜色代码 (如 "FF0000")
    char   type[8];        // 耗材类型 (PLA/PETG/ABS...)
    float  remain;         // 剩余量 mm
    int    tag;            // 托盘标签号
    bool   active;         // 是否在使用
} bambu_ams_tray_t;

#define BAMBU_AMS_TRAY_COUNT 4
#define BAMBU_AMS_COUNT      1

// 打印机完整状态（所有字段都来自 MQTT report JSON）
typedef struct {
    // 温度
    float nozzle_temp;
    float nozzle_target;
    float bed_temp;
    float bed_target;
    float chamber_temp;     // X1 系列才有

    // 进度
    int   layer_num;
    int   total_layer;
    int   mc_percent;       // 打印进度 0-100
    int   mc_remaining;     // 剩余时间（分钟）

    // 状态
    bambu_print_state_t state;
    char  gcode_state[16];  // 原始状态字符串

    // 速度
    int   spd_lvl;          // 速度等级 1-5
    int   spd_mag;          // 速度百分比

    // 风扇
    int   cooling_fan;      // 冷却风扇 0-100
    int   big_fan1;         // 腔体风扇1 0-100
    int   big_fan2;         // 腔体风扇2 0-100

    // AMS
    int   ams_count;
    bambu_ams_tray_t trays[BAMBU_AMS_COUNT * BAMBU_AMS_TRAY_COUNT];
    int   active_tray;      // 当前使用的托盘编号

    // 文件
    char  gcode_file[64];   // 文件名

    // 时间戳
    uint32_t last_update_ms; // 最后一次数据更新时间
    bool   data_valid;       // 是否有有效数据
} bambu_state_t;

// 全局状态实例（由 mqtt 模块更新，ui 模块读取）
extern bambu_state_t g_bambu_state;

// 工具函数
const char *bambu_state_str(bambu_print_state_t s);
void bambu_state_init(void);
