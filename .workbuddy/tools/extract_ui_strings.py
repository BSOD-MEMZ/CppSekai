"""Extract user-facing C++ string literals that would need translating.

Two filters do the work:
  * the literal must contain a non-ASCII character (Chinese / Japanese / symbols) -
    internal error text and gl/log strings are pure ASCII by convention here;
  * the innermost call wrapping it must not be a log sink.

Also lists the pure-ASCII UI literals separately (PERFECT / FULL COMBO / LIVE
RESULT ...), since those are shown too but are few.
"""
import os
import re

ROOTS = ['main.cpp', 'game', 'platform', 'downloader']
LOG_CALLS = {
    'logf', 'printf', 'fprintf', 'sprintf', 'snprintf', 'vprintf', 'vfprintf',
    'note', 'appendLog', 'logLine', 'puts', 'fputs', 'wprintf', 'swprintf',
    'write', 'dump', 'TraceLog', 'OutputDebugStringW', 'OutputDebugStringA',
    'stbi_write_png', 'throw', 'assert',
}
SKIP_EXT = re.compile(r'\.(png|jpg|jpeg|webp|usm|sus|mp3|json|ttf|otf|dll|exe|log|ini|wav|ogg|res|rc)$', re.I)


def tokens(src):
    i, n, line = 0, len(src), 1
    while i < n:
        c = src[i]
        if c == '\n':
            line += 1
            i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '/':
            while i < n and src[i] != '\n':
                i += 1
            continue
        if c == '/' and i + 1 < n and src[i + 1] == '*':
            i += 2
            while i + 1 < n and not (src[i] == '*' and src[i + 1] == '/'):
                if src[i] == '\n':
                    line += 1
                i += 1
            i += 2
            continue
        start = -1
        if c == '"':
            start = i
        elif c in 'LuU':
            j = i
            while j < n and src[j] in 'LuU8':
                j += 1
            if j < n and src[j] == '"':
                start = j
        if start >= 0:
            j = start + 1
            buf = []
            while j < n and src[j] != '"':
                if src[j] == '\\':
                    buf.append(src[j:j + 2])
                    j += 2
                    continue
                if src[j] == '\n':
                    line += 1
                buf.append(src[j])
                j += 1
            yield line, start, ''.join(buf)
            i = j + 1
            continue
        if c == "'":
            i += 1
            while i < n and src[i] != "'":
                if src[i] == '\\':
                    i += 1
                i += 1
            i += 1
            continue
        i += 1


CALL_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*\(')


def enclosing_call(src, pos):
    """Innermost call name before `pos`: same line first, then up to 3 lines back."""
    back = src[max(0, pos - 600):pos]
    lines = back.split('\n')
    for line in reversed(lines[-4:]):
        hits = CALL_RE.findall(line)
        if hits:
            return hits[-1]
    return ''


def keep(text, call):
    if call in LOG_CALLS:
        return False
    if not text.strip():
        return False
    if SKIP_EXT.search(text.strip()):
        return False
    if text.startswith('/') or text.startswith('\\'):
        return False
    if re.fullmatch(r'[0-9A-Za-z_./\\:-]+', text) and ' ' not in text:
        return False
    return True


def main():
    files = []
    for root in ROOTS:
        if os.path.isfile(root):
            files.append(root)
        else:
            for dirpath, _dirs, names in os.walk(root):
                for nm in names:
                    if nm.endswith(('.cpp', '.hpp')):
                        files.append(os.path.join(dirpath, nm))

    ascii_ui, i18n = {}, {}
    seen = set()
    scanned = 0
    for path in files:
        src = open(path, encoding='utf-8', errors='replace').read()
        for line_no, offset, value in tokens(src):
            scanned += 1
            call = enclosing_call(src, offset)
            if not keep(value, call):
                continue
            key = value
            if key in seen:
                continue
            seen.add(key)
            non_ascii = any(ord(ch) > 127 for ch in value)
            bucket = i18n if non_ascii else ascii_ui
            bucket.setdefault(path, []).append((line_no, call, value))

    def dump(title, table):
        n = sum(len(v) for v in table.values())
        print('# %s  (%d unique)\n' % (title, n))
        for path in sorted(table):
            print('## %s  (%d)' % (path, len(table[path])))
            for line_no, call, value in sorted(table[path]):
                print('%5d\t%s\t%s' % (line_no, call, value))
            print()

    print('scanned %d literals\n' % scanned)
    dump('non-ASCII literals (translate)', i18n)
    dump('ASCII literals that pass the UI filter', ascii_ui)


if __name__ == '__main__':
    main()
