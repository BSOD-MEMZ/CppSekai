"""Join C++ string literals that were accidentally split by a real newline.

The rewrite scripts are fed through a JSON layer that turns `\\n` into a real
newline, so `printf("...\\n")` ended up with the quote on the next line. A line
with an odd number of unescaped quotes is exactly that case: join it with the
next line and put the escape back.
"""
from pathlib import Path

BACKSLASH = chr(92)
QUOTE = chr(34)


def odd_quotes(line: str) -> bool:
    count = 0
    escaped = False
    for ch in line:
        if escaped:
            escaped = False
            continue
        if ch == BACKSLASH:
            escaped = True
            continue
        if ch == QUOTE:
            count += 1
    return count % 2 == 1


def main() -> None:
    for name in ["main.cpp", "game/Story.cpp", "game/StoryUi.cpp", "platform/ChartDlGuide.cpp"]:
        path = Path(name)
        lines = path.read_text(encoding="utf-8", newline="").split(chr(10))
        out = []
        joined = 0
        i = 0
        while i < len(lines):
            line = lines[i].rstrip(chr(13))
            while odd_quotes(line) and i + 1 < len(lines):
                line = line + BACKSLASH + "n" + lines[i + 1].rstrip(chr(13))
                i += 1
                joined += 1
            out.append(line)
            i += 1
        path.write_text(chr(10).join(out), encoding="utf-8", newline="")
        print(name, "joined", joined)


if __name__ == "__main__":
    main()
