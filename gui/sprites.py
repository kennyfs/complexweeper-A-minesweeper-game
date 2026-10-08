"""Sprite loading and the rules for picking a sprite for each cell.

Everything here is independent of Tk, so it can be tested (and used to render board snapshots)
without opening a window.
"""
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
# The vector sprites are drawn by tools/make_sprites.py.
SPRITES_JSON = ROOT / "assets" / "sprites.json"
SUPERSAMPLE = 4


class Atlas:
    """The vector sprites: named lists of simple shapes, rasterized at the size asked for."""

    def __init__(self, table=SPRITES_JSON):
        with open(table, encoding="utf-8") as f:
            data = json.load(f)
        self.sprites = data["sprites"]
        self.board_color = data["board"]
        self._cache = {}

    def has(self, name):
        return name in self.sprites

    def size(self, name):
        """The native size of a sprite in pixels (at zoom 1)."""
        s = self.sprites[name]
        return s["w"], s["h"]

    def sprite(self, name):
        return self.scaled(name, 1)

    def scaled(self, name, zoom):
        """The sprite at `zoom` times its native size, as an RGBA image."""
        key = (name, zoom)
        if key not in self._cache:
            self._cache[key] = self._render(name, zoom)
        return self._cache[key]

    def _render(self, name, zoom):
        spec = self.sprites[name]
        w, h = spec["w"] * zoom, spec["h"] * zoom
        k = SUPERSAMPLE * w / spec["vw"]  # virtual unit -> supersampled pixel
        img = Image.new("RGBA", (w * SUPERSAMPLE, h * SUPERSAMPLE), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        for item in spec["items"]:
            kind = item[0]
            if kind == "rect":
                _, x, y, rw, rh, r, fill = item
                draw.rounded_rectangle([x * k, y * k, (x + rw) * k - 1, (y + rh) * k - 1], radius=r * k, fill=fill)
            elif kind == "circle":
                _, cx, cy, r, fill = item
                draw.ellipse([(cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k], fill=fill)
            elif kind == "poly":
                pts = item[1]
                draw.polygon([(pts[i] * k, pts[i + 1] * k) for i in range(0, len(pts), 2)], fill=item[2])
            elif kind == "ring":
                _, cx, cy, r, lw, color = item
                draw.ellipse([(cx - r) * k, (cy - r) * k, (cx + r) * k, (cy + r) * k], outline=color, width=max(1, round(lw * k)))
            elif kind == "path":
                _, pts, lw, color, closed = item
                points = [(pts[i] * k, pts[i + 1] * k) for i in range(0, len(pts), 2)]
                if closed:
                    points.append(points[0])
                width = max(1, round(lw * k))
                draw.line(points, fill=color, width=width, joint="curve")
                r = width / 2  # round caps and joins
                for x, y in points:
                    draw.ellipse([x - r, y - r, x + r, y + r], fill=color)
        return img.resize((w, h), Image.LANCZOS)


def _typed(prefix, kind, hyper):
    """Name of a per-type sprite. Types 3 and 4 are the imaginary units; Minkowski mode (j)
    has its own artwork for them with an 'h' prefix."""
    return f"{'h' if hyper and kind >= 3 else ''}{prefix}_{kind}"


def number_name(value, hyper, atlas):
    """Sprite name for the number shown on an open cell with displayed value `value`
    (the square of the modulus; negative values only occur in Minkowski mode)."""
    return f"num_m{-value}" if value < 0 else f"num_{value}"


def cell_sprite(cell, index, hyper, over, boom, atlas):
    """Sprite name for one cell.

    `cell` has the fields of engine.Cell. `boom` is the index of the cell that was stepped on
    (or -1). While the game is running `cell.mine` is always 0, so nothing here can leak mines.
    """
    if not over:
        if cell.open:
            return "blank" if cell.blank else number_name(cell.clue, hyper, atlas)
        if cell.flag:
            return _typed("flag", cell.flag, hyper)
        return "closed"

    # The game is over: show where the mines were. The rules judge flags by position and by whether
    # they reproduce the numbers, not by their exact type, so a flag on a mine is simply "right".
    if cell.mine:
        if index == boom:
            return _typed("boom", cell.mine, hyper)
        if cell.flag:
            return _typed("rightflag", cell.flag, hyper)
        return _typed("mine", cell.mine, hyper)
    if cell.open:
        return "blank" if cell.blank else number_name(cell.clue, hyper, atlas)
    if cell.flag:
        return _typed("wrong", cell.flag, hyper)  # a flag on a cell that has no mine
    return "closed"


def render_board(cells, width, height, hyper, over, boom, atlas, zoom=2):
    """Compose the whole board into one image (used for snapshots in tests)."""
    cs = 16 * zoom
    img = Image.new("RGBA", (width * cs, height * cs), atlas.board_color)
    for i, c in enumerate(cells):
        name = cell_sprite(c, i, hyper, over, boom, atlas)
        sp = atlas.scaled(name, zoom)
        img.paste(sp, ((i % width) * cs, (i // width) * cs), sp)
    return img
