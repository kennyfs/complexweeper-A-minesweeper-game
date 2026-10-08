"""Sprite atlas loading and the rules for picking a sprite for each cell.

Everything here is independent of Tk, so it can be tested (and used to render board snapshots)
without opening a window.
"""
import json
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
# The asset directory and file names are the repository's existing paths.
ATLAS_DIR = ROOT / "素材"
ATLAS_PNG = ATLAS_DIR / "图集.png"
ATLAS_JSON = ATLAS_DIR / "图集.json"

SOLVER_FLAG = "solver_flag"  # synthetic sprite: "a mine, type unknown" marked by the solver


class Atlas:
    """The sprite sheet: a PNG plus a JSON table of named rectangles."""

    def __init__(self, png=ATLAS_PNG, table=ATLAS_JSON):
        self.image = Image.open(png).convert("RGBA")
        with open(table, encoding="utf-8") as f:
            slots = json.load(f)["slots"]
        self.slots = {s["name"]: (s["x"], s["y"], s["w"], s["h"]) for s in slots}
        self._cache = {}

    def has(self, name):
        return name in self.slots or name == SOLVER_FLAG

    def sprite(self, name):
        """The sprite at its native size, as an RGBA image."""
        if name not in self._cache:
            self._cache[name] = self._make_solver_flag() if name == SOLVER_FLAG else self._crop(name)
        return self._cache[name]

    def scaled(self, name, zoom):
        """The sprite enlarged by an integer factor with nearest-neighbor scaling."""
        img = self.sprite(name)
        return img if zoom == 1 else img.resize((img.width * zoom, img.height * zoom), Image.NEAREST)

    def _crop(self, name):
        x, y, w, h = self.slots[name]
        return self.image.crop((x, y, x + w, y + h))

    def _make_solver_flag(self):
        # A closed cell with an orange flag: the solver knows the cell is a mine, but the numbers
        # can never tell which of the four types it is.
        img = self._crop("closed").copy()
        d = ImageDraw.Draw(img)
        d.polygon([(4, 3), (11, 6), (4, 9)], fill=(255, 150, 0, 255))
        d.line([(4, 3), (4, 12)], fill=(0, 0, 0, 255))
        d.rectangle([(2, 12), (9, 13)], fill=(0, 0, 0, 255))
        return img


def _typed(prefix, kind, hyper):
    """Name of a per-type sprite. Types 3 and 4 are the imaginary units; Minkowski mode (j)
    has its own artwork for them with an 'h' prefix."""
    return f"{'h' if hyper and kind >= 3 else ''}{prefix}_{kind}"


def number_name(value, hyper, atlas):
    """Sprite name for the number shown on an open cell with displayed value `value`."""
    if value == 0:
        return "num_0"
    if value < 0:
        return f"hnum_{-value}_i"  # negative values only occur in Minkowski mode
    plain = f"num_{value}"
    if atlas.has(plain):
        return plain
    return f"hnum_{value}"


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
        if cell.mark:
            return SOLVER_FLAG
        return "closed"

    # The game is over: show where the mines were and which flags were right or wrong.
    if cell.mine:
        if index == boom:
            return _typed("boom", cell.mine, hyper)
        if cell.flag == cell.mine:
            return _typed("rightflag", cell.mine, hyper)
        if cell.flag:
            return _typed("wrongflag", cell.flag, hyper)
        if cell.mark:
            return SOLVER_FLAG
        return _typed("mine", cell.mine, hyper)
    if cell.open:
        return "blank" if cell.blank else number_name(cell.clue, hyper, atlas)
    if cell.flag:
        return _typed("wrong", cell.flag, hyper)  # a flag on a cell that has no mine
    return "closed"


def render_board(cells, width, height, hyper, over, boom, atlas, zoom=2):
    """Compose the whole board into one image (used for snapshots in tests)."""
    cs = 16 * zoom
    img = Image.new("RGBA", (width * cs, height * cs))
    for i, c in enumerate(cells):
        name = cell_sprite(c, i, hyper, over, boom, atlas)
        img.paste(atlas.scaled(name, zoom), ((i % width) * cs, (i // width) * cs))
    return img
