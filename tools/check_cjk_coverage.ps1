# tools/check_cjk_coverage.ps1 —— 校验 ui_lang.h 中文字符串是否被裁剪中文字体覆盖
#
# 用法 (项目根目录):
#   pwsh -File tools/check_cjk_coverage.ps1
#   pwsh -File tools/check_cjk_coverage.ps1 -FontFile <path> -LangFile <path>
#
# 原理: 解析 lv_font_conv 生成的字体 C 文件 cmap 表 (FORMAT0 = 连续区间, SPARSE = 稀疏偏移表),
#       与 ui_lang.h 的 LANG_CN 分支中出现的非 ASCII 字符逐一比对。
# 退出码: 0 = 全覆盖, 1 = 有缺字形 (需补充 tools/cn_chars.txt 后重跑 gen_cn_font.js)
param(
    [string]$FontFile = "main/ui/fonts/lv_font_cn_14.c",
    [string]$LangFile = "main/ui/ui_lang.h"
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
if (-not (Test-Path $FontFile)) { Write-Error "字体文件不存在: $FontFile"; exit 1 }
if (-not (Test-Path $LangFile)) { Write-Error "语言文件不存在: $LangFile"; exit 1 }

$fontText = Get-Content $FontFile -Raw -Encoding UTF8

# 1) 收集稀疏偏移表: unicode_list_N = { ... }
$sparseLists = @{}
foreach ($m in [regex]::Matches($fontText, 'unicode_list_(\d+)\[\]\s*=\s*\{([^}]*)\}')) {
    $vals = @()
    foreach ($v in ($m.Groups[2].Value -split ',')) {
        $t = $v.Trim()
        if ($t -match '^0x([0-9A-Fa-f]+)$') { $vals += [Convert]::ToInt32($Matches[1], 16) }
        elseif ($t -match '^\d+$')           { $vals += [int]$t }
    }
    $sparseLists[$m.Groups[1].Value] = $vals
}

# 2) 收集 cmap 覆盖的码点集合
$covered = New-Object 'System.Collections.Generic.HashSet[int]'
$pattern = '\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = \d+,\s*\.unicode_list = (NULL|unicode_list_(\d+)),'
foreach ($m in [regex]::Matches($fontText, $pattern)) {
    $start = [int]$m.Groups[1].Value
    $len   = [int]$m.Groups[2].Value
    $list  = $m.Groups[4].Value          # 稀疏表编号; NULL 时为空
    if (-not $list) {
        for ($i = 0; $i -lt $len; $i++) { [void]$covered.Add($start + $i) }
    } elseif ($sparseLists.ContainsKey($list)) {
        foreach ($ofs in $sparseLists[$list]) { [void]$covered.Add($start + $ofs) }
    } else {
        Write-Warning "未找到稀疏表 unicode_list_$list"
    }
}
Write-Output ("字体覆盖码点数: {0}" -f $covered.Count)

# 3) 提取 ui_lang.h 的 LANG_CN 分支字符串
$langText = Get-Content $LangFile -Raw -Encoding UTF8
$cnBlock = [regex]::Match($langText, '#if CFG_LANG == LANG_CN(?<body>.*?)#else', 'Singleline')
if (-not $cnBlock.Success) { Write-Error "未找到 LANG_CN 分支"; exit 1 }

$chars = New-Object 'System.Collections.Generic.HashSet[char]'
foreach ($m in [regex]::Matches($cnBlock.Groups['body'].Value, '"([^"]*)"')) {
    foreach ($ch in $m.Groups[1].Value.ToCharArray()) {
        if ([int]$ch -gt 127) { [void]$chars.Add($ch) }
    }
}
Write-Output ("中文分支用到的非 ASCII 字符数: {0}" -f $chars.Count)

# 4) 比对
$missing = @()
foreach ($ch in ($chars | Sort-Object)) {
    if (-not $covered.Contains([int]$ch)) { $missing += $ch }
}

if ($missing.Count -eq 0) {
    Write-Output "PASS: 全部中文字符均有字形覆盖"
    exit 0
} else {
    Write-Output ("FAIL: 以下 {0} 个字符缺少字形: {1}" -f $missing.Count, ($missing -join ' '))
    Write-Output "      请改用同义的常用字, 或启用/生成包含这些字的字体"
    exit 1
}
