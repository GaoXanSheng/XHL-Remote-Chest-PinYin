import argparse
import json
import re
from pathlib import Path


def parse_array(text, name):
    match = re.search(
        r"static const [^;]*?\b" + re.escape(name) + r"\[[^\]]*\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    )
    if not match:
        raise SystemExit(f"array not found: {name}")
    return [int(value) for value in re.findall(r"-?\d+", match.group(1))]


def parse_pool(text, name):
    match = re.search(
        r"static const unsigned char " + re.escape(name) + r"\[[^\]]*\]\s*=\s*\{(.*?)\};",
        text,
        re.S,
    )
    if not match:
        raise SystemExit(f"pool not found: {name}")
    return bytes(int(value) for value in re.findall(r"\d+", match.group(1)))


def tokens_by_kind(path):
    offset = parse_array(path, "TOKEN_OFF")
    length = parse_array(path, "TOKEN_LEN")
    kind = parse_array(path, "TOKEN_KIND")
    ident = parse_array(path, "TOKEN_ID")
    pool = bytes(parse_pool(path, "TOKEN_POOL"))
    lists = [[], [], []]
    for off, ln, kn, idx in zip(offset, length, kind, ident):
        name = pool[off : off + ln].decode("utf-8")
        while len(lists[kn]) <= idx:
            lists[kn].append(None)
        lists[kn][idx] = name
    for group in lists:
        if any(item is None for item in group):
            raise SystemExit("token index gap")
    return lists


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", default="item_table.h")
    parser.add_argument("--out", default="data/item_attributes.json")
    args = parser.parse_args()

    header = Path(args.header)
    text = header.read_text(encoding="utf-8")

    name_off = parse_array(text, "ITEM_NAME_OFF")
    name_len = parse_array(text, "ITEM_NAME_LEN")
    name_pool = bytes(parse_pool(text, "NAME_POOL"))
    cat = parse_array(text, "ITEM_CAT")
    typ = parse_array(text, "ITEM_TYPE")
    sub = parse_array(text, "ITEM_SUB")

    items = {}
    for off, ln, c, t, s in zip(name_off, name_len, cat, typ, sub):
        name = name_pool[off : off + ln].decode("utf-8")
        if name in items:
            continue
        items[name] = [c, t, s]

    cats, types, subs = tokens_by_kind(text)
    payload = {
        "source": header.name,
        "count": len(items),
        "cats": cats,
        "types": types,
        "subs": subs,
        "items": items,
    }
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(
        json.dumps(payload, ensure_ascii=False, indent=0, sort_keys=False) + "\n",
        encoding="utf-8",
    )
    print(f"items {len(items)} cats {len(cats)} types {len(types)} subs {len(subs)} -> {out}")


if __name__ == "__main__":
    main()
