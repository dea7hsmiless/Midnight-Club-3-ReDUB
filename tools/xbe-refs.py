#!/usr/bin/env python3
"""Find strings and cross-references in the game's XBE.

Used while mapping the ambient traffic/pedestrian code (M5): the lifted C
keeps only addresses, so the only way to go from a name like
"city/%s/ped/ped_male_slight" to the code that builds it is to read the XBE
and its section table.

    python tools/xbe-refs.py --xbe <path> str "ped|cop" [--limit N]
    python tools/xbe-refs.py --xbe <path> ref 0x0044CE90
    python tools/xbe-refs.py --xbe <path> dump 0x0044CE90 0x80

`str` prints the VA of every printable string matching the regex.
`ref` prints the VA of every dword equal to the address (vtable slots,
class tables, string-pointer tables).
`dump` prints dwords + ASCII, which is how a vtable is read.

XBE quirks this handles: the section headers are at a *virtual* address
(0x10370 -> file offset 0x370, base 0x10000), and the header fields used here
are at 0x104 (base), 0x11C (section count) and 0x120 (section headers).
"""
import re
import struct
import sys


def load(path):
    d = open(path, "rb").read()
    if d[:4] != b"XBEH":
        sys.exit("not an XBE")
    nsec, = struct.unpack_from("<I", d, 0x11C)
    secva, = struct.unpack_from("<I", d, 0x120)
    base, = struct.unpack_from("<I", d, 0x104)
    off = secva - base
    secs = []
    for i in range(nsec):
        _flags, va, vsize, faddr, fsize = struct.unpack_from("<IIIII", d, off + i * 0x38)
        secs.append((va, vsize, faddr, fsize))
    return d, secs


def read(d, secs, va, n):
    for sva, vsize, faddr, fsize in secs:
        if sva <= va < sva + vsize:
            o = faddr + (va - sva)
            return d[o:o + n]
    return b""


def cmd_str(d, secs, pattern):
    want = re.compile(pattern.encode(), 0)
    for va, vsize, faddr, fsize in secs:
        for m in re.finditer(rb"[ -~]{4,110}", d[faddr:faddr + fsize]):
            s = m.group(0)
            if want.search(s):
                print("VA %08X  %r" % (va + m.start(), s.decode("latin1")))


def cmd_ref(d, secs, needle):
    pat = struct.pack("<I", needle)
    hits = 0
    for va, vsize, faddr, fsize in secs:
        blob = d[faddr:faddr + fsize]
        start = 0
        while True:
            i = blob.find(pat, start)
            if i < 0:
                break
            print("VA %08X" % (va + i))
            hits += 1
            start = i + 4
    if not hits:
        print("no reference to %08X" % needle)


def cmd_dump(d, secs, va, n):
    b = read(d, secs, va, n)
    for i in range(0, len(b), 16):
        chunk = b[i:i + 16]
        words = " ".join("%08X" % struct.unpack_from("<I", chunk, j)[0]
                         for j in range(0, len(chunk), 4))
        asc = "".join(chr(c) if 32 <= c < 127 else "." for c in chunk)
        print("%08X  %-47s  %s" % (va + i, words, asc))


def main(argv):
    if len(argv) < 4 or argv[1] != "--xbe":
        sys.exit(__doc__)
    path = argv[2]
    cmd = argv[3]
    rest = argv[4:]
    d, secs = load(path)
    if cmd == "str":
        cmd_str(d, secs, rest[0] if rest else ".")
    elif cmd == "ref":
        cmd_ref(d, secs, int(rest[0], 16))
    elif cmd == "dump":
        cmd_dump(d, secs, int(rest[0], 16), int(rest[1], 16) if len(rest) > 1 else 0x80)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main(sys.argv)
