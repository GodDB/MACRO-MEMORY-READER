"""Read the saved Cheat Engine pointer scan without modifying it."""

from pathlib import Path
import struct
import sys


def take(data, pos, fmt):
    size = struct.calcsize(fmt)
    return struct.unpack_from(fmt, data, pos)[0], pos + size


def parse(path):
    data = path.read_bytes()
    if data[:1] != b"\xce":
        raise ValueError("Not a Cheat Engine pointer scan")
    version = data[1]
    pos = 2
    module_count, pos = take(data, pos, "<I")
    modules = []
    for _ in range(module_count):
        size, pos = take(data, pos, "<I")
        name = data[pos : pos + size].decode("utf-8", "replace")
        pos += size
        base, pos = take(data, pos, "<Q")
        modules.append((name, base))
    max_level, pos = take(data, pos, "<I")
    compressed, pos = take(data, pos, "<B")
    if compressed:
        aligned, pos = take(data, pos, "<B")
        index_bits, pos = take(data, pos, "<B")
        base_bits, pos = take(data, pos, "<B")
        level_bits, pos = take(data, pos, "<B")
        offset_bits, pos = take(data, pos, "<B")
        fixed_count, pos = take(data, pos, "<B")
        fixed = []
        for _ in range(fixed_count):
            x, pos = take(data, pos, "<I")
            fixed.append(x)
        entry_size = (base_bits + index_bits + level_bits + offset_bits * (max_level - fixed_count) + 7) // 8
    else:
        aligned = index_bits = base_bits = level_bits = offset_bits = 0
        fixed = []
        entry_size = 16 + 4 * max_level
    if version >= 2:
        base_range, pos = take(data, pos, "<B")
        if base_range:
            _, pos = take(data, pos, "<Q")
    print(f"Version {version}; modules {module_count}; max level {max_level}; compressed {compressed}; record size {entry_size}")
    print(f"Header end {pos:#x}; module/index/level/offset bits: {base_bits}/{index_bits}/{level_bits}/{offset_bits}; aligned={aligned}; fixed={fixed}")

    def bits(record, start, width):
        return (int.from_bytes(record, "little") >> start) & ((1 << width) - 1)

    records = []
    for part in sorted(path.parent.glob(path.name + ".results.*")):
        raw = part.read_bytes()
        if len(raw) % entry_size:
            print(f"Warning: {part.name} is not divisible by record size")
        for i in range(0, len(raw) - entry_size + 1, entry_size):
            record = raw[i : i + entry_size]
            if compressed:
                bit = 0
                base_offset = bits(record, bit, base_bits)
                bit += base_bits
                module_id = bits(record, bit, index_bits)
                if module_id & (1 << (index_bits - 1)):
                    module_id -= 1 << index_bits
                bit += index_bits
                count = bits(record, bit, level_bits) + len(fixed)
                bit += level_bits
                offsets = list(fixed)
                for _ in range(count - len(fixed)):
                    offset = bits(record, bit, offset_bits)
                    bit += offset_bits
                    offsets.append(offset * 4 if aligned else offset)
            else:
                module_id, base_offset, count = struct.unpack_from("<IQI", record)
                if module_id == 0xFFFFFFFF:
                    module_id = -1
                offsets = list(struct.unpack_from("<" + "I" * count, record, 16))
            if module_id == -1:
                origin = f"{base_offset:08X}"
            elif module_id < len(modules):
                origin = f"{modules[module_id][0]}+{base_offset:X}"
            else:
                origin = f"bad_module_{module_id}+{base_offset:X}"
            records.append((origin, offsets))
    print(f"Records: {len(records)}")
    if len(sys.argv) > 2 and sys.argv[2] == "--stats":
        from collections import Counter
        print("Levels:", dict(sorted(Counter(len(o) for _, o in records).items())))
        print("Game-module levels:", dict(sorted(Counter(len(o) for m, o in records if m.startswith("darkeden.exe+")).items())))
        print("Most frequent bases:", Counter(m for m, _ in records).most_common(20))
        print("Shortest game-module paths:")
        for origin, offsets in sorted((r for r in records if r[0].startswith("darkeden.exe+")), key=lambda r: (len(r[1]), sum(r[1])))[:40]:
            print(origin, ",".join(f"{o:X}" for o in offsets))
    elif len(sys.argv) > 2:
        output = Path(sys.argv[2])
        max_output_level = int(sys.argv[3]) if len(sys.argv) > 3 else 99
        lines = ["-- Pointer candidates from the saved Cheat Engine scan.", "return {"]
        for origin, offsets in records:
            if "+" not in origin or len(offsets) > max_output_level:
                continue
            module, base = origin.rsplit("+", 1)
            values = ", ".join(f"0x{o:X}" for o in offsets)
            lines.append(f'  {{module={module!r}, base=0x{base}, offsets={{{values}}}}},')
        lines.append("}")
        output.write_text("\n".join(lines) + "\n", encoding="utf-8")
        print(f"Wrote {output}")
    else:
        for origin, offsets in records[:100]:
            print(origin, "offsets(target-first):", ",".join(f"{o:X}" for o in offsets))


if __name__ == "__main__":
    parse(Path(sys.argv[1]))
