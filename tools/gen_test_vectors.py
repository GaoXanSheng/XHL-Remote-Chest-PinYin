import argparse
import json
from pathlib import Path

from pypinyin import Style, pinyin


def reading(char):
    if char.isascii():
        return char.lower() if char.isalnum() else None
    if 0x4E00 <= ord(char) <= 0x9FFF:
        values = pinyin(char, style=Style.NORMAL, heteronym=True)[0]
        for value in values:
            text = value.lower()
            if text.isascii() and text.isalpha():
                return text
    return None


def wide(text):
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')
    return 'L"' + escaped + '"'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="data")
    parser.add_argument("--out", default="build/generated/vectors.h")
    args = parser.parse_args()

    rows = json.loads((Path(args.data) / "items_full.json").read_text(encoding="utf-8"))
    seen = set()
    entries = []
    for row in rows:
        name = row["name"]
        if name in seen:
            continue
        seen.add(name)
        full = []
        initials = []
        for char in name:
            value = reading(char)
            if value is None:
                continue
            full.append(value)
            initials.append(value[0])
        if not full:
            continue
        entries.append((name, "".join(full), "".join(initials)))

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8", newline="\n") as handle:
        handle.write("typedef struct { const wchar_t *name; const char *full; const char *initials; } TestVector;\n")
        handle.write(f"#define VECTOR_N {len(entries)}\n")
        handle.write("static const TestVector VECTORS[VECTOR_N] = {\n")
        for name, full, initials in entries:
            handle.write("{" + wide(name) + ',"' + full + '","' + initials + '"},\n')
        handle.write("};\n")
    print(f"vectors {len(entries)} -> {out}")


if __name__ == "__main__":
    main()
