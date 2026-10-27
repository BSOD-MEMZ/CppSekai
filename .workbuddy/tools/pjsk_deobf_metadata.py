#!/usr/bin/env python3
"""Recover a pjsk `global-metadata.dat` that is XOR-obfuscated with a repeating key.

Method (see RE.md §5), deliberately free of hard-coded offsets:
  1. The plain Il2Cpp metadata always starts with the magic `AF 1B B1 FA`, so the first
     four key bytes follow from the ciphertext.
  2. Those four bytes are a literal in `libil2cpp.so`'s .rodata (the key array). Every
     occurrence is a candidate; take `KEY_LEN` bytes from it as the repeating key.
  3. Validate a candidate by decrypting the header: magic, a plausible metadata version,
     and header offsets that stay inside the file. Exactly one candidate normally wins.

Usage:
  python .workbuddy/tools/pjsk_deobf_metadata.py <global-metadata.dat> <libil2cpp.so> <out.dat>
"""
import struct
import sys

MAGIC = 0xFAB11BAF  # little-endian `AF 1B B1 FA`
KEY_LEN = 128


def xor(data: bytes, key: bytes) -> bytes:
    n = len(key)
    return bytes(b ^ key[i % n] for i, b in enumerate(data))


def find_key(meta: bytes, so: bytes, version_min=20, version_max=40):
    key4 = bytes(a ^ b for a, b in zip(meta[:4], MAGIC.to_bytes(4, "little")))
    head = meta[:KEY_LEN]
    decrypted_head = xor(head, (key4 * (KEY_LEN // 4 + 1))[:KEY_LEN])
    # Quick reject: with a period-128 key the first 4 bytes already have to match.
    if struct.unpack("<I", decrypted_head[0:4])[0] != MAGIC:
        raise SystemExit("ciphertext does not look like XOR'd metadata")

    hits, pos = [], 0
    while True:
        pos = so.find(key4, pos)
        if pos < 0:
            break
        hits.append(pos)
        pos += 1
    print(f"key[0:4] = {key4.hex()}  -> {len(hits)} candidate anchor(s) in libil2cpp.so")

    good = []
    for off in hits:
        candidate = so[off:off + KEY_LEN]
        if len(candidate) < KEY_LEN:
            continue
        dec = xor(meta[:0x100], candidate)
        magic, version = struct.unpack("<II", dec[0:8])
        if magic != MAGIC or not (version_min <= version <= version_max):
            continue
        # header sanity: every u32 in the first 0x40 acts as an offset/length; require the
        # plausible ones (string/type/field tables) to be inside the file.
        offsets = struct.unpack("<16I", dec[0x40:0x80])
        inside = sum(1 for v in offsets if 0 < v < len(meta))
        good.append((off, version, inside, candidate))
        print(f"  anchor 0x{off:X}: version={version} header-offsets-inside={inside}/16")

    if not good:
        raise SystemExit("no anchor produced a valid header")
    good.sort(key=lambda t: t[2], reverse=True)
    if len(good) > 1 and good[0][2] == good[1][2]:
        print("warning: several anchors validate equally; taking the first")
    return good[0]


def main() -> int:
    if len(sys.argv) != 4:
        print(__doc__)
        return 2
    meta_path, so_path, out_path = sys.argv[1:4]
    meta = open(meta_path, "rb").read()
    so = open(so_path, "rb").read()
    off, version, _, key = find_key(meta, so)
    plain = xor(meta, key)
    if struct.unpack("<I", plain[:4])[0] != MAGIC:
        raise SystemExit("decrypted file lost the magic; key is wrong")
    open(out_path, "wb").write(plain)
    print(f"key anchor 0x{off:X}, metadata version {version}, wrote {out_path} ({len(plain)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
