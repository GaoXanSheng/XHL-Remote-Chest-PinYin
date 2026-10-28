import argparse
import json
from pathlib import Path

from pypinyin import Style, pinyin

PY_FIRST = 0x4E00
PY_LAST = 0x9FFF
PY_MAX_READINGS = 2
FNV_OFFSET = 1469598103934665603
FNV_PRIME = 1099511628211

SLUG_CAT = {
    "tool": 0,
    "weapon": 1,
    "armor": 2,
    "accessory": 3,
    "ammo": 4,
    "consumable": 5,
    "material": 6,
    "collectible": 7,
    "block": 8,
    "structure": 8,
    "furniture": 8,
    "facility": 8,
    "homeware": 0,
    "vanity": 2,
    "plant": 5,
    "palette": 6,
    "decoration": 6,
}


def fnv1a(data):
    value = FNV_OFFSET
    for byte in data:
        value ^= byte
        value = (value * FNV_PRIME) & 0xFFFFFFFFFFFFFFFF
    return value


def load_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def clean_readings(values):
    result = []
    for value in values:
        text = "".join(ch for ch in value.lower() if "a" <= ch <= "z")
        if text and text not in result:
            result.append(text)
    return result[:PY_MAX_READINGS]


def readings_of(char, overrides):
    if char in overrides:
        return clean_readings(overrides[char])
    return clean_readings(pinyin(char, style=Style.NORMAL, heteronym=True)[0])


def token_aliases(name, overrides):
    parts = []
    for char in name:
        values = readings_of(char, overrides)
        if not values:
            return []
        parts.append(values[0])
    full = "".join(parts)
    initials = "".join(part[0] for part in parts)
    aliases = [full]
    if initials != full:
        aliases.append(initials)
    return aliases


def emit(handle, ctype, name, values, per_line=16):
    handle.write(f"static const {ctype} {name}[{len(values)}] = {{")
    suffix = "ULL" if ctype == "unsigned long long" else ""
    for index, value in enumerate(values):
        if index % per_line == 0:
            handle.write("\n")
        handle.write(f"{value}{suffix},")
    handle.write("\n};\n")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="data")
    parser.add_argument("--out", default="build/generated")
    args = parser.parse_args()

    data_dir = Path(args.data)
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    full = load_json(data_dir / "items_full.json")
    desc_rows = load_json(data_dir / "items_desc.json")
    attrs = load_json(data_dir / "item_attributes.json")
    overrides_path = data_dir / "pinyin_overrides.json"
    overrides = load_json(overrides_path) if overrides_path.exists() else {}

    desc_by_name = {}
    for row in desc_rows:
        desc_by_name.setdefault(row["name"], row)

    cats = attrs["cats"]
    types = attrs["types"]
    subs = attrs["subs"]
    type_index = {name: index for index, name in enumerate(types)}

    items = []
    seen = set()
    for row in full:
        name = row["name"]
        if name in seen:
            continue
        seen.add(name)
        desc_row = desc_by_name.get(name, {})
        attr = attrs["items"].get(name)
        if attr is None:
            cat = SLUG_CAT.get(row["slug"], -1)
            typ = type_index.get(desc_row.get("type", ""), -1)
            sub = -1
        else:
            cat, typ, sub = attr
        items.append(
            {
                "name": name,
                "desc": desc_row.get("desc", ""),
                "cat": cat,
                "type": typ,
                "sub": sub,
            }
        )

    name_pool = bytearray()
    desc_pool = bytearray()
    name_off = []
    name_len = []
    desc_off = []
    desc_len = []
    cat_values = []
    type_values = []
    sub_values = []
    hashes = []

    for item in items:
        name_bytes = item["name"].encode("utf-8")
        desc_bytes = item["desc"].encode("utf-8")
        if len(name_bytes) > 0xFFFF or len(desc_bytes) > 0xFFFF:
            raise SystemExit(f"item too large: {item['name']}")
        name_off.append(len(name_pool))
        name_len.append(len(name_bytes))
        name_pool.extend(name_bytes)
        desc_off.append(len(desc_pool))
        desc_len.append(len(desc_bytes))
        desc_pool.extend(desc_bytes)
        cat_values.append(item["cat"])
        type_values.append(item["type"])
        sub_values.append(item["sub"])
        hashes.append(fnv1a(name_bytes))

    order = sorted(range(len(items)), key=lambda i: (hashes[i], items[i]["name"].encode("utf-8")))
    hash_sorted = [hashes[i] for i in order]
    index_sorted = order

    entries = []
    for kind, group in enumerate((cats, types, subs)):
        for index, name in enumerate(group):
            for alias in token_aliases(name, overrides):
                entries.append((name, kind, index, alias))

    token_pool = bytearray()
    token_off = []
    token_len = []
    token_kind = []
    token_id = []
    alias_pool = bytearray()
    alias_off = []
    alias_len = []
    for name, kind, index, alias in entries:
        name_bytes = name.encode("utf-8")
        token_off.append(len(token_pool))
        token_len.append(len(name_bytes))
        token_pool.extend(name_bytes)
        token_kind.append(kind)
        token_id.append(index)
        alias_bytes = alias.encode("ascii")
        alias_off.append(len(alias_pool))
        alias_len.append(len(alias_bytes))
        alias_pool.extend(alias_bytes)

    if len(index_sorted) > 0xFFFF:
        raise SystemExit("item count exceeds 16-bit index")

    with (out_dir / "item_table.h").open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"#define ITEM_N {len(items)}\n")
        handle.write(f"#define TOKEN_N {len(entries)}\n")
        emit(handle, "unsigned long long", "ITEM_HASH", hash_sorted)
        emit(handle, "unsigned short", "ITEM_IDX", index_sorted)
        emit(handle, "unsigned int", "ITEM_NAME_OFF", name_off)
        emit(handle, "unsigned short", "ITEM_NAME_LEN", name_len)
        emit(handle, "unsigned int", "ITEM_DESC_OFF", desc_off)
        emit(handle, "unsigned short", "ITEM_DESC_LEN", desc_len)
        emit(handle, "unsigned char", "ITEM_CAT", cat_values)
        emit(handle, "short", "ITEM_TYPE", type_values)
        emit(handle, "short", "ITEM_SUB", sub_values)
        emit(handle, "unsigned int", "TOKEN_OFF", token_off)
        emit(handle, "unsigned char", "TOKEN_LEN", token_len)
        emit(handle, "unsigned char", "TOKEN_KIND", token_kind)
        emit(handle, "short", "TOKEN_ID", token_id)
        emit(handle, "unsigned int", "ALIAS_OFF", alias_off)
        emit(handle, "unsigned char", "ALIAS_LEN", alias_len)
        emit(handle, "unsigned char", "NAME_POOL", list(name_pool))
        emit(handle, "unsigned char", "DESC_POOL", list(desc_pool))
        emit(handle, "unsigned char", "TOKEN_POOL", list(token_pool))
        emit(handle, "unsigned char", "ALIAS_POOL", list(alias_pool))

    py_off = []
    py_cnt = []
    py_pool = bytearray()
    uncovered = []
    for codepoint in range(PY_FIRST, PY_LAST + 1):
        values = readings_of(chr(codepoint), overrides)
        py_off.append(len(py_pool))
        count = 0
        for value in values:
            py_pool.extend(value.encode("ascii"))
            py_pool.append(0)
            count += 1
        py_cnt.append(count)
        if count == 0:
            uncovered.append(codepoint)

    for offset in py_off:
        if offset > 0xFFFFFFFF:
            raise SystemExit("pinyin pool exceeds 32-bit offsets")

    with (out_dir / "pinyin_table.h").open("w", encoding="utf-8", newline="\n") as handle:
        handle.write(f"#define PY_FIRST 0x{PY_FIRST:X}\n")
        handle.write(f"#define PY_LAST 0x{PY_LAST:X}\n")
        emit(handle, "unsigned int", "PY_OFF", py_off)
        emit(handle, "unsigned char", "PY_CNT", py_cnt)
        emit(handle, "unsigned char", "PY_POOL", list(py_pool))

    missing_names = [item["name"] for item in items if item["type"] < 0 and item["sub"] < 0]
    print(f"items {len(items)} tokens {len(entries)}")
    print(f"name_pool {len(name_pool)} desc_pool {len(desc_pool)}")
    print(f"pinyin chars {PY_LAST - PY_FIRST + 1} pool {len(py_pool)} uncovered {len(uncovered)}")
    print(f"items without legacy attrs {len(missing_names)}")
    if uncovered:
        print("uncovered sample", [hex(c) for c in uncovered[:10]])


if __name__ == "__main__":
    main()
