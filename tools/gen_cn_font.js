#!/usr/bin/env node
// tools/gen_cn_font.js —— 从 Noto Sans SC 生成裁剪版中文字体 (LVGL 9 C 数组)
//
// 用法 (项目根目录):
//   node tools/gen_cn_font.js
//
// 产物:
//   main/ui/fonts/lv_font_cn_14.c   正文中文字体 (14px, 4bpp)
//   main/ui/fonts/lv_font_cn_20.c   标题中文字体 (20px, 4bpp)
//   main/ui/fonts/lv_font_cn.h      LV_FONT_DECLARE 声明
//
// 字符集来源: tools/cn_chars.txt (预留词库) + main/ui/ui_lang.h 的 LANG_CN 分支实际用字
// 源字体: Noto Sans SC (SIL Open Font License 1.1, 可自由裁剪与再分发)
//
// 依赖: lv_font_conv (通过 npx 缓存或本地 node_modules 提供), 首次运行需联网拉取一次
'use strict';

const fs = require('fs');
const path = require('path');
const { spawnSync } = require('child_process');

const ROOT = path.resolve(__dirname, '..');
const OUT_DIR = path.join(ROOT, 'main', 'ui', 'fonts');
const SIZES = [14, 20];

// 源字体候选 (按优先级): Noto Sans SC (OFL) > 思源黑体
const FONT_CANDIDATES = [
  'C:\\Windows\\Fonts\\Noto Sans SC (TrueType).otf',
  'C:\\Windows\\Fonts\\NotoSansSC-Regular.otf',
  'C:\\Windows\\Fonts\\SourceHanSansSC-Regular.otf',
];

// 额外并入的标点是 ASCII 区之外的常用符号
const EXTRA_CODEPOINTS = [0x00B0, 0x2103];   // ° ℃

function findSourceFont() {
  for (const p of FONT_CANDIDATES) {
    if (fs.existsSync(p)) return p;
  }
  throw new Error('未找到 Noto Sans SC / 思源黑体源字体, 请安装后重试 (候选: ' + FONT_CANDIDATES.join(', ') + ')');
}

// 定位 lv_font_conv 的 CLI 入口 (优先本地/缓存的 JS, 避免 shell 传参编码问题)
function findLvFontConvCli() {
  const candidates = [];
  const local = path.join(ROOT, 'tools', '.fontconv', 'node_modules', 'lv_font_conv', 'lv_font_conv.js');
  candidates.push(local);
  const cacheRoot = path.join(process.env.LOCALAPPDATA || '', 'npm-cache', '_npx');
  if (fs.existsSync(cacheRoot)) {
    for (const dir of fs.readdirSync(cacheRoot)) {
      candidates.push(path.join(cacheRoot, dir, 'node_modules', 'lv_font_conv', 'lv_font_conv.js'));
    }
  }
  candidates.push(path.join(path.dirname(process.execPath), 'node_modules', 'lv_font_conv', 'lv_font_conv.js'));
  for (const c of candidates) {
    if (fs.existsSync(c)) return c;
  }
  return null;   // 退回 npx (shell) 方式
}

// 收集字符集: 词库文件 + ui_lang.h 的 LANG_CN 分支
function collectChars() {
  const set = new Set();
  const addText = (s) => { for (const ch of s) if (ch.codePointAt(0) > 0x7F) set.add(ch); };

  const charsFile = path.join(ROOT, 'tools', 'cn_chars.txt');
  if (fs.existsSync(charsFile)) addText(fs.readFileSync(charsFile, 'utf8'));

  const langFile = path.join(ROOT, 'main', 'ui', 'ui_lang.h');
  if (fs.existsSync(langFile)) {
    const src = fs.readFileSync(langFile, 'utf8');
    const m = src.match(/#if CFG_LANG == LANG_CN([\s\S]*?)#else/);
    if (m) {
      for (const lit of m[1].matchAll(/"([^"]*)"/g)) addText(lit[1]);
    }
  }
  for (const cp of EXTRA_CODEPOINTS) set.add(String.fromCodePoint(cp));
  return [...set].sort();
}

function runConv(cli, fontPath, size, symbols) {
  const out = path.join(OUT_DIR, `lv_font_cn_${size}.c`);
  const args = [
    cli,
    '--font', fontPath,
    '--size', String(size),
    '--bpp', '4',
    '--format', 'lvgl',
    '--no-compress',                 // 不依赖 LV_USE_FONT_COMPRESSED
    '--no-kerning',                  // 中文无需 kerning, 省空间
    '--lv-include', 'lvgl.h',
    '--lv-font-name', `lv_font_cn_${size}`,
    // ASCII/数字不含在本字体内, 通过 fallback 回落 Montserrat, 与英文界面观感一致
    '--lv-fallback', `lv_font_montserrat_${size}`,
    '--symbols', symbols,
    '-o', out,
  ];
  const r = spawnSync(process.execPath, args, { encoding: 'utf8' });
  if (r.status !== 0) {
    throw new Error(`lv_font_conv 生成 ${size}px 失败:\n${r.stdout || ''}${r.stderr || ''}`);
  }
  return out;
}

function main() {
  const fontPath = findSourceFont();
  const chars = collectChars();
  const symbols = chars.join('');
  fs.mkdirSync(OUT_DIR, { recursive: true });

  console.log(`源字体: ${fontPath}`);
  console.log(`字符集: ${chars.length} 个非 ASCII 字符 (ASCII 通过 fallback 回落 Montserrat)`);

  const cli = findLvFontConvCli();
  const outs = [];
  if (cli) {
    console.log(`转换器: ${cli}`);
    for (const size of SIZES) outs.push(runConv(cli, fontPath, size, symbols));
  } else {
    // 退回 npx (需要 shell, 中文参数经 cmd 传递)
    console.log('转换器: npx lv_font_conv (未在缓存中找到, 首次运行会联网下载)');
    for (const size of SIZES) {
      const out = path.join(OUT_DIR, `lv_font_cn_${size}.c`);
      const cmd = [
        'npx', '--yes', 'lv_font_conv@1.5.3',
        '--font', `"${fontPath}"`, '--size', String(size), '--bpp', '4',
        '--format', 'lvgl', '--no-compress', '--no-kerning',
        '--lv-include', 'lvgl.h', '--lv-font-name', `lv_font_cn_${size}`,
        '--lv-fallback', `lv_font_montserrat_${size}`,
        '--symbols', `"${symbols}"`, '-o', `"${out}"`,
      ].join(' ');
      const r = spawnSync(cmd, { shell: true, encoding: 'utf8' });
      if (r.status !== 0) throw new Error(`lv_font_conv 生成 ${size}px 失败:\n${r.stdout || ''}${r.stderr || ''}`);
      outs.push(out);
    }
  }

  // 生成声明头文件
  const header = [
    '// main/ui/fonts/lv_font_cn.h —— 裁剪版中文字体声明 (由 tools/gen_cn_font.js 生成)',
    '// 源字体: Noto Sans SC, SIL Open Font License 1.1',
    '// 只含汉字子集; ASCII/数字通过 .fallback 回落同字号 Montserrat',
    '#pragma once',
    '',
    '#include "lvgl.h"',
    '',
    ...SIZES.map((s) => `LV_FONT_DECLARE(lv_font_cn_${s})`),
    '',
  ].join('\n');
  const headerPath = path.join(OUT_DIR, 'lv_font_cn.h');
  fs.writeFileSync(headerPath, header, 'utf8');
  outs.push(headerPath);

  for (const f of outs) {
    const kb = (fs.statSync(f).size / 1024).toFixed(1);
    console.log(`  -> ${path.relative(ROOT, f)}  (${kb} KB)`);
  }
}

main();
