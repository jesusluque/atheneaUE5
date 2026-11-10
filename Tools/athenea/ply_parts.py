# Copyright (c) 2026 jesus luque.
# A binary PLY of gaussians cut into parts of consecutive records, each under
# a size (athenea writes a .athc of at most 4 GiB of records, so a capture
# larger than that is converted a part at a time): NAME_0.ply, NAME_1.ply ...
# beside the file, every part with the file's own header and its own count.
# Plain Python, no Unreal:
#   python ply_parts.py FILE.ply [GIB]        (3.5 GiB of records a part by default)
import os, sys

SIZES = {"char": 1, "uchar": 1, "int8": 1, "uint8": 1, "short": 2, "ushort": 2, "int16": 2, "uint16": 2,
         "int": 4, "uint": 4, "int32": 4, "uint32": 4, "float": 4, "float32": 4, "double": 8, "float64": 8}
path = sys.argv[1]
limit = int(float(sys.argv[2]) * (1 << 30)) if len(sys.argv) > 2 else int(3.5 * (1 << 30))
with open(path, "rb") as f:
    header = b""
    while not header.endswith(b"end_header\n"):
        line = f.readline()
        if not line:
            sys.exit("no end_header in " + path)
        header += line
    lines = header.decode("ascii").split("\n")
    if not any(l.startswith("format binary_little_endian") for l in lines):
        sys.exit("only binary_little_endian PLY files are cut")
    elements = [l for l in lines if l.startswith("element ")]
    if len(elements) != 1 or not elements[0].startswith("element vertex "):
        sys.exit("one element, vertex, is what a gaussian PLY holds; this one has: " + ", ".join(elements))
    count = int(elements[0].split()[2])
    record = 0
    for l in lines:
        if l.startswith("property "):
            kind = l.split()[1]
            if kind not in SIZES:
                sys.exit("a property of type %s cannot be sized" % kind)
            record += SIZES[kind]
    each = max(limit // record, 1)
    parts = (count + each - 1) // each
    # Parts of the same size, so no part is a sliver.
    each = (count + parts - 1) // parts
    base = os.path.splitext(path)[0]
    left = count
    for p in range(parts):
        n = min(each, left)
        out = "%s_%d.ply" % (base, p)
        with open(out, "wb") as o:
            o.write("\n".join(("element vertex %d" % n) if l.startswith("element vertex ") else l for l in lines).encode("ascii"))
            todo = n * record
            while todo > 0:
                block = f.read(min(todo, 64 << 20))
                if not block:
                    sys.exit("the file ends before its %d records" % count)
                o.write(block)
                todo -= len(block)
        left -= n
        print("%s: %d gaussians, %.2f GiB of records" % (out, n, n * record / float(1 << 30)))
