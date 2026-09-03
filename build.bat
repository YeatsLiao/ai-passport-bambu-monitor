@echo off
REM ============================================
REM ai-passport-bambu-monitor 自动编译脚本
REM ============================================

REM 设置环境变量
set IDF_PATH=D:\1.Soft\Espressif\frameworks\esp-idf-v5.5.5
set IDF_PYTHON_ENV_PATH=D:\1.Soft\Espressif\python_env\idf5.5_py3.11_env

REM 设置工具链路径
set PATH=D:\1.Soft\Espressif\python_env\idf5.5_py3.11_env\Scripts;%PATH%
set PATH=D:\1.Soft\Espressif\tools\riscv32-esp-elf\esp-14.2.0_20260121\riscv32-esp-elf\bin;%PATH%
set PATH=D:\1.Soft\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin;%PATH%
set PATH=D:\1.Soft\Espressif\tools\cmake\3.30.5\bin;%PATH%
set PATH=D:\1.Soft\Espressif\tools\ninja\1.12.1;%PATH%

REM 切换到项目目录
cd /d "%~dp0"
echo [INFO] 项目目录: %CD%

REM 开始编译
echo [INFO] 开始编译...
python "%IDF_PATH%\tools\idf.py" build
if errorlevel 1 (
    echo [ERROR] 编译失败
    exit /b 1
)

echo [INFO] 编译成功!
echo [INFO] 固件路径: build\ai-passport-bambu-monitor.bin
pause
