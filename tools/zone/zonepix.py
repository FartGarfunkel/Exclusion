# tools/zone/zonepix.py — tiny pixel-art toolkit shared by paint_items.py and paint_skins.py (no Pillow needed:
# PNGs are written with zlib + struct). A canvas is a w*h list of None (transparent) or (r, g, b).
import math, struct, zlib


def clamp(v): return max(0, min(255, int(v + 0.5)))
def sh(c, f): return tuple(clamp(v * f + (f - 1) * 28) for v in c)          # lighten (f>1) / darken (f<1)
def mix(a, b, t): return tuple(clamp(a[i] + (b[i] - a[i]) * t) for i in range(3))


def png_bytes(w, h, rgba):
    raw = b''.join(b'\0' + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))
    def ch(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b'')


def write_png(path, w, h, px):      # px: list of (r,g,b) / (r,g,b,a) / None
    out = bytearray()
    for p in px:
        out += bytes((0, 0, 0, 0)) if p is None else bytes((p[0], p[1], p[2], p[3] if len(p) > 3 else 255))
    with open(path, 'wb') as f: f.write(png_bytes(w, h, out))


def _inside(pts, x, y):
    ins = False; j = len(pts) - 1
    for i in range(len(pts)):
        xi, yi = pts[i]; xj, yj = pts[j]
        if (yi > y) != (yj > y) and x < (xj - xi) * (y - yi) / (yj - yi) + xi: ins = not ins
        j = i
    return ins


def _segdist(px, py, a, b):
    ax, ay = a; bx, by = b; dx, dy = bx - ax, by - ay
    t = 0 if dx == dy == 0 else max(0, min(1, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def bez(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n; u = 1 - t
        out.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                    u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))
    return out


class Cv:
    def __init__(s, w=32, h=32): s.w, s.h = w, h; s.p = [None] * (w * h)
    def get(s, x, y): return s.p[y * s.w + x] if 0 <= x < s.w and 0 <= y < s.h else None
    def put(s, x, y, c):
        x = int(round(x)); y = int(round(y))
        if 0 <= x < s.w and 0 <= y < s.h: s.p[y * s.w + x] = c
    def hole(s, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1): s.put(x, y, None)
    def rect(s, x0, y0, x1, y1, c, g=0.16):             # inclusive; light on top, dark at the bottom
        h = y1 - y0
        for y in range(y0, y1 + 1):
            cc = sh(c, 1 + g * (1 - 2 * (y - y0) / h)) if h and g else c
            for x in range(x0, x1 + 1): s.put(x, y, cc)
    def vrect(s, x0, y0, x1, y1, c, g=0.16):            # light on the left, dark on the right
        w = x1 - x0
        for x in range(x0, x1 + 1):
            cc = sh(c, 1 + g * (1 - 2 * (x - x0) / w)) if w and g else c
            for y in range(y0, y1 + 1): s.put(x, y, cc)
    def cyl(s, x0, y0, x1, y1, c):                      # horizontal round bar, lit from above
        h = y1 - y0
        for y in range(y0, y1 + 1):
            t = (y - y0) / h if h else 0.4
            cc = sh(c, 1.18 - 0.72 * abs(t - 0.28))
            for x in range(x0, x1 + 1): s.put(x, y, cc)
    def vcyl(s, x0, y0, x1, y1, c):                     # upright round bar, lit from the left
        w = x1 - x0
        for x in range(x0, x1 + 1):
            t = (x - x0) / w if w else 0.4
            cc = sh(c, 1.18 - 0.72 * abs(t - 0.28))
            for y in range(y0, y1 + 1): s.put(x, y, cc)
    def poly(s, pts, c, g=0.0):
        xs = [p[0] for p in pts]; ys = [p[1] for p in pts]
        y0, y1 = int(math.floor(min(ys))), int(math.ceil(max(ys))); x0, x1 = int(math.floor(min(xs))), int(math.ceil(max(xs)))
        h = max(1, y1 - y0); n = len(pts)
        for y in range(y0, y1 + 1):
            cc = sh(c, 1 + g * (1 - 2 * (y - y0) / h)) if g else c
            for x in range(x0, x1 + 1):
                if _inside(pts, x, y) or min(_segdist(x, y, pts[i], pts[(i + 1) % n]) for i in range(n)) <= 0.55: s.put(x, y, cc)
    def sym(s, pts, c, g=0.0):                          # polygon and its mirror image (x -> w-1-x)
        s.poly(pts, c, g); s.poly([(s.w - 1 - x, y) for x, y in pts], c, g)
    def line(s, x0, y0, x1, y1, c):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx, dy = abs(x1 - x0), -abs(y1 - y0); sx = 1 if x0 < x1 else -1; sy = 1 if y0 < y1 else -1; e = dx + dy
        while True:
            s.put(x0, y0, c)
            if x0 == x1 and y0 == y1: break
            e2 = 2 * e
            if e2 >= dy: e += dy; x0 += sx
            if e2 <= dx: e += dx; y0 += sy
    def ball(s, cx, cy, rx, ry, c, k=0.5):              # shaded ellipse, light from the top left
        for y in range(int(cy - ry - 1), int(cy + ry + 2)):
            for x in range(int(cx - rx - 1), int(cx + rx + 2)):
                dx = (x - cx) / rx; dy = (y - cy) / ry
                if dx * dx + dy * dy <= 1.0:
                    hx = dx + 0.4; hy = dy + 0.4
                    s.put(x, y, sh(c, 1 + k * (0.55 - math.sqrt(hx * hx + hy * hy) * 0.75)))
    def disc(s, cx, cy, r, c):
        for y in range(int(cy - r - 1), int(cy + r + 2)):
            for x in range(int(cx - r - 1), int(cx + r + 2)):
                if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + 0.3: s.put(x, y, c)
    def ring(s, cx, cy, ro, ri, c):
        for y in range(int(cy - ro - 1), int(cy + ro + 2)):
            for x in range(int(cx - ro - 1), int(cx + ro + 2)):
                d = (x - cx) ** 2 + (y - cy) ** 2
                if ri * ri <= d <= ro * ro + 0.3: s.put(x, y, c)
    def tube(s, pts, r0, r1, c):                        # fat curved worm along a dense point list
        n = len(pts) - 1
        rs = [r0 + (r1 - r0) * i / max(1, n) for i in range(n + 1)]
        for (x, y), r in zip(pts, rs): s.disc(x, y, r, sh(c, 0.68))
        for (x, y), r in zip(pts, rs): s.disc(x - 0.12 * r, y - 0.12 * r, r * 0.82, c)
        for (x, y), r in zip(pts, rs):
            if r >= 1.8: s.disc(x - 0.3 * r, y - 0.3 * r, r * 0.4, sh(c, 1.28))
    def noise_fill(s, x0, y0, x1, y1, c, rnd, amt=0.12, p=0.5):   # speckle inside an area (only on painted pixels)
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if s.get(x, y) is not None and rnd.random() < p: s.put(x, y, sh(s.get(x, y), 1 + rnd.uniform(-amt, amt)))


def finish(cv, rnd, wear=0.5, grade=0.10, noise=0.06, outline=True):
    """Light from the top left, a little noise and bare-metal wear, a muted grade, then a dark 1px outline."""
    W, H = cv.w, cv.h; src = list(cv.p)
    A = lambda x, y: 0 <= x < W and 0 <= y < H and src[y * W + x] is not None
    out = [None] * (W * H)
    for y in range(H):
        for x in range(W):
            c = src[y * W + x]
            if c is None: continue
            up, lf, dn, rt = not A(x, y - 1), not A(x - 1, y), not A(x, y + 1), not A(x + 1, y)
            f = 1.0
            if up or lf: f *= 1.13
            if dn or rt: f *= 0.82
            c = sh(c, f * (1 + rnd.uniform(-noise, noise)))
            r = rnd.random()
            if (up or lf or dn or rt) and r < 0.22 * wear: c = mix(c, (170, 168, 158), 0.33)   # worn through to bare metal
            elif r < 0.04 * wear + 0.0: c = mix(c, (176, 170, 150), 0.26)                        # scratch
            elif r < 0.075 * wear: c = sh(c, 0.72)                                               # grime
            if grade: c = mix(c, (112, 104, 90), grade)
            out[y * W + x] = c
    if outline:
        for y in range(H):
            for x in range(W):
                if out[y * W + x] is not None: continue
                nb = [out[ny * W + nx] for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)) if 0 <= nx < W and 0 <= ny < H and out[ny * W + nx] is not None]
                if nb:
                    m = min(nb, key=lambda q: q[0] + q[1] + q[2])
                    cv.p[y * W + x] = tuple(clamp(v * 0.22 + 7) for v in m)
                    continue
        # (the outline is written into cv.p below so it is not mistaken for part of the body while scanning)
    keep = list(cv.p)
    for i, c in enumerate(out):
        if c is not None: keep[i] = c
    cv.p = keep if outline else out
    return cv
