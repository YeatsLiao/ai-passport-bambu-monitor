"""Verify CJK UI string literals against the actual LVGL font cmaps."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|/\*[\s\S]*?\*/|//[^\n]*')


def coverage(path):
    source = path.read_text(encoding='utf-8-sig')
    lists = {m[0]: [int(x.strip(), 0) for x in m[1].split(',') if x.strip()]
             for m in re.findall(r'unicode_list_(\d+)\[\]\s*=\s*\{([^}]*)\}', source)}
    result = set()
    pattern = r'\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = \d+,\s*\.unicode_list = (NULL|unicode_list_(\d+)),'
    for m in re.finditer(pattern, source):
        start, length = int(m[1]), int(m[2])
        result.update(start + n for n in (range(length) if m[3] == 'NULL' else lists[m[4]]))
    if not result:
        raise ValueError(f'No cmap entries in {path}')
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--font', action='append', type=Path)
    parser.add_argument('--lang', type=Path)
    args = parser.parse_args()
    sources = [args.lang] if args.lang else [p for p in (ROOT / 'main/ui').rglob('*') if p.suffix in ('.c', '.h')]
    chars = set()
    for path in sources:
        for token in TOKEN.findall(path.read_text(encoding='utf-8-sig')):
            if token.startswith('"'):
                chars.update(c for c in token if '\u3000' <= c <= '\u9fff' or c in '°℃，：')
    fonts = args.font or [ROOT / f'main/ui/fonts/lv_font_cn_{size}.c' for size in (14, 20)]
    failed = False
    for path in fonts:
        missing = sorted(c for c in chars if ord(c) not in coverage(path))
        print(f'{path.name}: {len(chars)} required; missing {len(missing)} ' + ' '.join(f'U+{ord(c):04X}' for c in missing))
        failed |= bool(missing)
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
