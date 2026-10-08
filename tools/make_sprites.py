#!/usr/bin/env python3
"""Draws the vector sprites of Complexweeper and writes them as data.

Run from the repository root:   python3 tools/make_sprites.py

Output: assets/sprites.json (read by gui/sprites.py) and web/sprites-data.js (the same data for the
web page). Every sprite is a list of simple shapes on a virtual canvas, so both front ends can
draw them at any size:

  ["rect", x, y, w, h, radius, fill]            ["circle", cx, cy, r, fill]
  ["poly", [x0, y0, x1, y1, ...], fill]         ["ring", cx, cy, r, line_width, color]
  ["path", [x0, y0, ...], line_width, color, closed]   (round caps and joins)

Numbers and signs are drawn with a small stroke font defined here, so nothing depends on fonts.
"""
import itertools
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# ---------------------------------------------------------------- palette
BOARD = "#e4e6ea"      # the grid lines show this color between the cells
CLOSED = "#c7ccd4"
OPEN = "#f7f8fa"
BOOM_BG = "#f8c9c4"
RIGHT_BG = "#d5efdc"
INK = "#1f2430"
TYPE_COLOR = {1: "#2563eb", 2: "#dc2626", 3: "#16a34a", 4: "#ea580c"}
NUM_COLORS = {1: "#2563eb", 2: "#15803d", 3: "#dc2626", 4: "#1e3a8a", 5: "#9a3412", 6: "#0e7490", 7: "#1f2430", 8: "#6b7280"}
ROOT_COLOR = "#7c3aed"
IMAG_COLOR = "#c2410c"
LED_ON, LED_OFF, LED_BG = "#ff3b30", "#3b1f22", "#16181d"
FACE = "#ffcf3f"


# ---------------------------------------------------------------- helpers
def flat(points):
    return [round(v, 2) for p in points for v in p]


def arc(cx, cy, rx, ry, a0, a1, n=14):
    """Points on an ellipse arc; angles in degrees, 0 = right, 90 = down (the y axis points down)."""
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * k / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * k / n))) for k in range(n + 1)]


def bez(p0, p1, p2, n=10):
    return [((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * p1[0] + t * t * p2[0],
             (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * p1[1] + t * t * p2[1]) for t in (k / n for k in range(n + 1))]


def rot180(pts):
    return [(6 - x, 10 - y) for x, y in pts]


# ---------------------------------------------------------------- stroke font (6 wide, 10 high)
# Where two curves meet, they meet at the same point with the same direction: in the 3 both bowls
# touch at the bottom of the upper one and the top of the lower one (both horizontal there); in the
# 6 and the 9 the tail leaves the circle at its leftmost / rightmost point (both vertical there).
def _three():
    upper = arc(3, 2.7, 2.5, 2.3, 200, 450)       # ends at its bottom point (3, 5.0), heading left
    lower = arc(3, 7.4, 2.8, 2.4, 270, 520)       # starts at its top point (3, 5.0), heading right
    return upper + lower


def _five():
    bowl = arc(3, 6.9, 2.8, 2.9, 225, 520)
    return [(5.3, 0.4), (bowl[0][0], 0.4)] + bowl


def _six():
    circle = arc(3, 6.8, 2.7, 3.0, 0, 360, 24)
    tail = arc(5.5, 6.8, 5.2, 6.4, 180, 265, 14)  # starts at the circle's leftmost point (0.3, 6.8), heading up
    return circle, tail


def _eight():
    # Two ellipses of different size that touch at one point; the whole figure is scaled to the
    # height of the other digits (0.4 .. 9.8).
    k = 9.4 / 10.4
    def sc(cx, cy, rx, ry):
        return arc(3, 5 + (cy - 5) * k, rx * k, ry * k, 0, 360, 24)
    return [sc(3, 2.3, 2.4, 2.5), sc(3, 7.5, 2.8, 2.7)]


SIX = _six()


def rot180(pts):
    return [(6 - x, 10 - y) for x, y in pts]


GLYPHS = {
    "0": ([arc(3, 5, 2.7, 4.7, 0, 360, 20)], 6),
    "1": ([[(1.2, 2.5), (3.4, 0.4), (3.4, 9.8)]], 5),
    "2": ([arc(3, 3.0, 2.6, 2.6, 180, 405) + [(0.4, 9.8), (5.7, 9.8)]], 6),
    "3": ([_three()], 6),
    "4": ([[(4.3, 9.8), (4.3, 0.3), (0.3, 6.9), (5.8, 6.9)]], 6),
    "5": ([_five()], 6),
    "6": ([SIX[0], SIX[1]], 6),
    "7": ([[(0.3, 0.4), (5.6, 0.4), (2.0, 9.8)]], 6),
    "8": (_eight(), 6),
    "9": ([rot180(SIX[0]), rot180(SIX[1])], 6),
    "i": ([[(2, 3.6), (2, 9.8)], [(2, 0.9), (2, 1.0)]], 4),
    "j": ([[(3.2, 3.6), (3.2, 8.4), (2.7, 9.8), (0.8, 9.8)], [(3.2, 0.9), (3.2, 1.0)]], 4),
    "+": ([[(0.4, 5), (5.6, 5)], [(3, 2.4), (3, 7.6)]], 6),
    "-": ([[(0.4, 5), (5.6, 5)]], 6),
}
GAP = 1.6


def text_items(parts, color, box, line=None):
    """Stroke text. `parts` is a list of strings or ("root", radicand) entries. `box` = (x, y, w, h):
    the text is scaled to fit and centered in it. Returns shapes."""
    BAR_Y = 0.2  # height of the bar over the radicand, in glyph units
    GL_TOP, GL_BOT = 0.4, 9.8  # top and bottom of the glyphs

    def layout(f):
        """Positions in glyph units; the radicand digits are scaled by f."""
        seq, x = [], 0.0
        for part in parts:
            if isinstance(part, tuple):
                digits = part[1]
                rw = f * (sum(GLYPHS[d][1] for d in digits) + GAP * (len(digits) - 1))
                seq.append(("root", digits, x, rw))
                x += 5.6 + 0.8 + rw + GAP
            else:
                for ch in part:
                    seq.append(("g", ch, x, GLYPHS[ch][1]))
                    x += GLYPHS[ch][1] + GAP
        return seq, x - GAP

    bx, by, bw, bh = box
    f = 1.0
    for _ in range(4):  # the digits under a root sign shrink until a clear gap is left below the bar
        seq, total = layout(f)
        s = min(bh / 10.0, bw / total)
        lw_cell = max(5.0, min(9.5, s * 1.5)) if line is None else line
        lw = lw_cell / s
        has_root = any(k == "root" for k, *_ in seq)
        top = BAR_Y + 1.75 * lw  # center line of the digits' top strokes: one line width and 3/4 of a gap below the bar
        f_new = min(1.0, (GL_BOT - top) / (GL_BOT - GL_TOP)) if has_root else 1.0
        if abs(f_new - f) < 1e-3:
            break
        f = f_new
    seq, total = layout(f)
    s = min(bh / 10.0, bw / total)
    lw_cell = max(5.0, min(9.5, s * 1.5)) if line is None else line
    ox = bx + (bw - total * s) / 2
    oy = by + (bh - 10 * s) / 2
    items = []

    def put(pts, closed=False):
        items.append(["path", flat([(ox + px * s, oy + py * s) for px, py in pts]), round(lw_cell, 2), color, int(closed)])

    def glyph(ch, gx, k=1.0):
        for stroke in GLYPHS[ch][0]:
            if k == 1.0:
                put([(gx + px, py) for px, py in stroke])
            else:  # scaled about the glyph's bottom-left corner
                put([(gx + px * k, GL_BOT - (GL_BOT - py) * k) for px, py in stroke])

    for kind, payload, gx, w in seq:
        if kind == "g":
            glyph(payload, gx)
        else:
            put([(gx + 0.2, 5.9), (gx + 1.5, 5.2), (gx + 3.0, 9.8), (gx + 5.6, BAR_Y), (gx + 5.6 + 0.8 + w + 0.4, BAR_Y)])
            gx2 = gx + 5.6 + 0.8
            for d in payload:
                glyph(d, gx2, f)
                gx2 += f * (GLYPHS[d][1] + GAP)
    return items


def type_label(t, hyper):
    unit = "j" if hyper else "i"
    return {1: ["+", "1"], 2: ["-", "1"], 3: ["+", unit], 4: ["-", unit]}[t]


# ---------------------------------------------------------------- cells
def cell(bg):
    return [["rect", 3, 3, 94, 94, 16, bg]]


def number_label(value):
    """The parts of the text for a displayed value (the square of the modulus)."""
    n = abs(value)
    k = max(d for d in range(1, 9) if n % (d * d) == 0)
    r = n // (k * k)
    parts = []
    if value == 0:
        return ["0"]
    if r == 1:
        if not (value < 0 and k == 1):
            parts.append(str(k))
    else:
        if k > 1:
            parts.append(str(k))
        parts.append(("root", str(r)))
    if value < 0:
        parts.append("i")
    return parts


def number_color(value):
    n = abs(value)
    k = max(d for d in range(1, 9) if n % (d * d) == 0)
    if value < 0:
        return IMAG_COLOR
    if n // (k * k) > 1:
        return ROOT_COLOR
    return NUM_COLORS.get(k, INK)


def number_sprite(value):
    parts = number_label(value)
    if value == 0:
        return cell(OPEN) + text_items(parts, "#9aa1ad", (28, 22, 44, 56))
    return cell(OPEN) + text_items(parts, number_color(value), (16, 22, 68, 56))


def flag_shape(t, hyper):
    c = TYPE_COLOR[t]
    items = [
        ["path", [34, 16, 34, 72], 7, INK, 0],
        ["poly", [34, 17, 76, 34, 34, 51], c],
        ["rect", 22, 74, 28, 7, 3, INK],
    ]
    return items + flag_label(t, hyper)


def mine_shape(t, hyper):
    c = TYPE_COLOR[t]
    items = []
    for k in range(8):
        a = math.radians(45 * k)
        items.append(["path", flat([(50 + 20 * math.cos(a), 50 + 20 * math.sin(a)),
                                    (50 + 34 * math.cos(a), 50 + 34 * math.sin(a))]), 7, c, 0])
    items.append(["circle", 50, 50, 24, c])
    items += text_items(type_label(t, hyper), "#ffffff", (31, 38, 38, 24), line=5)
    return items


def flag_label(t, hyper):
    return text_items(type_label(t, hyper), TYPE_COLOR[t], (52, 58, 36, 16), line=4.5)


def typed_sprites(sprites, t, hyper):
    h = "h" if hyper and t >= 3 else ""
    flag = flag_shape(t, hyper)
    sprites[f"{h}flag_{t}"] = cell(CLOSED) + flag
    sprites[f"{h}mine_{t}"] = cell(OPEN) + mine_shape(t, hyper)
    sprites[f"{h}boom_{t}"] = cell(BOOM_BG) + mine_shape(t, hyper)
    sprites[f"{h}rightflag_{t}"] = cell(RIGHT_BG) + flag
    cross = [["path", [22, 22, 78, 78], 7, "#b91c1c", 0], ["path", [78, 22, 22, 78], 7, "#b91c1c", 0]]
    sprites[f"{h}wrong_{t}"] = cell(CLOSED) + flag + cross


# ---------------------------------------------------------------- face and LED digits
def face(kind):
    items = [["circle", 50, 50, 46, FACE]]
    eye = lambda cx, cy: ["circle", cx, cy, 6.5, INK]
    smile = ["path", flat(arc(50, 52, 24, 20, 25, 155)), 6, INK, 0]
    if kind == "normal":
        items += [eye(34, 40), eye(66, 40), smile]
    elif kind == "down":
        items += [eye(34, 40), eye(66, 40), ["ring", 50, 68, 8, 5, INK]]
    elif kind == "scan":
        items += [["ring", 34, 40, 9, 5, INK], ["ring", 66, 40, 9, 5, INK], ["path", [36, 70, 64, 70], 6, INK, 0]]
    elif kind == "dead":
        for cx in (34, 66):
            items += [["path", [cx - 8, 32, cx + 8, 48], 6, INK, 0], ["path", [cx + 8, 32, cx - 8, 48], 6, INK, 0]]
        items.append(["path", flat(arc(50, 76, 22, 16, 200, 340)), 6, INK, 0])
    elif kind == "win":
        items += [["rect", 20, 31, 27, 19, 7, INK], ["rect", 53, 31, 27, 19, 7, INK],
                  ["path", [46, 38, 54, 38], 5, INK, 0], smile]
    return items


SEGMENTS = {  # (is_horizontal, cx, cy, length)
    "a": (True, 29, 10, 28), "g": (True, 29, 50, 28), "d": (True, 29, 90, 28),
    "f": (False, 8, 30, 28), "b": (False, 50, 30, 28), "e": (False, 8, 70, 28), "c": (False, 50, 70, 28),
}
LIT = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc",
       "7": "abc", "8": "abcdefg", "9": "abcdfg", "-": "g", " ": ""}


def led(ch):
    items = [["rect", 0, 0, 58, 100, 7, LED_BG]]
    h = 5
    for name, (horiz, cx, cy, length) in SEGMENTS.items():
        a, b = (cx - length / 2, cx + length / 2) if horiz else (cy - length / 2, cy + length / 2)
        if horiz:
            pts = [(a, cy), (a + h, cy - h), (b - h, cy - h), (b, cy), (b - h, cy + h), (a + h, cy + h)]
        else:
            pts = [(cx, a), (cx + h, a + h), (cx + h, b - h), (cx, b), (cx - h, b - h), (cx - h, a + h)]
        items.append(["poly", flat(pts), LED_ON if name in LIT[ch] else LED_OFF])
    return items


# ---------------------------------------------------------------- build
def displayed_values():
    values = set()
    for n1, n2, n3, n4 in itertools.product(range(9), repeat=4):
        if n1 + n2 + n3 + n4 <= 8:
            a, b = n1 - n2, n3 - n4
            values.add(a * a + b * b)
            values.add(a * a - b * b)
    return sorted(values)


def number_key(value):
    return "num_m%d" % -value if value < 0 else "num_%d" % value


def build():
    sprites = {}
    # name: [native pixel width, native pixel height, virtual width, virtual height, shapes]
    out = {}

    def add(name, shapes, px, vw=100, vh=100, py=None):
        out[name] = {"w": px, "h": py or px, "vw": vw, "vh": vh, "items": shapes}

    add("closed", cell(CLOSED), 16)
    add("blank", cell(OPEN), 16)
    for v in displayed_values():
        add(number_key(v), number_sprite(v), 16)
    for hyper in (False, True):
        for t in range(1, 5):
            if hyper and t < 3:
                continue
            typed_sprites(sprites, t, hyper)
    for name, shapes in sprites.items():
        add(name, shapes, 16)
    for kind in ("normal", "down", "scan", "dead", "win"):
        add("face_" + kind, face(kind), 24)
    for ch in "0123456789":
        add("led_" + ch, led(ch), 13, 58, 100, 23)
    add("led_minus", led("-"), 13, 58, 100, 23)
    add("led_blank", led(" "), 13, 58, 100, 23)
    return {"version": 2, "board": BOARD, "ink": INK, "sprites": out}


def main():
    data = build()
    compact = json.dumps(data, separators=(",", ":"), ensure_ascii=False)
    (ROOT / "assets").mkdir(exist_ok=True)
    (ROOT / "assets" / "sprites.json").write_text(compact + "\n", encoding="utf-8")
    (ROOT / "web" / "sprites-data.js").write_text(
        "/* Generated by tools/make_sprites.py. Do not edit. */\nconst SPRITE_DATA = " + compact + ";\n", encoding="utf-8")
    print("%d sprites, %d bytes" % (len(data["sprites"]), len(compact)))


if __name__ == "__main__":
    main()
