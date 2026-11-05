"""Render and exercise all actual LVGL themes on Windows without flashing.

python tools/ui/ui_simulator.py --idf D:/.../esp-idf-v5.5.5
Outputs build/ui-simulator/previews/<theme>-<language>/*.png.
Requires MinGW GCC, CMake and Ninja on PATH. Hardware services are stubbed.
"""
import argparse
import os
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[2]


def write_if_changed(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


STUBS = {
    'host_compat.h': '#pragma once\n#include <time.h>\n#ifdef _WIN32\nstatic inline struct tm *localtime_r(const time_t *value, struct tm *out) { return localtime_s(out, value) == 0 ? out : NULL; }\n#endif\n',
    'esp_err.h': '#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n',
    'esp_log.h': '#pragma once\n#define ESP_LOGI(...) ((void)0)\n#define ESP_LOGW(...) ((void)0)\n#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGD(...) ((void)0)\n',
    'esp_timer.h': '#pragma once\n#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
    'bsp_display.h': '#pragma once\nvoid bsp_display_backlight(int brightness);\n',
    'bsp_battery.h': '#pragma once\nint bsp_battery_soc(void);\n',
    'lv_conf.h': '''#ifndef LV_CONF_H
#define LV_CONF_H
#define LV_COLOR_DEPTH 16
#define LV_USE_OS 0
#define LV_USE_STDLIB_MALLOC 1
#define LV_USE_STDLIB_STRING 1
#define LV_USE_STDLIB_SPRINTF 1
#define LV_USE_LOG 0
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_MONTSERRAT_48 1
#endif
''',
}


def png_from_ppm(source):
    data = source.read_bytes()
    header = b'P6\n240 320\n255\n'
    assert data.startswith(header)
    pixels = data[len(header):]
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind + value))
    rows = b''.join(b'\0' + pixels[y * 720:(y + 1) * 720] for y in range(320))
    result = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 240, 320, 8, 2, 0, 0, 0))
    result += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
    source.with_suffix('.png').write_bytes(result)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--idf', default=os.environ.get('IDF_PATH'))
    p.add_argument('--style', action='append', type=int)
    args = p.parse_args()
    if not args.idf:
        p.error('Provide --idf or IDF_PATH')
    folder = ROOT / 'build/ui-simulator'
    stubs = folder / 'stubs'
    stubs.mkdir(parents=True, exist_ok=True)
    for name, text in STUBS.items():
        write_if_changed(stubs / name, text)
    cjson = Path(args.idf) / 'components/json/cJSON'
    env = dict(os.environ)
    cc, cxx = shutil.which('gcc'), shutil.which('g++')
    if not cc or not cxx:
        p.error('GCC and G++ are required')
    env['PATH'] = str(Path(cc).parent) + os.pathsep + env['PATH']
    def q(path):
        return '"' + Path(path).as_posix() + '"'
    cmake = f'''cmake_minimum_required(VERSION 3.20)
project(bambu_ui_probe C)
set(CMAKE_C_STANDARD 11)
file(GLOB_RECURSE LV_SOURCES {q(ROOT / 'managed_components/lvgl__lvgl/src/*.c')})
file(GLOB UI_SOURCES {q(ROOT / 'main/ui/*.c')} {q(ROOT / 'main/ui/styles/*.c')})
add_executable(ui-probe ${{LV_SOURCES}} ${{UI_SOURCES}}
 {q(ROOT / 'main/ui/fonts/lv_font_cn_14.c')} {q(ROOT / 'main/ui/fonts/lv_font_cn_20.c')}
 {q(ROOT / 'tools/ui/ui_simulator.c')} {q(ROOT / 'main/bambu/bambu_state.c')}
 {q(ROOT / 'main/bambu/bambu_report.c')} {q(ROOT / 'main/bambu/bambu_pages.c')} {q(cjson / 'cJSON.c')})
target_include_directories(ui-probe PRIVATE {q(stubs)} {q(ROOT / 'main')}
 {q(ROOT / 'main/config')} {q(ROOT / 'main/bambu')} {q(ROOT / 'main/network')} {q(ROOT / 'main/device')} {q(ROOT / 'main/ui')}
 {q(ROOT / 'managed_components/lvgl__lvgl')} {q(ROOT / 'components/bsp/include')} {q(cjson)})
target_compile_definitions(ui-probe PRIVATE LV_CONF_INCLUDE_SIMPLE LV_KCONFIG_IGNORE)
target_compile_options(ui-probe PRIVATE -UNDEBUG -include {q(stubs / 'host_compat.h')})
target_link_libraries(ui-probe PRIVATE m)
'''
    write_if_changed(folder / 'CMakeLists.txt', cmake)
    output = folder / 'out'
    subprocess.run(['cmake', '-S', str(folder), '-B', str(output), '-G', 'Ninja', '-DCMAKE_C_COMPILER=' + cc,
                    '-DCMAKE_BUILD_TYPE=Release'], check=True, env=env)
    subprocess.run(['cmake', '--build', str(output), '--parallel', '8'], check=True, env=env)
    binary = output / ('ui-probe.exe' if os.name == 'nt' else 'ui-probe')
    for style in args.style or range(1, 13):
        for language in (1, 2):
            images = folder / 'previews' / f'{style:02d}-{language}'
            images.mkdir(parents=True, exist_ok=True)
            subprocess.run([str(binary), str(style), str(language), str(images)], check=True, env=env)
            for ppm in images.glob('*.ppm'):
                png_from_ppm(ppm)


if __name__ == '__main__':
    main()
