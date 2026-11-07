# Copyright (c) 2026 jesus luque.
# The TX test frame's regions, UE against athenea: python3 tx_regions.py ue.exr athenea.exr
# Both converted to PFM by oiiotool, athenea's at UE's size; the ball's disc fitted from athenea's frame
# (its ball is darker than 0.15 below the horizon, the ground near 1); UE's
# values divided by the capture's exposure factor (2.02, decisions §11).
import struct, subprocess, sys, os, tempfile

def load(path, size=None):
    out = os.path.join(tempfile.mkdtemp(), "f.pfm")
    resize = ["--resize", "%dx%d" % size] if size else []
    subprocess.run(["oiiotool", path, "--ch", "R,G,B"] + resize + ["-o", out], check=True)
    b = open(out, "rb").read()
    parts = b.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    scale = float(parts[2])
    data = parts[3]
    fmt = "<" if scale < 0 else ">"
    v = struct.unpack(fmt + "%df" % (w * h * 3), data[: w * h * 12])
    rows = [[0.2126 * v[(y * w + x) * 3] + 0.7152 * v[(y * w + x) * 3 + 1] + 0.0722 * v[(y * w + x) * 3 + 2] for x in range(w)] for y in range(h)]
    return w, h, rows[::-1]   # PFM is bottom row first

w, h, ue = load(sys.argv[1])
w2, h2, ref = load(sys.argv[2], (w, h))
assert (w, h) == (w2, h2)
k = float(sys.argv[3]) if len(sys.argv) > 3 else 2.02
# The disc: athenea's pixels darker than 0.15 that have ground (> 0.5) somewhere left and right on their row.
pts = [(x, y) for y in range(h) for x in range(w) if 0.0 < ref[y][x] < 0.15 and max(ref[y][:x] or [0]) > 0.5 and max(ref[y][x:] or [0]) > 0.5]
xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
cx = sum(xs) / len(xs)
widths = {}
for x, y in pts:
    lo, hi = widths.get(y, (x, x)); widths[y] = (min(lo, x), max(hi, x))
r = max(hi - lo for lo, hi in widths.values()) / 2.0
cy = max(widths, key=lambda y: widths[y][1] - widths[y][0])
def region(name, inside):
    a = u = n = err = 0.0
    for y in range(h):
        for x in range(w):
            if inside(x, y):
                rv, uv = ref[y][x], ue[y][x] / k
                a += rv; u += uv; n += 1; err += abs(uv - rv) / max(rv, 1e-3)
    print("%-28s athenea %.4f  UE %.4f  UE/athenea %.3f  mean abs rel %.3f  (%d px)" % (name, a / n, u / n, u / a, err / n, n))
inball = lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 < (0.95 * r) ** 2
region("ball", inball)
region("ball, upper half", lambda x, y: inball(x, y) and y < cy)
region("ball, lower half", lambda x, y: inball(x, y) and y >= cy)
region("ground", lambda x, y: ref[y][x] > 0.5 and (x - cx) ** 2 + (y - cy) ** 2 > (1.3 * r) ** 2)
print("disc centre (%.1f, %.1f) radius %.1f" % (cx, cy, r))
